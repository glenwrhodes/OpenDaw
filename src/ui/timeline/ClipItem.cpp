#include "ClipItem.h"
#include "GridSnapper.h"
#include "EditTool.h"
#include "engine/EditManager.h"
#include "utils/ThemeManager.h"
#include "utils/WaveformCache.h"
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QCursor>
#include <QTimer>
#include <QMenu>
#include <algorithm>

namespace OpenDaw {

ClipItem::ClipItem(te::Clip* clip, int trackIndex, double pixelsPerBeat,
                   double trackHeight, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), clip_(clip), trackIndex_(trackIndex),
      isMidiClip_(dynamic_cast<te::MidiClip*>(clip) != nullptr)
{
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
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

bool ClipItem::isNearLeftEdge(const QPointF& localPos) const
{
    constexpr double edgeGrabWidth = 8.0;
    return localPos.x() <= edgeGrabWidth && rect().width() > 3 * edgeGrabWidth;
}

QRectF ClipItem::gainHandleRect() const
{
    if (isMidiClip_ || rect().height() < 34.0) return {};
    const QRectF r = rect();
    constexpr double w = 46.0;
    if (r.width() < w + 60.0) return {};
    return QRectF(r.right() - w - 3.0, r.top() + 1.0, w, 12.0);
}

bool ClipItem::isOverGainHandle(const QPointF& localPos) const
{
    const QRectF h = gainHandleRect();
    return !h.isEmpty() && h.adjusted(-2, -1, 2, 2).contains(localPos);
}

double ClipItem::secondsPerPixel() const
{
    if (!clip_ || rect().width() <= 0.0) return 0.0;
    return clip_->getPosition().getLength().inSeconds() / rect().width();
}

void ClipItem::updateHoverCursor(const QPointF& pos, Qt::KeyboardModifiers mods)
{
    const auto tool = activeEditTool();
    if (tool != EditTool::Smart && tool != EditTool::Pen) {
        setCursor(cursorForTool(tool));
        return;
    }
    if (isOverGainHandle(pos))
        setCursor(QCursor(Qt::SizeVerCursor));
    else if (isNearFadeInHandle(pos) || isNearFadeOutHandle(pos)
             || isNearRightEdge(pos) || isNearLeftEdge(pos))
        setCursor(QCursor(Qt::SizeHorCursor));
    else if ((mods & Qt::AltModifier) && !isMidiClip_)
        setCursor(QCursor(Qt::SizeAllCursor));   // slip
    else
        setCursor(QCursor(Qt::OpenHandCursor));
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

    // An audio clip that doesn't loop stops where its file ends, unless this
    // drag (Ctrl) turns looping on.
    if (auto* acb = dynamic_cast<const te::AudioClipBase*>(clip_);
        acb && !clip_->isLooping() && !resizeMakesLoop_) {
        const auto pos = clip_->getPosition();
        const auto fileEnd = pos.getStart() - pos.getOffset() + acb->getSourceLength();
        endBeat = std::min(endBeat, ts.toBeats(fileEnd).inBeats());
        return std::max(endBeat, startBeat + 0.01);
    }

    return std::max(endBeat, startBeat + minLengthBeats);
}

double ClipItem::fadeInWidthPx() const
{
    if (isMidiClip_ || !clip_ || !pixelsPerBeatPtr_) return 0.0;
    auto* audioClip = dynamic_cast<te::AudioClipBase*>(clip_);
    if (!audioClip) return 0.0;
    double fadeSec = audioClip->getFadeIn().inSeconds();
    if (fadeSec <= 0.0) return 0.0;
    auto& ts = clip_->edit.tempoSequence;
    auto clipStart = clip_->getPosition().getStart();
    auto fadeEnd = clipStart + tracktion::TimeDuration::fromSeconds(fadeSec);
    double fadeBeats = (ts.toBeats(fadeEnd) - ts.toBeats(clipStart)).inBeats();
    return std::max(0.0, fadeBeats * *pixelsPerBeatPtr_);
}

double ClipItem::fadeOutWidthPx() const
{
    if (isMidiClip_ || !clip_ || !pixelsPerBeatPtr_) return 0.0;
    auto* audioClip = dynamic_cast<te::AudioClipBase*>(clip_);
    if (!audioClip) return 0.0;
    double fadeSec = audioClip->getFadeOut().inSeconds();
    if (fadeSec <= 0.0) return 0.0;
    auto& ts = clip_->edit.tempoSequence;
    auto clipEnd = clip_->getPosition().getEnd();
    auto fadeStart = clipEnd - tracktion::TimeDuration::fromSeconds(fadeSec);
    double fadeBeats = (ts.toBeats(clipEnd) - ts.toBeats(fadeStart)).inBeats();
    return std::max(0.0, fadeBeats * *pixelsPerBeatPtr_);
}

bool ClipItem::isNearFadeInHandle(const QPointF& localPos) const
{
    constexpr double tolerance = 8.0;
    double fiw = fadeInWidthPx();
    double handleX = fiw;
    return !isMidiClip_ && localPos.x() >= (handleX - tolerance)
           && localPos.x() <= (handleX + tolerance)
           && localPos.y() <= rect().height() * 0.5;
}

bool ClipItem::isNearFadeOutHandle(const QPointF& localPos) const
{
    constexpr double tolerance = 8.0;
    double fow = fadeOutWidthPx();
    double handleX = rect().width() - fow;
    return !isMidiClip_ && localPos.x() >= (handleX - tolerance)
           && localPos.x() <= (handleX + tolerance)
           && localPos.y() <= rect().height() * 0.5;
}

void ClipItem::paintFadeOverlays(QPainter* painter, const QRectF& r)
{
    if (isMidiClip_) return;

    double fiw = fadeInWidthPx();
    double fow = fadeOutWidthPx();

    QColor overlay(0, 0, 0, 80);
    constexpr double handleSize = 6.0;
    QColor handleColor(255, 255, 255, 200);

    // The darkened area sits above the fade's gain curve, in the fade's shape.
    auto* audioClip = dynamic_cast<te::AudioClipBase*>(clip_);
    auto curveY = [&](te::AudioFadeCurve::Type type, double alpha) {
        const double gain = te::AudioFadeCurve::alphaToGainForType(type, float(alpha));
        return r.bottom() - gain * r.height();
    };
    constexpr int kCurveSteps = 24;

    if (fiw > 1.0) {
        const auto type = audioClip ? audioClip->getFadeInType() : te::AudioFadeCurve::linear;
        QPainterPath fadeInPath;
        fadeInPath.moveTo(r.left(), r.top());
        fadeInPath.lineTo(r.left() + fiw, r.top());
        for (int k = kCurveSteps; k >= 0; --k) {
            const double a = double(k) / kCurveSteps;
            fadeInPath.lineTo(r.left() + fiw * a, curveY(type, a));
        }
        fadeInPath.closeSubpath();
        painter->fillPath(fadeInPath, overlay);
    }
    // Fade-in handle (top edge at fade boundary)
    {
        double hx = r.left() + fiw - handleSize / 2.0;
        painter->fillRect(QRectF(hx, r.top(), handleSize, handleSize), handleColor);
    }

    if (fow > 1.0) {
        const auto type = audioClip ? audioClip->getFadeOutType() : te::AudioFadeCurve::linear;
        QPainterPath fadeOutPath;
        fadeOutPath.moveTo(r.right(), r.top());
        fadeOutPath.lineTo(r.right() - fow, r.top());
        for (int k = kCurveSteps; k >= 0; --k) {
            const double a = double(k) / kCurveSteps;
            fadeOutPath.lineTo(r.right() - fow * a, curveY(type, a));
        }
        fadeOutPath.closeSubpath();
        painter->fillPath(fadeOutPath, overlay);
    }
    // Fade-out handle (top edge at fade boundary)
    {
        double hx = r.right() - fow - handleSize / 2.0;
        painter->fillRect(QRectF(hx, r.top(), handleSize, handleSize), handleColor);
    }
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
    double y = trackYOffsetFunc_ ? trackYOffsetFunc_(trackIndex_) - scrollY
                                 : trackIndex_ * trackHeight - scrollY;

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
            waveSourceSecs_ = data.sampleRate > 0.0 ? double(data.totalSamples) / data.sampleRate : 0.0;
        }
    }
}

