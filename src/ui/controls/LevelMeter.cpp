#include "LevelMeter.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <cmath>

namespace OpenDaw {

LevelMeter::LevelMeter(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Level Meter");

    connect(&decayTimer_, &QTimer::timeout, this, [this]() {
        const float decay = 0.92f;
        peakL_ *= decay;
        peakR_ *= decay;
        levelL_ *= 0.85f;
        levelR_ *= 0.85f;
        update();
    });
    decayTimer_.start(30);
}

void LevelMeter::setLevel(float l, float r)
{
    levelL_ = std::clamp(l, 0.0f, 1.0f);
    levelR_ = std::clamp(r, 0.0f, 1.0f);
    if (levelL_ > peakL_) peakL_ = levelL_;
    if (levelR_ > peakR_) peakR_ = levelR_;
    update();
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    auto drawBar = [&](QRectF rect, float level, float peak) {
        p.fillRect(rect, QColor(18, 18, 22));

        double levelH = rect.height() * level;
        QRectF levelRect(rect.left(), rect.bottom() - levelH,
                         rect.width(), levelH);

        QLinearGradient grad(rect.bottomLeft(), rect.topLeft());
        grad.setColorAt(0.0,  QColor(0, 100, 90));
        grad.setColorAt(0.5,  theme.meterGreen);
        grad.setColorAt(0.85, theme.meterYellow);
        grad.setColorAt(0.95, theme.meterRed);

        p.fillRect(levelRect, grad);

        p.setRenderHint(QPainter::Antialiasing, true);
        if (level > 0.02f) {
            QRectF glowRect(rect.left() - 1.5, rect.bottom() - levelH - 1,
                            rect.width() + 3.0, levelH + 2);
            QColor glow = theme.meterGlow;
            glow.setAlpha(35 + static_cast<int>(level * 50));
            p.setCompositionMode(QPainter::CompositionMode_Plus);
            p.fillRect(glowRect.intersected(rect), glow);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        p.setRenderHint(QPainter::Antialiasing, false);

        if (peak > 0.01f) {
            double peakY = rect.bottom() - rect.height() * peak;
            QColor peakColor = peak > 0.95f ? theme.meterRed : theme.accentLight;
            p.setPen(QPen(peakColor, 1));
            p.drawLine(QPointF(rect.left(), peakY), QPointF(rect.right(), peakY));
        }
    };

    QRectF outer(0, 0, width(), height());
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(8, 8, 12));
    p.drawRoundedRect(outer, 3, 3);
    p.setRenderHint(QPainter::Antialiasing, false);

    const double inset = 1.5;
    QRectF inner = outer.adjusted(inset, inset, -inset, -inset);

    if (stereo_) {
        double halfW = (inner.width() - 1) / 2.0;
        drawBar(QRectF(inner.left(), inner.top(), halfW, inner.height()), levelL_, peakL_);
        drawBar(QRectF(inner.left() + halfW + 1, inner.top(), halfW, inner.height()), levelR_, peakR_);
    } else {
        float mono = std::max(levelL_, levelR_);
        float peakMono = std::max(peakL_, peakR_);
        drawBar(inner, mono, peakMono);
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);

    p.setPen(QPen(QColor(0, 0, 0, 180), 1.0));
    p.drawLine(QPointF(outer.left() + 3, outer.top() + 0.5),
               QPointF(outer.right() - 3, outer.top() + 0.5));
    p.drawLine(QPointF(outer.left() + 0.5, outer.top() + 3),
               QPointF(outer.left() + 0.5, outer.bottom() - 3));

    p.setPen(QPen(QColor(255, 255, 255, 80), 1.0));
    p.drawLine(QPointF(outer.left() + 3, outer.bottom() - 0.5),
               QPointF(outer.right() - 3, outer.bottom() - 0.5));
    p.drawLine(QPointF(outer.right() - 0.5, outer.top() + 3),
               QPointF(outer.right() - 0.5, outer.bottom() - 3));
}

} // namespace OpenDaw
