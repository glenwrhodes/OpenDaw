#pragma once

#include <QGraphicsRectItem>
#include <tracktion_engine/tracktion_engine.h>
#include <functional>

namespace te = tracktion::engine;

namespace freedaw {

class GridSnapper;

class NoteItem : public QGraphicsRectItem {
public:
    NoteItem(te::MidiNote* note, te::MidiClip* clip,
             double pixelsPerBeat, double noteRowHeight,
             int lowestNote, int channelNumber = 1,
             QGraphicsItem* parent = nullptr);

    te::MidiNote* note() const { return note_; }
    te::MidiClip* clip() const { return clip_; }
    int channelNumber() const { return channelNumber_; }
    void updateGeometry(double pixelsPerBeat, double noteRowHeight,
                        int lowestNote, int totalNotes,
                        double beatOffset = 0.0);

    void setGridSnapper(GridSnapper* s) { snapper_ = s; }
    void setRefreshCallback(std::function<void()> cb) { refreshCb_ = std::move(cb); }
    void setActiveChannel(bool active) { isActiveChannel_ = active; }

    enum { Type = QGraphicsItem::UserType + 10 };
    int type() const override { return Type; }

    static int parsePitchString(const QString& text, bool* ok = nullptr);
    static QString pitchToString(int midiNote);

protected:
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void showEditDialog();
    void collectPeers();
    void createGhosts();
    void destroyGhosts();

    struct PeerState {
        NoteItem* item;
        double origBeat;
        double origLength;
        int origPitch;
    };

    te::MidiNote* note_;
    te::MidiClip* clip_;
    int channelNumber_ = 1;
    bool isActiveChannel_ = true;
    GridSnapper* snapper_ = nullptr;
    std::function<void()> refreshCb_;

    double pixelsPerBeat_ = 60.0;
    double noteRowHeight_ = 12.0;
    int lowestNote_ = 0;
    double beatOffset_ = 0.0;

    bool dragging_ = false;
    bool resizingRight_ = false;
    bool resizingLeft_ = false;
    QPointF dragStartScene_;
    double origStartBeat_ = 0;
    double origLengthBeats_ = 0;
    int origPitch_ = 0;

    std::vector<PeerState> peers_;
    std::vector<QGraphicsRectItem*> ghostItems_;
    int previewingNote_ = -1;
};

} // namespace freedaw