void ClipItem::loadMidiPreview()
{
    if (isMidiClip_ && clip_) {
        auto* mc = dynamic_cast<te::MidiClip*>(clip_);
        if (mc)
            loadMidiPreviewFromClips({mc});
    }
}

void ClipItem::loadMidiPreviewFromClips(const std::vector<te::MidiClip*>& clips)
{
    midiNotes_.clear();
    midiLowestNote_ = 127;
    midiHighestNote_ = 0;

    if (!isMidiClip_ || clips.empty()) return;

    auto* primaryClip = clips.front();
    auto& ts = primaryClip->edit.tempoSequence;
    double primaryStartBeat = ts.toBeats(primaryClip->getPosition().getStart()).inBeats();

    for (auto* mc : clips) {
        double clipStartBeat = ts.toBeats(mc->getPosition().getStart()).inBeats();
        double beatOffset = clipStartBeat - primaryStartBeat;

        auto& seq = mc->getSequence();
        for (auto* note : seq.getNotes()) {
            MidiNotePreview np;
            np.pitch = note->getNoteNumber();
            np.startBeat = note->getStartBeat().inBeats() + beatOffset;
            np.lengthBeats = note->getLengthBeats().inBeats();
            np.velocity = note->getVelocity();
            midiNotes_.push_back(np);

            midiLowestNote_ = std::min(midiLowestNote_, np.pitch);
            midiHighestNote_ = std::max(midiHighestNote_, np.pitch);
        }
    }

    if (midiLowestNote_ > midiHighestNote_) {
        midiLowestNote_ = 60;
        midiHighestNote_ = 72;
    }

    int margin = 2;
    midiLowestNote_ = std::max(0, midiLowestNote_ - margin);
    midiHighestNote_ = std::min(127, midiHighestNote_ + margin);
}

