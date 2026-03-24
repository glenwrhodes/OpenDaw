#include "TimelineView.h"
#include "AutomationLaneItem.h"
#include "AutomationLaneHeader.h"
#include "AutomationPointItem.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <QPushButton>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QSet>
#include <QScrollBar>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QDebug>
#include <QLineF>
#include <cmath>

namespace OpenDaw {

// ── TimelineScene ───────────────────────────────────────────────────────────

TimelineScene::TimelineScene(QObject* parent) : QGraphicsScene(parent) {}

void TimelineScene::cancelBackgroundDrag()
{
    backgroundDragCandidate_ = false;
    backgroundDragging_ = false;
}

void TimelineScene::dragEnterEvent(QGraphicsSceneDragDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;

    static const QSet<QString> kSupportedExts = {
        ".wav", ".mp3", ".flac", ".ogg", ".aiff", ".aif",
        ".mid", ".midi"
    };
    for (const auto& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        QString ext = QFileInfo(url.toLocalFile()).suffix().toLower();
        if (kSupportedExts.contains("." + ext)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void TimelineScene::dragMoveEvent(QGraphicsSceneDragDropEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void TimelineScene::dropEvent(QGraphicsSceneDragDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;

    double xOffset = 0.0;
    for (const auto& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        QString path = url.toLocalFile();
        QPointF pos = event->scenePos();
        emit fileDropped(path, pos.x() + xOffset, int(pos.y()));

        juce::AudioFormatManager fmtMgr;
        fmtMgr.registerBasicFormats();
        juce::File f(path.toStdString());
        if (auto reader = std::unique_ptr<juce::AudioFormatReader>(fmtMgr.createReaderFor(f))) {
            double durationSecs = double(reader->lengthInSamples) / reader->sampleRate;
            xOffset += durationSecs * 40.0;
        } else {
            xOffset += 200.0;
        }
    }
    event->acceptProposedAction();
}

void TimelineScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        auto hitItems = items(event->scenePos());
        bool hitInteractive = false;
        for (auto* item : hitItems) {
            if (dynamic_cast<ClipItem*>(item) ||
                dynamic_cast<AutomationPointItem*>(item) ||
                dynamic_cast<AutomationLaneItem*>(item))
            { hitInteractive = true; break; }
        }
        backgroundDragCandidate_ = !hitInteractive;
        backgroundDragging_ = false;
        if (backgroundDragCandidate_)
            backgroundDragStartScenePos_ = event->scenePos();
    }
    QGraphicsScene::mousePressEvent(event);
}

void TimelineScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (backgroundDragCandidate_) {
        if (!backgroundDragging_) {
            constexpr double dragThresholdPixels = 5.0;
            if (QLineF(backgroundDragStartScenePos_, event->scenePos()).length() >= dragThresholdPixels) {
                backgroundDragging_ = true;
                emit backgroundDragStarted(backgroundDragStartScenePos_);
            }
        }

        if (backgroundDragging_)
            emit backgroundDragUpdated(backgroundDragStartScenePos_, event->scenePos());
    }

    QGraphicsScene::mouseMoveEvent(event);
}

void TimelineScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && backgroundDragCandidate_) {
        if (backgroundDragging_)
            emit backgroundDragFinished(backgroundDragStartScenePos_, event->scenePos());
        else
            emit backgroundClicked(event->scenePos().x(), event->scenePos().y());
    }

    backgroundDragCandidate_ = false;
    backgroundDragging_ = false;
    QGraphicsScene::mouseReleaseEvent(event);
}

void TimelineScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        auto hitItems = items(event->scenePos());
        bool hitInteractive = false;
        for (auto* item : hitItems) {
            if (dynamic_cast<ClipItem*>(item) ||
                dynamic_cast<AutomationPointItem*>(item) ||
                dynamic_cast<AutomationLaneItem*>(item))
            { hitInteractive = true; break; }
        }
        if (!hitInteractive) {
            emit emptyAreaDoubleClicked(event->scenePos().x(), event->scenePos().y());
            event->accept();
            return;
        }
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void TimelineScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    auto hitItems = items(event->scenePos());
    bool hitInteractive = false;
    for (auto* item : hitItems) {
        if (dynamic_cast<ClipItem*>(item) ||
            dynamic_cast<AutomationPointItem*>(item) ||
            dynamic_cast<AutomationLaneItem*>(item))
        { hitInteractive = true; break; }
    }
    if (!hitInteractive) {
        emit backgroundRightClicked(event->scenePos(), event->screenPos());
        event->accept();
        return;
    }
    QGraphicsScene::contextMenuEvent(event);
}

// ── TimelineView ────────────────────────────────────────────────────────────

