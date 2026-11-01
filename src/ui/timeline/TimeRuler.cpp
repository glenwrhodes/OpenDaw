#include "TimeRuler.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <cmath>

namespace OpenDaw {

TimeRuler::TimeRuler(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Time Ruler");
    setFixedHeight(28);
    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setMouseTracking(true);   // resize cursor over the loop/punch handles
}

double TimeRuler::xToBeat(int x) const
{
    double beat = (x + scrollX_) / pixelsPerBeat_;
    return std::max(0.0, beat);
}

double TimeRuler::beatToX(double beat) const
{
    return beat * pixelsPerBeat_ - scrollX_;
}

void TimeRuler::setLoopRegion(double inBeat, double outBeat)
{
    if (inBeat > outBeat)
        std::swap(inBeat, outBeat);
    loopInBeat_ = std::max(0.0, inBeat);
    loopOutBeat_ = std::max(0.0, outBeat);
    update();
}

void TimeRuler::setLoopEnabled(bool enabled)
{
    loopEnabled_ = enabled;
    update();
}

void TimeRuler::setPunchEnabled(bool enabled)
{
    punchEnabled_ = enabled;
    update();
}

bool TimeRuler::regionVisible() const
{
    return (loopEnabled_ || punchEnabled_) && loopOutBeat_ > loopInBeat_;
}

void TimeRuler::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter p(this);
    p.fillRect(rect(), theme.surface);

    // Loop region overlay (drawn first, behind everything else). The same
    // range is the punch range, drawn in the record colour while punch is on.
    if (regionVisible()) {
        double lx1 = beatToX(loopInBeat_);
        double lx2 = beatToX(loopOutBeat_);
        const QColor regionColor = punchEnabled_ ? theme.transportRecord : theme.accent;
        QColor loopColor = regionColor;
        loopColor.setAlpha(40);
        p.fillRect(QRectF(lx1, 0, lx2 - lx1, height()), loopColor);

        // Loop boundary lines
        QColor boundaryColor = regionColor;
        boundaryColor.setAlpha(180);
        p.setPen(QPen(boundaryColor, 1.5));
        p.drawLine(QPointF(lx1, 0), QPointF(lx1, height()));
        p.drawLine(QPointF(lx2, 0), QPointF(lx2, height()));

        // Loop-in flag (downward triangle at top-left)
        QPainterPath flagIn;
        flagIn.moveTo(lx1, 0);
        flagIn.lineTo(lx1 + 7, 0);
        flagIn.lineTo(lx1, 9);
        flagIn.closeSubpath();
        p.fillPath(flagIn, regionColor);

        // Loop-out flag (downward triangle at top-right, mirrored)
        QPainterPath flagOut;
        flagOut.moveTo(lx2, 0);
        flagOut.lineTo(lx2 - 7, 0);
        flagOut.lineTo(lx2, 9);
        flagOut.closeSubpath();
        p.fillPath(flagOut, regionColor);
    }

    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);

    double beatsPerBar = timeSigNum_;

    double startBeat = scrollX_ / pixelsPerBeat_;
    int firstBar = int(std::floor(startBeat / beatsPerBar));
    if (firstBar < 0) firstBar = 0;

    double endBeat = startBeat + width() / pixelsPerBeat_;
    int lastBar = int(std::ceil(endBeat / beatsPerBar)) + 1;

    for (int bar = firstBar; bar <= lastBar; ++bar) {
        double beat = bar * beatsPerBar;
        double x = beat * pixelsPerBeat_ - scrollX_;

        p.setPen(QPen(theme.textDim, 1));
        p.drawLine(QPointF(x, height() - 8), QPointF(x, height()));

        p.setPen(theme.text);
        p.drawText(QRectF(x + 3, 0, 60, height() - 8),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(bar + 1));

        if (pixelsPerBeat_ > 15) {
            p.setPen(QPen(theme.border, 0.5));
            for (int b = 1; b < timeSigNum_; ++b) {
                double bx = (beat + b) * pixelsPerBeat_ - scrollX_;
                p.drawLine(QPointF(bx, height() - 5), QPointF(bx, height()));
            }
        }
    }

    // Playhead
    double phX = playheadBeat_ * pixelsPerBeat_ - scrollX_;
    if (phX >= 0 && phX <= width()) {
        p.setPen(QPen(theme.playhead, 2));
        p.drawLine(QPointF(phX, 0), QPointF(phX, height()));

        QPainterPath tri;
        tri.moveTo(phX - 5, 0);
        tri.lineTo(phX + 5, 0);
        tri.lineTo(phX, 7);
        tri.closeSubpath();
        p.fillPath(tri, theme.playhead);
    }

    p.setPen(theme.border);
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void TimeRuler::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        double clickX = event->pos().x();

        // Check if clicking near a loop handle
        if (regionVisible()) {
            double inX = beatToX(loopInBeat_);
            double outX = beatToX(loopOutBeat_);

            if (std::abs(clickX - inX) <= kHandleHitRadius) {
                draggingLoopIn_ = true;
                event->accept();
                return;
            }
            if (std::abs(clickX - outX) <= kHandleHitRadius) {
                draggingLoopOut_ = true;
                event->accept();
                return;
            }
        }

        dragging_ = false;
        double beat = xToBeat(event->pos().x());
        emit positionClicked(beat);
        event->accept();
    }
}

void TimeRuler::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        double beat = xToBeat(event->pos().x());

        if (draggingLoopIn_) {
            if (snapFn_) beat = snapFn_(beat);
            if (beat >= loopOutBeat_) beat = loopOutBeat_ - 0.01;
            loopInBeat_ = std::max(0.0, beat);
            update();
            emit loopRegionChanged(loopInBeat_, loopOutBeat_);
            event->accept();
            return;
        }
        if (draggingLoopOut_) {
            if (snapFn_) beat = snapFn_(beat);
            if (beat <= loopInBeat_) beat = loopInBeat_ + 0.01;
            loopOutBeat_ = std::max(0.0, beat);
            update();
            emit loopRegionChanged(loopInBeat_, loopOutBeat_);
            event->accept();
            return;
        }

        dragging_ = true;
        emit positionDragged(beat);
        event->accept();
    }

    // Update cursor when hovering near loop handles
    if (regionVisible() && !(event->buttons() & Qt::LeftButton)) {
        double mx = event->pos().x();
        double inX = beatToX(loopInBeat_);
        double outX = beatToX(loopOutBeat_);
        if (std::abs(mx - inX) <= kHandleHitRadius ||
            std::abs(mx - outX) <= kHandleHitRadius) {
            setCursor(Qt::SizeHorCursor);
        } else {
            setCursor(Qt::PointingHandCursor);
        }
    }
}

void TimeRuler::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        draggingLoopIn_ = false;
        draggingLoopOut_ = false;
        dragging_ = false;
        event->accept();
    }
}

void TimeRuler::contextMenuEvent(QContextMenuEvent* event)
{
    double beat = xToBeat(event->pos().x());

    QMenu menu(this);
    menu.setAccessibleName("Time Ruler Context Menu");

    auto* setInAction = menu.addAction(punchEnabled_ ? "Set Punch In Here" : "Set Loop In Here");
    auto* setOutAction = menu.addAction(punchEnabled_ ? "Set Punch Out Here" : "Set Loop Out Here");

    auto* chosen = menu.exec(event->globalPos());
    if (chosen == setInAction) {
        emit loopInRequested(beat);
    } else if (chosen == setOutAction) {
        emit loopOutRequested(beat);
    }
}

} // namespace OpenDaw
