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
             int lowestNote, QGraphicsItem* parent = nullptr);

    te::MidiNote* note() const { return note_; }
    void updateGeometry(double pixelsPerBeat, double noteRowHeight,
                        int lowestNote, int totalNotes);

    void setGridSnapper(GridSnapper* s) { snapper_ = s; }
    void setRefreshCallback(std::function<void()> cb) { refreshCb_ = std::move(cb); }

    enum { Type = QGraphicsItem::UserType + 10 };
    int type() const override { return Type; }

protected:
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    te::MidiNote* note_;
    te::MidiClip* clip_;
    GridSnapper* snapper_ = nullptr;
    std::function<void()> refreshCb_;

    double pixelsPerBeat_ = 60.0;
    double noteRowHeight_ = 12.0;
    int lowestNote_ = 0;

    bool dragging_ = false;
    bool resizingRight_ = false;
    bool resizingLeft_ = false;
    QPointF dragStartScene_;
    double origStartBeat_ = 0;
    double origLengthBeats_ = 0;
    int origPitch_ = 0;
};

} // namespace freedaw
