#include "RecordingPreview.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace OpenDaw {

RecordingPreviewItem::RecordingPreviewItem(te::Edit& edit, te::EditItemID trackId)
    : edit_(edit), trackId_(trackId)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setPen(Qt::NoPen);
    setZValue(50);   // over clips (the track's old content is muted while recording)
}

double RecordingPreviewItem::timeToX(double secs) const
{
    const double beat = edit_.tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(secs)).inBeats();
    return beat * pixelsPerBeat_ - pos().x();
}

bool RecordingPreviewItem::refresh(double pixelsPerBeat, double y, double height,
                                   const QColor& trackColour)
{
    auto& transport = edit_.getTransport();
    auto* track = te::findAudioTrackForID(edit_, trackId_);
    if (!track || !transport.isRecording()) return false;

    te::InputDeviceInstance* instance = nullptr;
    for (auto* idi : edit_.getEditInputDevices().getDevicesForTargetTrack(*track))
        if (idi->isRecording(trackId_)) { instance = idi; break; }
    if (!instance) return false;

    pixelsPerBeat_ = pixelsPerBeat;
    trackColour_ = trackColour;
    isMidi_ = instance->getInputDevice().isMidi();

    double punchIn = instance->getPunchInTime(trackId_).inSeconds();
    if (!isMidi_) {
        if (!thumb_) {
            const auto file = instance->getRecordingFile(trackId_);
            if (file != juce::File())
                thumb_ = edit_.engine.getRecordingThumbnailManager().getThumbnailFor(file);
        }
        // The thumbnail's first sample is at its punch-in time.
        if (thumb_) punchIn = thumb_->punchInTime.inSeconds();
    }

    auto* context = transport.getCurrentPlaybackContext();
    const double now = context ? context->getUnloopedPosition().inSeconds()
                               : transport.getPosition().inSeconds();
    const double elapsed = now - punchIn;

    const auto marked = transport.getLoopRange();
    const double markedStart = marked.getStart().inSeconds();
    const double markedEnd = marked.getEnd().inSeconds();
    originSecs_ = punchIn;
    passOffset_ = 0.0;
    loopLength_ = 0.0;

    if (transport.looping && markedEnd - markedStart > 0.01) {
        // Each pass over the loop carries on in the same file.
        loopLength_ = markedEnd - markedStart;
        pass_ = elapsed > 0.0 ? int(std::floor(elapsed / loopLength_)) : 0;
        const double inPass = std::max(0.0, elapsed - pass_ * loopLength_);
        originSecs_ = markedStart;
        passOffset_ = pass_ * loopLength_;
        boxStart_ = markedStart;
        splitSecs_ = markedStart + inPass;
        boxEnd_ = pass_ > 0 ? markedEnd : splitSecs_;
    } else if (edit_.recordingPunchInOut) {
        // Recording starts a little early; only the punch range is kept.
        boxStart_ = std::max(punchIn, markedStart);
        boxEnd_ = std::min(punchIn + std::max(0.0, elapsed), markedEnd);
        splitSecs_ = boxEnd_;
    } else {
        boxStart_ = punchIn;
        boxEnd_ = punchIn + std::max(0.0, elapsed);
        splitSecs_ = boxEnd_;
    }

    if (isMidi_) drainMidi(*instance);

    const auto& ts = edit_.tempoSequence;
    const double x1 = ts.toBeats(tracktion::TimePosition::fromSeconds(boxStart_)).inBeats() * pixelsPerBeat;
    const double x2 = ts.toBeats(tracktion::TimePosition::fromSeconds(boxEnd_)).inBeats() * pixelsPerBeat;
    setVisible(x2 - x1 >= 1.0);
    prepareGeometryChange();
    setPos(x1, y);
    setRect(0, 0, std::max(0.0, x2 - x1), std::max(0.0, height - 2.0));
    update();
    return true;
}

void RecordingPreviewItem::drainMidi(te::InputDeviceInstance& instance)
{
    auto fifo = instance.getRecordingNotes(trackId_);
    if (!fifo) return;

    // Live notes carry their (looped) edit time.
    juce::MidiMessage m;
    while (fifo->pop(m)) {
        const double t = m.getTimeStamp();
        if (m.isNoteOn()) {
            heldNotes_[m.getNoteNumber()] = { t, t, m.getNoteNumber(), m.getVelocity() };
        } else if (m.isNoteOff()) {
            auto it = heldNotes_.find(m.getNoteNumber());
            if (it == heldNotes_.end()) continue;
            LiveNote n = it->second;
            n.end = std::max(t, n.start);
            notes_.push_back(n);
            heldNotes_.erase(it);
        }
    }
}

void RecordingPreviewItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QRectF r = rect();
    if (r.width() < 1.0) return;

    auto& theme = ThemeManager::instance().current();
    const QColor rec = theme.transportRecord;
    auto mix = [](const QColor& a, const QColor& b, double t) {
        return QColor(int(a.red() * t + b.red() * (1 - t)),
                      int(a.green() * t + b.green() * (1 - t)),
                      int(a.blue() * t + b.blue() * (1 - t)));
    };
    const QColor body = mix(trackColour_, theme.trackBackground, 0.30);
    const QColor content = theme.isLight ? trackColour_.darker(165) : trackColour_.lighter(165);
    const double titleH = r.height() >= 34.0 ? 14.0 : 0.0;
    const QRectF bodyRect = r.adjusted(0, titleH, 0, 0);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(body);
    painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);

    painter->save();
    painter->setClipRect(bodyRect);
    if (isMidi_) paintNotes(painter, bodyRect, content);
    else paintWaveform(painter, bodyRect, content);
    painter->restore();

    if (titleH > 0.0) {
        const QRectF title(r.left(), r.top(), r.width(), titleH);
        QPainterPath path;
        path.addRoundedRect(title.adjusted(0.5, 0.5, -0.5, 0), 3.0, 3.0);
        painter->fillPath(path, rec.darker(115));
        painter->save();
        painter->setClipRect(title.adjusted(4, 0, -2, 0));
        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setPixelSize(10);
        f.setBold(true);
        painter->setFont(f);
        const QString label = pass_ > 0 ? QStringLiteral("● Recording, pass %1").arg(pass_ + 1)
                                        : QStringLiteral("● Recording");
        painter->drawText(title.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
        painter->restore();
    }

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(rec, 1.5));
    painter->drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), 3.0, 3.0);
}

namespace {
// Draws a thumbnail's min/max per pixel column across `body`. `secsAt(px)`
// gives the thumbnail time for a column (negative to skip it), and
// `colourAt(px)` its colour.
template <typename SecsAt, typename ColourAt>
void drawThumbnail(QPainter* painter, juce::AudioThumbnailBase& thumb, const QRectF& body,
                   double secsPerPx, SecsAt secsAt, ColourAt colourAt)
{
    const int channels = std::max(1, thumb.getNumChannels());
    const double midY = body.center().y();
    const double halfH = body.height() / 2.0 - 1.0;
    painter->setRenderHint(QPainter::Antialiasing, false);

    const int left = int(std::floor(body.left()));
    const int right = int(std::ceil(body.right()));
    for (int px = left; px < right; ++px) {
        const double from = secsAt(px);
        if (from < 0.0) continue;
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            float cMin = 0.0f, cMax = 0.0f;
            thumb.getApproximateMinMax(from, from + secsPerPx, ch, cMin, cMax);
            lo = std::min(lo, cMin);
            hi = std::max(hi, cMax);
        }
        const double y1 = midY - std::clamp(double(hi), -1.0, 1.0) * halfH;
        const double y2 = midY - std::clamp(double(lo), -1.0, 1.0) * halfH;
        painter->setPen(colourAt(px));
        painter->drawLine(QPointF(px + 0.5, y1), QPointF(px + 0.5, std::max(y2, y1 + 1.0)));
    }
}

QColor mixColours(const QColor& a, const QColor& b, double t)
{
    return QColor(int(a.red() * t + b.red() * (1 - t)),
                  int(a.green() * t + b.green() * (1 - t)),
                  int(a.blue() * t + b.blue() * (1 - t)));
}
}

void RecordingPreviewItem::paintWaveform(QPainter* painter, const QRectF& body, const QColor& colour)
{
    if (!thumb_ || !thumb_->thumb) return;

    const double splitX = timeToX(splitSecs_);
    const double secsPerPx = [&] {
        const double x0 = timeToX(boxStart_), x1 = timeToX(boxEnd_);
        return x1 - x0 > 0.0 ? (boxEnd_ - boxStart_) / (x1 - x0) : 0.0;
    }();
    if (secsPerPx <= 0.0) return;

    QColor previous = colour;
    previous.setAlphaF(0.35f);
    drawThumbnail(painter, *thumb_->thumb, body, secsPerPx,
        [&](int px) {
            const double t = boxStart_ + (px - body.left()) * secsPerPx;
            double from = t - originSecs_ + passOffset_;
            if (px >= splitX) {
                if (loopLength_ <= 0.0) return -1.0;
                from -= loopLength_;   // the previous pass
            }
            return from;
        },
        [&](int px) { return px < splitX ? colour : previous; });
}