TimelineView::TimelineView(EditManager* editMgr, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("Timeline View");
    auto& theme = ThemeManager::instance().current();
    qDebug() << "[TimelineView] ctor: theme loaded";

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ── Top row: header corner + ruler ──
    auto* topRow = new QWidget(this);
    topRowLayout_ = new QHBoxLayout(topRow);
    topRowLayout_->setContentsMargins(0, 0, 0, 0);
    topRowLayout_->setSpacing(0);

    headerCorner_ = new QWidget(topRow);
    headerCorner_->setFixedSize(HEADER_WIDTH, 28);
    headerCorner_->setAutoFillBackground(true);
    QPalette cornerPal;
    cornerPal.setColor(QPalette::Window, theme.surface);
    headerCorner_->setPalette(cornerPal);

    auto* cornerLayout = new QHBoxLayout(headerCorner_);
    cornerLayout->setContentsMargins(4, 2, 4, 2);
    cornerLayout->setSpacing(2);

    const int iconSz = 14;
    const QFont faFont = icons::fontAudio(iconSz);
    const QFont miFont = icons::materialIcons(iconSz);

    QString cornerBtnStyle = QString(
        "QPushButton { background: transparent; color: %1; border: 1px solid transparent; "
        "border-radius: 4px; padding: 2px; }"
        "QPushButton:hover { background: %2; border: 1px solid %3; }"
        "QPushButton:pressed { background: %4; }")
        .arg(theme.accentLight.name(), theme.surfaceLight.name(),
             theme.border.name(), theme.surface.name());

    auto* addAudioBtn = new QPushButton(headerCorner_);
    addAudioBtn->setAccessibleName("Add Audio Track");
    addAudioBtn->setToolTip("Add Audio Track");
    addAudioBtn->setIcon(icons::glyphIcon(faFont, icons::fa::Waveform, theme.accentLight, iconSz));
    addAudioBtn->setIconSize(QSize(iconSz, iconSz));
    addAudioBtn->setFixedSize(24, 24);
    addAudioBtn->setStyleSheet(cornerBtnStyle);
    connect(addAudioBtn, &QPushButton::clicked, this, [this]() {
        editMgr_->addAudioTrack();
    });
    cornerLayout->addWidget(addAudioBtn);

    auto* addMidiBtn = new QPushButton(headerCorner_);
    addMidiBtn->setAccessibleName("Add MIDI Track");
    addMidiBtn->setToolTip("Add MIDI Track");
    addMidiBtn->setIcon(icons::glyphIcon(faFont, icons::fa::Keyboard, theme.accentLight, iconSz));
    addMidiBtn->setIconSize(QSize(iconSz, iconSz));
    addMidiBtn->setFixedSize(24, 24);
    addMidiBtn->setStyleSheet(cornerBtnStyle);
    connect(addMidiBtn, &QPushButton::clicked, this, [this]() {
        editMgr_->addMidiTrack();
    });
    cornerLayout->addWidget(addMidiBtn);

    auto* addFolderBtn = new QPushButton(headerCorner_);
    addFolderBtn->setAccessibleName("Add Folder");
    addFolderBtn->setToolTip("Add Folder");
    addFolderBtn->setIcon(icons::glyphIcon(miFont, icons::mi::CreateNewFolder, theme.accentLight, iconSz));
    addFolderBtn->setIconSize(QSize(iconSz, iconSz));
    addFolderBtn->setFixedSize(24, 24);
    addFolderBtn->setStyleSheet(cornerBtnStyle);
    connect(addFolderBtn, &QPushButton::clicked, this, [this]() {
        editMgr_->addFolder("New Folder");
    });
    cornerLayout->addWidget(addFolderBtn);

    cornerLayout->addStretch();
    topRowLayout_->addWidget(headerCorner_);

    ruler_ = new TimeRuler(topRow);
    ruler_->setSnapFunction([this](double beat) { return snapper_.snapBeat(beat); });
    topRowLayout_->addWidget(ruler_, 1);
    qDebug() << "[TimelineView] ctor: ruler created";

    outerLayout->addWidget(topRow);

    // ── Marker/Tempo lane (toggleable) ──
    markerTempoRow_ = new QWidget(this);
    auto* mtLayout = new QHBoxLayout(markerTempoRow_);
    mtLayout->setContentsMargins(0, 0, 0, 0);
    mtLayout->setSpacing(0);

    auto* mtLabel = new QWidget(markerTempoRow_);
    mtLabel->setFixedSize(HEADER_WIDTH, 70);
    mtLabel->setAutoFillBackground(true);
    QPalette mtPal;
    mtPal.setColor(QPalette::Window, theme.surface);
    mtLabel->setPalette(mtPal);
    mtLayout->addWidget(mtLabel);

    markerTempoLane_ = new MarkerTempoLane(editMgr_, &snapper_, markerTempoRow_);
    mtLayout->addWidget(markerTempoLane_, 1);

    markerTempoRow_->setVisible(false);
    outerLayout->addWidget(markerTempoRow_);

    // ── Body row: track headers (left) + graphics view (right) ──
    auto* bodyRow = new QWidget(this);
    bodyLayout_ = new QHBoxLayout(bodyRow);
    bodyLayout_->setContentsMargins(0, 0, 0, 0);
    bodyLayout_->setSpacing(0);

    // Track header panel
    headerScrollArea_ = new QScrollArea(bodyRow);
    headerScrollArea_->setFixedWidth(HEADER_WIDTH);
    headerScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    headerScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    headerScrollArea_->setFrameStyle(QFrame::NoFrame);
    headerScrollArea_->setWidgetResizable(true);
    headerScrollArea_->setStyleSheet(
        QString("QScrollArea { background: %1; border: none; }")
            .arg(theme.surface.name()));

    headerContainer_ = new QWidget();
    headerContainer_->setAutoFillBackground(true);
    QPalette hcPal;
    hcPal.setColor(QPalette::Window, theme.surface);
    headerContainer_->setPalette(hcPal);
    headerVLayout_ = new QVBoxLayout(headerContainer_);
    headerVLayout_->setContentsMargins(0, 0, 0, 0);
    headerVLayout_->setSpacing(1);
    headerVLayout_->setAlignment(Qt::AlignTop);

    headerScrollArea_->setWidget(headerContainer_);
    qDebug() << "[TimelineView] ctor: header container created";
    bodyLayout_->addWidget(headerScrollArea_);

    // Timeline graphics view
    scene_ = new TimelineScene(this);
    graphicsView_ = new QGraphicsView(scene_, bodyRow);
    graphicsView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    graphicsView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    graphicsView_->setDragMode(QGraphicsView::NoDrag);
    graphicsView_->setAcceptDrops(true);
    graphicsView_->setRenderHint(QPainter::Antialiasing, true);
    graphicsView_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    graphicsView_->setBackgroundBrush(theme.background);
    graphicsView_->setFocusPolicy(Qt::StrongFocus);
    graphicsView_->viewport()->setFocusPolicy(Qt::StrongFocus);
    graphicsView_->installEventFilter(this);
    graphicsView_->viewport()->installEventFilter(this);
    headerScrollArea_->installEventFilter(this);
    headerScrollArea_->viewport()->installEventFilter(this);
    bodyLayout_->addWidget(graphicsView_, 1);
    qDebug() << "[TimelineView] ctor: graphics view created";

    outerLayout->addWidget(bodyRow, 1);

    // Playhead line
    playheadLine_ = scene_->addLine(0, 0, 0, 2000, QPen(theme.playhead, 1.5));
    playheadLine_->setZValue(100);

    // Loop region overlay on the scene (behind playhead, in front of clips)
    {
        QColor loopColor = theme.accent;
        loopColor.setAlpha(25);
        loopOverlayItem_ = scene_->addRect(0, 0, 0, 0, Qt::NoPen, loopColor);
        loopOverlayItem_->setZValue(90);
        loopOverlayItem_->setVisible(false);
    }
    qDebug() << "[TimelineView] ctor: playhead and loop overlay created";

    // Sync ruler scroll with graphics view horizontal scroll, and expand scene if near edge
    connect(graphicsView_->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) {
                ruler_->setScrollX(val);
                if (markerTempoLane_) markerTempoLane_->setScrollX(val);
                QRectF sr = scene_->sceneRect();
                double viewRight = val + graphicsView_->viewport()->width();
                if (viewRight > sr.width() - 200.0) {
                    double newWidth = sr.width() + 800.0;
                    scene_->setSceneRect(0, 0, newWidth, sr.height());
                }
            });

    // Sync track header vertical scroll with timeline vertical scroll
    connect(graphicsView_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &TimelineView::syncHeaderScroll);

    // Ruler click -> snap to grid, then set transport position
    connect(ruler_, &TimeRuler::positionClicked, this, [this](double beat) {
        if (!editMgr_ || !editMgr_->edit()) return;
        beat = snapper_.snapBeat(beat);
        if (beat < 0) beat = 0;
        auto& ts = editMgr_->edit()->tempoSequence;
        auto time = ts.toTime(tracktion::BeatPosition::fromBeats(beat));
        editMgr_->transport().setPosition(time);
    });

    connect(ruler_, &TimeRuler::positionDragged, this, [this](double beat) {
        if (!editMgr_ || !editMgr_->edit()) return;
        if (beat < 0) beat = 0;
        auto& ts = editMgr_->edit()->tempoSequence;
        auto time = ts.toTime(tracktion::BeatPosition::fromBeats(beat));
        editMgr_->transport().setPosition(time);
    });

    // Loop region handle drag -> update transport loop points
    connect(ruler_, &TimeRuler::loopRegionChanged, this,
            [this](double inBeat, double outBeat) {
                applyLoopRegionToTransport(inBeat, outBeat);
            });

    // Right-click "Set Loop In/Out Here" on ruler
    connect(ruler_, &TimeRuler::loopInRequested, this, [this](double beat) {
        beat = snapper_.snapBeat(beat);
        double outBeat = ruler_->loopOutBeat();
        if (beat >= outBeat) outBeat = beat + editMgr_->getTimeSigNumerator();
        ruler_->setLoopRegion(beat, outBeat);
        applyLoopRegionToTransport(beat, outBeat);
    });

    connect(ruler_, &TimeRuler::loopOutRequested, this, [this](double beat) {
        beat = snapper_.snapBeat(beat);
        double inBeat = ruler_->loopInBeat();
        if (beat <= inBeat) inBeat = std::max(0.0, beat - editMgr_->getTimeSigNumerator());
        ruler_->setLoopRegion(inBeat, beat);
        applyLoopRegionToTransport(inBeat, beat);
    });
    qDebug() << "[TimelineView] ctor: ruler connections done";

    // Scene drop -> add audio clip
    connect(scene_, &TimelineScene::fileDropped,
            this, &TimelineView::handleFileDrop);

    // Click on timeline blank area -> clear track selection
    connect(scene_, &TimelineScene::backgroundClicked,
            this, [this](double, double) {
                selectTrack(nullptr);
            });

    connect(scene_, &TimelineScene::backgroundDragStarted,
            this, &TimelineView::handleBackgroundDragStarted);
    connect(scene_, &TimelineScene::backgroundDragUpdated,
            this, &TimelineView::handleBackgroundDragUpdated);
    connect(scene_, &TimelineScene::backgroundDragFinished,
            this, &TimelineView::handleBackgroundDragFinished);
    qDebug() << "[TimelineView] ctor: scene connections done";

    // Double-click on empty area -> create blank MIDI clip on MIDI tracks
    connect(scene_, &TimelineScene::emptyAreaDoubleClicked,
            this, &TimelineView::handleEmptyAreaDoubleClick);

    // Right-click on empty area -> timeline context menu
    connect(scene_, &TimelineScene::backgroundRightClicked,
            this, [this](QPointF scenePos, QPoint screenPos) {
                QMenu menu;
                menu.setAccessibleName("Timeline Context Menu");

                int trackIdx = trackIndexAtSceneY(scenePos.y());
                auto tracks = getVisibleTracks();
                te::AudioTrack* clickedTrack = (trackIdx >= 0 && trackIdx < tracks.size())
                                                ? tracks[trackIdx] : nullptr;

                menu.addAction("Add Audio Track", [this]() {
                    editMgr_->addAudioTrack();
                });
                menu.addAction("Add MIDI Track", [this]() {
                    editMgr_->addMidiTrack();
                });
                menu.addAction("Add Folder", [this, clickedTrack]() {
                    editMgr_->addFolder("New Folder", clickedTrack);
                });
                if (trackIdx >= 0 && trackIdx < tracks.size()) {
                    auto* track = tracks[trackIdx];
                    bool isMidi = editMgr_->isMidiTrack(track);

                    menu.addSeparator();
                    if (isMidi) {
                        double beat = snapper_.snapBeat(scenePos.x() / pixelsPerBeat_);
                        if (beat < 0) beat = 0;
                        double beatsPerBar = editMgr_->getTimeSigNumerator();
                        double barBeat = std::floor(beat / beatsPerBar) * beatsPerBar;

                        menu.addAction("Create Empty MIDI Clip Here", [this, track, barBeat, beatsPerBar]() {
                            auto* clip = editMgr_->addMidiClipToTrack(*track, barBeat, beatsPerBar);
                            rebuildClips();
                            if (clip)
                                emit editMgr_->midiClipDoubleClicked(clip);
                        });
                    }
                    if (!editMgr_->isTrackFrozen(track)) {
                        menu.addAction("Bounce Track to Audio", [this, track]() {
                            QMessageBox confirmDlg(this);
                            confirmDlg.setWindowTitle("Bounce Track");
                            confirmDlg.setText(
                                "This will render the track with all effects applied "
                                "and replace its contents with the rendered audio.\n\n"
                                "Existing clips and effects will be removed.");
                            confirmDlg.setInformativeText("Continue?");
                            confirmDlg.setStandardButtons(
                                QMessageBox::Yes | QMessageBox::No);
                            confirmDlg.setDefaultButton(QMessageBox::No);
                            confirmDlg.setIcon(QMessageBox::Question);
                            if (confirmDlg.exec() == QMessageBox::Yes) {
                                bool ok = editMgr_->bounceTrackToAudio(*track);
                                if (ok) rebuildClips();
                            }
                        });
                    }

                    // Folder assignment submenu
                    auto folders = editMgr_->getFolders();
                    if (!folders.isEmpty() || editMgr_->getTrackFolderId(track) > 0) {
                        auto* folderMenu = menu.addMenu("Move to Folder");
                        folderMenu->setAccessibleName("Move to Folder Menu");
                        int currentFolderId = editMgr_->getTrackFolderId(track);
                        if (currentFolderId > 0) {
                            folderMenu->addAction("Root Level (No Folder)", [this, track]() {
                                editMgr_->moveTrackToFolder(track, 0);
                                emit editMgr_->tracksChanged();
                            });
                            folderMenu->addSeparator();
                        }
                        for (auto& f : folders) {
                            if (f.id == currentFolderId) continue;
                            folderMenu->addAction(f.name, [this, track, fid = f.id]() {
                                editMgr_->moveTrackToFolder(track, fid);
                                emit editMgr_->tracksChanged();
                            });
                        }
                    }

                    menu.addAction("Remove Track", [this, track]() {
                        auto answer = QMessageBox::question(
                            this, "Remove Track",
                            QString("Remove track \"%1\" and all its clips?")
                                .arg(QString::fromStdString(track->getName().toStdString())),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (answer == QMessageBox::Yes)
                            editMgr_->removeTrack(track);
                    });
                }

                menu.exec(screenPos);
            });

    // Playhead animation
    connect(&playheadTimer_, &QTimer::timeout,
            this, &TimelineView::updatePlayhead);
    playheadTimer_.start(33);
    qDebug() << "[TimelineView] ctor: playhead timer started";

    snapper_.setBpm(editMgr_->getBpm());
    snapper_.setTimeSig(editMgr_->getTimeSigNumerator(),
                        editMgr_->getTimeSigDenominator());

    connect(editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
        qDebug() << "[TimelineView] aboutToChangeEdit - clearing widgets";
        clearMidiClipDrawPreview();
        cleanupAutomationItems();

        for (auto* h : automationLaneHeaders_) {
            headerVLayout_->removeWidget(h);
            delete h;
        }
        automationLaneHeaders_.clear();

        for (auto* h : folderHeaders_) {
            headerVLayout_->removeWidget(h);
            delete h;
        }
        folderHeaders_.clear();

        for (auto* h : trackHeaders_) {
            headerVLayout_->removeWidget(h);
            delete h;
        }
        trackHeaders_.clear();

        orderedHeaders_.clear();

        for (auto* item : clipItems_) scene_->removeItem(item);
        qDeleteAll(clipItems_);
        clipItems_.clear();

        layout_.clear();
        folderDividers_.clear();
    });
    qDebug() << "[TimelineView] ctor: edit signals connected";

    connect(editMgr_, &EditManager::tracksChanged,
            this, &TimelineView::onTracksChanged);
    connect(editMgr_, &EditManager::editChanged,
            this, &TimelineView::onEditChanged);
    connect(editMgr_, &EditManager::routingChanged,
            this, &TimelineView::rebuildTrackHeaders, Qt::QueuedConnection);

    qDebug() << "[TimelineView] ctor: calling onTracksChanged";
    onTracksChanged();

}

