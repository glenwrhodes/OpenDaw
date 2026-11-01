#pragma once

#include "engine/AudioEngine.h"
#include "engine/LooperEngine.h"
#include <QColor>
#include <QGraphicsRectItem>
#include <map>
#include <vector>

namespace OpenDaw {

// The clip a track is recording into, drawn while recording: a box that grows
// with the playhead, with the incoming waveform (or MIDI notes) inside it.
// The real clip replaces it when recording stops.
class RecordingPreviewItem : public QGraphicsRectItem {
public:
    RecordingPreviewItem(te::Edit& edit, te::EditItemID trackId);

    // Refreshes the box from the engine. Returns false once the track has
    // stopped recording, when the item should be removed.
    bool refresh(double pixelsPerBeat, double y, double height, const QColor& trackColour);

    enum { Type = QGraphicsItem::UserType + 8 };
    int type() const override { return Type; }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    struct LiveNote { double start = 0.0, end = 0.0; int pitch = 60; int velocity = 100; };

    double timeToX(double secs) const;
    void drainMidi(te::InputDeviceInstance& instance);
    void paintWaveform(QPainter* painter, const QRectF& body, const QColor& colour);
    void paintNotes(QPainter* painter, const QRectF& body, const QColor& colour);

    te::Edit& edit_;
    te::EditItemID trackId_;
    double pixelsPerBeat_ = 40.0;
    QColor trackColour_;

    te::RecordingThumbnailManager::Thumbnail::Ptr thumb_;
    bool isMidi_ = false;

    // The box spans [boxStart_, boxEnd_] in edit seconds. Audio before
    // splitSecs_ is the current pass, read at thumbnail time
    // (t - originSecs_ + passOffset_); after it, while loop recording, is the
    // previous pass, read one loop length earlier and drawn dimmer.
    double boxStart_ = 0.0, boxEnd_ = 0.0, splitSecs_ = 0.0;
    double originSecs_ = 0.0, passOffset_ = 0.0, loopLength_ = 0.0;
    int pass_ = 0;

    std::vector<LiveNote> notes_;
    std::map<int, LiveNote> heldNotes_;
};

// What a looper pad has played so far this session, drawn on its track while
// the transport runs: a box for each stretch the pad was sounding, with the
// waveform being printed. The clips replace it when the transport stops.
class LooperPrintPreviewItem : public QGraphicsRectItem {
public:
    explicit LooperPrintPreviewItem(te::Edit& edit);
    void refresh(const LooperEngine::LivePrint& print, double pixelsPerBeat,
                 double y, double height, const QColor& trackColour);

    enum { Type = QGraphicsItem::UserType + 9 };
    int type() const override { return Type; }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    double timeToX(double secs) const;

    te::Edit& edit_;
    double pixelsPerBeat_ = 40.0;
    QColor trackColour_;
    LooperEngine::LivePrint print_;
};

} // namespace OpenDaw
