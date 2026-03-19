#include "SplashScreen.h"
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QFont>
#include <QDebug>

#ifndef FREEDAW_VERSION
#define FREEDAW_VERSION "dev"
#endif

namespace freedaw {

static constexpr int kSplashWidth  = 800;
static constexpr int kSplashHeight = 450;

SplashScreen::SplashScreen(bool deleteOnClose, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SplashScreen)
{
    if (deleteOnClose)
        setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::PointingHandCursor);

    QPixmap raw(":/splash.png");
    if (raw.isNull()) {
        qWarning() << "SplashScreen: failed to load :/splash.png";
        background_ = QPixmap(kSplashWidth, kSplashHeight);
        background_.fill(QColor(10, 10, 15));
    } else {
        background_ = raw.scaled(kSplashWidth, kSplashHeight,
                                 Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
    }

    setFixedSize(kSplashWidth, kSplashHeight);

    if (auto* screen = QGuiApplication::primaryScreen()) {
        QRect screenGeom = screen->availableGeometry();
        move((screenGeom.width()  - width())  / 2,
             (screenGeom.height() - height()) / 2);
    }
}

void SplashScreen::finish()
{
    ready_ = true;
    update();
}

void SplashScreen::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    p.drawPixmap(0, 0, background_);

    int textZoneY = height() * 3 / 4;
    QLinearGradient fade(0, textZoneY - 60, 0, textZoneY + 10);
    fade.setColorAt(0.0, QColor(10, 10, 15, 0));
    fade.setColorAt(1.0, QColor(10, 10, 15, 220));
    p.fillRect(0, textZoneY - 60, width(), height() - textZoneY + 60, fade);

    int textAreaTop = textZoneY - 30;

    QFont titleFont("Segoe UI", 26, QFont::Bold);
    p.setFont(titleFont);
    p.setPen(QColor(230, 230, 230));
    p.drawText(QRect(0, textAreaTop, width(), 36), Qt::AlignCenter, "FreeDaw");

    QFont versionFont("Segoe UI", 11);
    p.setFont(versionFont);
    p.setPen(QColor(100, 210, 210));
    QString versionStr = QString("Version %1").arg(FREEDAW_VERSION);
    p.drawText(QRect(0, textAreaTop + 36, width(), 20), Qt::AlignCenter, versionStr);

    QFont devFont("Segoe UI", 9);
    p.setFont(devFont);
    p.setPen(QColor(190, 190, 190));
    p.drawText(QRect(0, textAreaTop + 58, width(), 18), Qt::AlignCenter,
               "Developed by Glen Rhodes");

    QFont smallFont("Segoe UI", 8);
    p.setFont(smallFont);
    p.setPen(QColor(140, 140, 140));
    QString copyright = QString::fromUtf8(
        "\u00A9 2025\u20132026 FreeDaw  \u2022  Licensed under GPLv3");
    p.drawText(QRect(0, textAreaTop + 78, width(), 16), Qt::AlignCenter, copyright);

    if (!ready_) {
        QFont loadFont("Segoe UI", 8, QFont::Normal, true);
        p.setFont(loadFont);
        p.setPen(QColor(120, 120, 120));
        p.drawText(QRect(0, height() - 22, width(), 18), Qt::AlignCenter, "Loading\u2026");
    } else {
        QFont loadFont("Segoe UI", 8);
        p.setFont(loadFont);
        p.setPen(QColor(120, 120, 120));
        p.drawText(QRect(0, height() - 22, width(), 18), Qt::AlignCenter,
                   "Click anywhere to continue");
    }
}

void SplashScreen::mousePressEvent(QMouseEvent* event)
{
    if (ready_) {
        emit dismissed();
        close();
    }
    QWidget::mousePressEvent(event);
}

} // namespace freedaw