void TimelineView::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat_ = std::clamp(ppb, 5.0, 200.0);
    ruler_->setPixelsPerBeat(pixelsPerBeat_);
    if (markerTempoLane_) markerTempoLane_->setPixelsPerBeat(pixelsPerBeat_);
    rebuildClips();
}

void TimelineView::setTrackHeight(double h)
{
    double minH = 80.0;
    for (int i = 0; i < static_cast<int>(trackHeaders_.size()); ++i) {
        if (i < static_cast<int>(layout_.size()) && layout_[i].collapsed) continue;
        minH = std::max(minH, double(trackHeaders_[i]->minimumSizeHint().height()));
    }
    trackHeight_ = std::clamp(h, minH, 250.0);
    rebuildClips();
    rebuildTrackHeaders();
}

void TimelineView::zoomIn()      { setPixelsPerBeat(pixelsPerBeat_ * 1.3); }
void TimelineView::zoomOut()     { setPixelsPerBeat(pixelsPerBeat_ / 1.3); }
void TimelineView::zoomVerticalIn()  { setTrackHeight(trackHeight_ * 1.2); }
void TimelineView::zoomVerticalOut() { setTrackHeight(trackHeight_ / 1.2); }

bool TimelineView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == graphicsView_ || watched == graphicsView_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            graphicsView_->setFocus(Qt::MouseFocusReason);

            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QPointF scenePos = graphicsView_->mapToScene(mouseEvent->pos());
                for (auto* handle : laneResizeHandles_) {
                    if (handle->contains(handle->mapFromScene(scenePos))) {
                        int trackIdx = handle->data(0).toInt();
                        if (trackIdx >= 0 && trackIdx < static_cast<int>(layout_.size())) {
                            laneResizing_ = true;
                            laneResizeTrackIndex_ = trackIdx;
                            laneResizeStartHeight_ = layout_[trackIdx].automationLaneHeight;
                            laneResizeStartY_ = scenePos.y();
                            graphicsView_->viewport()->setCursor(Qt::SizeVerCursor);
                            return true;
                        }
                    }
                }
            }
        }

        if (event->type() == QEvent::MouseMove && laneResizing_) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            QPointF scenePos = graphicsView_->mapToScene(mouseEvent->pos());
            double delta = scenePos.y() - laneResizeStartY_;
            double newHeight = std::clamp(laneResizeStartHeight_ + delta, kMinLaneHeight, kMaxLaneHeight);
            if (laneResizeTrackIndex_ >= 0 && laneResizeTrackIndex_ < static_cast<int>(layout_.size())) {
                layout_[laneResizeTrackIndex_].automationLaneHeight = newHeight;
                rebuildLayout();
                rebuildClips();
                rebuildTrackHeaders();
            }
            return true;
        }

        if (event->type() == QEvent::MouseButtonRelease && laneResizing_) {
            laneResizing_ = false;
            laneResizeTrackIndex_ = -1;
            graphicsView_->viewport()->unsetCursor();
            return true;
        }

        if (event->type() == QEvent::Wheel) {
            auto* wheelEvent = static_cast<QWheelEvent*>(event);

            if (wheelEvent->modifiers() & Qt::ControlModifier) {
                const QPoint delta = wheelEvent->angleDelta();
                if (delta.y() != 0) {
                    const double viewX = wheelEvent->position().x();
                    const int oldScrollX = graphicsView_->horizontalScrollBar()->value();
                    const double beatAtCursor = (oldScrollX + viewX) / pixelsPerBeat_;

                    const double zoomSteps = static_cast<double>(delta.y()) / 120.0;
                    const double zoomFactor = std::pow(1.15, zoomSteps);
                    setPixelsPerBeat(pixelsPerBeat_ * zoomFactor);

                    const int newScrollX = static_cast<int>(
                        std::round(beatAtCursor * pixelsPerBeat_ - viewX));
                    graphicsView_->horizontalScrollBar()->setValue(newScrollX);
                }

                wheelEvent->accept();
                return true;
            }
        }

        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape && isMidiClipDrawActive_) {
                scene_->cancelBackgroundDrag();
                clearMidiClipDrawPreview();
                keyEvent->accept();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
                deleteSelectedClips();
                keyEvent->accept();
                return true;
            }
            if (keyEvent->key() == Qt::Key_I && !(keyEvent->modifiers() & Qt::ControlModifier)) {
                setLoopInAtPlayhead();
                keyEvent->accept();
                return true;
            }
            if (keyEvent->key() == Qt::Key_O && !(keyEvent->modifiers() & Qt::ControlModifier)) {
                setLoopOutAtPlayhead();
                keyEvent->accept();
                return true;
            }
            if (keyEvent->key() == Qt::Key_A && selectedTrack_) {
                int trackIdx = -1;
                auto tracks = getVisibleTracks();
                for (int i = 0; i < tracks.size(); ++i) {
                    if (tracks[i] == selectedTrack_) { trackIdx = i; break; }
                }
                if (trackIdx >= 0 && trackIdx < static_cast<int>(layout_.size())) {
                    bool vis = !layout_[trackIdx].automationVisible;
                    toggleAutomation(selectedTrack_, vis);
                }
                keyEvent->accept();
                return true;
            }
        }
    }

    if (watched == headerScrollArea_ || watched == headerScrollArea_->viewport()) {
        if (event->type() == QEvent::Wheel) {
            QCoreApplication::sendEvent(graphicsView_->viewport(), event);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TimelineView::onTransportPositionChanged()
{
    updatePlayhead();
}

void TimelineView::onEditChanged()
{
    qDebug() << "[TimelineView] onEditChanged start";
    snapper_.setBpm(editMgr_->getBpm());
    snapper_.setTimeSig(editMgr_->getTimeSigNumerator(),
                        editMgr_->getTimeSigDenominator());
    ruler_->setBpm(editMgr_->getBpm());
    ruler_->setTimeSig(editMgr_->getTimeSigNumerator(),
                       editMgr_->getTimeSigDenominator());
    qDebug() << "[TimelineView] about to rebuildClips";
    rebuildClips();
    for (auto* header : trackHeaders_)
        header->refresh();
    syncLoopStateFromTransport();
    qDebug() << "[TimelineView] onEditChanged done";
}

void TimelineView::onTracksChanged()
{
    if (insideTracksChanged_) return;
    insideTracksChanged_ = true;

    qDebug() << "[TimelineView] onTracksChanged start";
    auto tracks = editMgr_->getAudioTracks();
    bool selectedTrackStillExists = false;
    for (auto* track : tracks) {
        if (track == selectedTrack_) {
            selectedTrackStillExists = true;
            break;
        }
    }
    if (!selectedTrackStillExists && selectedTrack_ != nullptr) {
        selectedTrack_ = nullptr;
        emit trackSelected(nullptr);
    }

    onEditChanged();
    qDebug() << "[TimelineView] about to rebuildTrackHeaders";
    rebuildTrackHeaders();

    // Clean up display order to remove deleted track IDs (preserving folder entries)
    auto rawOrder = editMgr_->loadRawDisplayOrder();
    if (!rawOrder.isEmpty()) {
        auto currentTracks = editMgr_->getAudioTracks();
        QStringList cleanedOrder;
        for (auto& entry : rawOrder) {
            if (entry.startsWith("f")) {
                cleanedOrder.append(entry);
            } else {
                uint64_t rawId = entry.toULongLong();
                for (auto* t : currentTracks) {
                    if (t->itemID.getRawID() == rawId) {
                        cleanedOrder.append(entry);
                        break;
                    }
                }
            }
        }
        if (cleanedOrder.size() != rawOrder.size())
            editMgr_->saveRawDisplayOrder(cleanedOrder);
    }

    qDebug() << "[TimelineView] onTracksChanged done";
    insideTracksChanged_ = false;
}

void TimelineView::rebuildTrackHeaders()
{
    qDebug() << "[rebuildTrackHeaders] start";

    for (auto* h : trackHeaders_) {
        headerVLayout_->removeWidget(h);
        delete h;
    }
    trackHeaders_.clear();

    for (auto* h : folderHeaders_) {
        headerVLayout_->removeWidget(h);
        delete h;
    }
    folderHeaders_.clear();

    for (auto* h : automationLaneHeaders_) {
        headerVLayout_->removeWidget(h);
        delete h;
    }
    automationLaneHeaders_.clear();
    orderedHeaders_.clear();

    if (!editMgr_ || !editMgr_->edit()) return;

    auto displayItems = editMgr_->getDisplayItems();
    int trackLayoutIdx = 0;

    for (auto& item : displayItems) {
        if (item.type == EditManager::DisplayItem::Folder) {
            auto* fh = new FolderHeaderWidget(item.folderId, editMgr_, headerContainer_);
            connect(fh, &FolderHeaderWidget::collapseToggled,
                    this, [this](int, bool) {
                        QTimer::singleShot(0, this, [this]() {
                            rebuildLayout();
                            rebuildClips();
                            rebuildTrackHeaders();
                        });
                    });
            connect(fh, &FolderHeaderWidget::dragStarted,
                    this, &TimelineView::onFolderDragStarted);
            connect(fh, &FolderHeaderWidget::dragMoved,
                    this, &TimelineView::onFolderDragMoved);
            connect(fh, &FolderHeaderWidget::dragFinished,
                    this, &TimelineView::onFolderDragFinished);
            headerVLayout_->addWidget(fh);
            folderHeaders_.push_back(fh);

            OrderedHeader oh;
            oh.isFolder = true;
            oh.folderId = item.folderId;
            oh.widget = fh;
            orderedHeaders_.push_back(oh);
            continue;
        }

        // Skip tracks hidden by collapsed parent folder
        if (item.parentFolderId > 0 && editMgr_->isFolderCollapsed(item.parentFolderId))
            continue;

        auto* track = item.track;
        qDebug() << "[rebuildTrackHeaders] creating header" << trackLayoutIdx
                 << QString::fromStdString(track->getName().toStdString());

        auto* header = new TrackHeaderWidget(track, editMgr_, headerContainer_);
        connect(header, &TrackHeaderWidget::instrumentSelectRequested,
                this, &TimelineView::instrumentSelectRequested);
        connect(header, &TrackHeaderWidget::trackSelected,
                this, &TimelineView::selectTrack);
        connect(header, &TrackHeaderWidget::automationToggled,
                this, &TimelineView::toggleAutomation);
        connect(header, &TrackHeaderWidget::collapseToggled,
                this, &TimelineView::toggleCollapse);
        connect(header, &TrackHeaderWidget::dragStarted,
                this, &TimelineView::onTrackDragStarted);
        connect(header, &TrackHeaderWidget::dragMoved,
                this, &TimelineView::onTrackDragMoved);
        connect(header, &TrackHeaderWidget::dragFinished,
                this, &TimelineView::onTrackDragFinished);
        header->setSelected(track == selectedTrack_);

        if (item.parentFolderId > 0)
            header->setIndented(true);

        if (trackLayoutIdx < static_cast<int>(layout_.size())) {
            header->setCollapsed(layout_[trackLayoutIdx].collapsed);
            header->setAutomationVisible(layout_[trackLayoutIdx].automationVisible);
        }

        headerVLayout_->addWidget(header);
        trackHeaders_.push_back(header);

        OrderedHeader oh;
        oh.isFolder = false;
        oh.track = track;
        oh.widget = header;
        orderedHeaders_.push_back(oh);

        if (trackLayoutIdx < static_cast<int>(layout_.size())
            && layout_[trackLayoutIdx].automationVisible
            && !layout_[trackLayoutIdx].collapsed) {
            auto* laneHeader = new AutomationLaneHeader(track, editMgr_, headerContainer_);
            int idx = trackLayoutIdx;
            connect(laneHeader, &AutomationLaneHeader::parameterChanged,
                    this, [this, idx](te::AutomatableParameter* p) {
                        onAutomationParamChanged(idx, p);
                    });
            connect(laneHeader, &AutomationLaneHeader::closeRequested,
                    this, [this, track]() { toggleAutomation(track, false); });
            laneHeader->setLaneHeight(int(layout_[trackLayoutIdx].automationLaneHeight));
            laneHeader->selectParam(layout_[trackLayoutIdx].shownParam);

            headerVLayout_->addWidget(laneHeader);
            automationLaneHeaders_.push_back(laneHeader);
        }

        trackLayoutIdx++;
    }

    int minNeeded = 0;
    for (int i = 0; i < static_cast<int>(trackHeaders_.size()); ++i) {
        if (i < static_cast<int>(layout_.size()) && layout_[i].collapsed) continue;
        minNeeded = std::max(minNeeded, trackHeaders_[i]->minimumSizeHint().height());
    }

    if (trackHeight_ < minNeeded) {
        trackHeight_ = minNeeded;
        rebuildClips();
    }

    bool needsRebuild = false;
    for (int i = 0; i < static_cast<int>(trackHeaders_.size()); ++i) {
        double h = (i < static_cast<int>(layout_.size())) ? layout_[i].clipRowHeight : trackHeight_;
        trackHeaders_[i]->setTrackHeight(static_cast<int>(h));
        int actualH = trackHeaders_[i]->height();
        if (i < static_cast<int>(layout_.size()) && actualH > layout_[i].clipRowHeight) {
            layout_[i].clipRowHeight = actualH;
            needsRebuild = true;
        }
    }
    if (needsRebuild) {
        double y = 0.0;
        for (int i = 0; i < static_cast<int>(layout_.size()); ++i) {
            layout_[i].yOffset = y;
            y += layout_[i].totalHeight();
        }
        rebuildClips();
    }

    qDebug() << "[rebuildTrackHeaders] done, trackHeight =" << trackHeight_;
}

void TimelineView::syncHeaderScroll()
{
    int vScrollVal = graphicsView_->verticalScrollBar()->value();
    headerScrollArea_->verticalScrollBar()->setValue(vScrollVal);
}

void TimelineView::toggleMarkerTempoLane()
{
    if (markerTempoRow_) {
        markerTempoRow_->setVisible(!markerTempoRow_->isVisible());
    }
}

void TimelineView::onTrackDragStarted(TrackHeaderWidget* header)
{
    reorderDragSourceIndex_ = -1;
    for (int i = 0; i < static_cast<int>(orderedHeaders_.size()); ++i) {
        if (orderedHeaders_[i].widget == header) {
            reorderDragSourceIndex_ = i;
            break;
        }
    }
    reorderCurrentIndex_ = reorderDragSourceIndex_;
    reorderGroupSize_ = 1;

    if (!reorderGhost_ && header->track()) {
        reorderGhost_ = new QLabel(nullptr);
        reorderGhost_->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        reorderGhost_->setAttribute(Qt::WA_TransparentForMouseEvents);
        QString name = QString::fromStdString(header->track()->getName().toStdString());
        reorderGhost_->setText(name);
        reorderGhost_->setStyleSheet(
            "QLabel { background: rgba(0,168,150,200); color: #fff; "
            "font-size: 11px; font-weight: bold; padding: 4px 12px; "
            "border-radius: 4px; }");
        reorderGhost_->adjustSize();
        reorderGhost_->show();
    }
}

void TimelineView::onTrackDragMoved(TrackHeaderWidget* /*header*/, int globalY)
{
    if (reorderDragSourceIndex_ < 0) return;
    int n = static_cast<int>(orderedHeaders_.size());
    if (n <= 1) return;
    reorderLastGlobalY_ = globalY;

    if (reorderGhost_)
        reorderGhost_->move(QPoint(QCursor::pos().x() + 15, globalY - 10));

    QPoint containerPos = headerContainer_->mapFromGlobal(QPoint(0, globalY));

    // Highlight folder under cursor, clear others
    for (auto* fh : folderHeaders_) {
        bool over = containerPos.y() >= fh->geometry().top()
                 && containerPos.y() <= fh->geometry().bottom();
        fh->setDropHighlight(over);
    }

    // If cursor is over a folder header, don't reorder -- just hold
    // position and let the highlight indicate a drop target.
    for (int i = 0; i < n; ++i) {
        if (i == reorderCurrentIndex_) continue;
        if (!orderedHeaders_[i].isFolder) continue;
        auto* w = orderedHeaders_[i].widget;
        if (containerPos.y() >= w->geometry().top()
            && containerPos.y() <= w->geometry().bottom())
            return;
    }

    int targetIndex = n - 1;
    for (int i = 0; i < n; ++i) {
        auto* w = orderedHeaders_[i].widget;
        int mid = w->geometry().top() + w->height() / 2;
        if (containerPos.y() < mid) {
            targetIndex = i;
            break;
        }
    }
    targetIndex = std::clamp(targetIndex, 0, n - 1);

    if (targetIndex == reorderCurrentIndex_) return;

    auto moving = orderedHeaders_[reorderCurrentIndex_];
    orderedHeaders_.erase(orderedHeaders_.begin() + reorderCurrentIndex_);
    orderedHeaders_.insert(orderedHeaders_.begin() + targetIndex, moving);

    // Rebuild layout and reapply indent based on current positions
    for (auto& oh : orderedHeaders_)
        headerVLayout_->removeWidget(oh.widget);
    for (auto* h : automationLaneHeaders_)
        headerVLayout_->removeWidget(h);

    int currentFolder = 0;
    for (auto& oh : orderedHeaders_) {
        if (oh.isFolder) {
            currentFolder = oh.folderId;
        } else if (oh.track) {
            auto* th = qobject_cast<TrackHeaderWidget*>(oh.widget);
            if (th) {
                int fid = editMgr_->getTrackFolderId(oh.track);
                bool inFolder = (fid > 0) || (currentFolder > 0 && oh.widget == moving.widget);
                th->setIndented(inFolder);
            }
        }
        headerVLayout_->addWidget(oh.widget);
    }

    reorderCurrentIndex_ = targetIndex;
}

void TimelineView::onTrackDragFinished(TrackHeaderWidget* /*header*/)
{
    // Clear folder highlights
    for (auto* fh : folderHeaders_)
        fh->setDropHighlight(false);

    // Check if the drop landed on a folder header
    reorderDroppedOnFolder_ = false;
    reorderDropFolderId_ = 0;
    if (reorderLastGlobalY_ != 0 && reorderCurrentIndex_ >= 0) {
        QPoint containerPos = headerContainer_->mapFromGlobal(
            QPoint(0, reorderLastGlobalY_));
        for (int fi = 0; fi < static_cast<int>(orderedHeaders_.size()); ++fi) {
            if (!orderedHeaders_[fi].isFolder) continue;
            if (fi == reorderCurrentIndex_) continue;
            auto* w = orderedHeaders_[fi].widget;
            if (containerPos.y() >= w->geometry().top()
                && containerPos.y() <= w->geometry().bottom()) {
                reorderDroppedOnFolder_ = true;
                reorderDropFolderId_ = orderedHeaders_[fi].folderId;

                // Place dragged entry right after this folder header
                if (reorderCurrentIndex_ != fi + 1) {
                    auto moving = orderedHeaders_[reorderCurrentIndex_];
                    orderedHeaders_.erase(orderedHeaders_.begin() + reorderCurrentIndex_);
                    int insertAt = (reorderCurrentIndex_ < fi) ? fi : fi + 1;
                    insertAt = std::clamp(insertAt, 0,
                        static_cast<int>(orderedHeaders_.size()));
                    orderedHeaders_.insert(orderedHeaders_.begin() + insertAt, moving);
                    reorderCurrentIndex_ = insertAt;
                }
                break;
            }
        }
    }

    commitReorder();
}

void TimelineView::onFolderDragStarted(FolderHeaderWidget* header)
{
    reorderDragSourceIndex_ = -1;
    for (int i = 0; i < static_cast<int>(orderedHeaders_.size()); ++i) {
        if (orderedHeaders_[i].widget == header) {
            reorderDragSourceIndex_ = i;
            break;
        }
    }
    reorderCurrentIndex_ = reorderDragSourceIndex_;

    // Count the folder + its children as a group
    reorderGroupSize_ = 1;
    if (reorderDragSourceIndex_ >= 0) {
        int fid = orderedHeaders_[reorderDragSourceIndex_].folderId;
        for (int j = reorderDragSourceIndex_ + 1; j < static_cast<int>(orderedHeaders_.size()); ++j) {
            if (orderedHeaders_[j].isFolder) break;
            if (orderedHeaders_[j].track && editMgr_->getTrackFolderId(orderedHeaders_[j].track) == fid)
                reorderGroupSize_++;
            else
                break;
        }
    }

    if (!reorderGhost_) {
        reorderGhost_ = new QLabel(nullptr);
        reorderGhost_->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        reorderGhost_->setAttribute(Qt::WA_TransparentForMouseEvents);
        QString name = editMgr_->getFolderName(header->folderId());
        reorderGhost_->setText(name);
        reorderGhost_->setStyleSheet(
            "QLabel { background: rgba(200,160,40,200); color: #fff; "
            "font-size: 11px; font-weight: bold; padding: 4px 12px; "
            "border-radius: 4px; }");
        reorderGhost_->adjustSize();
        reorderGhost_->show();
    }
}

void TimelineView::onFolderDragMoved(FolderHeaderWidget* /*header*/, int globalY)
{
    if (reorderDragSourceIndex_ < 0) return;
    int n = static_cast<int>(orderedHeaders_.size());
    if (n <= 1) return;
    reorderLastGlobalY_ = globalY;

    if (reorderGhost_)
        reorderGhost_->move(QPoint(QCursor::pos().x() + 15, globalY - 10));

    QPoint containerPos = headerContainer_->mapFromGlobal(QPoint(0, globalY));
    int targetIndex = n - reorderGroupSize_;
    for (int i = 0; i < n; ++i) {
        auto* w = orderedHeaders_[i].widget;
        int mid = w->geometry().top() + w->height() / 2;
        if (containerPos.y() < mid) {
            targetIndex = i;
            break;
        }
    }
    targetIndex = std::clamp(targetIndex, 0, n - reorderGroupSize_);

    if (targetIndex == reorderCurrentIndex_) return;

    // Move the folder group as a unit
    std::vector<OrderedHeader> group(
        orderedHeaders_.begin() + reorderCurrentIndex_,
        orderedHeaders_.begin() + reorderCurrentIndex_ + reorderGroupSize_);
    orderedHeaders_.erase(
        orderedHeaders_.begin() + reorderCurrentIndex_,
        orderedHeaders_.begin() + reorderCurrentIndex_ + reorderGroupSize_);
    if (targetIndex > reorderCurrentIndex_)
        targetIndex -= reorderGroupSize_;
    targetIndex = std::clamp(targetIndex, 0, static_cast<int>(orderedHeaders_.size()));
    orderedHeaders_.insert(orderedHeaders_.begin() + targetIndex,
                           group.begin(), group.end());

    for (auto& oh : orderedHeaders_)
        headerVLayout_->removeWidget(oh.widget);
    for (auto* h : automationLaneHeaders_)
        headerVLayout_->removeWidget(h);
    for (auto& oh : orderedHeaders_)
        headerVLayout_->addWidget(oh.widget);

    reorderCurrentIndex_ = targetIndex;
}

void TimelineView::onFolderDragFinished(FolderHeaderWidget* /*header*/)
{
    commitReorder();
}

void TimelineView::commitReorder()
{
    if (reorderGhost_) {
        delete reorderGhost_;
        reorderGhost_ = nullptr;
    }

    if (reorderDragSourceIndex_ < 0 || !editMgr_ || !editMgr_->edit()) {
        reorderDragSourceIndex_ = -1;
        reorderCurrentIndex_ = -1;
        reorderGroupSize_ = 1;
        reorderDroppedOnFolder_ = false;
        reorderDropFolderId_ = 0;
        reorderLastGlobalY_ = 0;
        return;
    }

    if (reorderCurrentIndex_ != reorderDragSourceIndex_
        || reorderDroppedOnFolder_) {
        // For single track drags: update ONLY the dragged track's folder.
        if (reorderGroupSize_ == 1) {
            auto& dragged = orderedHeaders_[reorderCurrentIndex_];
            if (!dragged.isFolder && dragged.track) {
                int newFolderId = 0;

                // If dropped on a folder header, join that folder
                if (reorderDroppedOnFolder_) {
                    newFolderId = reorderDropFolderId_;
                } else if (reorderCurrentIndex_ > 0) {
                    // Otherwise derive from previous entry
                    auto& prev = orderedHeaders_[reorderCurrentIndex_ - 1];
                    if (prev.isFolder)
                        newFolderId = prev.folderId;
                    else if (prev.track)
                        newFolderId = editMgr_->getTrackFolderId(prev.track);
                }
                editMgr_->moveTrackToFolder(dragged.track, newFolderId);
            }
        }
        // For folder drags (reorderGroupSize_ > 1): no membership changes.

        // Build display order from visible headers, re-inserting hidden
        // children of collapsed folders so they aren't lost.
        // Use a set to prevent any track appearing twice.
        QSet<uint64_t> addedTrackIds;

        auto allItems = editMgr_->getDisplayItems();
        QMap<int, QVector<EditManager::DisplayItem>> hiddenChildren;
        for (auto& fi : allItems) {
            if (fi.type == EditManager::DisplayItem::Track
                && fi.parentFolderId > 0
                && editMgr_->isFolderCollapsed(fi.parentFolderId)) {
                hiddenChildren[fi.parentFolderId].append(fi);
            }
        }

        QVector<EditManager::DisplayItem> newItems;
        for (auto& oh : orderedHeaders_) {
            EditManager::DisplayItem di;
            if (oh.isFolder) {
                di.type = EditManager::DisplayItem::Folder;
                di.folderId = oh.folderId;
                newItems.append(di);
                if (hiddenChildren.contains(oh.folderId)) {
                    for (auto& hidden : hiddenChildren[oh.folderId]) {
                        uint64_t tid = hidden.track->itemID.getRawID();
                        if (!addedTrackIds.contains(tid)) {
                            newItems.append(hidden);
                            addedTrackIds.insert(tid);
                        }
                    }
                }
            } else if (oh.track) {
                uint64_t tid = oh.track->itemID.getRawID();
                if (!addedTrackIds.contains(tid)) {
                    di.type = EditManager::DisplayItem::Track;
                    di.track = oh.track;
                    di.parentFolderId = editMgr_->getTrackFolderId(oh.track);
                    newItems.append(di);
                    addedTrackIds.insert(tid);
                }
            }
        }
        editMgr_->saveDisplayItems(newItems);
    }

    reorderDragSourceIndex_ = -1;
    reorderCurrentIndex_ = -1;
    reorderGroupSize_ = 1;
    reorderDroppedOnFolder_ = false;
    reorderDropFolderId_ = 0;
    reorderLastGlobalY_ = 0;

    onTracksChanged();
}

juce::Array<te::AudioTrack*> TimelineView::getVisibleTracks() const
{
    juce::Array<te::AudioTrack*> result;
    if (!editMgr_ || !editMgr_->edit()) return result;
    auto items = editMgr_->getDisplayItems();
    for (auto& item : items) {
        if (item.type != EditManager::DisplayItem::Track) continue;
        if (item.parentFolderId > 0 && editMgr_->isFolderCollapsed(item.parentFolderId))
            continue;
        result.add(item.track);
    }
    return result;
}

void TimelineView::rebuildLayout()
{
    if (!editMgr_ || !editMgr_->edit()) {
        layout_.clear();
        folderDividers_.clear();
        return;
    }

    auto displayItems = editMgr_->getDisplayItems();

    // Save old layout state keyed by track ID
    std::map<uint64_t, TrackLayoutInfo> oldStateById;
    auto oldVisibleTracks = getVisibleTracks();
    for (int i = 0; i < static_cast<int>(layout_.size()) && i < oldVisibleTracks.size(); ++i)
        oldStateById[oldVisibleTracks[i]->itemID.getRawID()] = layout_[i];

    layout_.clear();
    folderDividers_.clear();

    double y = 0.0;
    int trackLayoutIdx = 0;

    for (auto& item : displayItems) {
        if (item.type == EditManager::DisplayItem::Folder) {
            FolderDividerInfo div;
            div.folderId = item.folderId;
            div.yOffset = y;
            folderDividers_.push_back(div);
            y += FolderDividerInfo::kHeight;
            continue;
        }

        if (item.parentFolderId > 0 && editMgr_->isFolderCollapsed(item.parentFolderId))
            continue;

        TrackLayoutInfo info;
        info.trackIndex = trackLayoutIdx;

        auto it = oldStateById.find(item.track->itemID.getRawID());
        if (it != oldStateById.end()) {
            info.automationVisible = it->second.automationVisible;
            info.automationLaneHeight = it->second.automationLaneHeight;
            info.shownParam = it->second.shownParam;
            info.collapsed = it->second.collapsed;
            info.clipRowHeight = it->second.clipRowHeight;
        } else {
            info.automationVisible = false;
            info.automationLaneHeight = kDefaultLaneHeight;
            info.shownParam = nullptr;
            info.collapsed = false;
            info.clipRowHeight = trackHeight_;
        }

        if (!info.collapsed)
            info.clipRowHeight = trackHeight_;

        if (info.automationVisible && !info.shownParam)
            info.shownParam = editMgr_->getVolumeParam(item.track);

        info.yOffset = y;
        layout_.push_back(info);
        y += info.totalHeight();
        trackLayoutIdx++;
    }
}

double TimelineView::trackYOffset(int trackIndex) const
{
    if (trackIndex >= 0 && trackIndex < static_cast<int>(layout_.size()))
        return layout_[trackIndex].yOffset;
    return trackIndex * trackHeight_;
}

int TimelineView::trackIndexAtSceneY(double sceneY) const
{
    for (int i = 0; i < static_cast<int>(layout_.size()); ++i) {
        double top = layout_[i].yOffset;
        double bottom = top + layout_[i].totalHeight();
        if (sceneY >= top && sceneY < bottom)
            return i;
    }
    if (!layout_.empty() && sceneY >= layout_.back().yOffset + layout_.back().totalHeight())
        return static_cast<int>(layout_.size()) - 1;
    return 0;
}

void TimelineView::cleanupAutomationItems()
{
    for (auto* item : automationLaneItems_) {
        if (item->scene()) scene_->removeItem(item);
        delete item;
    }
    automationLaneItems_.clear();

    // Note: automationLaneHeaders_ are cleaned up by rebuildTrackHeaders(),
    // not here, to avoid double-delete.

    for (auto* handle : laneResizeHandles_) {
        if (handle->scene()) scene_->removeItem(handle);
        delete handle;
    }
    laneResizeHandles_.clear();
}

void TimelineView::rebuildAutomationLanes(double sceneWidth)
{
    cleanupAutomationItems();
    if (!editMgr_ || !editMgr_->edit()) return;

    auto tracks = getVisibleTracks();
    auto& theme = ThemeManager::instance().current();

    for (int i = 0; i < static_cast<int>(layout_.size()); ++i) {
        if (!layout_[i].automationVisible || layout_[i].collapsed || i >= tracks.size()) continue;

        auto* track = tracks[i];
        auto* param = layout_[i].shownParam;
        if (!param) param = editMgr_->getVolumeParam(track);
        if (!param) continue;

        double laneY = layout_[i].yOffset + layout_[i].clipRowHeight;
        double laneH = layout_[i].automationLaneHeight;

        auto* laneItem = new AutomationLaneItem(param, editMgr_->edit(),
                                                 pixelsPerBeat_, laneH,
                                                 &snapper_);
        laneItem->setPos(0, laneY);
        laneItem->setSceneWidth(sceneWidth);
        laneItem->setZValue(2);
        scene_->addItem(laneItem);
        automationLaneItems_.push_back(laneItem);

        // Resize handle at bottom of lane
        auto* handle = scene_->addRect(0, laneY + laneH - kResizeHandleHeight,
                                        sceneWidth, kResizeHandleHeight,
                                        QPen(Qt::NoPen),
                                        QBrush(theme.border.lighter(130)));
        handle->setZValue(3);
        handle->setCursor(Qt::SizeVerCursor);
        handle->setData(0, i);
        laneResizeHandles_.push_back(handle);
    }
}

void TimelineView::toggleAutomation(te::AudioTrack* track, bool visible)
{
    if (!editMgr_) return;
    auto visTracks = getVisibleTracks();
    for (int i = 0; i < visTracks.size(); ++i) {
        if (visTracks[i] == track && i < static_cast<int>(layout_.size())) {
            layout_[i].automationVisible = visible;
            if (visible && !layout_[i].shownParam)
                layout_[i].shownParam = editMgr_->getVolumeParam(track);

            QTimer::singleShot(0, this, [this]() {
                rebuildLayout();
                rebuildClips();
                rebuildTrackHeaders();
            });
            return;
        }
    }
}

void TimelineView::toggleCollapse(te::AudioTrack* track, bool collapsed)
{
    if (!editMgr_) return;
    auto visTracks = getVisibleTracks();
    for (int i = 0; i < visTracks.size(); ++i) {
        if (visTracks[i] == track && i < static_cast<int>(layout_.size())) {
            layout_[i].collapsed = collapsed;
            layout_[i].clipRowHeight = collapsed ? kCollapsedTrackHeight : trackHeight_;

            QTimer::singleShot(0, this, [this]() {
                rebuildLayout();
                rebuildClips();
                rebuildTrackHeaders();
            });
            return;
        }
    }
}

void TimelineView::onAutomationParamChanged(int trackIndex, te::AutomatableParameter* param)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(layout_.size())) return;
    layout_[trackIndex].shownParam = param;
    QTimer::singleShot(0, this, [this]() {
        rebuildClips();
        rebuildTrackHeaders();
    });
}

