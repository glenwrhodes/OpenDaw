#pragma once

#include "AiTypes.h"
#include "AiService.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QKeyEvent>

namespace freedaw {

class EditManager;
class AudioEngine;
class PluginScanner;

class AiChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit AiChatWidget(EditManager* editMgr, AudioEngine* audioEngine,
                          PluginScanner* pluginScanner, QWidget* parent = nullptr);

    void submitPrompt(const QString& text);
    void focusInput();
    void openSettings();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void requestRaise();

private:
    void setupUi();
    void onSendClicked();
    void onTokenReceived(const QString& token);
    void onResponseComplete(const AiMessage& message);
    void onToolCallStarted(const QString& toolName, const QString& toolId);
    void onToolCallFinished(const QString& toolName, const QString& toolId,
                            const QString& result, bool isError);
    void onError(const QString& error);
    void onConfirmDestructive(const QString& toolName, const AiToolCall& call);
    void onBusyChanged(bool busy);

    void appendUserBubble(const QString& text);
    void appendAssistantBubble(const QString& text);
    void appendToolBubble(const QString& toolName, const QString& result, bool isError);
    void appendErrorBubble(const QString& text);
    void updateStreamingBubble(const QString& token);
    void finalizeStreamingBubble();
    void scrollToBottom();

    void showSettingsDialog();
    void clearChat();

    QString renderMarkdown(const QString& text) const;

    AiService* aiService_;
    EditManager* editMgr_;

    QVBoxLayout* mainLayout_ = nullptr;
    QWidget* headerBar_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* messagesContainer_ = nullptr;
    QVBoxLayout* messagesLayout_ = nullptr;
    QTextEdit* inputEdit_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QLabel* streamingLabel_ = nullptr;
    QString streamingText_;
    bool isStreaming_ = false;

    bool showToolOutput_ = false;
    QPushButton* toolToggleBtn_ = nullptr;
    QVector<QWidget*> toolBubbles_;
    void updateToolBubbleVisibility();
};

} // namespace freedaw
