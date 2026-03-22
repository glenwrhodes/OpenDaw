#include "TimelineView.h"
#include "AutomationLaneItem.h"
#include "AutomationLaneHeader.h"
#include "AutomationPointItem.h"
#include "utils/ThemeManager.h"
#include <juce_audio_formats/juce_audio_formats.h>
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
    topRowLayout_->addWidget(headerCorner_);

    ruler_ = new TimeRuler(topRow);
    ruler_->setSnapFunction([this](double beat) { return snapper_.snapBeat(beat); });
    topRowLayout_->addWidget(ruler_, 1);

    outerLayout->addWidget(topRow);

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

    // Sync ruler scroll with graphics view horizontal scroll, and expand scene if near edge
    connect(graphicsView_->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) {
                ruler_->setScrollX(val);
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

    // Double-click on empty area -> create blank MIDI clip on MIDI tracks
    connect(scene_, &TimelineScene::emptyAreaDoubleClicked,
            this, &TimelineView::handleEmptyAreaDoubleClick);

    // Right-click on empty area -> timeline context menu
    connect(scene_, &TimelineScene::backgroundRightClicked,
            this, [this](QPointF scenePos, QPoint screenPos) {
                QMenu menu;
                menu.setAccessibleName("Timeline Context Menu");

                menu.addAction("Add Audio Track", [this]() {
                    editMgr_->addAudioTrack();
                });
                menu.addAction("Add MIDI Track", [this]() {
                    editMgr_->addMidiTrack();
                });

                int trackIdx = trackIndexAtSceneY(scenePos.y());
                auto tracks = editMgr_->getAudioTracks();
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

        for (auto* h : trackHeaders_) {
            headerVLayout_->removeWidget(h);
            delete h;
        }
        trackHeaders_.clear();

        for (auto* item : clipItems_) scene_->removeItem(item);
        qDeleteAll(clipItems_);
        clipItems_.clear();

        layout_.clear();
    });

    connect(editMgr_, &EditManager::tracksChanged,
            this, &TimelineView::onTracksChanged);
    connect(editMgr_, &EditManager::editChanged,
            this, &TimelineView::onEditChanged);
    connect(editMgr_, &EditManager::routingChanged,
            this, &TimelineView::rebuildTrackHeaders, Qt::QueuedConnection);

    onTracksChanged();
}

void TimelineView::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat_ = std::clamp(ppb, 5.0, 200.0);
    ruler_->setPixelsPerBeat(pixelsPerBeat_);
    rebuildClips();
}

void TimelineView::setTrackHeight(double h)
{
    double minH = 80.0;
    for (auto* hdr : trackHeaders_)
        minH = std::max(minH, double(hdr->minimumSizeHint().height()));
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
                auto tracks = editMgr_->getAudioTracks();
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
    qDebug() << "[TimelineView] onTracksChanged done";
}

void TimelineView::rebuildTrackHeaders()
{
    qDebug() << "[rebuildTrackHeaders] start";

    // Clean up old track headers
    for (auto* h : trackHeaders_) {
        headerVLayout_->removeWidget(h);
        delete h;
    }
    trackHeaders_.clear();

    // Clean up old automation lane headers (don't rely on cleanupAutomationItems)
    for (auto* h : automationLaneHeaders_) {
        headerVLayout_->removeWidget(h);
        delete h;
    }
    automationLaneHeaders_.clear();

    if (!editMgr_ || !editMgr_->edit()) return;

    auto tracks = editMgr_->getAudioTracks();
    for (int i = 0; i < tracks.size(); ++i) {
        auto* track = tracks[i];
        qDebug() << "[rebuildTrackHeaders] creating header" << i
                 << QString::fromStdString(track->getName().toStdString());
        auto* header = new TrackHeaderWidget(track, editMgr_, headerContainer_);
        connect(header, &TrackHeaderWidget::instrumentSelectRequested,
                this, &TimelineView::instrumentSelectRequested);
        connect(header, &TrackHeaderWidget::trackSelected,
                this, &TimelineView::selectTrack);
        connect(header, &TrackHeaderWidget::automationToggled,
                this, &TimelineView::toggleAutomation);
        header->setSelected(track == selectedTrack_);

        // Sync the automation toggle button to current layout state
        if (i < static_cast<int>(layout_.size()))
            header->setAutomationVisible(layout_[i].automationVisible);

        headerVLayout_->addWidget(header);
        trackHeaders_.push_back(header);

        // Add automation lane header if visible
        if (i < static_cast<int>(layout_.size()) && layout_[i].automationVisible) {
            auto* laneHeader = new AutomationLaneHeader(track, editMgr_, headerContainer_);
            int idx = i;
            connect(laneHeader, &AutomationLaneHeader::parameterChanged,
                    this, [this, idx](te::AutomatableParameter* p) {
                        onAutomationParamChanged(idx, p);
                    });
            connect(laneHeader, &AutomationLaneHeader::closeRequested,
                    this, [this, track]() { toggleAutomation(track, false); });
            laneHeader->setLaneHeight(int(layout_[i].automationLaneHeight));

            // Sync combo box to the currently displayed parameter
            laneHeader->selectParam(layout_[i].shownParam);

            headerVLayout_->addWidget(laneHeader);
            automationLaneHeaders_.push_back(laneHeader);
        }
    }

    int minNeeded = 0;
    for (auto* h : trackHeaders_)
        minNeeded = std::max(minNeeded, h->minimumSizeHint().height());

    if (trackHeight_ < minNeeded) {
        trackHeight_ = minNeeded;
        rebuildClips();
    }

    for (auto* h : trackHeaders_)
        h->setTrackHeight(int(trackHeight_));

    qDebug() << "[rebuildTrackHeaders] done, trackHeight =" << trackHeight_;
}