void TimelineView::rebuildClips()
{
    qDebug() << "[rebuildClips] start";
    auto& theme = ThemeManager::instance().current();

    for (auto* item : clipItems_) scene_->removeItem(item);
    for (auto* item : trackBgItems_) scene_->removeItem(item);
    for (auto* item : gridLineItems_) scene_->removeItem(item);
    for (auto* item : trackSeparatorItems_) scene_->removeItem(item);
    clearMidiClipDrawPreview();
    cleanupAutomationItems();

    qDeleteAll(clipItems_);
    qDeleteAll(trackBgItems_);
    qDeleteAll(gridLineItems_);
    qDeleteAll(trackSeparatorItems_);
    clipItems_.clear();
    trackBgItems_.clear();
    gridLineItems_.clear();
    trackSeparatorItems_.clear();

    if (!editMgr_ || !editMgr_->edit()) return;

    rebuildLayout();

    auto tracks = getVisibleTracks();
    int numTracks = tracks.size();
    qDebug() << "[rebuildClips] numTracks:" << numTracks;

    double beatsPerBar = editMgr_ ? editMgr_->getTimeSigNumerator() : 4;
    double totalBeats = beatsPerBar * 200.0;
    for (auto* track : tracks) {
        for (auto* clip : track->getClips()) {
            auto& ts = clip->edit.tempoSequence;
            auto endTime = clip->getPosition().getEnd();
            const double endBeat = ts.toBeats(endTime).inBeats();
            totalBeats = std::max(totalBeats, endBeat + beatsPerBar * 8.0);
        }
    }
    double sceneWidth = totalBeats * pixelsPerBeat_;
    double sceneHeight = 0.0;
    if (!layout_.empty())
        sceneHeight = layout_.back().yOffset + layout_.back().totalHeight();
    if (!folderDividers_.empty()) {
        double folderBottom = folderDividers_.back().yOffset + FolderDividerInfo::kHeight;
        sceneHeight = std::max(sceneHeight, folderBottom);
    }
    if (sceneHeight <= 0.0)
        sceneHeight = numTracks * trackHeight_;
    scene_->setSceneRect(0, 0, sceneWidth, std::max(sceneHeight, double(height())));

    playheadLine_->setLine(0, 0, 0, sceneHeight);

    // Draw folder divider backgrounds and collapsed clip previews
    QColor folderBg = theme.surface.lighter(112);
    QPen folderBorder(QColor(255, 185, 50, 80), 1.0);
    for (auto& div : folderDividers_) {
        auto* bgItem = scene_->addRect(0, div.yOffset, sceneWidth,
                                        FolderDividerInfo::kHeight,
                                        folderBorder, QBrush(folderBg));
        bgItem->setZValue(-1.5);
        trackBgItems_.push_back(bgItem);

        if (!editMgr_->isFolderCollapsed(div.folderId)) continue;

        // Gather child tracks and draw mini clip rectangles
        auto allTracks = editMgr_->getAudioTracks();
        int childCount = 0;
        for (auto* t : allTracks)
            if (editMgr_->getTrackFolderId(t) == div.folderId)
                childCount++;
        if (childCount == 0) continue;

        double laneH = FolderDividerInfo::kHeight - 4.0;
        double perTrackH = std::max(2.0, std::min(laneH / childCount, 6.0));
        int childIdx = 0;

        for (auto* t : allTracks) {
            if (editMgr_->getTrackFolderId(t) != div.folderId) continue;
            bool isMidi = editMgr_->isMidiTrack(t);
            QColor clipColor = isMidi ? theme.midiClipBody.lighter(120)
                                      : theme.clipBody.lighter(120);
            clipColor.setAlpha(180);
            double clipY = div.yOffset + 2.0 + childIdx * perTrackH;

            auto& ts = editMgr_->edit()->tempoSequence;
            for (auto* clip : t->getClips()) {
                double startBeat = ts.toBeats(clip->getPosition().getStart()).inBeats();
                double endBeat = ts.toBeats(clip->getPosition().getEnd()).inBeats();
                double cx = startBeat * pixelsPerBeat_;
                double cw = std::max(2.0, (endBeat - startBeat) * pixelsPerBeat_);

                auto* miniClip = scene_->addRect(
                    cx, clipY, cw, std::max(1.5, perTrackH - 0.5),
                    QPen(Qt::NoPen), QBrush(clipColor));
                miniClip->setZValue(-1.0);
                trackBgItems_.push_back(miniClip);
            }
            childIdx++;
        }
    }

    for (int i = 0; i < numTracks; ++i) {
        double y = trackYOffset(i);
        double thisH = (i < static_cast<int>(layout_.size())) ? layout_[i].clipRowHeight : trackHeight_;
        QColor bg = (i % 2 == 0) ? theme.trackBackground
                                  : theme.trackBackground.lighter(108);
        auto* bgItem = scene_->addRect(0, y, sceneWidth, thisH,
                                        QPen(Qt::NoPen), QBrush(bg));
        bgItem->setZValue(-2);
        trackBgItems_.push_back(bgItem);

        if (i < static_cast<int>(layout_.size()) && layout_[i].automationVisible && !layout_[i].collapsed) {
            double laneY = y + thisH;
            double laneH = layout_[i].automationLaneHeight;
            QColor laneBg = theme.surface.darker(110);
            auto* laneBgItem = scene_->addRect(0, laneY, sceneWidth, laneH,
                                                QPen(Qt::NoPen), QBrush(laneBg));
            laneBgItem->setZValue(-2);
            trackBgItems_.push_back(laneBgItem);
        }
    }

    QPen separatorPen(theme.border.lighter(120), 1.0);
    for (int i = 0; i <= numTracks; ++i) {
        double y;
        if (i < numTracks)
            y = trackYOffset(i);
        else if (!layout_.empty())
            y = layout_.back().yOffset + layout_.back().totalHeight();
        else
            y = numTracks * trackHeight_;
        auto* line = scene_->addLine(0, y, sceneWidth, y, separatorPen);
        line->setZValue(-0.5);
        trackSeparatorItems_.push_back(line);
    }

    drawGridLines();
    qDebug() << "[rebuildClips] grid done, building clip items";

    for (int ti = 0; ti < numTracks; ++ti) {
        auto* track = tracks[ti];
        double clipH = (ti < static_cast<int>(layout_.size())) ? layout_[ti].clipRowHeight : trackHeight_;
        int clipIdx = 0;
        for (auto* clip : track->getClips()) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                if (editMgr_->isLinkedSecondary(mc))
                    continue;
            }

            qDebug() << "[rebuildClips] track" << ti << "clip" << clipIdx
                     << QString::fromStdString(clip->getName().toStdString());
            auto* item = new ClipItem(clip, ti, pixelsPerBeat_, clipH);
            item->setDragContext(&snapper_, editMgr_,
                                &pixelsPerBeat_, &trackHeight_, numTracks,
                                [this]() { rebuildClips(); });
            item->setTrackYOffsetFunc([this](int idx) { return trackYOffset(idx); });
            if (item->isMidiClip()) {
                if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                    auto linked = editMgr_->getLinkedMidiClips(track, mc);
                    item->setLinkedChannelCount(static_cast<int>(linked.size()));
                    item->loadMidiPreviewFromClips(linked);
                } else {
                    item->loadMidiPreview();
                }
            } else {
                const int waveformPoints = std::clamp(
                    static_cast<int>(std::max(50.0, item->rect().width() / 2.0)),
                    50, 4096);
                item->loadWaveform(waveformPoints);
            }
            item->updateGeometry(pixelsPerBeat_, clipH, 0);
            double yOff = trackYOffset(ti);
            item->setPos(item->pos().x(), yOff);
            item->setRect(0, 0, item->rect().width(), clipH - 2);
            item->setZValue(1);
            scene_->addItem(item);
            clipItems_.push_back(item);
            ++clipIdx;
        }
    }

    rebuildAutomationLanes(sceneWidth);
    updateLoopOverlay();
    qDebug() << "[rebuildClips] done";
}

