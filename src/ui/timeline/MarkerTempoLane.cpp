#include "MarkerTempoLane.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMenu>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <cmath>

namespace OpenDaw {

MarkerTempoLane::MarkerTempoLane(EditManager* editMgr, GridSnapper* snapper,
                                  QWidget* parent)
    : QWidget(parent), editMgr_(editMgr), snapper_(snapper)
{
    setFixedHeight(kDefaultHeight);
    laneHeight_ = kDefaultHeight;
    setMouseTracking(true);
    setAccessibleName("Marker and Tempo Lane");
}

void MarkerTempoLane::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat_ = ppb;
    update();
}

void MarkerTempoLane::setScrollX(int scrollX)
{
    scrollX_ = scrollX;
    update();
}

void MarkerTempoLane::refresh()
{
    update();
}

void MarkerTempoLane::setPlayheadBeat(double beat)
{
    playheadBeat_ = beat;
    update();
}

double MarkerTempoLane::xToBeat(int x) const
{
    return (x + scrollX_) / pixelsPerBeat_;
}

int MarkerTempoLane::beatToX(double beat) const
{
    return static_cast<int>(beat * pixelsPerBeat_ - scrollX_);
}

void MarkerTempoLane::paintEvent(QPaintEvent*)
{
    if (!editMgr_ || !editMgr_->edit()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto& theme = ThemeManager::instance().current();

    p.fillRect(rect(), theme.surface);

    p.setPen(QPen(theme.border, 1));
    p.drawLine(0, kMarkerZoneHeight, width(), kMarkerZoneHeight);
    p.drawLine(0, height() - 1, width(), height() - 1);

    auto& ts = editMgr_->edit()->tempoSequence;

    // ── Draw markers ──
    auto markers = editMgr_->getMarkers();
    for (auto* marker : markers) {
        double startBeat = ts.toBeats(marker->getPosition().getStart()).inBeats();
        int x = beatToX(startBeat);

        if (x < -100 || x > width() + 100) continue;

        QColor flagColor = QColor(marker->getColour().getRed(),
                                   marker->getColour().getGreen(),
                                   marker->getColour().getBlue());
        if (flagColor == QColor(0, 0, 0) || !flagColor.isValid())
            flagColor = theme.accent;

        QRect flagRect(x, 2, 80, kMarkerZoneHeight - 4);
        p.setPen(Qt::NoPen);
        p.setBrush(flagColor);
        p.drawRoundedRect(flagRect, 3, 3);

        p.setPen(QPen(flagColor, 1));
        p.drawLine(x, 0, x, height());

        p.setPen(Qt::white);
        QFont f = font();
        f.setPixelSize(10);
        f.setBold(true);
        p.setFont(f);
        QString name = QString::fromStdString(marker->getName().toStdString());
        p.drawText(flagRect.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, name);
    }

    // ── Draw tempo graph (step/CC style) ──
    int tempoTop = kMarkerZoneHeight + 2;
    int tempoHeight = tempoZoneHeight() - 4;

    auto numTempos = ts.getNumTempos();
    if (numTempos == 0) return;

    auto bpmToY = [&](double bpm) -> int {
        double norm = std::clamp((bpm - kMinBpm) / (kMaxBpm - kMinBpm), 0.0, 1.0);
        return tempoTop + tempoHeight - static_cast<int>(norm * tempoHeight);
    };

    // Draw step-function lines (CC style: horizontal then vertical)
    p.setPen(QPen(theme.accent, 1.5));
    for (int i = 0; i < numTempos; ++i) {
        auto* tempo = ts.getTempo(i);
        double beat = tempo->getStartBeat().inBeats();
        double bpm = tempo->getBpm();
        int x = beatToX(beat);
        int y = bpmToY(bpm);

        // Horizontal line from this point to the next point (or right edge)
        int nextX = width();
        if (i + 1 < numTempos) {
            auto* nextTempo = ts.getTempo(i + 1);
            nextX = beatToX(nextTempo->getStartBeat().inBeats());
        }
        p.drawLine(x, y, nextX, y);

        // Vertical line connecting to the next tempo value (step jump)
        if (i + 1 < numTempos) {
            auto* nextTempo = ts.getTempo(i + 1);
            int nextY = bpmToY(nextTempo->getBpm());
            p.drawLine(nextX, y, nextX, nextY);
        }

        // Draw node circle
        p.setPen(Qt::NoPen);
        p.setBrush(theme.accent);
        p.drawEllipse(QPointF(x, y), kNodeRadius, kNodeRadius);

        // BPM label
        p.setPen(theme.text);
        QFont bf = font();
        bf.setPixelSize(9);
        p.setFont(bf);
        p.drawText(x + kNodeRadius + 2, y - 2, QString::number(bpm, 'f', 1));

        // Reset pen for next iteration
        p.setPen(QPen(theme.accent, 1.5));
    }

    // ── Draw playhead ──
    int playheadX = beatToX(playheadBeat_);
    if (playheadX >= 0 && playheadX <= width()) {
        p.setPen(QPen(theme.playhead, 1.5));
        p.drawLine(playheadX, 0, playheadX, height());
    }

    // ── Resize handle at bottom ──
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(theme.border.red(), theme.border.green(), theme.border.blue(), 120));
    p.drawRect(0, height() - kResizeGrabHeight, width(), kResizeGrabHeight);
}

