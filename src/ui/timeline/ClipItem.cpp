#include "ClipItem.h"
#include "GridSnapper.h"
#include "engine/EditManager.h"
#include "utils/ThemeManager.h"
#include "utils/WaveformCache.h"
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QTimer>
#include <QMenu>
#include <algorithm>

namespace freedaw {

ClipItem::ClipItem(te::Clip* clip, int trackIndex, double pixelsPerBeat,
                   double trackHeight, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), clip_(clip), trackIndex_(trackIndex),
      isMidiClip_(dynamic_cast<te::MidiClip*>(clip) != nullptr)
{
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(QCursor(Qt::OpenHandCursor));
    updateGeometry(pixelsPerBeat, trackHeight, 0);
    setAcceptedMouseButtons(Qt::LeftButton);
    setToolTip(QString::fromStdString(clip->getName().toStdString()));
}

bool ClipItem::isNearRightEdge(const QPointF& localPos) const
{
    constexpr double edgeGrabWidth = 10.0;
    return localPos.x() >= (rect().width() - edgeGrabWidth);
}

double ClipItem::computeSnappedEndBeatFromSceneX(double sceneX) const
{
    if (!clip_ || !pixelsPerBeatPtr_)
        return 0.0;

    auto& ts = clip_->edit.tempoSequence;
    const double startBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double endBeat = sceneX / *pixelsPerBeatPtr_;

    if (snapper_)
        endBeat = snapper_->snapBeat(endBeat);

    double minLengthBeats = 0.1;
    if (snapper_) {
        const double interval = snapper_->gridIntervalBeats();
        if (interval > 0.0)
            minLengthBeats = interval;
    }

    return std::max(endBeat, startBeat + minLengthBeats);
}

void ClipItem::setDragContext(GridSnapper* snapper, EditManager* editMgr,
                               double* pixelsPerBeatPtr, double* trackHeightPtr,
                               int trackCount,
                               std::function<void()> requestRefresh)
{
    snapper_ = snapper;
    editMgr_ = editMgr;
    pixelsPerBeatPtr_ = pixelsPerBeatPtr;
    trackHeightPtr_ = trackHeightPtr;
    trackCount_ = trackCount;
    requestRefresh_ = std::move(requestRefresh);
}

void ClipItem::updateGeometry(double pixelsPerBeat, double trackHeight, double scrollY)
{
    if (!clip_) return;

    auto& edit = clip_->edit;
    auto& ts = edit.tempoSequence;

    auto startTime = clip_->getPosition().getStart();
    auto endTime   = clip_->getPosition().getEnd();
    double startBeat = ts.toBeats(startTime).inBeats();
    double endBeat   = ts.toBeats(endTime).inBeats();

    double x = startBeat * pixelsPerBeat;
    double w = (endBeat - startBeat) * pixelsPerBeat;
    double y = trackIndex_ * trackHeight - scrollY;

    setRect(0, 0, w, trackHeight - 2);
    setPos(x, y);
}

void ClipItem::loadWaveform(int numPoints)
{
    if (!clip_) return;

    if (auto* audioClip = dynamic_cast<te::WaveAudioClip*>(clip_)) {
        auto file = audioClip->getOriginalFile();
        if (file.existsAsFile()) {
            auto data = WaveformCache::instance().getWaveform(file, numPoints);
            waveMin_ = data.minValues;
            waveMax_ = data.maxValues;
        }
    }
}

void ClipItem::loadMidiPreview()
{
    midiNotes_.clear();
    midiLowestNote_ = 127;
    midiHighestNote_ = 0;

    if (!isMidiClip_ || !clip_) return;

    auto* midiClip = dynamic_cast<te::MidiClip*>(clip_);
    if (!midiClip) return;

    auto& seq = midiClip->getSequence();
    for (auto* note : seq.getNotes()) {
        MidiNotePreview np;
        np.pitch = note->getNoteNumber();
        np.startBeat = note->getStartBeat().inBeats();
        np.lengthBeats = note->getLengthBeats().inBeats();
        np.velocity = note->getVelocity();
        midiNotes_.push_back(np);

        midiLowestNote_ = std::min(midiLowestNote_, np.pitch);
        midiHighestNote_ = std::max(midiHighestNote_, np.pitch);
    }

    if (midiLowestNote_ > midiHighestNote_) {
        midiLowestNote_ = 60;
        midiHighestNote_ = 72;
    }

    int margin = 2;
    midiLowestNote_ = std::max(0, midiLowestNote_ - margin);
    midiHighestNote_ = std::min(127, midiHighestNote_ + margin);
}

void ClipItem::paintMidiNotes(QPainter* painter, const QRectF& r)
{
    auto& theme = ThemeManager::instance().current();
    if (midiNotes_.empty() || !clip_ || !pixelsPerBeatPtr_) return;

    int pitchRange = midiHighestNote_ - midiLowestNote_ + 1;
    double noteRowH = r.height() / pitchRange;
    double ppb = *pixelsPerBeatPtr_;

    painter->save();
    painter->setClipRect(r);

    for (auto& note : midiNotes_) {
        double x = r.left() + note.startBeat * ppb;
        double w = std::max(1.0, note.lengthBeats * ppb);
        double y = r.bottom() - (note.pitch - midiLowestNote_ + 1) * noteRowH;
        double h = std::max(1.0, noteRowH - 0.5);

        double alpha = 0.5 + 0.5 * (note.velocity / 127.0);
        QColor noteColor = theme.midiNotePreview;
        noteColor.setAlphaF(alpha);

        painter->fillRect(QRectF(x, y, w, h), noteColor);
    }

    painter->restore();
}

void ClipItem::paint(QPainter* painter,
                     const QStyleOptionGraphicsItem*,
                     QWidget*)
{
    auto& theme = ThemeManager::instance().current();
    QRectF r = rect();

    QColor bg;
    if (isMidiClip_)
        bg = isSelected() ? theme.midiClipBodySelected : theme.midiClipBody;
    else
        bg = isSelected() ? theme.clipBodySelected : theme.clipBody;
    painter->fillRect(r, bg);

    if (!waveMin_.empty() && !waveMax_.empty()) {
        int n = static_cast<int>(waveMin_.size());
        double segmentWidth = r.width();
        if (clip_ && pixelsPerBeatPtr_ && clip_->isLooping()) {
            double loopBeats = 0.0;
            if (clip_->beatBasedLooping()) {
                loopBeats = clip_->getLoopLengthBeats().inBeats();
            } else {
                auto& ts = clip_->edit.tempoSequence;
                const auto clipStartTime = clip_->getPosition().getStart();
                const auto loopEndTime = clipStartTime + clip_->getLoopLength();
                loopBeats = (ts.toBeats(loopEndTime) - ts.toBeats(clipStartTime)).inBeats();
            }

            if (loopBeats > 0.001)
                segmentWidth = std::max(1.0, loopBeats * *pixelsPerBeatPtr_);
        } else if (resizingRight_ && resizePreviewSegmentWidthPx_ > 1.0) {
            // During right-edge resize preview, keep waveform scale fixed and reveal/hide by clipping.
            segmentWidth = resizePreviewSegmentWidthPx_;
        }

        auto drawWaveSegment = [&](double left, double width) {
            if (width <= 0.0 || n <= 0)
                return;

            const double xScale = width / n;
            const double midY   = r.height() / 2.0;
            const double amp    = r.height() / 2.0 * 0.9;

            QPainterPath path;
            path.moveTo(left, r.top() + midY);
            for (int i = 0; i < n; ++i)
                path.lineTo(left + i * xScale,
                            r.top() + midY - waveMax_[size_t(i)] * amp);
            for (int i = n - 1; i >= 0; --i)
                path.lineTo(left + i * xScale,
                            r.top() + midY - waveMin_[size_t(i)] * amp);
            path.closeSubpath();

            painter->setPen(Qt::NoPen);
            painter->setBrush(theme.waveform);
            painter->drawPath(path);
        };

        painter->save();
        painter->setClipRect(r);

        if (segmentWidth < r.width() - 0.5) {
            for (double left = r.left(); left < r.right(); left += segmentWidth)
                drawWaveSegment(left, segmentWidth);
        } else {
            // Draw at natural segment width; clip rect reveals partial segment when narrower.
            drawWaveSegment(r.left(), segmentWidth);
        }

        painter->restore();
    }

    if (isMidiClip_ && !midiNotes_.empty()) {
        paintMidiNotes(painter, r);
    }

    painter->setPen(QPen(theme.border, 0.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(r);

    if (clip_) {
        painter->setPen(theme.text);
        QFont f = painter->font();
        f.setPixelSize(10);
        painter->setFont(f);
        QString name = QString::fromStdString(clip_->getName().toStdString());
        painter->drawText(r.adjusted(4, 2, -2, 0),
                          Qt::AlignLeft | Qt::AlignTop, name);
    }
}

void ClipItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::RightButton && clip_ && editMgr_) {
        setSelected(true);
        QMenu menu;
        menu.setAccessibleName("Clip Context Menu");

        if (isMidiClip_) {
            menu.addAction("Edit in Piano Roll", [this]() {
                if (auto* mc = dynamic_cast<te::MidiClip*>(clip_))
                    emit editMgr_->midiClipDoubleClicked(mc);
            });

            menu.addAction("Quantize Notes", [this]() {
                if (auto* mc = dynamic_cast<te::MidiClip*>(clip_)) {
                    auto& seq = mc->getSequence();
                    auto* um = &clip_->edit.getUndoManager();
                    for (auto* note : seq.getNotes()) {
                        double startBeat = note->getStartBeat().inBeats();
                        double snapped = std::round(startBeat * 4.0) / 4.0;
                        double len = note->getLengthBeats().inBeats();
                        double snappedLen = std::max(0.25, std::round(len * 4.0) / 4.0);
                        note->setStartAndLength(
                            tracktion::BeatPosition::fromBeats(snapped),
                            tracktion::BeatDuration::fromBeats(snappedLen), um);
                    }
                    if (requestRefresh_) requestRefresh_();
                }
            });
            menu.addSeparator();
        }

        menu.addAction("Duplicate", [this]() {
            if (!clip_ || !editMgr_ || !pixelsPerBeatPtr_) return;
            auto clipDuration = clip_->getPosition().getLength();
            auto endTime = clip_->getPosition().getEnd();
            auto* clipTrack = dynamic_cast<te::ClipTrack*>(clip_->getClipTrack());
            if (!clipTrack) return;

            if (auto* srcWave = dynamic_cast<te::WaveAudioClip*>(clip_)) {
                if (auto nc = te::insertWaveClip(
                        *clipTrack, srcWave->getName(), srcWave->getOriginalFile(),
                        {{endTime, endTime + clipDuration}},
                        te::DeleteExistingClips::no)) {
                    nc->cloneFrom(srcWave);
                    nc->setStart(endTime, false, true);
                }
            } else {
                auto pos = tracktion::TimeRange{endTime, endTime + clipDuration};
                if (auto* nc = clipTrack->insertNewClip(
                        clip_->type, clip_->getName(), pos, nullptr)) {
                    nc->cloneFrom(clip_);
                    nc->setStart(endTime, false, true);
                }
            }
            if (requestRefresh_) {
                auto refresh = requestRefresh_;
                QTimer::singleShot(0, [refresh]() { refresh(); });
            }
        });

        menu.addAction("Delete", [this]() {
            if (!clip_ || !editMgr_) return;
            clip_->removeFromParent();
            if (requestRefresh_) {
                auto refresh = requestRefresh_;
                QTimer::singleShot(0, [refresh]() { refresh(); });
            }
        });

        menu.exec(event->screenPos());
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (isNearRightEdge(event->pos()) && pixelsPerBeatPtr_) {
            resizingRight_ = true;
            dragging_ = false;
            duplicateDragging_ = false;
            resizePreviewSegmentWidthPx_ = rect().width();

            if (clip_ && clip_->isLooping()) {
                auto& ts = clip_->edit.tempoSequence;
                double loopBeats = 0.0;
                if (clip_->beatBasedLooping()) {
                    loopBeats = clip_->getLoopLengthBeats().inBeats();
                } else {
                    const auto clipStartTime = clip_->getPosition().getStart();
                    const auto loopEndTime = clipStartTime + clip_->getLoopLength();
                    loopBeats = (ts.toBeats(loopEndTime) - ts.toBeats(clipStartTime)).inBeats();
                }
                if (loopBeats > 0.001)
                    resizePreviewSegmentWidthPx_ = std::max(1.0, loopBeats * *pixelsPerBeatPtr_);
            }

            setCursor(QCursor(Qt::SizeHorCursor));
        } else {
            dragging_ = true;
            resizingRight_ = false;
            duplicateDragging_ = (event->modifiers() & Qt::ControlModifier);
            dragStartScene_ = event->scenePos();
            dragStartBeat_ = 0;
            dragStartTrack_ = trackIndex_;

            if (clip_ && pixelsPerBeatPtr_) {
                auto& ts = clip_->edit.tempoSequence;
                auto startTime = clip_->getPosition().getStart();
                dragStartBeat_ = ts.toBeats(startTime).inBeats();
            }

            if (duplicateDragging_ && scene()) {
                auto& theme = ThemeManager::instance().current();
                duplicateGhostItem_ = scene()->addRect(rect(),
                                                       QPen(theme.border, 1.0, Qt::DashLine),
                                                       QBrush(theme.clipBodySelected));
                duplicateGhostItem_->setOpacity(0.75);
                duplicateGhostItem_->setPos(pos());
                duplicateGhostItem_->setZValue(zValue() + 0.2);

                if (!waveMin_.empty() && !waveMax_.empty()) {
                    const int n = static_cast<int>(waveMin_.size());
                    const double w = rect().width();
                    const double h = rect().height();
                    const double xScale = (n > 0) ? (w / n) : 0.0;
                    const double midY = h / 2.0;
                    const double amp = h / 2.0 * 0.9;

                    QPainterPath wavePath;
                    wavePath.moveTo(0.0, midY);
                    for (int i = 0; i < n; ++i)
                        wavePath.lineTo(i * xScale, midY - waveMax_[size_t(i)] * amp);
                    for (int i = n - 1; i >= 0; --i)
                        wavePath.lineTo(i * xScale, midY - waveMin_[size_t(i)] * amp);
                    wavePath.closeSubpath();

                    duplicateGhostWaveItem_ = new QGraphicsPathItem(wavePath, duplicateGhostItem_);
                    duplicateGhostWaveItem_->setPen(Qt::NoPen);
                    duplicateGhostWaveItem_->setBrush(theme.waveform);
                    duplicateGhostWaveItem_->setOpacity(0.85);
                }
            }

            setCursor(QCursor(duplicateDragging_ ? Qt::DragCopyCursor
                                                : Qt::ClosedHandCursor));
        }

        setSelected(true);
        event->accept();
    }
    QGraphicsRectItem::mousePressEvent(event);
}

void ClipItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (resizingRight_ && clip_ && pixelsPerBeatPtr_) {
        const double endBeat = computeSnappedEndBeatFromSceneX(event->scenePos().x());
        const auto& ts = clip_->edit.tempoSequence;
        const double startBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();

        const double newWidthPx = std::max(1.0, (endBeat - startBeat) * *pixelsPerBeatPtr_);
        setRect(0, 0, newWidthPx, rect().height());

        if (scene()) {
            const double rightEdge = pos().x() + newWidthPx + 120.0;
            QRectF sr = scene()->sceneRect();
            if (rightEdge > sr.width())
                scene()->setSceneRect(0, 0, rightEdge, sr.height());
        }

        event->accept();
        return;
    }