void TimelineView::drawGridLines()
{
    auto& theme = ThemeManager::instance().current();
    double sceneW = scene_->sceneRect().width();
    double sceneH = scene_->sceneRect().height();

    double beatsPerBar = editMgr_ ? editMgr_->getTimeSigNumerator() : 4;
    double totalBeats = sceneW / pixelsPerBeat_;

    for (double beat = 0; beat < totalBeats; beat += 1.0) {
        double x = beat * pixelsPerBeat_;
        bool isMajor = (std::fmod(beat, beatsPerBar) < 0.01);
        QPen pen(isMajor ? theme.gridLineMajor : theme.gridLine,
                 isMajor ? 1.0 : 0.5);
        auto* line = scene_->addLine(x, 0, x, sceneH, pen);
        line->setZValue(-1);
        gridLineItems_.push_back(line);
    }
}

// ── Lane resize handling ─────────────────────────────────────────────────────


void TimelineView::updatePlayhead()
{
    if (!editMgr_ || !editMgr_->edit()) return;

    auto& ts = editMgr_->edit()->tempoSequence;
    auto pos = editMgr_->transport().getPosition();
    double beat = ts.toBeats(pos).inBeats();

    double x = beat * pixelsPerBeat_;
    playheadLine_->setLine(x, 0, x, scene_->sceneRect().height());
    ruler_->setPlayheadBeat(beat);

    bool isPlaying = editMgr_->transport().isPlaying();

    for (auto* lane : automationLaneItems_)
        lane->setPlayheadBeat(beat);

    // When transport stops, refresh automation lanes to show any
    // newly recorded automation data from the engine's punchOut
    if (wasPlaying_ && !isPlaying) {
        for (auto* lane : automationLaneItems_)
            lane->rebuildFromCurve();
    }
    wasPlaying_ = isPlaying;

    if (markerTempoLane_ && markerTempoLane_->isVisible()) {
        double playBeat = ts.toBeats(editMgr_->transport().getPosition()).inBeats();
        markerTempoLane_->setPlayheadBeat(playBeat);
    }

    if (isPlaying) {
        graphicsView_->ensureVisible(x, graphicsView_->mapToScene(
            graphicsView_->viewport()->rect().center()).y(),
            50, 10, 50, 0);
    }
}

