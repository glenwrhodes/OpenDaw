#include "RotaryKnob.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

namespace freedaw {

RotaryKnob::RotaryKnob(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Rotary Knob");
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void RotaryKnob::setValue(double v)
{
    v = std::clamp(v, min_, max_);
    if (std::abs(v - value_) < 1e-9)
        return;
    value_ = v;
    update();
    emit valueChanged(value_);
}

double RotaryKnob::normalised() const
{
    if (max_ <= min_) return 0.0;
    return (value_ - min_) / (max_ - min_);
}

void RotaryKnob::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int side = std::min(width(), height());
    const double margin = side * 0.12;
    QRectF knobRect(margin, margin, side - 2 * margin, side - 2 * margin);
    knobRect.moveCenter(QPointF(width() / 2.0, height() / 2.0 - 6));

    // Background arc
    const double startAngle = 225.0;
    const double spanAngle  = -270.0;

    QPen arcPen(theme.border, 3.0, Qt::SolidLine, Qt::RoundCap);
    p.setPen(arcPen);
    p.drawArc(knobRect, int(startAngle * 16), int(spanAngle * 16));

    // Value arc
    double valueSpan = spanAngle * normalised();
    QPen valuePen(theme.accent, 3.0, Qt::SolidLine, Qt::RoundCap);
    p.setPen(valuePen);
    p.drawArc(knobRect, int(startAngle * 16), int(valueSpan * 16));

    // Knob body
    double knobInset = side * 0.22;
    QRectF innerRect = knobRect.adjusted(knobInset, knobInset, -knobInset, -knobInset);

    QRadialGradient grad(innerRect.center(), innerRect.width() / 2.0);
    grad.setColorAt(0.0, theme.surfaceLight);
    grad.setColorAt(1.0, theme.surface);
    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawEllipse(innerRect);

    // Indicator line
    double angle = (startAngle + valueSpan) * M_PI / 180.0;
    QPointF center = innerRect.center();
    double r = innerRect.width() / 2.0 - 2.0;
    // Qt's y-axis is downward, so use +cos/-sin to match arc direction.
    QPointF tip(center.x() + r * std::cos(angle),
                center.y() - r * std::sin(angle));
    QPointF base(center.x() + (r * 0.3) * std::cos(angle),
                 center.y() - (r * 0.3) * std::sin(angle));

    p.setPen(QPen(theme.accentLight, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(base, tip);

    // Label
    if (!label_.isEmpty()) {
        p.setPen(theme.textDim);
        QFont f = font();
        f.setPixelSize(std::max(8, side / 6));
        p.setFont(f);
        p.drawText(QRectF(0, height() - 14, width(), 14),
                   Qt::AlignHCenter | Qt::AlignBottom, label_);
    }
}

void RotaryKnob::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragStartY_ = event->pos().y();
        dragStartVal_ = value_;
        event->accept();
    }
}

void RotaryKnob::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) return;

    int dy = dragStartY_ - event->pos().y();
    double sensitivity = (max_ - min_) / 150.0;
    setValue(dragStartVal_ + dy * sensitivity);
    event->accept();
}

void RotaryKnob::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        event->accept();
    }
}

void RotaryKnob::wheelEvent(QWheelEvent* event)
{
    double step = (max_ - min_) / 100.0;
    double delta = event->angleDelta().y() > 0 ? step : -step;
    setValue(value_ + delta);
    event->accept();
}

} // namespace freedaw
