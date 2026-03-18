#include "AiChatWidget.h"
#include "engine/EditManager.h"
#include "engine/AudioEngine.h"
#include "engine/PluginScanner.h"
#include "utils/ThemeManager.h"
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>
#include <QApplication>
#include <QRegularExpression>
#include <QSettings>

namespace freedaw {

AiChatWidget::AiChatWidget(EditManager* editMgr, AudioEngine* audioEngine,
                           PluginScanner* pluginScanner, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("AI Assistant Panel");
    aiService_ = new AiService(editMgr, audioEngine, pluginScanner, this);

    connect(aiService_, &AiService::tokenReceived, this, &AiChatWidget::onTokenReceived);
    connect(aiService_, &AiService::responseComplete, this, &AiChatWidget::onResponseComplete);
    connect(aiService_, &AiService::toolCallStarted, this, &AiChatWidget::onToolCallStarted);
    connect(aiService_, &AiService::toolCallFinished, this, &AiChatWidget::onToolCallFinished);
    connect(aiService_, &AiService::errorOccurred, this, &AiChatWidget::onError);
    connect(aiService_, &AiService::confirmDestructiveAction, this, &AiChatWidget::onConfirmDestructive);
    connect(aiService_, &AiService::busyChanged, this, &AiChatWidget::onBusyChanged);

    setupUi();
}

void AiChatWidget::setupUi()
{
    auto& theme = ThemeManager::instance().current();

    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.background);
    setPalette(pal);

    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(0, 0, 0, 0);
    mainLayout_->setSpacing(0);

    // Header bar
    headerBar_ = new QWidget(this);
    headerBar_->setFixedHeight(36);
    headerBar_->setAutoFillBackground(true);
    QPalette headerPal;
    headerPal.setColor(QPalette::Window, theme.surface);
    headerBar_->setPalette(headerPal);

    auto* headerLayout = new QHBoxLayout(headerBar_);
    headerLayout->setContentsMargins(8, 4, 8, 4);

    auto* titleLabel = new QLabel("AI Assistant", headerBar_);
    titleLabel->setStyleSheet(
        QString("font-weight: bold; font-size: 12px; color: %1;").arg(theme.text.name()));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    {
        QSettings s;
        showToolOutput_ = s.value("ai/showToolOutput", false).toBool();
    }

    toolToggleBtn_ = new QPushButton(showToolOutput_ ? "Tools: ON" : "Tools: OFF", headerBar_);
    toolToggleBtn_->setAccessibleName("Toggle Tool Output Display");
    toolToggleBtn_->setFixedSize(68, 24);
    toolToggleBtn_->setCheckable(true);
    toolToggleBtn_->setChecked(showToolOutput_);
    toolToggleBtn_->setStyleSheet(
        QString("QPushButton { font-size: 10px; background: %1; color: %2; "
                "border: 1px solid %3; border-radius: 3px; }"
                "QPushButton:hover { background: %4; }"
                "QPushButton:checked { background: %5; color: #fff; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.surfaceLight.name(),
                 theme.accent.darker(130).name()));
    connect(toolToggleBtn_, &QPushButton::toggled, this, [this](bool on) {
        showToolOutput_ = on;
        toolToggleBtn_->setText(on ? "Tools: ON" : "Tools: OFF");
        QSettings s;
        s.setValue("ai/showToolOutput", on);
        updateToolBubbleVisibility();
    });
    headerLayout->addWidget(toolToggleBtn_);

