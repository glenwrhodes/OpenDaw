#include "NoteItem.h"
#include "ui/timeline/GridSnapper.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <algorithm>

namespace freedaw {

NoteItem::NoteItem(te::MidiNote* note, te::MidiClip* clip,
                   double pixelsPerBeat, double noteRowHeight,
                   int lowestNote, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), note_(note), clip_(clip),
      pixelsPerBeat_(pixelsPerBeat), noteRowHeight_(noteRowHeight),
      lowestNote_(lowestNote)
{
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(QCursor(Qt::PointingHandCursor));
    setAcceptedMouseButtons(Qt::LeftButton);
}

void NoteItem::updateGeometry(double pixelsPerBeat, double noteRowHeight,
                              int lowestNote, int totalNotes)
{
    pixelsPerBeat_ = pixelsPerBeat;
    noteRowHeight_ = noteRowHeight;
    lowestNote_ = lowestNote;

    double startBeat = note_->getStartBeat().inBeats();
    double lengthBeats = note_->getLengthBeats().inBeats();
    int pitch = note_->getNoteNumber();

    double x = startBeat * pixelsPerBeat;
    double w = std::max(2.0, lengthBeats * pixelsPerBeat);
    int row = (lowestNote + totalNotes - 1) - pitch;
    double y = row * noteRowHeight;
    double h = noteRowHeight - 1.0;

    setRect(0, 0, w, h);
    setPos(x, y);
}

void NoteItem::paint(QPainter* painter,
                     const QStyleOptionGraphicsItem*,
                     QWidget*)
{
    auto& theme = ThemeManager::instance().current();
    QRectF r = rect();

    QColor color = isSelected() ? theme.pianoRollNoteSelected : theme.pianoRollNote;
    double alpha = 0.6 + 0.4 * (note_->getVelocity() / 127.0);
    color.setAlphaF(alpha);

    painter->fillRect(r, color);
    painter->setPen(QPen(color.darker(130), 0.5));
    painter->drawRect(r);
}

void NoteItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QGraphicsRectItem::mousePressEvent(event);
        return;
    }

    setSelected(true);
    dragStartScene_ = event->scenePos();
    origStartBeat_ = note_->getStartBeat().inBeats();
    origLengthBeats_ = note_->getLengthBeats().inBeats();
    origPitch_ = note_->getNoteNumber();

    double localX = event->pos().x();
    constexpr double edgeGrab = 6.0;

    if (localX >= rect().width() - edgeGrab) {
        resizingRight_ = true;
        setCursor(QCursor(Qt::SizeHorCursor));
    } else if (localX <= edgeGrab) {
        resizingLeft_ = true;
        setCursor(QCursor(Qt::SizeHorCursor));
    } else {
        dragging_ = true;
        setCursor(QCursor(Qt::ClosedHandCursor));
    }
    event->accept();
}

void NoteItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    QPointF delta = event->scenePos() - dragStartScene_;

    if (resizingRight_) {
        double newEndBeat = origStartBeat_ + origLengthBeats_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newEndBeat = snapper_->snapBeat(newEndBeat);
        double newLength = std::max(0.125, newEndBeat - origStartBeat_);
        double w = std::max(2.0, newLength * pixelsPerBeat_);
        setRect(0, 0, w, rect().height());
    } else if (resizingLeft_) {
        double newStartBeat = origStartBeat_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newStartBeat = snapper_->snapBeat(newStartBeat);
        if (newStartBeat < 0) newStartBeat = 0;
        double newLength = (origStartBeat_ + origLengthBeats_) - newStartBeat;
        if (newLength < 0.125) return;
        double x = newStartBeat * pixelsPerBeat_;
        double w = std::max(2.0, newLength * pixelsPerBeat_);
        setPos(x, pos().y());
        setRect(0, 0, w, rect().height());
    } else if (dragging_) {
        double newBeat = origStartBeat_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newBeat = snapper_->snapBeat(newBeat);
        if (newBeat < 0) newBeat = 0;

        int pitchDelta = -static_cast<int>(std::round(delta.y() / noteRowHeight_));
        int newPitch = std::clamp(origPitch_ + pitchDelta, 0, 127);
        int totalNotes = 128;
        int row = (lowestNote_ + totalNotes - 1) - newPitch;

        setPos(newBeat * pixelsPerBeat_, row * noteRowHeight_);
    }
    event->accept();
}

void NoteItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QGraphicsRectItem::mouseReleaseEvent(event);
        return;
    }

    setCursor(QCursor(Qt::PointingHandCursor));
    QPointF delta = event->scenePos() - dragStartScene_;

    auto* um = clip_ ? &clip_->edit.getUndoManager() : nullptr;

    if (resizingRight_) {
        double newEndBeat = origStartBeat_ + origLengthBeats_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newEndBeat = snapper_->snapBeat(newEndBeat);
        double newLength = std::max(0.125, newEndBeat - origStartBeat_);
        note_->setStartAndLength(
            tracktion::BeatPosition::fromBeats(origStartBeat_),
            tracktion::BeatDuration::fromBeats(newLength), um);
    } else if (resizingLeft_) {
        double newStartBeat = origStartBeat_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newStartBeat = snapper_->snapBeat(newStartBeat);
        if (newStartBeat < 0) newStartBeat = 0;
        double newLength = (origStartBeat_ + origLengthBeats_) - newStartBeat;
        if (newLength >= 0.125) {
            note_->setStartAndLength(
                tracktion::BeatPosition::fromBeats(newStartBeat),
                tracktion::BeatDuration::fromBeats(newLength), um);
        }
    } else if (dragging_) {
        double newBeat = origStartBeat_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newBeat = snapper_->snapBeat(newBeat);
        if (newBeat < 0) newBeat = 0;

        int pitchDelta = -static_cast<int>(std::round(delta.y() / noteRowHeight_));
        int newPitch = std::clamp(origPitch_ + pitchDelta, 0, 127);

        note_->setStartAndLength(
            tracktion::BeatPosition::fromBeats(newBeat),
            tracktion::BeatDuration::fromBeats(origLengthBeats_), um);
        note_->setNoteNumber(newPitch, um);
    }

    dragging_ = false;
    resizingRight_ = false;
    resizingLeft_ = false;

    if (refreshCb_) refreshCb_();
    event->accept();
}

} // namespace freedaw