// ── Loop region helpers ──────────────────────────────────────────────────────

void TimelineView::updateLoopOverlay()
{
    if (!loopOverlayItem_) return;

    bool visible = ruler_->loopEnabled() && ruler_->loopOutBeat() > ruler_->loopInBeat();
    loopOverlayItem_->setVisible(visible);
    if (visible) {
        double x1 = ruler_->loopInBeat() * pixelsPerBeat_;
        double x2 = ruler_->loopOutBeat() * pixelsPerBeat_;
        double h = scene_->sceneRect().height();
        if (h < 1.0) h = 2000.0;
        loopOverlayItem_->setRect(x1, 0, x2 - x1, h);
    }
}

void TimelineView::setLoopInAtPlayhead()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& ts = editMgr_->edit()->tempoSequence;
    double beat = ts.toBeats(editMgr_->transport().getPosition()).inBeats();
    beat = snapper_.snapBeat(std::max(0.0, beat));

    double outBeat = ruler_->loopOutBeat();
    if (outBeat <= beat)
        outBeat = beat + editMgr_->getTimeSigNumerator();

    ruler_->setLoopRegion(beat, outBeat);
    ruler_->setLoopEnabled(true);
    applyLoopRegionToTransport(beat, outBeat);
    updateLoopOverlay();
}