void MarkerTempoLane::contextMenuEvent(QContextMenuEvent* event)
{
    if (!editMgr_ || !editMgr_->edit()) return;

    double beat = xToBeat(event->pos().x());
    if (snapper_) beat = snapper_->snapBeat(beat);
    if (beat < 0) beat = 0;

    auto& ts = editMgr_->edit()->tempoSequence;
    auto time = ts.toTime(tracktion::BeatPosition::fromBeats(beat));

    QMenu menu;
    menu.setAccessibleName("Marker Tempo Lane Context Menu");

    menu.addAction("Insert Marker Here", [this, time, beat]() {
        bool ok = false;
        QString name = QInputDialog::getText(this, "New Marker", "Marker name:",
                                              QLineEdit::Normal, "Marker", &ok);
        if (ok && !name.isEmpty()) {
            editMgr_->addMarker(name, time);
            update();
        }
    });

    menu.addAction("Insert Tempo Change Here", [this, beat]() {
        bool ok = false;
        double bpm = QInputDialog::getDouble(this, "Tempo Change", "BPM:",
                                              editMgr_->getBpm(), kMinBpm, kMaxBpm, 1, &ok);
        if (ok) {
            auto& ts2 = editMgr_->edit()->tempoSequence;
            ts2.insertTempo(tracktion::BeatPosition::fromBeats(beat), bpm, 0.0f);
            emit tempoChanged();
            update();
        }
    });

    auto markers = editMgr_->getMarkers();
    for (auto* marker : markers) {
        double mBeat = ts.toBeats(marker->getPosition().getStart()).inBeats();
        int mx = beatToX(mBeat);
        if (std::abs(event->pos().x() - mx) < 40) {
            menu.addSeparator();
            QString mName = QString::fromStdString(marker->getName().toStdString());
            menu.addAction(QString("Rename \"%1\"").arg(mName), [this, marker]() {
                bool ok = false;
                QString name = QInputDialog::getText(this, "Rename Marker", "Name:",
                    QLineEdit::Normal,
                    QString::fromStdString(marker->getName().toStdString()), &ok);
                if (ok && !name.isEmpty()) {
                    marker->setName(juce::String(name.toUtf8().constData()));
                    update();
                }
            });
            menu.addAction(QString("Delete \"%1\"").arg(mName), [this, marker]() {
                editMgr_->removeMarker(marker);
                update();
            });
            break;
        }
    }

    for (int i = 1; i < ts.getNumTempos(); ++i) {
        auto* tempo = ts.getTempo(i);
        double tBeat = tempo->getStartBeat().inBeats();
        int tx = beatToX(tBeat);
        if (std::abs(event->pos().x() - tx) < 10) {
            menu.addSeparator();
            menu.addAction(QString("Set BPM at beat %1...").arg(tBeat, 0, 'f', 1), [this, i]() {
                auto& ts2 = editMgr_->edit()->tempoSequence;
                auto* t = ts2.getTempo(i);
                bool ok = false;
                double bpm = QInputDialog::getDouble(this, "Set Tempo", "BPM:",
                    t->getBpm(), kMinBpm, kMaxBpm, 1, &ok);
                if (ok) {
                    t->setBpm(bpm);
                    emit tempoChanged();
                    update();
                }
            });
            menu.addAction("Remove Tempo Change", [this, i]() {
                auto& ts2 = editMgr_->edit()->tempoSequence;
                ts2.removeTempo(i, false);
                emit tempoChanged();
                update();
            });
            break;
        }
    }

    menu.exec(event->globalPos());
}