void RecordingPreviewItem::paintNotes(QPainter* painter, const QRectF& body, const QColor& colour)
{
    std::vector<LiveNote> all = notes_;
    for (auto& [pitch, n] : heldNotes_) {
        LiveNote held = n;
        held.end = std::max(n.start, splitSecs_);
        all.push_back(held);
    }
    if (all.empty()) return;

    int lowest = 127, highest = 0;
    for (auto& n : all) {
        lowest = std::min(lowest, n.pitch);
        highest = std::max(highest, n.pitch);
    }
    // Show at least an octave so a few notes don't fill the box.
    if (highest - lowest < 11) {
        const int pad = (11 - (highest - lowest)) / 2;
        lowest = std::max(0, lowest - pad);
        highest = std::min(127, lowest + 11);
    }
    const double rowH = body.height() / (highest - lowest + 1);

    for (auto& n : all) {
        const double x1 = timeToX(n.start);
        const double x2 = timeToX(n.end);
        const double y = body.bottom() - (n.pitch - lowest + 1) * rowH;
        QColor c = colour;
        c.setAlphaF(0.55 + 0.45 * (n.velocity / 127.0));
        painter->fillRect(QRectF(x1, y, std::max(2.0, x2 - x1), std::max(1.0, rowH - 0.5)), c);
    }
}

} // namespace OpenDaw

namespace OpenDaw {

LooperPrintPreviewItem::LooperPrintPreviewItem(te::Edit& edit) : edit_(edit)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setPen(Qt::NoPen);
    setZValue(50);
}

double LooperPrintPreviewItem::timeToX(double secs) const
{
    const double beat = edit_.tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(secs)).inBeats();
    return beat * pixelsPerBeat_ - pos().x();
}

void LooperPrintPreviewItem::refresh(const LooperEngine::LivePrint& print, double pixelsPerBeat,
                                     double y, double height, const QColor& trackColour)
{
    print_ = print;
    pixelsPerBeat_ = pixelsPerBeat;
    trackColour_ = trackColour;
    if (print_.segments.empty()) { setVisible(false); return; }

    const auto& ts = edit_.tempoSequence;
    auto beatX = [&](double secs) {
        return ts.toBeats(tracktion::TimePosition::fromSeconds(secs)).inBeats() * pixelsPerBeat;
    };
    const double x1 = beatX(print_.segments.front().first);
    const double x2 = beatX(print_.segments.back().second);
    setVisible(true);
    prepareGeometryChange();
    setPos(x1, y);
    setRect(0, 0, std::max(1.0, x2 - x1), std::max(0.0, height - 2.0));
    update();
}

void LooperPrintPreviewItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    auto& theme = ThemeManager::instance().current();
    const QRectF r = rect();
    const QColor body = mixColours(trackColour_, theme.trackBackground, 0.30);
    const QColor content = theme.isLight ? trackColour_.darker(165) : trackColour_.lighter(165);
    const double titleH = r.height() >= 34.0 ? 14.0 : 0.0;

    for (const auto& [a, b] : print_.segments) {
        const double xa = timeToX(a), xb = timeToX(b);
        if (xb - xa < 1.0) continue;
        const QRectF seg(xa, r.top(), xb - xa, r.height());
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(body);
        painter->drawRoundedRect(seg.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);

        if (print_.thumbnail) {
            const QRectF bodyRect = seg.adjusted(0, titleH, 0, 0);
            const double secsPerPx = (b - a) / (xb - xa);
            const double from = a - print_.origin;
            painter->save();
            painter->setClipRect(bodyRect);
            drawThumbnail(painter, *print_.thumbnail, bodyRect, secsPerPx,
                [&](int px) { return from + (px - bodyRect.left()) * secsPerPx; },
                [&](int) { return content; });
            painter->restore();
        }
        if (titleH > 0.0) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            QPainterPath path;
            path.addRoundedRect(QRectF(seg.left(), seg.top(), seg.width(), titleH).adjusted(0.5, 0.5, -0.5, 0), 3.0, 3.0);
            painter->fillPath(path, trackColour_.darker(105));
            painter->save();
            painter->setClipRect(seg.adjusted(4, 0, -2, 0));
            painter->setPen(Qt::white);
            QFont f = painter->font();
            f.setPixelSize(10);
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(QRectF(seg.left() + 4, seg.top(), seg.width() - 6, titleH),
                              Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("\u25CF Looper"));
            painter->restore();
        }
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(theme.transportRecord, 1.5));
        painter->drawRoundedRect(seg.adjusted(0.75, 0.75, -0.75, -0.75), 3.0, 3.0);
    }
}

} // namespace OpenDaw