void TimelineView::setLoopOutAtPlayhead()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& ts = editMgr_->edit()->tempoSequence;
    double beat = ts.toBeats(editMgr_->transport().getPosition()).inBeats();
    beat = snapper_.snapBeat(std::max(0.0, beat));

    double inBeat = ruler_->loopInBeat();
    if (inBeat >= beat)
        inBeat = std::max(0.0, beat - editMgr_->getTimeSigNumerator());

    ruler_->setLoopRegion(inBeat, beat);
    ruler_->setLoopEnabled(true);
    applyLoopRegionToTransport(inBeat, beat);
    updateLoopOverlay();
}

void TimelineView::applyLoopRegionToTransport(double inBeat, double outBeat)
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& ts = editMgr_->edit()->tempoSequence;
    auto& transport = editMgr_->transport();

    auto inTime = ts.toTime(tracktion::BeatPosition::fromBeats(inBeat));
    auto outTime = ts.toTime(tracktion::BeatPosition::fromBeats(outBeat));

    transport.loopPoint1 = inTime;
    transport.loopPoint2 = outTime;

    updateLoopOverlay();
}

void TimelineView::onLoopToggled(bool enabled)
{
    Q_UNUSED(enabled);
    syncLoopStateFromTransport();
}

void TimelineView::syncLoopStateFromTransport()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& ts = editMgr_->edit()->tempoSequence;
    auto& transport = editMgr_->transport();

    bool looping = transport.looping.get();
    double inBeat = ts.toBeats(transport.loopPoint1.get()).inBeats();
    double outBeat = ts.toBeats(transport.loopPoint2.get()).inBeats();

    ruler_->setLoopRegion(std::max(0.0, inBeat), std::max(0.0, outBeat));
    ruler_->setLoopEnabled(looping);
    updateLoopOverlay();
}

void TimelineView::handleFileDrop(const QString& path, double xPos, int yPos)
{
    if (!editMgr_ || !editMgr_->edit()) return;

    juce::File droppedFile(path.toStdString());
    if (!droppedFile.existsAsFile()) return;

    double beat = xPos / pixelsPerBeat_;
    int trackIdx = trackIndexAtSceneY(double(yPos));

    beat = snapper_.snapBeat(beat);
    if (beat < 0) beat = 0;

    auto tracks = getVisibleTracks();
    if (trackIdx < 0) trackIdx = 0;
    if (trackIdx >= tracks.size()) trackIdx = tracks.size() - 1;

    if (tracks.isEmpty()) return;

    auto ext = droppedFile.getFileExtension().toLowerCase();
    bool isMidiFile = (ext == ".mid" || ext == ".midi");
    bool destIsMidiTrack = editMgr_->isMidiTrack(tracks[trackIdx]);

    if (isMidiFile) {
        editMgr_->importMidiFileToTrack(*tracks[trackIdx], droppedFile, beat);
    } else {
        if (destIsMidiTrack) {
            int altIdx = -1;
            for (int i = 0; i < tracks.size(); ++i) {
                if (!editMgr_->isMidiTrack(tracks[i])) { altIdx = i; break; }
            }
            if (altIdx < 0) {
                auto* newTrack = editMgr_->addAudioTrack();
                if (!newTrack) return;
                editMgr_->addAudioClipToTrack(*newTrack, droppedFile, beat);
            } else {
                editMgr_->addAudioClipToTrack(*tracks[altIdx], droppedFile, beat);
            }
        } else {
            editMgr_->addAudioClipToTrack(*tracks[trackIdx], droppedFile, beat);
        }
    }
    rebuildClips();
}