void MarkerTempoLane::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !editMgr_ || !editMgr_->edit()) return;

    // Check for resize handle at bottom
    if (event->pos().y() >= height() - kResizeGrabHeight - 2) {
        dragMode_ = DragMode::ResizeHeight;
        resizeStartY_ = event->globalPosition().toPoint().y();
        resizeStartHeight_ = laneHeight_;
        setCursor(Qt::SizeVerCursor);
        event->accept();
        return;
    }

    auto& ts = editMgr_->edit()->tempoSequence;
    int mx = event->pos().x();
    int my = event->pos().y();

    if (my < kMarkerZoneHeight) {
        auto markers = editMgr_->getMarkers();
        for (auto* marker : markers) {
            double mBeat = ts.toBeats(marker->getPosition().getStart()).inBeats();
            int markerX = beatToX(mBeat);
            if (mx >= markerX - 5 && mx <= markerX + 80) {
                dragMode_ = DragMode::DragMarker;
                draggedMarker_ = marker;
                dragStartBeat_ = mBeat;
                setCursor(Qt::ClosedHandCursor);
                return;
            }
        }
    }

    if (my >= kMarkerZoneHeight) {
        for (int i = 0; i < ts.getNumTempos(); ++i) {
            auto* tempo = ts.getTempo(i);
            double tBeat = tempo->getStartBeat().inBeats();
            double bpm = tempo->getBpm();
            int tx = beatToX(tBeat);
            double normalizedBpm = std::clamp((bpm - kMinBpm) / (kMaxBpm - kMinBpm), 0.0, 1.0);
            int ty = kMarkerZoneHeight + 2 + (tempoZoneHeight() - 4)
                     - static_cast<int>(normalizedBpm * (tempoZoneHeight() - 4));
            if (std::abs(mx - tx) < kNodeRadius + 3 && std::abs(my - ty) < kNodeRadius + 3) {
                dragMode_ = DragMode::DragTempo;
                draggedTempoIndex_ = i;
                setCursor(Qt::ClosedHandCursor);
                return;
            }
        }
    }
}

