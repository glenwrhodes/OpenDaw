#include "SplashScreen.h"
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QFont>

namespace freedaw {

SplashScreen::SplashScreen(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SplashScreen)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::PointingHandCursor);

    background_ = QPixmap(":/splash.png");
    setFixedSize(background_.size());

    if (auto* screen = QGuiApplication::primaryScreen()) {
        QRect screenGeom = screen->availableGeometry();
        move((screenGeom.width()  - width())  / 2,
             (screenGeom.height() - height()) / 2);
    }
}

void SplashScreen::finish()
{
    ready_ = true;
}

void SplashScreen::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    p.drawPixmap(0, 0, background_);

    int textAreaTop = height() - (height() / 4);

    QFont titleFont("Segoe UI", 28, QFont::Bold);
    p.setFont(titleFont);
    p.setPen(QColor(230, 230, 230));
    p.drawText(QRect(0, textAreaTop, width(), 40), Qt::AlignCenter, "FreeDaw");

    QFont versionFont("Segoe UI", 12);
    p.setFont(versionFont);
    p.setPen(QColor(100, 210, 210));
    p.drawText(QRect(0, textAreaTop + 40, width(), 22), Qt::AlignCenter, "Version 1.0");

    QFont smallFont("Segoe UI", 9);
    p.setFont(smallFont);
    p.setPen(QColor(160, 160, 160));
    QString copyright = QString::fromUtf8("\u00A9 2025\u20132026 FreeDaw contributors \u2022 Licensed under GPLv3");
    p.drawText(QRect(0, textAreaTop + 64, width(), 18), Qt::AlignCenter, copyright);

    if (!ready_) {
        QFont loadFont("Segoe UI", 9, QFont::Normal, true);
        p.setFont(loadFont);
        p.setPen(QColor(120, 120, 120));
        p.drawText(QRect(0, height() - 24, width(), 20), Qt::AlignCenter, "Loading\u2026");
    } else {
        QFont loadFont("Segoe UI", 9);
        p.setFont(loadFont);
        p.setPen(QColor(120, 120, 120));
        p.drawText(QRect(0, height() - 24, width(), 20), Qt::AlignCenter, "Click anywhere to continue");
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
