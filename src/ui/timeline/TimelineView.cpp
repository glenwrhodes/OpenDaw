#include "TimelineView.h"
#include "utils/ThemeManager.h"
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMimeData>
#include <QUrl>
#include <QScrollBar>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QMenu>
#include <QDebug>
#include <QLineF>
#include <cmath>

namespace freedaw {

// ── TimelineScene ───────────────────────────────────────────────────────────

TimelineScene::TimelineScene(QObject* parent) : QGraphicsScene(parent) {}

void TimelineScene::cancelBackgroundDrag()
{
    backgroundDragCandidate_ = false;
    backgroundDragging_ = false;
}

void TimelineScene::dragEnterEvent(QGraphicsSceneDragDropEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void TimelineScene::dragMoveEvent(QGraphicsSceneDragDropEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void TimelineScene::dropEvent(QGraphicsSceneDragDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;

    for (const auto& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        QString path = url.toLocalFile();
        QPointF pos = event->scenePos();
        emit fileDropped(path, pos.x(), int(pos.y()));
    }
    event->acceptProposedAction();
}

void TimelineScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        auto hitItems = items(event->scenePos());
        bool hitClip = false;
        for (auto* item : hitItems) {
            if (dynamic_cast<ClipItem*>(item)) { hitClip = true; break; }
        }
        backgroundDragCandidate_ = !hitClip;
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
        bool hitClip = false;
        for (auto* item : hitItems) {
            if (dynamic_cast<ClipItem*>(item)) { hitClip = true; break; }
        }
        if (!hitClip) {
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
    bool hitClip = false;
    for (auto* item : hitItems) {
        if (dynamic_cast<ClipItem*>(item)) { hitClip = true; break; }
    }
    if (!hitClip) {
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

    // Sync ruler scroll with graphics view horizontal scroll
    connect(graphicsView_->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) {
                ruler_->setScrollX(val);
            });

    // Sync track header vertical scroll with timeline vertical scroll
    connect(graphicsView_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &TimelineView::syncHeaderScroll);

    // Ruler click -> snap to grid, then set transport position
    connect(ruler_, &TimeRuler::positionClicked, this, [this](double beat) {
        if (!editMgr_ || !editMgr_->edit()) return;
        beat = snapper_.snapBeat(beat);
        auto& ts = editMgr_->edit()->tempoSequence;
        auto time = ts.toTime(tracktion::BeatPosition::fromBeats(beat));
        editMgr_->transport().setPosition(time);
    });

    // Ruler drag -> smooth scrub without snapping
    connect(ruler_, &TimeRuler::positionDragged, this, [this](double beat) {
        if (!editMgr_ || !editMgr_->edit()) return;
        auto& ts = editMgr_->edit()->tempoSequence;
        auto time = ts.toTime(tracktion::BeatPosition::fromBeats(beat));
        editMgr_->transport().setPosition(time);
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

                int trackIdx = static_cast<int>(scenePos.y() / trackHeight_);
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
                    menu.addAction("Remove Track", [this, track]() {
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
        for (auto* h : trackHeaders_) {
            headerVLayout_->removeWidget(h);
            delete h;
        }
        trackHeaders_.clear();

        for (auto* item : clipItems_) scene_->removeItem(item);
        qDeleteAll(clipItems_);
        clipItems_.clear();
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
    for (auto* h : trackHeaders_) {
        headerVLayout_->removeWidget(h);
        delete h;
    }
    trackHeaders_.clear();

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
        header->setSelected(track == selectedTrack_);
        headerVLayout_->addWidget(header);
        trackHeaders_.push_back(header);
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

void TimelineView::rebuildClips()
{
    qDebug() << "[rebuildClips] start";
    auto& theme = ThemeManager::instance().current();

    for (auto* item : clipItems_) scene_->removeItem(item);
    for (auto* item : trackBgItems_) scene_->removeItem(item);
    for (auto* item : gridLineItems_) scene_->removeItem(item);
    for (auto* item : trackSeparatorItems_) scene_->removeItem(item);
    clearMidiClipDrawPreview();

    qDeleteAll(clipItems_);
    qDeleteAll(trackBgItems_);
    qDeleteAll(gridLineItems_);
    qDeleteAll(trackSeparatorItems_);
    clipItems_.clear();
    trackBgItems_.clear();
    gridLineItems_.clear();
    trackSeparatorItems_.clear();

    if (!editMgr_ || !editMgr_->edit()) return;

    auto tracks = editMgr_->getAudioTracks();
    int numTracks = tracks.size();
    qDebug() << "[rebuildClips] numTracks:" << numTracks;

    double totalBeats = 200.0;
    for (auto* track : tracks) {
        for (auto* clip : track->getClips()) {
            auto& ts = clip->edit.tempoSequence;
            auto endTime = clip->getPosition().getEnd();
            const double endBeat = ts.toBeats(endTime).inBeats();
            totalBeats = std::max(totalBeats, endBeat + 16.0);
        }
    }
    double sceneWidth = totalBeats * pixelsPerBeat_;
    double sceneHeight = numTracks * trackHeight_;
    scene_->setSceneRect(0, 0, sceneWidth, std::max(sceneHeight, double(height())));

    playheadLine_->setLine(0, 0, 0, sceneHeight);

    for (int i = 0; i < numTracks; ++i) {
        double y = i * trackHeight_;
        QColor bg = (i % 2 == 0) ? theme.trackBackground
                                  : theme.trackBackground.lighter(108);
        auto* bgItem = scene_->addRect(0, y, sceneWidth, trackHeight_,
                                        QPen(Qt::NoPen), QBrush(bg));
        bgItem->setZValue(-2);
        trackBgItems_.push_back(bgItem);
    }

    // Horizontal track separators so row boundaries are always clear.
    QPen separatorPen(theme.border.lighter(120), 1.0);
    for (int i = 0; i <= numTracks; ++i) {
        const double y = i * trackHeight_;
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
            qDebug() << "[rebuildClips] track" << ti << "clip" << clipIdx
                     << QString::fromStdString(clip->getName().toStdString());
            auto* item = new ClipItem(clip, ti, pixelsPerBeat_, trackHeight_);
            item->setDragContext(&snapper_, editMgr_,
                                &pixelsPerBeat_, &trackHeight_, numTracks,
                                [this]() { rebuildClips(); });
            if (item->isMidiClip()) {
                qDebug() << "[rebuildClips]   -> loadMidiPreview";
                item->loadMidiPreview();
            } else {
                qDebug() << "[rebuildClips]   -> loadWaveform";
                const int waveformPoints = std::clamp(
                    static_cast<int>(std::max(50.0, item->rect().width() / 2.0)),
                    50, 4096);
                item->loadWaveform(waveformPoints);
            }
            qDebug() << "[rebuildClips]   -> loaded ok";
            item->updateGeometry(pixelsPerBeat_, trackHeight_, 0);
            item->setZValue(1);
            scene_->addItem(item);
            clipItems_.push_back(item);
            ++clipIdx;
        }
    }
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

void TimelineView::updatePlayhead()
{
    if (!editMgr_ || !editMgr_->edit()) return;

    auto& ts = editMgr_->edit()->tempoSequence;
    auto pos = editMgr_->transport().getPosition();
    double beat = ts.toBeats(pos).inBeats();

    double x = beat * pixelsPerBeat_;
    playheadLine_->setLine(x, 0, x, scene_->sceneRect().height());
    ruler_->setPlayheadBeat(beat);

    if (editMgr_->transport().isPlaying()) {
        graphicsView_->ensureVisible(x, graphicsView_->mapToScene(
            graphicsView_->viewport()->rect().center()).y(),
            50, 10, 50, 0);
    }
}

void TimelineView::handleFileDrop(const QString& path, double xPos, int yPos)
{
    if (!editMgr_ || !editMgr_->edit()) return;

    juce::File droppedFile(path.toStdString());
    if (!droppedFile.existsAsFile()) return;

    double beat = xPos / pixelsPerBeat_;
    int trackIdx = int(yPos / trackHeight_);

    beat = snapper_.snapBeat(beat);
    if (beat < 0) beat = 0;

    auto tracks = editMgr_->getAudioTracks();
    if (trackIdx < 0) trackIdx = 0;
    if (trackIdx >= tracks.size()) trackIdx = tracks.size() - 1;

    if (tracks.isEmpty()) return;

    auto ext = droppedFile.getFileExtension().toLowerCase();
    if (ext == ".mid" || ext == ".midi") {
        editMgr_->importMidiFileToTrack(*tracks[trackIdx], droppedFile, beat);
    } else {
        editMgr_->addAudioClipToTrack(*tracks[trackIdx], droppedFile, beat);
    }
    rebuildClips();
}

void TimelineView::splitSelectedClipsAtPlayhead()
{
    if (!editMgr_ || !editMgr_->edit())
        return;

    const auto playheadTime = editMgr_->transport().getPosition();
    bool didSplitAny = false;

    for (auto* item : clipItems_) {
        if (!item || !item->isSelected())
            continue;

        auto* clip = item->clip();
        if (!clip)
            continue;

        const auto clipRange = clip->getPosition().time;
        if (playheadTime <= clipRange.getStart() || playheadTime >= clipRange.getEnd())
            continue;

        if (auto* clipTrack = dynamic_cast<te::ClipTrack*>(clip->getClipTrack())) {
            if (clipTrack->splitClip(*clip, playheadTime) != nullptr)
                didSplitAny = true;
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

void TimelineView::handleEmptyAreaDoubleClick(double sceneX, double sceneY)
{
    if (!editMgr_ || !editMgr_->edit()) return;

    double beat = sceneX / pixelsPerBeat_;
    int trackIdx = static_cast<int>(sceneY / trackHeight_);

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

    const int trackIdx = static_cast<int>(startScenePos.y() / trackHeight_);
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
                                     trackIdx * trackHeight_);
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
    midiClipDrawPreviewItem_->setPos(leftBeat * pixelsPerBeat_, trackIdx * trackHeight_);

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

} // namespace freedaw