    auto* clearBtn = new QPushButton("Clear", headerBar_);
    clearBtn->setAccessibleName("Clear Chat");
    clearBtn->setFixedSize(48, 24);
    clearBtn->setStyleSheet(
        QString("QPushButton { font-size: 10px; background: %1; color: %2; "
                "border: 1px solid %3; border-radius: 3px; }"
                "QPushButton:hover { background: %4; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.surfaceLight.name()));
    connect(clearBtn, &QPushButton::clicked, this, &AiChatWidget::clearChat);
    headerLayout->addWidget(clearBtn);

    auto* settingsBtn = new QPushButton("Settings", headerBar_);
    settingsBtn->setAccessibleName("AI Settings");
    settingsBtn->setFixedSize(60, 24);
    settingsBtn->setStyleSheet(clearBtn->styleSheet());
    connect(settingsBtn, &QPushButton::clicked, this, &AiChatWidget::showSettingsDialog);
    headerLayout->addWidget(settingsBtn);

    mainLayout_->addWidget(headerBar_);

    // Messages area
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameStyle(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setStyleSheet(
        QString("QScrollArea { background: %1; border: none; }"
                "QScrollBar:vertical { background: %1; width: 8px; }"
                "QScrollBar::handle:vertical { background: %2; border-radius: 4px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(theme.background.name(), theme.border.name()));

    messagesContainer_ = new QWidget();
    messagesContainer_->setAutoFillBackground(true);
    QPalette msgPal;
    msgPal.setColor(QPalette::Window, theme.background);
    messagesContainer_->setPalette(msgPal);

    messagesLayout_ = new QVBoxLayout(messagesContainer_);
    messagesLayout_->setContentsMargins(8, 8, 8, 8);
    messagesLayout_->setSpacing(8);
    messagesLayout_->addStretch();

    scrollArea_->setWidget(messagesContainer_);
    mainLayout_->addWidget(scrollArea_, 1);

    // Status label
    statusLabel_ = new QLabel("", this);
    statusLabel_->setFixedHeight(18);
    statusLabel_->setStyleSheet(
        QString("font-size: 10px; color: %1; padding: 0 8px;").arg(theme.textDim.name()));
    mainLayout_->addWidget(statusLabel_);

    // Input area
    auto* inputContainer = new QWidget(this);
    inputContainer->setAutoFillBackground(true);
    QPalette inputPal;
    inputPal.setColor(QPalette::Window, theme.surface);
    inputContainer->setPalette(inputPal);

    auto* inputLayout = new QHBoxLayout(inputContainer);
    inputLayout->setContentsMargins(8, 6, 8, 6);
    inputLayout->setSpacing(6);

    inputEdit_ = new QTextEdit(inputContainer);
    inputEdit_->setAccessibleName("AI Chat Input");
    inputEdit_->setPlaceholderText("Ask the AI assistant...");
    inputEdit_->setMaximumHeight(72);
    inputEdit_->setMinimumHeight(32);
    inputEdit_->setStyleSheet(
        QString("QTextEdit { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; padding: 4px 6px; font-size: 12px; }"
                "QTextEdit:focus { border: 1px solid %4; }")
            .arg(theme.background.name(), theme.text.name(),
                 theme.border.name(), theme.accent.name()));
    inputEdit_->installEventFilter(this);
    inputLayout->addWidget(inputEdit_, 1);

    sendBtn_ = new QPushButton("Send", inputContainer);
    sendBtn_->setAccessibleName("Send Message");
    sendBtn_->setFixedSize(52, 32);
    sendBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: #fff; border: none; "
                "border-radius: 4px; font-size: 11px; font-weight: bold; }"
                "QPushButton:hover { background: %2; }"
                "QPushButton:disabled { background: %3; color: %4; }")
            .arg(theme.accent.name(), theme.accentLight.name(),
                 theme.surfaceLight.name(), theme.textDim.name()));
    connect(sendBtn_, &QPushButton::clicked, this, &AiChatWidget::onSendClicked);
    inputLayout->addWidget(sendBtn_, 0, Qt::AlignBottom);

    mainLayout_->addWidget(inputContainer);
}

// ── Event filter for Enter key ──────────────────────────────────────────────

bool AiChatWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == inputEdit_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                return false;
            }
            onSendClicked();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ── Actions ─────────────────────────────────────────────────────────────────

void AiChatWidget::submitPrompt(const QString& text)
{
    if (text.trimmed().isEmpty()) return;
    inputEdit_->clear();
    appendUserBubble(text);
    aiService_->sendMessage(text);
}

void AiChatWidget::focusInput()
{
    inputEdit_->setFocus();
}

void AiChatWidget::onSendClicked()
{
    QString text = inputEdit_->toPlainText().trimmed();
    if (text.isEmpty()) return;
    submitPrompt(text);
}

void AiChatWidget::openSettings()
{
    showSettingsDialog();
}

void AiChatWidget::clearChat()
{
    aiService_->clearConversation();
    QLayoutItem* item;
    while ((item = messagesLayout_->takeAt(0)) != nullptr) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    messagesLayout_->addStretch();
    streamingLabel_ = nullptr;
    isStreaming_ = false;
    streamingText_.clear();
    toolBubbles_.clear();
    statusLabel_->clear();
}

// ── Message bubbles ─────────────────────────────────────────────────────────

void AiChatWidget::appendUserBubble(const QString& text)
{
    auto& theme = ThemeManager::instance().current();
    auto* container = new QWidget(messagesContainer_);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();

    auto* label = new QLabel(container);
    label->setAccessibleName("User message");
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setText(text);
    label->setStyleSheet(
        QString("QLabel { background: %1; color: %2; border-radius: 8px; "
                "padding: 8px 12px; font-size: 12px; }")
            .arg(theme.accent.darker(120).name(), QColor(255, 255, 255).name()));
    label->setMaximumWidth(320);
    layout->addWidget(label);

    int stretchIdx = messagesLayout_->count() - 1;
    messagesLayout_->insertWidget(stretchIdx, container);
    scrollToBottom();
}

void AiChatWidget::appendAssistantBubble(const QString& text)
{
    auto& theme = ThemeManager::instance().current();
    auto* container = new QWidget(messagesContainer_);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel(container);
    label->setAccessibleName("Assistant message");
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    label->setText(renderMarkdown(text));
    label->setStyleSheet(
        QString("QLabel { background: %1; color: %2; border-radius: 8px; "
                "padding: 8px 12px; font-size: 12px; }")
            .arg(theme.surfaceLight.name(), theme.text.name()));
    label->setMaximumWidth(320);
    layout->addWidget(label);
    layout->addStretch();

    int stretchIdx = messagesLayout_->count() - 1;
    messagesLayout_->insertWidget(stretchIdx, container);
    scrollToBottom();
}

void AiChatWidget::appendToolBubble(const QString& toolName, const QString& result, bool isError)
{
    auto& theme = ThemeManager::instance().current();
    auto* container = new QWidget(messagesContainer_);
    container->setProperty("toolBubble", true);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel(container);
    label->setAccessibleName(QString("Tool call: %1").arg(toolName));
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QString icon = isError ? "&#10060;" : "&#9989;";
    QString truncatedResult = result.length() > 200
        ? result.left(200) + "..."
        : result;
    label->setText(QString("<small>%1 <b>%2</b><br/>%3</small>")
                       .arg(icon, toolName.toHtmlEscaped(), truncatedResult.toHtmlEscaped()));

    QColor bgColor = isError ? QColor(80, 30, 30) : QColor(30, 55, 45);
    label->setStyleSheet(
        QString("QLabel { background: %1; color: %2; border-radius: 6px; "
                "padding: 6px 10px; font-size: 11px; }")
            .arg(bgColor.name(), theme.textDim.name()));
    label->setMaximumWidth(320);
    layout->addWidget(label);
    layout->addStretch();

    toolBubbles_.append(container);
    container->setVisible(showToolOutput_);

    int stretchIdx = messagesLayout_->count() - 1;
    messagesLayout_->insertWidget(stretchIdx, container);
    scrollToBottom();
}

void AiChatWidget::updateToolBubbleVisibility()
{
    for (auto* w : toolBubbles_) {
        if (w) w->setVisible(showToolOutput_);
    }
}

void AiChatWidget::appendErrorBubble(const QString& text)
{
    auto& theme = ThemeManager::instance().current();
    auto* container = new QWidget(messagesContainer_);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel(container);
    label->setAccessibleName("Error message");
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setText(text);
    label->setStyleSheet(
        QString("QLabel { background: %1; color: %2; border-radius: 8px; "
                "padding: 8px 12px; font-size: 12px; }")
            .arg(QColor(80, 30, 30).name(), theme.meterRed.name()));
    label->setMaximumWidth(320);
    layout->addWidget(label);
    layout->addStretch();

    int stretchIdx = messagesLayout_->count() - 1;
    messagesLayout_->insertWidget(stretchIdx, container);
    scrollToBottom();
}

// ── Streaming ───────────────────────────────────────────────────────────────

void AiChatWidget::updateStreamingBubble(const QString& token)
{
    auto& theme = ThemeManager::instance().current();
    streamingText_ += token;

    if (!streamingLabel_) {
        auto* container = new QWidget(messagesContainer_);
        container->setObjectName("streamingContainer");
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);

        streamingLabel_ = new QLabel(container);
        streamingLabel_->setAccessibleName("Assistant message (streaming)");
        streamingLabel_->setTextFormat(Qt::RichText);
        streamingLabel_->setWordWrap(true);
        streamingLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        streamingLabel_->setStyleSheet(
            QString("QLabel { background: %1; color: %2; border-radius: 8px; "
                    "padding: 8px 12px; font-size: 12px; }")
                .arg(theme.surfaceLight.name(), theme.text.name()));
        streamingLabel_->setMaximumWidth(320);
        layout->addWidget(streamingLabel_);
        layout->addStretch();

        int stretchIdx = messagesLayout_->count() - 1;
        messagesLayout_->insertWidget(stretchIdx, container);
        isStreaming_ = true;
    }

    streamingLabel_->setText(renderMarkdown(streamingText_));
    scrollToBottom();
}

void AiChatWidget::finalizeStreamingBubble()
{
    if (streamingLabel_ && !streamingText_.isEmpty()) {
        streamingLabel_->setText(renderMarkdown(streamingText_));
    }
    streamingLabel_ = nullptr;
    streamingText_.clear();
    isStreaming_ = false;
}

void AiChatWidget::scrollToBottom()
{
    QTimer::singleShot(10, this, [this]() {
        auto* sb = scrollArea_->verticalScrollBar();
        sb->setValue(sb->maximum());
    });
}

// ── Signal handlers ─────────────────────────────────────────────────────────

void AiChatWidget::onTokenReceived(const QString& token)
{
    updateStreamingBubble(token);
}

void AiChatWidget::onResponseComplete(const AiMessage& message)
{
    finalizeStreamingBubble();

    QString text = message.plainText();
    if (!text.isEmpty() && !isStreaming_ && !streamingLabel_) {
        // Only append a bubble if we didn't stream the text
    }

    statusLabel_->clear();
}

void AiChatWidget::onToolCallStarted(const QString& toolName, const QString& /*toolId*/)
{
    if (showToolOutput_)
        statusLabel_->setText(QString("Calling %1...").arg(toolName));
    else
        statusLabel_->setText("Working...");
    finalizeStreamingBubble();
}

void AiChatWidget::onToolCallFinished(const QString& toolName, const QString& /*toolId*/,
                                      const QString& result, bool isError)
{
    appendToolBubble(toolName, result, isError);
    statusLabel_->clear();
}

void AiChatWidget::onError(const QString& error)
{
    finalizeStreamingBubble();
    appendErrorBubble(error);
    statusLabel_->clear();
}

void AiChatWidget::onConfirmDestructive(const QString& toolName, const AiToolCall& call)
{
    auto& theme = ThemeManager::instance().current();

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("Confirm Action");
    dialog->setModal(true);
    dialog->setMinimumWidth(320);
    dialog->setStyleSheet(
        QString("QDialog { background: %1; color: %2; }"
                "QLabel { color: %2; }"
                "QPushButton { background: %3; color: %2; border: 1px solid %4; "
                "border-radius: 4px; padding: 6px 16px; font-size: 12px; }"
                "QPushButton:hover { background: %5; }")
            .arg(theme.background.name(), theme.text.name(),
                 theme.surface.name(), theme.border.name(),
                 theme.surfaceLight.name()));

    auto* layout = new QVBoxLayout(dialog);
    auto* msgLabel = new QLabel(
        QString("The AI wants to perform a destructive action:\n\n<b>%1</b>\n\n"
                "Input: %2\n\nAllow this action?")
            .arg(toolName.toHtmlEscaped(),
                 QString::fromUtf8(QJsonDocument(call.input).toJson(QJsonDocument::Indented))
                     .toHtmlEscaped()),
        dialog);
    msgLabel->setTextFormat(Qt::RichText);
    msgLabel->setWordWrap(true);
    layout->addWidget(msgLabel);

    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Yes | QDialogButtonBox::No, dialog);
    btnBox->button(QDialogButtonBox::Yes)->setStyleSheet(
        QString("QPushButton { background: %1; color: #fff; }").arg(theme.accent.name()));
    layout->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, dialog, [this, call, dialog]() {
        dialog->accept();
        aiService_->executeConfirmedTool(call);
    });
    connect(btnBox, &QDialogButtonBox::rejected, dialog, [this, call, dialog]() {
        dialog->reject();
        aiService_->cancelPendingTool(call);
    });

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void AiChatWidget::onBusyChanged(bool busy)
{
    sendBtn_->setEnabled(!busy);
    if (busy)
        statusLabel_->setText("Thinking...");
    else
        statusLabel_->clear();
}