void TimelineView::syncHeaderScroll()
{
    int vScrollVal = graphicsView_->verticalScrollBar()->value();
    headerScrollArea_->verticalScrollBar()->setValue(vScrollVal);
}

void TimelineView::rebuildLayout()
{
    if (!editMgr_ || !editMgr_->edit()) {
        layout_.clear();
        return;
    }

    auto tracks = editMgr_->getAudioTracks();
    int numTracks = tracks.size();

    // Preserve existing automation state
    std::vector<TrackLayoutInfo> oldLayout = layout_;
    layout_.resize(numTracks);

    for (int i = 0; i < numTracks; ++i) {
        layout_[i].trackIndex = i;
        layout_[i].clipRowHeight = trackHeight_;

        if (i < static_cast<int>(oldLayout.size())) {
            layout_[i].automationVisible = oldLayout[i].automationVisible;
            layout_[i].automationLaneHeight = oldLayout[i].automationLaneHeight;
            layout_[i].shownParam = oldLayout[i].shownParam;
        } else {
            layout_[i].automationVisible = false;
            layout_[i].automationLaneHeight = kDefaultLaneHeight;
            layout_[i].shownParam = nullptr;
        }

        // Ensure shownParam defaults to Volume for any track with automation visible
        if (layout_[i].automationVisible && !layout_[i].shownParam) {
            layout_[i].shownParam = editMgr_->getVolumeParam(tracks[i]);
        }
    }

    // Compute Y offsets
    double y = 0.0;
    for (int i = 0; i < numTracks; ++i) {
        layout_[i].yOffset = y;
        y += layout_[i].totalHeight();
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

    auto tracks = editMgr_->getAudioTracks();
    auto& theme = ThemeManager::instance().current();

    for (int i = 0; i < static_cast<int>(layout_.size()); ++i) {
        if (!layout_[i].automationVisible || i >= tracks.size()) continue;

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
    auto tracks = editMgr_->getAudioTracks();
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i] == track && i < static_cast<int>(layout_.size())) {
            layout_[i].automationVisible = visible;
            if (visible && !layout_[i].shownParam)
                layout_[i].shownParam = editMgr_->getVolumeParam(track);

            // Defer rebuild so the button click handler that triggered this
            // finishes before we delete its owning TrackHeaderWidget.
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

    auto tracks = editMgr_->getAudioTracks();
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
    else
        sceneHeight = numTracks * trackHeight_;
    scene_->setSceneRect(0, 0, sceneWidth, std::max(sceneHeight, double(height())));

    playheadLine_->setLine(0, 0, 0, sceneHeight);

    for (int i = 0; i < numTracks; ++i) {
        double y = trackYOffset(i);
        QColor bg = (i % 2 == 0) ? theme.trackBackground
                                  : theme.trackBackground.lighter(108);
        auto* bgItem = scene_->addRect(0, y, sceneWidth, trackHeight_,
                                        QPen(Qt::NoPen), QBrush(bg));
        bgItem->setZValue(-2);
        trackBgItems_.push_back(bgItem);

        // Automation lane background
        if (i < static_cast<int>(layout_.size()) && layout_[i].automationVisible) {
            double laneY = y + trackHeight_;
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
        int clipIdx = 0;
        for (auto* clip : track->getClips()) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                if (editMgr_->isLinkedSecondary(mc))
                    continue;
            }

            qDebug() << "[rebuildClips] track" << ti << "clip" << clipIdx
                     << QString::fromStdString(clip->getName().toStdString());
            auto* item = new ClipItem(clip, ti, pixelsPerBeat_, trackHeight_);
            item->setDragContext(&snapper_, editMgr_,
                                &pixelsPerBeat_, &trackHeight_, numTracks,
                                [this]() { rebuildClips(); });
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
            item->updateGeometry(pixelsPerBeat_, trackHeight_, 0);
            double yOff = trackYOffset(ti);
            item->setPos(item->pos().x(), yOff);
            item->setRect(0, 0, item->rect().width(), trackHeight_ - 2);
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

    auto tracks = editMgr_->getAudioTracks();
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

    auto tracks = editMgr_->getAudioTracks();
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

    auto tracks = editMgr_->getAudioTracks();
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

    midiClipDrawPreviewItem_ = scene_->addRect(0, 0, 1.0, trackHeight_ - 2.0, pen, QBrush(fill));
    midiClipDrawPreviewItem_->setZValue(1.2);
    midiClipDrawPreviewItem_->setPos(midiClipDrawStartBeat_ * pixelsPerBeat_,
                                     trackYOffset(trackIdx));
}

void TimelineView::handleBackgroundDragUpdated(QPointF, QPointF currentScenePos)
{
    if (!isMidiClipDrawActive_ || !midiClipDrawPreviewItem_ || !midiClipDrawTrack_)
        return;

    auto tracks = editMgr_->getAudioTracks();
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

    midiClipDrawPreviewItem_->setRect(0, 0, widthPx, trackHeight_ - 2.0);
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
