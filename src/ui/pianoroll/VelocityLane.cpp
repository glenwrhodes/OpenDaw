#include "VelocityLane.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>

namespace freedaw {

VelocityLane::VelocityLane(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Velocity Lane");
    setFixedHeight(60);
    setMouseTracking(true);
}

void VelocityLane::setClip(te::MidiClip* clip)
{
    clip_ = clip;
    update();
}

void VelocityLane::refresh()
{
    update();
}

te::MidiNote* VelocityLane::noteAtX(double x) const
{
    if (!clip_) return nullptr;

    double beat = (x + scrollOffset_) / pixelsPerBeat_;
    auto& seq = clip_->getSequence();
    te::MidiNote* closest = nullptr;
    double closestDist = 999999.0;

    for (auto* note : seq.getNotes()) {
        double noteCenter = note->getStartBeat().inBeats()
                          + note->getLengthBeats().inBeats() * 0.5;
        double barX = note->getStartBeat().inBeats() * pixelsPerBeat_ - scrollOffset_;
        double barW = std::max(4.0, note->getLengthBeats().inBeats() * pixelsPerBeat_);

        if (x >= barX && x <= barX + barW) {
            double dist = std::abs(beat - noteCenter);
            if (dist < closestDist) {
                closestDist = dist;
                closest = note;
            }
        }
    }
    return closest;
}

void VelocityLane::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter painter(this);

    painter.fillRect(rect(), theme.pianoRollBackground);

    if (!clip_) return;

    auto& seq = clip_->getSequence();
    int h = height();

    for (auto* note : seq.getNotes()) {
        double x = note->getStartBeat().inBeats() * pixelsPerBeat_ - scrollOffset_;
        double w = std::max(3.0, note->getLengthBeats().inBeats() * pixelsPerBeat_);
        double barH = (note->getVelocity() / 127.0) * (h - 4);

        if (x + w < 0 || x > width()) continue;

        QRectF bar(x, h - barH - 2, w, barH);
        painter.fillRect(bar, theme.pianoRollVelocityBar);
        painter.setPen(QPen(theme.pianoRollVelocityBar.darker(130), 0.5));
        painter.drawRect(bar);
    }

    painter.setPen(QPen(theme.pianoRollGrid, 0.5));
    painter.drawLine(0, 0, width(), 0);
}

void VelocityLane::mousePressEvent(QMouseEvent* event)
{
    draggingNote_ = noteAtX(event->position().x());
    if (draggingNote_) {
        int vel = static_cast<int>(
            127.0 * (1.0 - event->position().y() / height()));
        vel = std::clamp(vel, 1, 127);
        auto* um = clip_ ? &clip_->edit.getUndoManager() : nullptr;
        draggingNote_->setVelocity(vel, um);
        update();
        emit velocityChanged();
    }
}

void VelocityLane::mouseMoveEvent(QMouseEvent* event)
{
    if (!draggingNote_ || !(event->buttons() & Qt::LeftButton)) return;

    int vel = static_cast<int>(
        127.0 * (1.0 - event->position().y() / height()));
    vel = std::clamp(vel, 1, 127);
    auto* um = clip_ ? &clip_->edit.getUndoManager() : nullptr;
    draggingNote_->setVelocity(vel, um);
    update();
    emit velocityChanged();
}

} // namespace freedaw