// ── Settings dialog ─────────────────────────────────────────────────────────

void AiChatWidget::showSettingsDialog()
{
    auto& theme = ThemeManager::instance().current();

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("AI Settings");
    dialog->setMinimumWidth(400);
    dialog->setStyleSheet(
        QString("QDialog { background: %1; color: %2; }"
                "QLabel { color: %2; }"
                "QLineEdit { background: %3; color: %2; border: 1px solid %4; "
                "border-radius: 4px; padding: 4px 8px; }"
                "QCheckBox { color: %2; }"
                "QPushButton { background: %5; color: %2; border: 1px solid %4; "
                "border-radius: 4px; padding: 6px 16px; }"
                "QPushButton:hover { background: %6; }")
            .arg(theme.background.name(), theme.text.name(),
                 theme.surface.name(), theme.border.name(),
                 theme.surface.name(), theme.surfaceLight.name()));

    auto* layout = new QFormLayout(dialog);

    auto* apiKeyEdit = new QLineEdit(aiService_->apiKey(), dialog);
    apiKeyEdit->setAccessibleName("Anthropic API Key");
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit->setPlaceholderText("sk-ant-...");
    layout->addRow("API Key:", apiKeyEdit);

    auto* modelEdit = new QLineEdit(aiService_->model(), dialog);
    modelEdit->setAccessibleName("Model Name");
    modelEdit->setPlaceholderText("claude-sonnet-4-20250514");
    layout->addRow("Model:", modelEdit);

    auto* confirmCheck = new QCheckBox("Confirm destructive actions", dialog);
    confirmCheck->setAccessibleName("Confirm destructive actions");
    confirmCheck->setChecked(aiService_->confirmDestructive());
    layout->addRow("", confirmCheck);

    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addRow(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, dialog, [=]() {
        aiService_->setApiKey(apiKeyEdit->text().trimmed());
        aiService_->setModel(modelEdit->text().trimmed());
        aiService_->setConfirmDestructive(confirmCheck->isChecked());
        dialog->accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

// ── Markdown rendering ──────────────────────────────────────────────────────

QString AiChatWidget::renderMarkdown(const QString& text) const
{
    QString html = text.toHtmlEscaped();

    // Code blocks (``` ... ```)
    static QRegularExpression codeBlock("```(?:\\w*)\n?(.*?)```",
                                        QRegularExpression::DotMatchesEverythingOption);
    html.replace(codeBlock,
        "<pre style=\"background:#1a1a1a; padding:6px; border-radius:4px; "
        "font-family:monospace; font-size:11px; white-space:pre-wrap;\">\\1</pre>");

    // Inline code
    static QRegularExpression inlineCode("`([^`]+)`");
    html.replace(inlineCode,
        "<code style=\"background:#1a1a1a; padding:1px 4px; border-radius:3px; "
        "font-family:monospace; font-size:11px;\">\\1</code>");

    // Bold
    static QRegularExpression bold("\\*\\*(.+?)\\*\\*");
    html.replace(bold, "<b>\\1</b>");

    // Italic
    static QRegularExpression italic("\\*(.+?)\\*");
    html.replace(italic, "<i>\\1</i>");

    // Line breaks
    html.replace("\n", "<br/>");

    return html;
}

} // namespace freedaw
