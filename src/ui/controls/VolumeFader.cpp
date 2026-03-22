#include "VolumeFader.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

namespace OpenDaw {

VolumeFader::VolumeFader(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Volume Fader");
    setCursor(Qt::SizeVerCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void VolumeFader::setValue(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    if (std::abs(v - value_) < 1e-9) return;
    value_ = v;
    update();
    emit valueChanged(value_);
}

void VolumeFader::setRange(double minDb, double maxDb)
{
    minDb_ = minDb;
    maxDb_ = maxDb;
    update();
}

double VolumeFader::valueDb() const
{
    return minDb_ + value_ * (maxDb_ - minDb_);
}

QRectF VolumeFader::trackRect() const
{
    double trackW = 4.0;
    double margin = 8.0;
    return QRectF((width() - trackW) / 2.0, margin,
                  trackW, height() - 2.0 * margin);
}

QRectF VolumeFader::thumbRect() const
{
    double thumbW = 24.0;
    double thumbH = 12.0;
    double y = valueToY(value_);
    return QRectF((width() - thumbW) / 2.0, y - thumbH / 2.0, thumbW, thumbH);
}

double VolumeFader::yToValue(int y) const
{
    auto tr = trackRect();
    double clamped = std::clamp(double(y), tr.top(), tr.bottom());
    return 1.0 - (clamped - tr.top()) / tr.height();
}

int VolumeFader::valueToY(double v) const
{
    auto tr = trackRect();
    return int(tr.bottom() - v * tr.height());
}

void VolumeFader::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto tr = trackRect();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(18, 18, 22));
    p.drawRoundedRect(tr, 2, 2);

    double fillY = valueToY(value_);
    QRectF fillRect(tr.left(), fillY, tr.width(), tr.bottom() - fillY);
    QLinearGradient fillGrad(fillRect.bottomLeft(), fillRect.topLeft());
    fillGrad.setColorAt(0.0, QColor(0, 120, 108));
    fillGrad.setColorAt(1.0, theme.accent);
    p.setBrush(fillGrad);
    p.drawRoundedRect(fillRect, 2, 2);

    p.setPen(QColor(190, 194, 200));
    QFont f = font();
    f.setPixelSize(8);
    p.setFont(f);

    for (double db : {0.0, -6.0, -12.0, -24.0, -48.0}) {
        double norm = (db - minDb_) / (maxDb_ - minDb_);
        int y = valueToY(norm);
        p.drawLine(int(tr.left() - 6), y, int(tr.left() - 2), y);
        p.drawText(QRect(0, y - 6, int(tr.left() - 8), 12),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(int(db)));
    }

    auto thumb = thumbRect();
    QLinearGradient grad(thumb.topLeft(), thumb.bottomLeft());
    grad.setColorAt(0.0, QColor(88, 92, 100));
    grad.setColorAt(0.5, QColor(68, 72, 78));
    grad.setColorAt(1.0, QColor(52, 54, 60));
    p.setBrush(grad);
    p.setPen(QPen(QColor(40, 42, 48), 0.5));
    p.drawRoundedRect(thumb, 4, 4);

    p.setPen(QPen(theme.accentLight, 1.0));
    double cy = thumb.center().y();
    p.drawLine(QPointF(thumb.left() + 4, cy), QPointF(thumb.right() - 4, cy));
}

void VolumeFader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        auto thumb = thumbRect();
        if (thumb.contains(event->pos())) {
            dragging_ = true;
            dragOffsetY_ = event->pos().y() - int(thumb.center().y());
        } else {
            setValue(yToValue(event->pos().y()));
            dragging_ = true;
            dragOffsetY_ = 0;
        }
        event->accept();
    }
}

void VolumeFader::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) return;
    setValue(yToValue(event->pos().y() - dragOffsetY_));
    event->accept();
}

void VolumeFader::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        event->accept();
    }
}

void VolumeFader::wheelEvent(QWheelEvent* event)
{
    double step = 0.02;
    double delta = event->angleDelta().y() > 0 ? step : -step;
    setValue(value_ + delta);
    event->accept();
}

} // namespace OpenDaw