void MarkerTempoLane::mouseMoveEvent(QMouseEvent* event)
{
    if (!editMgr_ || !editMgr_->edit()) return;

    // Show resize cursor when hovering near bottom edge (not during drags)
    if (dragMode_ == DragMode::None) {
        if (event->pos().y() >= height() - kResizeGrabHeight - 2)
            setCursor(Qt::SizeVerCursor);
        else if (event->pos().y() < kMarkerZoneHeight)
            setCursor(Qt::ArrowCursor);
        else
            setCursor(Qt::ArrowCursor);
    }

    if (dragMode_ == DragMode::ResizeHeight) {
        int delta = event->globalPosition().toPoint().y() - resizeStartY_;
        int newHeight = std::clamp(resizeStartHeight_ + delta, kMinHeight, kMaxHeight);
        laneHeight_ = newHeight;
        setFixedHeight(newHeight);
        update();
        return;
    }

    if (dragMode_ == DragMode::DragMarker && draggedMarker_) {
        double beat = xToBeat(event->pos().x());
        if (snapper_) beat = snapper_->snapBeat(beat);
        if (beat < 0) beat = 0;
        auto& ts = editMgr_->edit()->tempoSequence;
        auto newTime = ts.toTime(tracktion::BeatPosition::fromBeats(beat));
        draggedMarker_->setStart(newTime, false, true);
        update();
    }

    if (dragMode_ == DragMode::DragTempo && draggedTempoIndex_ >= 0) {
        auto& ts = editMgr_->edit()->tempoSequence;
        if (draggedTempoIndex_ < ts.getNumTempos()) {
            auto* tempo = ts.getTempo(draggedTempoIndex_);

            int tempoHeight = tempoZoneHeight() - 4;
            int tempoTop = kMarkerZoneHeight + 2;
            double normalizedY = 1.0 - (double(event->pos().y() - tempoTop) / tempoHeight);
            normalizedY = std::clamp(normalizedY, 0.0, 1.0);
            double newBpm = kMinBpm + normalizedY * (kMaxBpm - kMinBpm);
            newBpm = std::round(newBpm * 10.0) / 10.0;

            tempo->setBpm(newBpm);

            if (draggedTempoIndex_ > 0) {
                double beat = xToBeat(event->pos().x());
                if (snapper_) beat = snapper_->snapBeat(beat);
                if (beat < 0) beat = 0;
                tempo->set(tracktion::BeatPosition::fromBeats(beat),
                           newBpm, tempo->getCurve(), false);
            }

            emit tempoChanged();
            update();
        }
    }
}

void MarkerTempoLane::mouseReleaseEvent(QMouseEvent*)
{
    dragMode_ = DragMode::None;
    draggedMarker_ = nullptr;
    draggedTempoIndex_ = -1;
    setCursor(Qt::ArrowCursor);
}

void MarkerTempoLane::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!editMgr_ || !editMgr_->edit()) return;

    if (event->pos().y() < kMarkerZoneHeight) {
        auto& ts = editMgr_->edit()->tempoSequence;
        auto markers = editMgr_->getMarkers();
        for (auto* marker : markers) {
            double mBeat = ts.toBeats(marker->getPosition().getStart()).inBeats();
            int markerX = beatToX(mBeat);
            if (event->pos().x() >= markerX - 5 && event->pos().x() <= markerX + 80) {
                bool ok = false;
                QString name = QInputDialog::getText(this, "Rename Marker", "Name:",
                    QLineEdit::Normal,
                    QString::fromStdString(marker->getName().toStdString()), &ok);
                if (ok && !name.isEmpty()) {
                    marker->setName(juce::String(name.toUtf8().constData()));
                    update();
                }
                return;
            }
        }
    }

    // Double-click on tempo zone to insert a tempo point
    if (event->pos().y() >= kMarkerZoneHeight && event->pos().y() < height() - kResizeGrabHeight) {
        double beat = xToBeat(event->pos().x());
        if (snapper_) beat = snapper_->snapBeat(beat);
        if (beat < 0) beat = 0;

        int tempoTop = kMarkerZoneHeight + 2;
        int tHeight = tempoZoneHeight() - 4;
        double normalizedY = 1.0 - (double(event->pos().y() - tempoTop) / tHeight);
        normalizedY = std::clamp(normalizedY, 0.0, 1.0);
        double bpm = kMinBpm + normalizedY * (kMaxBpm - kMinBpm);
        bpm = std::round(bpm * 10.0) / 10.0;

        auto& ts = editMgr_->edit()->tempoSequence;
        ts.insertTempo(tracktion::BeatPosition::fromBeats(beat), bpm, 0.0f);
        emit tempoChanged();
        update();
    }
}

} // namespace OpenDaw
