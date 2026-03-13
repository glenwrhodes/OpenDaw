#include "TimelineView.h"
#include "utils/ThemeManager.h"
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMimeData>
#include <QUrl>
#include <QScrollBar>
#include <QWheelEvent>
#include <QMenu>
#include <cmath>

namespace freedaw {

// ── TimelineScene ───────────────────────────────────────────────────────────

TimelineScene::TimelineScene(QObject* parent) : QGraphicsScene(parent) {}

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
    graphicsView_->viewport()->installEventFilter(this);
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

    connect(editMgr_, &EditManager::tracksChanged,
            this, &TimelineView::onTracksChanged);
    connect(editMgr_, &EditManager::editChanged,
            this, &TimelineView::onEditChanged);

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
    trackHeight_ = std::clamp(h, 30.0, 250.0);
    rebuildClips();
    rebuildTrackHeaders();
}

void TimelineView::zoomIn()      { setPixelsPerBeat(pixelsPerBeat_ * 1.3); }
void TimelineView::zoomOut()     { setPixelsPerBeat(pixelsPerBeat_ / 1.3); }
void TimelineView::zoomVerticalIn()  { setTrackHeight(trackHeight_ * 1.2); }
void TimelineView::zoomVerticalOut() { setTrackHeight(trackHeight_ / 1.2); }

bool TimelineView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == graphicsView_->viewport() && event->type() == QEvent::Wheel) {
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

    return QWidget::eventFilter(watched, event);
}

void TimelineView::onTransportPositionChanged()
{
    updatePlayhead();
}

void TimelineView::onEditChanged()
{
    snapper_.setBpm(editMgr_->getBpm());
    snapper_.setTimeSig(editMgr_->getTimeSigNumerator(),
                        editMgr_->getTimeSigDenominator());
    ruler_->setBpm(editMgr_->getBpm());
    ruler_->setTimeSig(editMgr_->getTimeSigNumerator(),
                       editMgr_->getTimeSigDenominator());
    rebuildClips();
}

void TimelineView::onTracksChanged()
{
    onEditChanged();
    rebuildTrackHeaders();
}

void TimelineView::rebuildTrackHeaders()
{
    for (auto* h : trackHeaders_) {
        headerVLayout_->removeWidget(h);
        h->deleteLater();
    }
    trackHeaders_.clear();

    if (!editMgr_ || !editMgr_->edit()) return;

    auto tracks = editMgr_->getAudioTracks();
    for (auto* track : tracks) {
        auto* header = new TrackHeaderWidget(track, editMgr_, headerContainer_);
        header->setTrackHeight(int(trackHeight_));
        connect(header, &TrackHeaderWidget::instrumentSelectRequested,
                this, &TimelineView::instrumentSelectRequested);
        headerVLayout_->addWidget(header);
        trackHeaders_.push_back(header);
    }
}

void TimelineView::syncHeaderScroll()
{
    int vScrollVal = graphicsView_->verticalScrollBar()->value();
    headerScrollArea_->verticalScrollBar()->setValue(vScrollVal);
}

void TimelineView::rebuildClips()
{
    auto& theme = ThemeManager::instance().current();

    for (auto* item : clipItems_) scene_->removeItem(item);
    for (auto* item : trackBgItems_) scene_->removeItem(item);
    for (auto* item : gridLineItems_) scene_->removeItem(item);

    qDeleteAll(clipItems_);
    qDeleteAll(trackBgItems_);
    qDeleteAll(gridLineItems_);
    clipItems_.clear();
    trackBgItems_.clear();
    gridLineItems_.clear();

    if (!editMgr_ || !editMgr_->edit()) return;

    auto tracks = editMgr_->getAudioTracks();
    int numTracks = tracks.size();

    // Keep a sensible minimum range (~50 bars in 4/4), but grow to fit loaded clips.
    double totalBeats = 200.0;
    for (auto* track : tracks) {
        for (auto* clip : track->getClips()) {
            auto& ts = clip->edit.tempoSequence;
            auto endTime = clip->getPosition().getEnd();
            const double endBeat = ts.toBeats(endTime).inBeats();
            totalBeats = std::max(totalBeats, endBeat + 16.0); // add right-side scroll headroom
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

    drawGridLines();

    for (int ti = 0; ti < numTracks; ++ti) {
        auto* track = tracks[ti];
        for (auto* clip : track->getClips()) {
            auto* item = new ClipItem(clip, ti, pixelsPerBeat_, trackHeight_);
            item->setDragContext(&snapper_, editMgr_,
                                &pixelsPerBeat_, &trackHeight_, numTracks,
                                [this]() { rebuildClips(); });
            if (item->isMidiClip()) {
                item->loadMidiPreview();
            } else {
                const int waveformPoints = std::clamp(
                    static_cast<int>(std::max(50.0, item->rect().width() / 2.0)),
                    50, 4096);
                item->loadWaveform(waveformPoints);
            }
            item->updateGeometry(pixelsPerBeat_, trackHeight_, 0);
            item->setZValue(1);
            scene_->addItem(item);
            clipItems_.push_back(item);
        }
    }
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

    if (clip)
        emit editMgr_->midiClipDoubleClicked(clip);
}

} // namespace freedaw
