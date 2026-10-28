#pragma once

#include "AiTypes.h"
#include "AiToolDefs.h"
#include "AiToolExecutor.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

namespace OpenDaw {

class EditManager;
class AudioEngine;

class AiService : public QObject {
    Q_OBJECT
public:
    explicit AiService(EditManager* editMgr, AudioEngine* audioEngine,
                       PluginScanner* pluginScanner, QObject* parent = nullptr);

    void sendMessage(const QString& userText);
    void clearConversation();

    const QVector<AiMessage>& conversationHistory() const { return conversationHistory_; }
    void loadConversation(const QVector<AiMessage>& messages);

    void requestSessionTitle(const QString& userMessage);

    QString apiKey() const;
    void setApiKey(const QString& key);

    QString model() const;
    void setModel(const QString& model);
    static QString defaultModel();
    static QStringList suggestedModels();

    // For one-shot requests made outside the chat (e.g. AiHandSplitter).
    static QString configuredModel();
    static bool supportsStructuredOutputs(const QString& model);
    // Adds thinking/effort/fallback options for `model`; returns the anthropic-beta header value.
    static QByteArray applyModelOptions(QJsonObject& body, const QString& model,
                                        const QString& effort, bool allowFallback);

    bool confirmDestructive() const;
    void setConfirmDestructive(bool confirm);
    bool previewMixPlanMode() const;
    void setPreviewMixPlanMode(bool enabled);

    bool isBusy() const { return busy_; }

signals:
    void tokenReceived(const QString& token);
    void responseComplete(const AiMessage& message);
    void toolCallStarted(const QString& toolName, const QString& toolId);
    void toolCallFinished(const QString& toolName, const QString& toolId,
                          const QString& result, bool isError);
    void errorOccurred(const QString& error);
    void busyChanged(bool busy);
    void confirmDestructiveAction(const QString& toolName, const AiToolCall& call);
    void sessionTitleGenerated(const QString& title);

public slots:
    void executeConfirmedTool(const AiToolCall& call);
    void cancelPendingTool(const AiToolCall& call);

private:
    void sendToApi();
    void abortCurrentReply();
    void handleStreamChunk();
    void handleStreamFinished();
    void processAssistantResponse();
    void executeToolCalls(const QVector<AiToolCall>& calls);
    void continueAfterToolResults(const QVector<AiToolResult>& results,
                                  const AiMessage& assistantMsg);
    QString buildSystemPrompt() const;
    QJsonObject messageToJson(const AiMessage& msg) const;
    void setBusy(bool busy);

    EditManager* editMgr_;
    AudioEngine* audioEngine_;
    AiToolExecutor* toolExecutor_;
    QNetworkAccessManager* nam_;

    QVector<AiMessage> conversationHistory_;
    QByteArray streamBuffer_;
    QNetworkReply* currentReply_ = nullptr;

    QString currentAssistantText_;
    QVector<AiContentBlock> currentContentBlocks_;
    QJsonObject currentToolInput_;
    QString currentToolInputStr_;
    bool inToolInput_ = false;
    int currentBlockIndex_ = -1;
    QString currentStopReason_;
    QString currentRefusalCategory_;

    bool busy_ = false;

    static constexpr int MAX_TOOL_ROUNDS = 20;
    int toolRoundCount_ = 0;

    QSet<QString> executedToolIds_;
    void pruneHistory();
};

} // namespace OpenDaw
