#include "AiQuickPrompt.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QApplication>

namespace freedaw {

AiQuickPrompt::AiQuickPrompt(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    setFixedSize(520, 64);

    auto& theme = ThemeManager::instance().current();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    auto* inner = new QWidget(this);
    inner->setAutoFillBackground(true);
    inner->setObjectName("innerFrame");
    inner->setStyleSheet(
        QString("#innerFrame { background: %1; border: 1px solid %2; border-radius: 10px; }")
            .arg(theme.surface.name(), theme.accent.name()));

    auto* effect = new QGraphicsDropShadowEffect(inner);
    effect->setBlurRadius(24);
    effect->setOffset(0, 4);
    effect->setColor(QColor(0, 0, 0, 160));
    inner->setGraphicsEffect(effect);

    auto* innerLayout = new QHBoxLayout(inner);
    innerLayout->setContentsMargins(12, 6, 12, 6);
    innerLayout->setSpacing(8);

    auto* icon = new QLabel("AI", inner);
    icon->setStyleSheet(
        QString("font-weight: bold; font-size: 11px; color: %1; "
                "background: %2; border-radius: 4px; padding: 2px 6px;")
            .arg(QColor(255, 255, 255).name(), theme.accent.name()));
    icon->setFixedHeight(24);
    innerLayout->addWidget(icon);

    lineEdit_ = new QLineEdit(inner);
    lineEdit_->setAccessibleName("Quick AI Prompt");
    lineEdit_->setPlaceholderText("Ask AI anything... (Enter to send, Esc to close)");
    lineEdit_->setStyleSheet(
        QString("QLineEdit { background: transparent; color: %1; border: none; "
                "font-size: 14px; padding: 2px; }")
            .arg(theme.text.name()));
    lineEdit_->installEventFilter(this);
    innerLayout->addWidget(lineEdit_, 1);

    mainLayout->addWidget(inner);
}

void AiQuickPrompt::showCentered()
{
    if (parentWidget()) {
        QPoint center = parentWidget()->mapToGlobal(
            parentWidget()->rect().center());
        move(center.x() - width() / 2, center.y() - height() / 2 - 80);
    }
    lineEdit_->clear();
    show();
    raise();
    activateWindow();
    lineEdit_->setFocus();
}

bool AiQuickPrompt::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == lineEdit_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            QString text = lineEdit_->text().trimmed();
            if (!text.isEmpty()) {
                emit promptSubmitted(text);
                hide();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void AiQuickPrompt::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::transparent);
}

void AiQuickPrompt::focusOutEvent(QFocusEvent* /*event*/)
{
    if (!lineEdit_->hasFocus())
        hide();
}

} // namespace freedaw
