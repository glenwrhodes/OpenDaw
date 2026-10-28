#include "AiChatWidget.h"
#include "engine/EditManager.h"
#include "engine/AudioEngine.h"
#include "engine/PluginScanner.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include <QHBoxLayout>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>
#include <QApplication>
#include <QRegularExpression>
#include <QAbstractTextDocumentLayout>
#include <QSettings>

namespace OpenDaw {

AiChatWidget::AiChatWidget(EditManager* editMgr, AudioEngine* audioEngine,
                           PluginScanner* pluginScanner, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("AI Assistant Panel");
    aiService_ = new AiService(editMgr, audioEngine, pluginScanner, this);
    chatStore_ = new AiChatStore();

    connect(aiService_, &AiService::tokenReceived, this, &AiChatWidget::onTokenReceived);
    connect(aiService_, &AiService::responseComplete, this, &AiChatWidget::onResponseComplete);
    connect(aiService_, &AiService::toolCallStarted, this, &AiChatWidget::onToolCallStarted);
    connect(aiService_, &AiService::toolCallFinished, this, &AiChatWidget::onToolCallFinished);
    connect(aiService_, &AiService::errorOccurred, this, &AiChatWidget::onError);
    connect(aiService_, &AiService::confirmDestructiveAction, this, &AiChatWidget::onConfirmDestructive);
    connect(aiService_, &AiService::busyChanged, this, &AiChatWidget::onBusyChanged);
    connect(aiService_, &AiService::sessionTitleGenerated, this, &AiChatWidget::onSessionTitleGenerated);

    streamThrottle_ = new QTimer(this);
    streamThrottle_->setInterval(50);
    streamThrottle_->setSingleShot(false);
    connect(streamThrottle_, &QTimer::timeout, this, [this]() {
        if (streamDirty_ && streamingLabel_) {
            streamingLabel_->setMarkdown(streamingText_);
            scrollToBottom();
            streamDirty_ = false;
        }
    });

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

    const auto miFont = icons::materialIcons(14);
    const QString smallBtnStyle =
        QString("QPushButton { font-size: 14px; background: %1; color: %2; "
                "border: 1px solid %3; border-radius: 3px; }"
                "QPushButton:hover { background: %4; }"
                "QPushButton:checked { background: %5; color: #fff; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.surfaceLight.name(),
                 theme.accent.darker(130).name());

    historyToggleBtn_ = new QPushButton(headerBar_);
    historyToggleBtn_->setAccessibleName("Toggle Chat History Sidebar");
    historyToggleBtn_->setFont(miFont);
    historyToggleBtn_->setText(QString(icons::mi::Chat));
    historyToggleBtn_->setToolTip("Show chat history");
    historyToggleBtn_->setFixedSize(28, 24);
    historyToggleBtn_->setCheckable(true);
    historyToggleBtn_->setStyleSheet(smallBtnStyle);
    connect(historyToggleBtn_, &QPushButton::toggled, this, [this](bool on) {
        if (historyPanel_) {
            historyPanel_->setVisible(on);
            if (on) refreshSessionList();
        }
        historyToggleBtn_->setToolTip(on ? "Hide chat history" : "Show chat history");
    });
    headerLayout->addWidget(historyToggleBtn_);

    newChatBtn_ = new QPushButton(headerBar_);
    newChatBtn_->setAccessibleName("New Chat");
    newChatBtn_->setFont(miFont);
    newChatBtn_->setText(QString(icons::mi::Add));
    newChatBtn_->setToolTip("Start new chat");
    newChatBtn_->setFixedSize(28, 24);
    newChatBtn_->setStyleSheet(smallBtnStyle);
    connect(newChatBtn_, &QPushButton::clicked, this, &AiChatWidget::startNewChat);
    headerLayout->addWidget(newChatBtn_);

    toolToggleBtn_ = new QPushButton(headerBar_);
    toolToggleBtn_->setAccessibleName("Toggle Tool Output Display");
    toolToggleBtn_->setFont(miFont);
    toolToggleBtn_->setText(showToolOutput_ ? QString(icons::mi::Visibility)
                                            : QString(icons::mi::VisibilityOff));
    toolToggleBtn_->setToolTip(showToolOutput_ ? "Tool calls visible – click to hide"
                                               : "Tool calls hidden – click to show");
    toolToggleBtn_->setFixedSize(28, 24);
    toolToggleBtn_->setCheckable(true);
    toolToggleBtn_->setChecked(showToolOutput_);
    toolToggleBtn_->setStyleSheet(smallBtnStyle);
    connect(toolToggleBtn_, &QPushButton::toggled, this, [this](bool on) {
        showToolOutput_ = on;
        toolToggleBtn_->setText(on ? QString(icons::mi::Visibility)
                                   : QString(icons::mi::VisibilityOff));
        toolToggleBtn_->setToolTip(on ? "Tool calls visible – click to hide"
                                      : "Tool calls hidden – click to show");
        QSettings s;
        s.setValue("ai/showToolOutput", on);
        updateToolBubbleVisibility();
    });
    headerLayout->addWidget(toolToggleBtn_);

    const QString textBtnStyle =
        QString("QPushButton { font-size: 10px; background: %1; color: %2; "
                "border: 1px solid %3; border-radius: 3px; }"
                "QPushButton:hover { background: %4; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.surfaceLight.name());

    auto* settingsBtn = new QPushButton("Settings", headerBar_);
    settingsBtn->setAccessibleName("AI Settings");
    settingsBtn->setFixedSize(60, 24);
    settingsBtn->setStyleSheet(textBtnStyle);
    connect(settingsBtn, &QPushButton::clicked, this, &AiChatWidget::showSettingsDialog);
    headerLayout->addWidget(settingsBtn);

    mainLayout_->addWidget(headerBar_);

    // ── Content area: sidebar + messages ─────────────────────────────────────

    auto* contentWidget = new QWidget(this);
    auto* contentLayout = new QHBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // History sidebar (hidden by default)
    historyPanel_ = new QWidget(contentWidget);
    historyPanel_->setFixedWidth(200);
    historyPanel_->setAutoFillBackground(true);
    QPalette sidePal;
    sidePal.setColor(QPalette::Window, theme.surface);
    historyPanel_->setPalette(sidePal);
    historyPanel_->setVisible(false);

    auto* sideLayout = new QVBoxLayout(historyPanel_);
    sideLayout->setContentsMargins(4, 4, 4, 4);
    sideLayout->setSpacing(4);

    auto* sideTitle = new QLabel("Chat Sessions", historyPanel_);
    sideTitle->setAccessibleName("Chat Sessions List");
    sideTitle->setStyleSheet(
        QString("font-weight: bold; font-size: 11px; color: %1; padding: 4px;").arg(theme.text.name()));
    sideLayout->addWidget(sideTitle);

    sessionList_ = new QListWidget(historyPanel_);
    sessionList_->setAccessibleName("Chat Session List");
    sessionList_->setStyleSheet(
        QString("QListWidget { background: %1; border: none; font-size: 11px; color: %2; }"
                "QListWidget::item { padding: 6px 8px; border-bottom: 1px solid %3; }"
                "QListWidget::item:selected { background: %4; color: #fff; }"
                "QListWidget::item:hover { background: %5; }")
            .arg(theme.surface.name(), theme.text.name(),
                 theme.border.name(), theme.accent.darker(130).name(),
                 theme.surfaceLight.name()));
    connect(sessionList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        QString sessionId = item->data(Qt::UserRole).toString();
        if (sessionId != chatStore_->activeSessionId())
            switchToSession(sessionId);
    });
    sideLayout->addWidget(sessionList_, 1);

    contentLayout->addWidget(historyPanel_);

    // Messages area
    scrollArea_ = new QScrollArea(contentWidget);
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
    contentLayout->addWidget(scrollArea_, 1);

    mainLayout_->addWidget(contentWidget, 1);

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
    if (aiService_->isBusy()) {
        appendAssistantBubble("I'm still processing the previous request. Please wait.");
        return;
    }

    auto* session = chatStore_->activeSession();
    bool needsTitle = session && session->title == "New Chat" && session->messages.isEmpty();

    inputEdit_->clear();
    appendUserBubble(text);
    aiService_->sendMessage(text);

    if (needsTitle)
        aiService_->requestSessionTitle(text);
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

void AiChatWidget::clearChatUi()
{
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
    auto* container = new QWidget(messagesContainer_);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* browser = createMessageBrowser(container);
    browser->setAccessibleName("Assistant message");
    browser->setMarkdown(text);
    layout->addWidget(browser);
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
    streamingText_ += token;

    if (!streamingLabel_) {
        auto* container = new QWidget(messagesContainer_);
        container->setObjectName("streamingContainer");
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);

        streamingLabel_ = createMessageBrowser(container);
        streamingLabel_->setAccessibleName("Assistant message (streaming)");
        layout->addWidget(streamingLabel_);
        layout->addStretch();

        int stretchIdx = messagesLayout_->count() - 1;
        messagesLayout_->insertWidget(stretchIdx, container);
        isStreaming_ = true;
    }

    streamDirty_ = true;
    if (!streamThrottle_->isActive())
        streamThrottle_->start();
}

void AiChatWidget::finalizeStreamingBubble()
{
    streamThrottle_->stop();
    if (streamingLabel_ && !streamingText_.isEmpty()) {
        streamingLabel_->setMarkdown(streamingText_);
    }
    streamingLabel_ = nullptr;
    streamingText_.clear();
    isStreaming_ = false;
    streamDirty_ = false;
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
    QString stageLabel;
    if (toolName.startsWith("analyze_")) stageLabel = "Analyzing";
    else if (toolName.contains("preset") || toolName.contains("reverb") || toolName.contains("dynamic"))
        stageLabel = "Processing";
    else if (toolName.contains("route") || toolName.contains("bus"))
        stageLabel = "Routing";
    else if (toolName.contains("master"))
        stageLabel = "Mastering";

    if (showToolOutput_)
        statusLabel_->setText(QString("Calling %1...").arg(toolName));
    else
        statusLabel_->setText(stageLabel.isEmpty() ? "Working..." : QString("%1...").arg(stageLabel));
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

// ── Session management ──────────────────────────────────────────────────────

void AiChatWidget::startNewChat()
{
    chatStore_->updateActiveMessages(aiService_->conversationHistory());
    chatStore_->createSession();
    aiService_->clearConversation();
    clearChatUi();
    refreshSessionList();
}

void AiChatWidget::switchToSession(const QString& sessionId)
{
    if (aiService_->isBusy()) return;

    chatStore_->updateActiveMessages(aiService_->conversationHistory());
    chatStore_->setActiveSession(sessionId);

    auto* session = chatStore_->activeSession();
    if (!session) return;

    aiService_->loadConversation(session->messages);
    clearChatUi();
    rebuildUiFromHistory(session->messages);
    refreshSessionList();
}

void AiChatWidget::rebuildUiFromHistory(const QVector<AiMessage>& messages)
{
    for (auto& msg : messages) {
        if (msg.role == AiRole::User) {
            QString text = msg.plainText();
            if (!text.isEmpty())
                appendUserBubble(text);
        } else if (msg.role == AiRole::Assistant) {
            QString text = msg.plainText();
            if (!text.isEmpty())
                appendAssistantBubble(text);
            for (auto& block : msg.content) {
                if (block.type == "tool_use") {
                    appendToolBubble(block.toolCall.name, "", false);
                }
            }
        }
    }
}

void AiChatWidget::refreshSessionList()
{
    if (!sessionList_) return;
    sessionList_->clear();

    QDateTime now = QDateTime::currentDateTime();
    for (auto& session : chatStore_->sessions()) {
        QString dateStr;
        auto sessionDate = session.updatedAt.date();
        if (sessionDate == now.date())
            dateStr = session.updatedAt.toString("h:mm ap");
        else if (sessionDate == now.date().addDays(-1))
            dateStr = "Yesterday";
        else
            dateStr = session.updatedAt.toString("MMM d");

        auto* item = new QListWidgetItem();
        item->setData(Qt::UserRole, session.id);
        item->setText(session.title + "\n" + dateStr);
        item->setToolTip(session.title);

        if (session.id == chatStore_->activeSessionId()) {
            auto font = item->font();
            font.setBold(true);
            item->setFont(font);
        }

        sessionList_->addItem(item);
    }
}

void AiChatWidget::onSessionTitleGenerated(const QString& title)
{
    chatStore_->setSessionTitle(chatStore_->activeSessionId(), title);
    refreshSessionList();
}

QString AiChatWidget::chatFilePath() const
{
    if (!editMgr_) return {};
    return AiChatStore::chatFilePath(editMgr_->currentFile());
}

void AiChatWidget::saveChatSessions()
{
    chatStore_->updateActiveMessages(aiService_->conversationHistory());
    QString path = chatFilePath();
    if (!path.isEmpty())
        chatStore_->saveTo(path);
}

void AiChatWidget::loadChatSessions()
{
    QString path = chatFilePath();
    if (path.isEmpty()) {
        chatStore_->reset();
        aiService_->clearConversation();
        clearChatUi();
        return;
    }

    chatStore_->loadFrom(path);

    auto* session = chatStore_->activeSession();
    if (session && !session->messages.isEmpty()) {
        aiService_->loadConversation(session->messages);
        clearChatUi();
        rebuildUiFromHistory(session->messages);
    } else {
        aiService_->clearConversation();
        clearChatUi();
    }

    refreshSessionList();
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
                "QLineEdit, QComboBox { background: %3; color: %2; border: 1px solid %4; "
                "border-radius: 4px; padding: 4px 8px; }"
                "QComboBox QLineEdit { border: none; padding: 0; background: transparent; }"
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

    auto* modelCombo = new QComboBox(dialog);
    modelCombo->setAccessibleName("Model Name");
    modelCombo->setEditable(true);
    modelCombo->addItems(AiService::suggestedModels());
    modelCombo->setCurrentText(aiService_->model());
    modelCombo->lineEdit()->setPlaceholderText(AiService::defaultModel());
    modelCombo->setToolTip("Pick a model or type any Claude model ID. "
                           "Leave blank to use the default (" + AiService::defaultModel() + ").");
    layout->addRow("Model:", modelCombo);

    auto* confirmCheck = new QCheckBox("Confirm destructive actions", dialog);
    confirmCheck->setAccessibleName("Confirm destructive actions");
    confirmCheck->setChecked(aiService_->confirmDestructive());
    layout->addRow("", confirmCheck);

    auto* previewMixPlanCheck = new QCheckBox("Preview mix/master plan before applying changes", dialog);
    previewMixPlanCheck->setAccessibleName("Preview mix master plan");
    previewMixPlanCheck->setChecked(aiService_->previewMixPlanMode());
    layout->addRow("", previewMixPlanCheck);

    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addRow(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, dialog, [=]() {
        aiService_->setApiKey(apiKeyEdit->text().trimmed());
        aiService_->setModel(modelCombo->currentText().trimmed());
        aiService_->setConfirmDestructive(confirmCheck->isChecked());
        aiService_->setPreviewMixPlanMode(previewMixPlanCheck->isChecked());
        dialog->accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

// ── Markdown browser factory ────────────────────────────────────────────────

QTextBrowser* AiChatWidget::createMessageBrowser(QWidget* parent) const
{
    auto& theme = ThemeManager::instance().current();
    auto* browser = new QTextBrowser(parent);
    browser->setOpenExternalLinks(true);
    browser->setFrameStyle(QFrame::NoFrame);
    browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setReadOnly(true);
    browser->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);

    browser->setStyleSheet(
        QString("QTextBrowser { background: %1; color: %2; border: none; "
                "border-radius: 8px; font-size: 12px; }"
                "QScrollBar { width: 0; height: 0; }")
            .arg(theme.surfaceLight.name(), theme.text.name()));

    browser->document()->setDocumentMargin(10);
    browser->document()->setDefaultStyleSheet(
        QString("table { border-collapse: collapse; margin: 4px 0; }"
                "th, td { border: 1px solid %1; padding: 3px 8px; }"
                "th { background-color: %2; font-weight: bold; }"
                "code { background-color: #1e1e1e; font-family: 'Consolas', monospace; font-size: 11px; }"
                "pre { background-color: #1e1e1e; padding: 6px; font-family: 'Consolas', monospace; font-size: 11px; }"
                "blockquote { border-left: 3px solid %1; padding-left: 8px; margin-left: 0; color: %3; }"
                "a { color: %4; }"
                "h1 { font-size: 16px; margin-top: 6px; margin-bottom: 4px; }"
                "h2 { font-size: 14px; margin-top: 5px; margin-bottom: 3px; }"
                "h3 { font-size: 13px; margin-top: 4px; margin-bottom: 2px; }"
                "ul, ol { margin-top: 2px; margin-bottom: 2px; }")
            .arg(theme.border.name(), theme.surface.name(),
                 theme.textDim.name(), theme.accent.name()));

    browser->setMaximumWidth(500);

    connect(browser->document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged,
            browser, [browser](const QSizeF& newSize) {
        int h = qMax(28, static_cast<int>(newSize.height()) + 4);
        browser->setMinimumHeight(h);
        browser->setMaximumHeight(h);
    });

    return browser;
}

} // namespace OpenDaw