void TimelineView::splitSelectedClipsAtPlayhead()
{
    if (!editMgr_ || !editMgr_->edit())
        return;

    const auto playheadTime = editMgr_->transport().getPosition();
    bool didSplitAny = false;

    std::vector<te::MidiClip*> linkedToSplit;

    for (auto* item : clipItems_) {
        if (!item || !item->isSelected())
            continue;

        auto* clip = item->clip();
        if (!clip)
            continue;

        const auto clipRange = clip->getPosition().time;
        if (playheadTime <= clipRange.getStart() || playheadTime >= clipRange.getEnd())
            continue;

        if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
            auto* track = mc->getAudioTrack();
            if (track) {
                auto linked = editMgr_->getLinkedMidiClips(track, mc);
                for (auto* lc : linked) {
                    if (lc != mc)
                        linkedToSplit.push_back(lc);
                }
            }
        }

        if (auto* clipTrack = dynamic_cast<te::ClipTrack*>(clip->getClipTrack())) {
            if (clipTrack->splitClip(*clip, playheadTime) != nullptr)
                didSplitAny = true;
        }
    }

    for (auto* lc : linkedToSplit) {
        if (auto* clipTrack = dynamic_cast<te::ClipTrack*>(lc->getClipTrack())) {
            clipTrack->splitClip(*lc, playheadTime);
        }
    }

    if (didSplitAny)
        rebuildClips();
}

void TimelineView::deleteSelectedClips()
{
    if (!editMgr_ || !editMgr_->edit()) return;

    std::vector<te::Clip*> toDelete;
    for (auto* item : clipItems_) {
        if (item && item->isSelected() && item->clip())
            toDelete.push_back(item->clip());
    }

    if (toDelete.empty()) return;

    std::vector<te::Clip*> linkedToDelete;
    for (auto* clip : toDelete) {
        if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
            auto* track = mc->getAudioTrack();
            if (track) {
                auto linked = editMgr_->getLinkedMidiClips(track, mc);
                for (auto* lc : linked) {
                    if (lc != mc)
                        linkedToDelete.push_back(lc);
                }
            }
        }
    }
    for (auto* lc : linkedToDelete)
        toDelete.push_back(lc);

    for (auto* clip : toDelete)
        clip->removeFromParent();

    rebuildClips();
    emit selectedClipsDeleted();
}

void TimelineView::copySelectedClips()
{
    if (!editMgr_ || !editMgr_->edit()) return;

    clipboardEntries_.clear();

    for (auto* item : clipItems_) {
        if (!item || !item->isSelected() || !item->clip()) continue;
        auto* clip = item->clip();
        clip->flushStateToValueTree();
        ClipboardEntry entry;
        entry.state = clip->state.createCopy();
        entry.trackOffset = item->trackIndex();
        clipboardEntries_.push_back(entry);
    }

    if (!clipboardEntries_.empty())
        clipboardSourceTrackIndex_ = clipboardEntries_.front().trackOffset;
}

void TimelineView::cutSelectedClips()
{
    copySelectedClips();
    if (!clipboardEntries_.empty())
        deleteSelectedClips();
}

void TimelineView::pasteClips()
{
    if (clipboardEntries_.empty() || !editMgr_ || !editMgr_->edit()) return;

    auto& transport = editMgr_->edit()->getTransport();
    auto playheadTime = transport.getPosition();
    auto& ts = editMgr_->edit()->tempoSequence;
    double playheadBeat = ts.toBeats(playheadTime).inBeats();

    auto tracks = getVisibleTracks();
    if (tracks.isEmpty()) return;

    for (auto& entry : clipboardEntries_) {
        int targetTrackIdx = entry.trackOffset;
        if (targetTrackIdx < 0) targetTrackIdx = 0;
        if (targetTrackIdx >= tracks.size()) targetTrackIdx = tracks.size() - 1;

        auto* dstTrack = dynamic_cast<te::ClipTrack*>(tracks[targetTrackIdx]);
        if (!dstTrack) continue;

        auto newState = entry.state.createCopy();
        te::EditItemID::remapIDs(newState, nullptr, *editMgr_->edit());

        dstTrack->state.appendChild(newState, &editMgr_->edit()->getUndoManager());
        auto newClipId = te::EditItemID::fromID(newState);

        if (auto* newClip = dstTrack->findClipForID(newClipId)) {
            newClip->setStart(ts.toTime(tracktion::BeatPosition::fromBeats(playheadBeat)),
                              false, true);
        }
    }

    rebuildClips();
}

void TimelineView::selectTrack(te::AudioTrack* track)
{
    selectedTrack_ = track;
    for (auto* h : trackHeaders_)
        h->setSelected(h->track() == track);

    emit trackSelected(track);
}

void TimelineView::setSelectedTrack(te::AudioTrack* track)
{
    selectTrack(track);
}

void TimelineView::clearTrackSelection()
{
    selectedTrack_ = nullptr;
    for (auto* h : trackHeaders_)
        h->setSelected(false);
}

void TimelineView::handleEmptyAreaDoubleClick(double sceneX, double sceneY)
{
    if (!editMgr_ || !editMgr_->edit()) return;

    double beat = sceneX / pixelsPerBeat_;
    int trackIdx = trackIndexAtSceneY(sceneY);

    beat = snapper_.snapBeat(beat);
    if (beat < 0) beat = 0;

    auto tracks = getVisibleTracks();
    if (trackIdx < 0 || trackIdx >= tracks.size() || tracks.isEmpty()) return;

    auto* track = tracks[trackIdx];
    if (!editMgr_->isMidiTrack(track)) return;

    double beatsPerBar = editMgr_->getTimeSigNumerator();
    double barBeat = std::floor(beat / beatsPerBar) * beatsPerBar;

    auto* clip = editMgr_->addMidiClipToTrack(*track, barBeat, beatsPerBar);
    rebuildClips();

    if (clip) {
        selectClipItem(clip);
        emit editMgr_->midiClipDoubleClicked(clip);
    }
}

void TimelineView::handleBackgroundDragStarted(QPointF startScenePos)
{
    clearMidiClipDrawPreview();
    if (!editMgr_ || !editMgr_->edit())
        return;

    auto tracks = getVisibleTracks();
    if (tracks.isEmpty())
        return;

    const int trackIdx = trackIndexAtSceneY(startScenePos.y());
    if (trackIdx < 0 || trackIdx >= tracks.size())
        return;

    auto* track = tracks[trackIdx];
    if (!editMgr_->isMidiTrack(track))
        return;

    midiClipDrawTrack_ = track;
    midiClipDrawStartBeat_ = snapper_.snapBeat(startScenePos.x() / pixelsPerBeat_);
    if (midiClipDrawStartBeat_ < 0.0)
        midiClipDrawStartBeat_ = 0.0;
    isMidiClipDrawActive_ = true;

    auto& theme = ThemeManager::instance().current();
    QPen pen(theme.accent, 1.25, Qt::DashLine);
    QColor fill = theme.midiClipBody;
    fill.setAlpha(95);

    double previewH = (trackIdx < static_cast<int>(layout_.size())) ? layout_[trackIdx].clipRowHeight : trackHeight_;
    midiClipDrawPreviewItem_ = scene_->addRect(0, 0, 1.0, previewH - 2.0, pen, QBrush(fill));
    midiClipDrawPreviewItem_->setZValue(1.2);
    midiClipDrawPreviewItem_->setPos(midiClipDrawStartBeat_ * pixelsPerBeat_,
                                     trackYOffset(trackIdx));
}

void TimelineView::handleBackgroundDragUpdated(QPointF, QPointF currentScenePos)
{
    if (!isMidiClipDrawActive_ || !midiClipDrawPreviewItem_ || !midiClipDrawTrack_)
        return;

    auto tracks = getVisibleTracks();
    int trackIdx = -1;
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i] == midiClipDrawTrack_) {
            trackIdx = i;
            break;
        }
    }
    if (trackIdx < 0) {
        clearMidiClipDrawPreview();
        return;
    }

    double endBeat = snapper_.snapBeat(currentScenePos.x() / pixelsPerBeat_);
    if (endBeat < 0.0)
        endBeat = 0.0;

    const double leftBeat = std::min(midiClipDrawStartBeat_, endBeat);
    const double rightBeat = std::max(midiClipDrawStartBeat_, endBeat);
    const double widthPx = std::max(1.0, (rightBeat - leftBeat) * pixelsPerBeat_);

    double previewH = (trackIdx < static_cast<int>(layout_.size())) ? layout_[trackIdx].clipRowHeight : trackHeight_;
    midiClipDrawPreviewItem_->setRect(0, 0, widthPx, previewH - 2.0);
    midiClipDrawPreviewItem_->setPos(leftBeat * pixelsPerBeat_, trackYOffset(trackIdx));

    const double rightEdgePx = rightBeat * pixelsPerBeat_ + 120.0;
    QRectF sceneRect = scene_->sceneRect();
    if (rightEdgePx > sceneRect.width())
        scene_->setSceneRect(0, 0, rightEdgePx, sceneRect.height());
}

void TimelineView::handleBackgroundDragFinished(QPointF, QPointF endScenePos)
{
    if (!editMgr_ || !editMgr_->edit() || !isMidiClipDrawActive_ || !midiClipDrawTrack_) {
        clearMidiClipDrawPreview();
        return;
    }

    double endBeat = snapper_.snapBeat(endScenePos.x() / pixelsPerBeat_);
    if (endBeat < 0.0)
        endBeat = 0.0;

    const double startBeat = std::min(midiClipDrawStartBeat_, endBeat);
    const double draggedLengthBeats = std::abs(endBeat - midiClipDrawStartBeat_);
    double minLengthBeats = snapper_.gridIntervalBeats();
    if (minLengthBeats <= 0.0)
        minLengthBeats = 0.25;
    const double clipLengthBeats = std::max(draggedLengthBeats, minLengthBeats);

    auto* clip = editMgr_->addMidiClipToTrack(*midiClipDrawTrack_, startBeat, clipLengthBeats);
    clearMidiClipDrawPreview();
    rebuildClips();

    if (clip) {
        selectClipItem(clip);
        emit editMgr_->midiClipDoubleClicked(clip);
    }
}

void TimelineView::clearMidiClipDrawPreview()
{
    if (midiClipDrawPreviewItem_ && scene_) {
        scene_->removeItem(midiClipDrawPreviewItem_);
        delete midiClipDrawPreviewItem_;
    }
    midiClipDrawPreviewItem_ = nullptr;
    midiClipDrawTrack_ = nullptr;
    midiClipDrawStartBeat_ = 0.0;
    isMidiClipDrawActive_ = false;
}

void TimelineView::selectClipItem(te::Clip* clip)
{
    for (auto* item : clipItems_) {
        if (!item)
            continue;
        item->setSelected(item->clip() == clip);
    }
}

} // namespace OpenDaw