void ClipItem::paintMidiNotes(QPainter* painter, const QRectF& r, const QColor& noteBase)
{
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

        double alpha = 0.55 + 0.45 * (note.velocity / 127.0);
        QColor noteColor = noteBase;
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
    const bool selected = isSelected();

    // Clips take their track's colour: a solid title bar over a tinted body.
    QColor trackColour = theme.accent;
    if (editMgr_ && clip_)
        trackColour = editMgr_->getTrackColour(clip_->getTrack());
    auto mix = [](const QColor& a, const QColor& b, double t) {
        return QColor(int(a.red() * t + b.red() * (1 - t)),
                      int(a.green() * t + b.green() * (1 - t)),
                      int(a.blue() * t + b.blue() * (1 - t)));
    };
    const QColor body = mix(trackColour, theme.trackBackground, selected ? 0.55 : 0.38);
    const QColor titleBar = selected ? trackColour.lighter(115) : trackColour.darker(105);
    const QColor content = theme.isLight
        ? (selected ? trackColour.darker(230) : trackColour.darker(165))
        : (selected ? QColor(245, 247, 250) : trackColour.lighter(165));
    const double radius = 3.0;
    const double titleH = r.height() >= 34.0 ? 14.0 : 0.0;
    const QRectF bodyRect = r.adjusted(0, titleH, 0, 0);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(body);
    painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    if (titleH > 0.0) {
        QPainterPath titlePath;
        titlePath.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        QPainterPath clipToTitle;
        clipToTitle.addRect(QRectF(r.left(), r.top(), r.width(), titleH));
        painter->setBrush(titleBar);
        painter->drawPath(titlePath.intersected(clipToTitle));
    }

    if (!waveMin_.empty() && !waveMax_.empty()) {
        int n = static_cast<int>(waveMin_.size());
        double segmentWidth = r.width();
        // The clip shows its source from its offset on, so a trimmed, split or
        // comped clip draws only its own part of the file.
        double segmentLeft = r.left();
        const double clipSecs = clip_ ? clip_->getPosition().getLength().inSeconds() : 0.0;
        // Clip-time scale from the clip as it stands in the edit; a resize
        // preview only changes how much of it shows.
        const double baseWidth = (resizingRight_ && resizePreviewSegmentWidthPx_ > 1.0
                                  && clip_ && !clip_->isLooping())
                                     ? resizePreviewSegmentWidthPx_ : r.width();
        const double pxPerSec = clipSecs > 0.0 ? baseWidth / clipSecs : 0.0;
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

            if (loopBeats > 0.001) {
                segmentWidth = std::max(1.0, loopBeats * *pixelsPerBeatPtr_);
                const double offsetPx = clip_->beatBasedLooping()
                    ? clip_->getOffsetInBeats().inBeats() * *pixelsPerBeatPtr_
                    : clip_->getPosition().getOffset().inSeconds() * pxPerSec;
                segmentLeft = r.left() - std::fmod(std::max(0.0, offsetPx), segmentWidth);
            }
        } else if (clip_ && waveSourceSecs_ > 0.0 && pxPerSec > 0.0) {
            segmentWidth = waveSourceSecs_ * pxPerSec;
            segmentLeft = r.left() - clip_->getPosition().getOffset().inSeconds() * pxPerSec;
        } else if (resizingRight_ && resizePreviewSegmentWidthPx_ > 1.0) {
            // During right-edge resize preview, keep waveform scale fixed and reveal/hide by clipping.
            segmentWidth = resizePreviewSegmentWidthPx_;
        }

        auto drawWaveSegment = [&](double left, double width) {
            if (width <= 0.0 || n <= 0)
                return;

            const double xScale = width / n;
            const double midY   = bodyRect.height() / 2.0;
            // Clip gain shows as the waveform's height.
            double gainScale = 1.0;
            if (auto* acb = dynamic_cast<te::AudioClipBase*>(clip_))
                gainScale = std::min(2.5, double(juce::Decibels::decibelsToGain(acb->getGainDB())));
            const double amp    = bodyRect.height() / 2.0 * 0.9 * gainScale;

            QPainterPath path;
            path.moveTo(left, bodyRect.top() + midY);
            for (int i = 0; i < n; ++i)
                path.lineTo(left + i * xScale,
                            bodyRect.top() + midY - waveMax_[size_t(i)] * amp);
            for (int i = n - 1; i >= 0; --i)
                path.lineTo(left + i * xScale,
                            bodyRect.top() + midY - waveMin_[size_t(i)] * amp);
            path.closeSubpath();

            painter->setPen(Qt::NoPen);
            painter->setBrush(content);
            painter->drawPath(path);
        };

        painter->save();
        painter->setClipRect(bodyRect);

        if (clip_ && clip_->isLooping() && segmentWidth > 1.0) {
            for (double left = segmentLeft; left < r.right(); left += segmentWidth)
                drawWaveSegment(left, segmentWidth);
        } else {
            // Draw at natural segment width; clip rect reveals the visible part.
            drawWaveSegment(segmentLeft, segmentWidth);
        }

        painter->restore();
    }

    if (isMidiClip_ && !midiNotes_.empty()) {
        paintMidiNotes(painter, bodyRect.adjusted(0, 2, 0, -2), content);
    }

    paintFadeOverlays(painter, r);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(selected ? QPen(theme.isLight ? theme.text : QColor(255, 255, 255, 230), 1.5)
                             : QPen(trackColour.darker(160), 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    if (clip_) {
        // Dark text on light track colours, light text on dark ones.
        const bool lightTitle = titleBar.lightnessF() > 0.55;
        painter->setPen(titleH > 0.0 ? (lightTitle ? QColor(20, 20, 22) : QColor(250, 250, 252))
                                     : theme.text);
        QFont f = painter->font();
        f.setPixelSize(10);
        f.setBold(true);
        painter->setFont(f);
        QString name = QString::fromStdString(clip_->getName().toStdString());
        const QRectF textRect = r.adjusted(5, 0, -4, 0);
        painter->drawText(QRectF(textRect.left(), r.top(), textRect.width(),
                                 titleH > 0.0 ? titleH : 14.0),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(f).elidedText(name, Qt::ElideRight, int(textRect.width())));

        // Clip gain: shown while hovered, dragged, or when not 0 dB.
        if (auto* acb = dynamic_cast<te::AudioClipBase*>(clip_)) {
            const QRectF gh = gainHandleRect();
            const float db = acb->getGainDB();
            if (!gh.isEmpty() && (hovered_ || gainDragging_ || std::abs(db) > 0.05f)) {
                QFont gf = f;
                gf.setPixelSize(9);
                painter->setFont(gf);
                painter->setPen(Qt::NoPen);
                painter->setBrush(gainDragging_ ? theme.accent : QColor(0, 0, 0, 120));
                painter->drawRoundedRect(gh, 3, 3);
                painter->setPen(gainDragging_ ? QColor(Qt::white) : QColor(225, 228, 232));
                painter->drawText(gh, Qt::AlignCenter,
                                  QString("%1%2 dB").arg(db > 0.05f ? "+" : "").arg(db, 0, 'f', 1));
                painter->setFont(f);
            }
        }

        if (linkedChannelCount_ > 1) {
            QString badge = QString("%1ch").arg(linkedChannelCount_);
            QFont badgeFont = f;
            badgeFont.setPixelSize(8);
            badgeFont.setBold(true);
            painter->setFont(badgeFont);
            QFontMetrics fm(badgeFont);
            int tw = fm.horizontalAdvance(badge) + 6;
            int th = fm.height() + 2;
            QRectF badgeRect(r.right() - tw - 3, r.top() + 2, tw, th);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 140));
            painter->drawRoundedRect(badgeRect, 3, 3);
            painter->setPen(QColor(220, 220, 220));
            painter->drawText(badgeRect, Qt::AlignCenter, badge);
        }
    } else {
        return;
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
                    if (requestRefresh_) {
                        auto refresh = requestRefresh_;
                        QTimer::singleShot(0, editMgr_, [refresh]() { refresh(); });
                    }
                }
            });
            menu.addSeparator();
        } else {
            menu.addAction("Edit Audio Clip", [this]() {
                if (auto* wc = dynamic_cast<te::WaveAudioClip*>(clip_))
                    emit editMgr_->audioClipDoubleClicked(wc);
            });
            if (auto* acb = dynamic_cast<te::AudioClipBase*>(clip_)) {
                auto* shapes = menu.addMenu("Fade Shape");
                const std::pair<QString, te::AudioFadeCurve::Type> kinds[] = {
                    { "Linear", te::AudioFadeCurve::linear },
                    { "Fast (convex)", te::AudioFadeCurve::convex },
                    { "Slow (concave)", te::AudioFadeCurve::concave },
                    { "S-Curve", te::AudioFadeCurve::sCurve },
                };
                for (const auto& [label, type] : kinds) {
                    auto* a = shapes->addAction(label, [acb, t = type]() {
                        acb->edit.getUndoManager().beginNewTransaction("Fade Shape");
                        acb->setFadeInType(t);
                        acb->setFadeOutType(t);
                    });
                    a->setCheckable(true);
                    a->setChecked(acb->getFadeInType() == type);
                }
                auto* xfade = menu.addAction("Crossfade Where Clips Overlap", [acb](bool on) {
                    acb->setAutoCrossfade(on);
                });
                xfade->setCheckable(true);
                xfade->setChecked(acb->getAutoCrossfade());
                if (std::abs(acb->getGainDB()) > 0.05f)
                    menu.addAction("Reset Clip Gain", [acb]() { acb->setGainDB(0.0f); });
                if (!acb->isLooping() && acb->canLoop())
                    menu.addAction("Loop Clip", [acb]() {
                        acb->setLoopRange({ tracktion::TimePosition::fromSeconds(0.0),
                                            acb->getSourceLength() });
                    });
            }
            menu.addSeparator();
        }

        menu.addAction("Duplicate", [this]() {
            if (!clip_ || !editMgr_ || !pixelsPerBeatPtr_) return;
            auto clipDuration = clip_->getPosition().getLength();
            auto endTime = clip_->getPosition().getEnd();
            auto* clipTrack = dynamic_cast<te::ClipTrack*>(clip_->getClipTrack());
            if (!clipTrack) return;

            if (auto* srcWave = dynamic_cast<te::WaveAudioClip*>(clip_)) {
                auto srcFile = srcWave->getOriginalFile();
                if (auto nc = te::insertWaveClip(
                        *clipTrack, srcWave->getName(), srcFile,
                        {{endTime, endTime + clipDuration}},
                        te::DeleteExistingClips::no)) {
                    nc->cloneFrom(srcWave);
                    nc->getSourceFileReference().setToDirectFileReference(srcFile, false);
                    nc->setStart(endTime, false, true);
                }
            } else if (isMidiClip_ && linkedChannelCount_ > 1) {
                if (auto* mc = dynamic_cast<te::MidiClip*>(clip_)) {
                    auto* srcTrack = mc->getAudioTrack();
                    if (srcTrack) {
                        auto linked = editMgr_->getLinkedMidiClips(srcTrack, mc);
                        for (auto* linkedClip : linked) {
                            auto lDur = linkedClip->getPosition().getLength();
                            auto lPos = tracktion::TimeRange{endTime, endTime + lDur};
                            if (auto* nc = clipTrack->insertNewClip(
                                    linkedClip->type, linkedClip->getName(),
                                    lPos, nullptr)) {
                                nc->cloneFrom(linkedClip);
                                nc->setStart(endTime, false, true);
                            }
                        }
                    }
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
            // Deferred: the resulting editChanged rebuilds the clip items,
            // which would delete this item inside its own mouse event.
            auto* mgr = editMgr_;
            te::Clip* clip = clip_;
            QTimer::singleShot(0, mgr, [mgr, clip]() { mgr->removeClip(clip); });
        });

        menu.exec(event->screenPos());
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        auto* audioClip = dynamic_cast<te::AudioClipBase*>(clip_);
        if (audioClip && isOverGainHandle(event->pos())) {
            gainDragging_ = true;
            gainStartDb_ = audioClip->getGainDB();
            dragStartScene_ = event->scenePos();
            clip_->edit.getUndoManager().beginNewTransaction("Clip Gain");
            setSelected(true);
            update();
            event->accept();
            return;
        }
        if (isNearFadeInHandle(event->pos()) && pixelsPerBeatPtr_) {
            fadeDragMode_ = FadeDragMode::FadeIn;
            dragging_ = false;
            resizingRight_ = false;
            duplicateDragging_ = false;
            setCursor(QCursor(Qt::SizeHorCursor));
            setSelected(true);
            event->accept();
            return;
        }
        if (isNearFadeOutHandle(event->pos()) && pixelsPerBeatPtr_) {
            fadeDragMode_ = FadeDragMode::FadeOut;
            dragging_ = false;
            resizingRight_ = false;
            duplicateDragging_ = false;
            setCursor(QCursor(Qt::SizeHorCursor));
            setSelected(true);
            event->accept();
            return;
        }
        if (isNearLeftEdge(event->pos()) && pixelsPerBeatPtr_) {
            trimmingLeft_ = true;
            dragging_ = false;
            clip_->edit.getUndoManager().beginNewTransaction("Trim Clip");
            setCursor(QCursor(Qt::SizeHorCursor));
            setSelected(true);
            event->accept();
            return;
        }
        if (audioClip && (event->modifiers() & Qt::AltModifier) && !isNearRightEdge(event->pos())) {
            // Slip: move the audio inside the clip, which stays put.
            slipping_ = true;
            slipStartOffset_ = clip_->getPosition().getOffset().inSeconds();
            dragStartScene_ = event->scenePos();
            clip_->edit.getUndoManager().beginNewTransaction("Slip Clip");
            setCursor(QCursor(Qt::SizeAllCursor));
            setSelected(true);
            event->accept();
            return;
        }
        if (isNearRightEdge(event->pos()) && pixelsPerBeatPtr_) {
            resizingRight_ = true;
            resizeMakesLoop_ = (event->modifiers() & Qt::ControlModifier) != 0;
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

        if (isMidiClip_ && editMgr_) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip_))
                emit editMgr_->midiClipSelected(mc);
        } else if (!isMidiClip_ && editMgr_) {
            if (auto* wc = dynamic_cast<te::WaveAudioClip*>(clip_))
                emit editMgr_->audioClipSelected(wc);
        }

        event->accept();
    }
    QGraphicsRectItem::mousePressEvent(event);
}

void ClipItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (gainDragging_) {
        if (auto* acb = dynamic_cast<te::AudioClipBase*>(clip_)) {
            // Up is louder; Shift for fine steps.
            const double perPixel = (event->modifiers() & Qt::ShiftModifier) ? 0.05 : 0.25;
            const double dy = event->scenePos().y() - dragStartScene_.y();
            const float db = float(std::clamp(gainStartDb_ - dy * perPixel, -48.0, 24.0));
            acb->setGainDB(std::round(db * 10.0f) / 10.0f);
            update();
        }
        event->accept();
        return;
    }

    if (trimmingLeft_ && clip_ && pixelsPerBeatPtr_) {
        auto& ts = clip_->edit.tempoSequence;
        double beat = std::max(0.0, event->scenePos().x() / *pixelsPerBeatPtr_);
        if (snapper_)
            beat = snapper_->snapBeat(beat);
        const auto pos = clip_->getPosition();
        // An audio clip can't start before its file does.
        double minStart = 0.0;
        if (!isMidiClip_ && !clip_->isLooping())
            minStart = std::max(0.0, (pos.getStart() - pos.getOffset()).inSeconds());
        const double maxStart = pos.getEnd().inSeconds() - 0.01;
        const double newStart = std::clamp(ts.toTime(tracktion::BeatPosition::fromBeats(beat)).inSeconds(),
                                           minStart, maxStart);
        if (std::abs(newStart - pos.getStart().inSeconds()) > 1e-6) {
            clip_->setStart(tracktion::TimePosition::fromSeconds(newStart), true, false);
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip_); mc && editMgr_)
                editMgr_->propagateClipPosition(*mc);
            updateGeometry(*pixelsPerBeatPtr_, rect().height() + 2.0, 0.0);
        }
        event->accept();
        return;
    }

    if (slipping_ && clip_) {
        const double dx = event->scenePos().x() - dragStartScene_.x();
        double offset = std::max(0.0, slipStartOffset_ - dx * secondsPerPixel());
        if (auto* acb = dynamic_cast<te::AudioClipBase*>(clip_); acb && !clip_->isLooping()) {
            const double maxOffset = acb->getSourceLength().inSeconds()
                                   - clip_->getPosition().getLength().inSeconds();
            if (maxOffset > 0.0)
                offset = std::min(offset, maxOffset);
        }
        clip_->setOffset(tracktion::TimeDuration::fromSeconds(offset));
        update();
        event->accept();
        return;
    }

    if (fadeDragMode_ != FadeDragMode::None && clip_ && pixelsPerBeatPtr_) {
        auto* audioClip = dynamic_cast<te::AudioClipBase*>(clip_);
        if (!audioClip) { fadeDragMode_ = FadeDragMode::None; return; }

        auto& ts = clip_->edit.tempoSequence;
        auto clipStart = clip_->getPosition().getStart();
        auto clipEnd = clip_->getPosition().getEnd();
        double clipLenSec = (clipEnd - clipStart).inSeconds();

        double localX = event->pos().x();

        if (fadeDragMode_ == FadeDragMode::FadeIn) {
            double fadeBeats = localX / *pixelsPerBeatPtr_;
            if (fadeBeats < 0.0) fadeBeats = 0.0;
            auto fadeEndTime = ts.toTime(ts.toBeats(clipStart) + tracktion::BeatDuration::fromBeats(fadeBeats));
            double fadeSec = std::max(0.0, (fadeEndTime - clipStart).inSeconds());
            double maxFade = clipLenSec - audioClip->getFadeOut().inSeconds();
            fadeSec = std::min(fadeSec, std::max(0.0, maxFade));
            audioClip->setFadeIn(tracktion::TimeDuration::fromSeconds(fadeSec));
        } else {
            double fadeBeats = (rect().width() - localX) / *pixelsPerBeatPtr_;
            if (fadeBeats < 0.0) fadeBeats = 0.0;
            auto fadeStartTime = ts.toTime(ts.toBeats(clipEnd) - tracktion::BeatDuration::fromBeats(fadeBeats));
            double fadeSec = std::max(0.0, (clipEnd - fadeStartTime).inSeconds());
            double maxFade = clipLenSec - audioClip->getFadeIn().inSeconds();
            fadeSec = std::min(fadeSec, std::max(0.0, maxFade));
            audioClip->setFadeOut(tracktion::TimeDuration::fromSeconds(fadeSec));
        }
        update();
        event->accept();
        return;
    }

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

    int liveTrackCount = editMgr_ ? editMgr_->trackCount() : trackCount_;
    int newTrack = trackAtYFunc_ ? trackAtYFunc_(event->scenePos().y())
                                 : dragStartTrack_ + int(std::round(delta.y() / th));
    newTrack = std::clamp(newTrack, 0, std::max(0, liveTrackCount - 1));

    double x = newBeat * ppb;
    double y = trackYOffsetFunc_ ? trackYOffsetFunc_(newTrack) : newTrack * th;
    if (duplicateDragging_ && duplicateGhostItem_) {
        duplicateGhostItem_->setPos(x, y);
    } else {
        setPos(x, y);
    }

    event->accept();
}

void ClipItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && (gainDragging_ || slipping_ || trimmingLeft_)) {
        const bool trimmed = trimmingLeft_;
        gainDragging_ = slipping_ = trimmingLeft_ = false;
        updateHoverCursor(event->pos(), event->modifiers());
        update();
        if (clip_ && editMgr_ && editMgr_->edit()) {
            editMgr_->edit()->restartPlayback();
            if (trimmed) {
                if (auto* mc = dynamic_cast<te::MidiClip*>(clip_)) {
                    editMgr_->trimNotesToClipBounds(*mc);
                    emit editMgr_->midiClipModified(mc);
                }
                if (requestRefresh_) {
                    auto refresh = requestRefresh_;
                    QTimer::singleShot(0, [refresh]() { refresh(); });
                }
            }
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && fadeDragMode_ != FadeDragMode::None) {
        fadeDragMode_ = FadeDragMode::None;
        setCursor(QCursor(Qt::OpenHandCursor));
        if (editMgr_ && editMgr_->edit())
            editMgr_->edit()->restartPlayback();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && resizingRight_) {
        resizingRight_ = false;
        resizePreviewSegmentWidthPx_ = 0.0;
        setCursor(QCursor(Qt::OpenHandCursor));

        if (clip_ && pixelsPerBeatPtr_) {
            const double endBeat = computeSnappedEndBeatFromSceneX(event->scenePos().x());
            auto& ts = clip_->edit.tempoSequence;

            if (auto* acb = dynamic_cast<te::AudioClipBase*>(clip_); acb && resizeMakesLoop_) {
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

            clip_->setEnd(ts.toTime(tracktion::BeatPosition::fromBeats(endBeat)), true);

            if (isMidiClip_ && editMgr_) {
                if (auto* mc = dynamic_cast<te::MidiClip*>(clip_)) {
                    editMgr_->propagateClipPosition(*mc);
                    editMgr_->trimNotesToClipBounds(*mc);
                    emit editMgr_->midiClipModified(mc);
                }
            }

            if (trackHeightPtr_)
                updateGeometry(*pixelsPerBeatPtr_, *trackHeightPtr_, 0.0);
            if (editMgr_ && editMgr_->edit())
                editMgr_->edit()->restartPlayback();
        }

        if (isMidiClip_ && requestRefresh_) {
            auto refresh = requestRefresh_;
            QTimer::singleShot(0, [refresh]() { refresh(); });
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

            int newTrack = trackAtYFunc_ ? trackAtYFunc_(event->scenePos().y())
                                         : int(std::round(releasePos.y() / th));
            newTrack = std::clamp(newTrack, 0, std::max(0, trackCount_ - 1));

            auto& ts = clip_->edit.tempoSequence;
            auto clipDuration = clip_->getPosition().getLength();

            auto newStartTime = ts.toTime(tracktion::BeatPosition::fromBeats(newBeat));
            if (duplicateDragging_) {
                auto tracks = editMgr_->getAudioTracksInDisplayOrder();
                if (newTrack >= 0 && newTrack < tracks.size()) {
                    if (auto* dstClipTrack = dynamic_cast<te::ClipTrack*>(tracks[newTrack])) {
                        if (auto* srcWave = dynamic_cast<te::WaveAudioClip*>(clip_)) {
                            auto srcFile = srcWave->getOriginalFile();
                            if (auto newClip = te::insertWaveClip(
                                    *dstClipTrack,
                                    srcWave->getName(),
                                    srcFile,
                                    {{newStartTime, newStartTime + clipDuration}},
                                    te::DeleteExistingClips::no)) {
                                newClip->cloneFrom(srcWave);
                                newClip->getSourceFileReference().setToDirectFileReference(srcFile, false);
                                newClip->setStart(newStartTime, false, true);
                            }
                        } else if (isMidiClip_ && linkedChannelCount_ > 1) {
                            if (auto* mc = dynamic_cast<te::MidiClip*>(clip_)) {
                                auto* srcTrack = mc->getAudioTrack();
                                if (srcTrack) {
                                    auto linked = editMgr_->getLinkedMidiClips(srcTrack, mc);
                                    for (auto* linkedClip : linked) {
                                        auto linkedDuration = linkedClip->getPosition().getLength();
                                        auto lPos = tracktion::TimeRange{newStartTime, newStartTime + linkedDuration};
                                        if (auto* newClip = dstClipTrack->insertNewClip(
                                                linkedClip->type,
                                                linkedClip->getName(),
                                                lPos,
                                                nullptr)) {
                                            newClip->cloneFrom(linkedClip);
                                            newClip->setStart(newStartTime, false, true);
                                        }
                                    }
                                }
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

                if (isMidiClip_) {
                    if (auto* mc = dynamic_cast<te::MidiClip*>(clip_))
                        editMgr_->propagateClipPosition(*mc);
                }

                if (newTrack != trackIndex_) {
                    auto tracks = editMgr_->getAudioTracksInDisplayOrder();
                    if (newTrack >= 0 && newTrack < tracks.size()) {
                        auto* dstTrack = tracks[newTrack];
                        if (dstTrack) {
                            bool dstIsMidi = editMgr_->isMidiTrack(dstTrack);
                            if (isMidiClip_ != dstIsMidi) {
                                double revertY = trackYOffsetFunc_
                                    ? trackYOffsetFunc_(dragStartTrack_)
                                    : dragStartTrack_ * (*trackHeightPtr_);
                                setPos(dragStartBeat_ * (*pixelsPerBeatPtr_), revertY);
                            } else {
                                dstTrack->addClip(clip_);
                                trackIndex_ = newTrack;
                            }
                        }
                    }
                }
            }

            if (editMgr_->edit())
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

void ClipItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    hovered_ = true;
    updateHoverCursor(event->pos(), event->modifiers());
    update();
}

void ClipItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    updateHoverCursor(event->pos(), event->modifiers());
}

void ClipItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    hovered_ = false;
    update();
}

void ClipItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (auto* acb = dynamic_cast<te::AudioClipBase*>(clip_); acb && isOverGainHandle(event->pos())) {
        clip_->edit.getUndoManager().beginNewTransaction("Reset Clip Gain");
        acb->setGainDB(0.0f);
        update();
        event->accept();
        return;
    }
    if (isMidiClip_ && editMgr_ && clip_) {
        if (auto* midiClip = dynamic_cast<te::MidiClip*>(clip_))
            emit editMgr_->midiClipDoubleClicked(midiClip);
        event->accept();
        return;
    }
    if (!isMidiClip_ && editMgr_ && clip_) {
        if (auto* waveClip = dynamic_cast<te::WaveAudioClip*>(clip_))
            emit editMgr_->audioClipDoubleClicked(waveClip);
        event->accept();
        return;
    }
    QGraphicsRectItem::mouseDoubleClickEvent(event);
}

} // namespace OpenDaw