    if (!dragging_ || !pixelsPerBeatPtr_ || !trackHeightPtr_)
        return;

    double ppb = *pixelsPerBeatPtr_;
    double th  = *trackHeightPtr_;

    QPointF delta = event->scenePos() - dragStartScene_;

    double beatDelta = delta.x() / ppb;
    double newBeat = dragStartBeat_ + beatDelta;
    if (newBeat < 0) newBeat = 0;

    if (snapper_)
        newBeat = snapper_->snapBeat(newBeat);

    int newTrack = dragStartTrack_ + int(std::round(delta.y() / th));
    newTrack = std::clamp(newTrack, 0, std::max(0, trackCount_ - 1));

    double x = newBeat * ppb;
    double y = newTrack * th;
    if (duplicateDragging_ && duplicateGhostItem_) {
        duplicateGhostItem_->setPos(x, y);
    } else {
        setPos(x, y);
    }

    event->accept();
}

void ClipItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && resizingRight_) {
        resizingRight_ = false;
        resizePreviewSegmentWidthPx_ = 0.0;
        setCursor(QCursor(Qt::OpenHandCursor));

        if (clip_ && pixelsPerBeatPtr_) {
            const double endBeat = computeSnappedEndBeatFromSceneX(event->scenePos().x());
            auto& ts = clip_->edit.tempoSequence;

            if (auto* acb = dynamic_cast<te::AudioClipBase*>(clip_)) {
                if (acb->canLoop() && !acb->isLooping()) {
                    const auto& loopInfo = acb->getLoopInfo();
                    if (loopInfo.isLoopable() && !loopInfo.isOneShot() && loopInfo.getNumBeats() > 0.0) {
                        acb->setAutoTempo(true);
                        acb->setLoopRangeBeats({
                            tracktion::BeatPosition::fromBeats(0.0),
                            tracktion::BeatDuration::fromBeats(loopInfo.getNumBeats())
                        });
                    } else {
                        acb->setLoopRange({
                            tracktion::TimePosition::fromSeconds(0.0),
                            acb->getSourceLength()
                        });
                    }
                }
            }

            clip_->setEnd(ts.toTime(tracktion::BeatPosition::fromBeats(endBeat)), false);
            if (trackHeightPtr_)
                updateGeometry(*pixelsPerBeatPtr_, *trackHeightPtr_, 0.0);
            if (editMgr_ && editMgr_->edit())
                editMgr_->edit()->restartPlayback();
        }

        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        setCursor(QCursor(Qt::OpenHandCursor));

        if (clip_ && editMgr_ && pixelsPerBeatPtr_ && trackHeightPtr_) {
            double ppb = *pixelsPerBeatPtr_;
            double th  = *trackHeightPtr_;

            const QPointF releasePos = (duplicateDragging_ && duplicateGhostItem_)
                                           ? duplicateGhostItem_->pos()
                                           : pos();
            double newBeat = releasePos.x() / ppb;
            if (newBeat < 0) newBeat = 0;

            int newTrack = int(std::round(releasePos.y() / th));
            newTrack = std::clamp(newTrack, 0, std::max(0, trackCount_ - 1));

            auto& ts = clip_->edit.tempoSequence;
            auto clipDuration = clip_->getPosition().getLength();

            auto newStartTime = ts.toTime(tracktion::BeatPosition::fromBeats(newBeat));
            if (duplicateDragging_) {
                auto tracks = editMgr_->getAudioTracks();
                if (newTrack >= 0 && newTrack < tracks.size()) {
                    if (auto* dstClipTrack = dynamic_cast<te::ClipTrack*>(tracks[newTrack])) {
                        if (auto* srcWave = dynamic_cast<te::WaveAudioClip*>(clip_)) {
                            if (auto newClip = te::insertWaveClip(
                                    *dstClipTrack,
                                    srcWave->getName(),
                                    srcWave->getOriginalFile(),
                                    {{newStartTime, newStartTime + clipDuration}},
                                    te::DeleteExistingClips::no)) {
                                newClip->cloneFrom(srcWave);
                                newClip->setStart(newStartTime, false, true);
                            }
                        } else {
                            auto newPos = tracktion::TimeRange{newStartTime, newStartTime + clipDuration};
                            if (auto* newClip = dstClipTrack->insertNewClip(
                                    clip_->type,
                                    clip_->getName(),
                                    newPos,
                                    nullptr)) {
                                newClip->cloneFrom(clip_);
                                newClip->setStart(newStartTime, false, true);
                            }
                        }
                    }
                }
                if (requestRefresh_) {
                    auto refresh = requestRefresh_;
                    QTimer::singleShot(0, [refresh]() { refresh(); });
                }
            } else {
                clip_->setStart(newStartTime, false, true);

                if (newTrack != trackIndex_) {
                    auto tracks = editMgr_->getAudioTracks();
                    if (newTrack >= 0 && newTrack < tracks.size()) {
                        auto* dstTrack = tracks[newTrack];
                        if (dstTrack) {
                            dstTrack->addClip(clip_);
                            trackIndex_ = newTrack;
                        }
                    }
                }
            }

            editMgr_->edit()->restartPlayback();
        }

        if (duplicateGhostWaveItem_)
            duplicateGhostWaveItem_ = nullptr; // child of ghost rect, auto-destroyed with parent
        if (duplicateGhostItem_)
            delete duplicateGhostItem_;
        duplicateGhostItem_ = nullptr;
        duplicateDragging_ = false;

        event->accept();
    }
    QGraphicsRectItem::mouseReleaseEvent(event);
}

void ClipItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (isMidiClip_ && editMgr_ && clip_) {
        if (auto* midiClip = dynamic_cast<te::MidiClip*>(clip_))
            emit editMgr_->midiClipDoubleClicked(midiClip);
        event->accept();
        return;
    }
    QGraphicsRectItem::mouseDoubleClickEvent(event);
}

} // namespace freedaw
