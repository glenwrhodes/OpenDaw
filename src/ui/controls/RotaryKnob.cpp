#include "RotaryKnob.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

namespace OpenDaw {

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
    double yShift = label_.isEmpty() ? 0.0 : -6.0;
    knobRect.moveCenter(QPointF(width() / 2.0, height() / 2.0 + yShift));

    const double minAngle = 225.0;
    const double maxAngle = -45.0;
    const double centerAngle = 90.0;
    const double fullSpan = maxAngle - minAngle;
    const double angle = minAngle + fullSpan * normalised();
    const bool isBipolar = (min_ < 0.0 && max_ > 0.0);

    QPen arcPen(QColor(38, 40, 46), 3.5, Qt::SolidLine, Qt::RoundCap);
    p.setPen(arcPen);
    p.drawArc(knobRect, int(minAngle * 16), int(fullSpan * 16));

    const double valueStart = isBipolar ? centerAngle : minAngle;
    const double valueSpan = isBipolar ? (angle - centerAngle) : (angle - minAngle);

    QRectF glowArcRect = knobRect.adjusted(-1.5, -1.5, 1.5, 1.5);
    QPen glowPen(QColor(theme.accent.red(), theme.accent.green(),
                        theme.accent.blue(), 50), 6.0, Qt::SolidLine, Qt::RoundCap);
    p.setPen(glowPen);
    p.drawArc(glowArcRect, int(valueStart * 16), int(valueSpan * 16));

    QPen valuePen(theme.accent, 3.5, Qt::SolidLine, Qt::RoundCap);
    p.setPen(valuePen);
    p.drawArc(knobRect, int(valueStart * 16), int(valueSpan * 16));

    double knobInset = side * 0.11;
    QRectF innerRect = knobRect.adjusted(knobInset, knobInset, -knobInset, -knobInset);

    QRectF shadowRect = innerRect.adjusted(-1, 0, 1, 2);
    QRadialGradient shadowGrad(shadowRect.center(), shadowRect.width() / 2.0);
    shadowGrad.setColorAt(0.0, QColor(0, 0, 0, 60));
    shadowGrad.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.setBrush(shadowGrad);
    p.setPen(Qt::NoPen);
    p.drawEllipse(shadowRect);

    QRadialGradient grad(innerRect.center(), innerRect.width() / 2.0);
    grad.setColorAt(0.0, QColor(72, 76, 84));
    grad.setColorAt(0.85, QColor(44, 46, 52));
    grad.setColorAt(1.0, QColor(36, 38, 44));
    p.setBrush(grad);
    p.setPen(QPen(QColor(28, 30, 34), 0.5));
    p.drawEllipse(innerRect);

    const double angleRad = angle * M_PI / 180.0;
    QPointF center = innerRect.center();
    double r = innerRect.width() / 2.0 - 2.0;
    QPointF tip(center.x() + r * std::cos(angleRad),
                center.y() - r * std::sin(angleRad));
    QPointF base(center.x() + (r * 0.3) * std::cos(angleRad),
                 center.y() - (r * 0.3) * std::sin(angleRad));

    p.setPen(QPen(theme.accentLight, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(base, tip);

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

} // namespace OpenDaw
