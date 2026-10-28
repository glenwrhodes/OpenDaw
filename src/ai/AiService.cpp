#include "AiService.h"
#include <utility>
#include "engine/EditManager.h"
#include "engine/AudioEngine.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

namespace OpenDaw {

static const QString kApiUrl = "https://api.anthropic.com/v1/messages";
static const QString kDefaultModel = "claude-opus-5-5";
// Thinking counts toward max_tokens, so leave room for it as well as the reply.
static const int kMaxTokens = 64000;

// Saved by older builds as the default; treat it as "use the current default".
static const QStringList kLegacyDefaultModels = { "claude-opus-4-6" };

// Models that take adaptive thinking and output_config.effort.
static const QStringList kAdaptiveThinkingModels = {
    "claude-opus-5-5", "claude-opus-5", "claude-opus-4-8", "claude-opus-4-7", "claude-opus-4-6",
    "claude-sonnet-5-5", "claude-sonnet-5", "claude-sonnet-4-6",
    "claude-fable-5-1", "claude-fable-5", "claude-haiku-5-5"
};

// Models whose thinking blocks are bound to the conversation prefix. The system
// prompt carries a live project snapshot that changes every turn, so ask the API
// to drop stale thinking blocks instead of rejecting the request.
static const QStringList kPreservedThinkingModels = {
    "claude-opus-5-5", "claude-sonnet-5-5", "claude-fable-5-1", "claude-haiku-5-5"
};

// Models that support server-side refusal fallbacks ("default" routing).
static const QStringList kFallbackModels = {
    "claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5", "claude-fable-5-1"
};

// Models that accept output_config.format (structured JSON outputs).
static const QStringList kStructuredOutputModels = {
    "claude-opus-5-5", "claude-opus-5", "claude-opus-4-8",
    "claude-sonnet-5-5", "claude-sonnet-5",
    "claude-fable-5-1", "claude-fable-5", "claude-haiku-5-5", "claude-haiku-4-5"
};

bool AiService::supportsStructuredOutputs(const QString& model)
{
    return kStructuredOutputModels.contains(model);
}

// Adds the model-specific request options and returns the anthropic-beta header value.
QByteArray AiService::applyModelOptions(QJsonObject& body, const QString& model,
                                        const QString& effort, bool allowFallback)
{
    QStringList betas;

    if (kAdaptiveThinkingModels.contains(model)) {
        QJsonObject thinking;
        thinking["type"] = "adaptive";
        if (kPreservedThinkingModels.contains(model)) {
            QJsonObject binding;
            binding["prefix_mismatch_behavior"] = "drop_block";
            thinking["block_binding"] = binding;
            betas << "thinking-binding-controls-2026-08-01";
        }
        body["thinking"] = thinking;

        QJsonObject outputConfig;
        outputConfig["effort"] = effort;
        body["output_config"] = outputConfig;
    }

    if (allowFallback && kFallbackModels.contains(model)) {
        body["fallbacks"] = "default";
        betas << "server-side-fallback-2026-07-01";
    }

    return betas.join(',').toUtf8();
}

AiService::AiService(EditManager* editMgr, AudioEngine* audioEngine,
                     PluginScanner* pluginScanner, QObject* parent)
    : QObject(parent), editMgr_(editMgr), audioEngine_(audioEngine)
{
    toolExecutor_ = new AiToolExecutor(editMgr, audioEngine, pluginScanner, this);
    nam_ = new QNetworkAccessManager(this);
}

// ── Settings ────────────────────────────────────────────────────────────────

QString AiService::apiKey() const
{
    QSettings s;
    return s.value("ai/apiKey").toString();
}

void AiService::setApiKey(const QString& key)
{
    QSettings s;
    s.setValue("ai/apiKey", key);
}

QString AiService::model() const
{
    return configuredModel();
}

QString AiService::configuredModel()
{
    QSettings s;
    QString m = s.value("ai/model").toString().trimmed();
    if (m.isEmpty() || kLegacyDefaultModels.contains(m))
        return kDefaultModel;
    return m;
}

QString AiService::defaultModel()
{
    return kDefaultModel;
}

QStringList AiService::suggestedModels()
{
    return { "claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-5-5", "claude-fable-5-1" };
}

void AiService::setModel(const QString& m)
{
    QSettings s;
    s.setValue("ai/model", m);
}

bool AiService::confirmDestructive() const
{
    QSettings s;
    return s.value("ai/confirmDestructive", true).toBool();
}

void AiService::setConfirmDestructive(bool confirm)
{
    QSettings s;
    s.setValue("ai/confirmDestructive", confirm);
}

bool AiService::previewMixPlanMode() const
{
    QSettings s;
    return s.value("ai/previewMixPlanMode", false).toBool();
}

void AiService::setPreviewMixPlanMode(bool enabled)
{
    QSettings s;
    s.setValue("ai/previewMixPlanMode", enabled);
}

void AiService::setBusy(bool b)
{
    if (busy_ != b) {
        busy_ = b;
        emit busyChanged(b);
    }
}

// ── System prompt ───────────────────────────────────────────────────────────

QString AiService::buildSystemPrompt() const
{
    QString prompt =
        "You are an AI assistant integrated into OpenDaw, a digital audio workstation. "
        "You help users manage their music projects by manipulating tracks, effects, "
        "routing, and transport controls.\n\n";

    if (editMgr_ && editMgr_->edit()) {
        prompt += "Current project state (live snapshot — includes any changes you have already made in this conversation):\n";
        prompt += QString("- Tempo: %1 BPM\n").arg(editMgr_->getBpm(), 0, 'f', 1);
        prompt += QString("- Time signature: %1/%2\n")
                      .arg(editMgr_->getTimeSigNumerator())
                      .arg(editMgr_->getTimeSigDenominator());

        auto& transport = editMgr_->transport();
        QString state = transport.isRecording() ? "Recording" :
                        transport.isPlaying() ? "Playing" : "Stopped";
        prompt += QString("- Transport: %1 at %2s\n")
                      .arg(state)
                      .arg(transport.getPosition().inSeconds(), 0, 'f', 2);

        auto tracks = editMgr_->getAudioTracks();
        prompt += QString("- Tracks (%1):\n").arg(tracks.size());
        for (int i = 0; i < tracks.size(); ++i) {
            auto* t = tracks[i];
            QString type = editMgr_->isBusTrack(t) ? "bus" :
                          editMgr_->isMidiTrack(t) ? "midi" : "audio";
            QString name = QString::fromStdString(t->getName().toStdString());
            prompt += QString("  [%1] \"%2\" (%3)%4%5\n")
                          .arg(i).arg(name, type,
                               t->isMuted(false) ? " MUTED" : "",
                               t->isSolo(false) ? " SOLO" : "");
        }
    }

    prompt += "\nRules:\n"
              "- Use the provided tools to manipulate the DAW. Call tools as needed.\n"
              "- For bulk operations, call multiple tools in sequence.\n"
              "- The project state shown above is a LIVE snapshot that updates after each tool call. "
              "If you just created tracks or clips, they will appear in the snapshot — do NOT treat "
              "them as pre-existing or comment on them as if seeing them for the first time. "
              "Only reference things the USER mentioned or that existed BEFORE your first action.\n"
              "- After completing actions, briefly confirm what you did.\n"
              "- You can create MIDI clips and write notes into them. Use create_midi_clip to make a clip, "
              "then add_midi_notes to populate it. Note positions are in beats relative to clip start. "
              "Batch as many notes as possible into a single add_midi_notes call for efficiency. "
              "Middle C is MIDI note 60. A bar in 4/4 time is 4 beats.\n"
              "- When users ask about effects parameters, use get_track_effects to see "
              "current values before modifying them.\n"
              "- Parameter values are normalized 0.0 to 1.0.\n"
              "- For mix/master requests, use staged workflow: analyze -> apply -> verify.\n"
              "- Before major processing, call analysis tools first.\n"
              "- Stop when targets are met or no meaningful improvement remains.\n"
              "\nMIDI & Music Theory (CRITICAL — follow strictly):\n"
              "- ALWAYS establish the key and scale BEFORE writing any notes. If the user specifies "
              "a key, use it. If not, ask or assume C major. State the key in your response.\n"
              "- Every note you write MUST belong to the scale of the stated key unless it is an "
              "intentional chromatic passing tone, approach note, or borrowed chord tone. "
              "Random out-of-key notes like Db in C major are WRONG.\n"
              "- Scale reference (use these and their relative modes):\n"
              "  Major (Ionian): W-W-H-W-W-W-H  (C major = C D E F G A B)\n"
              "  Natural minor (Aeolian): W-H-W-W-H-W-W  (A minor = A B C D E F G)\n"
              "  Harmonic minor: raise 7th  (A harm. minor = A B C D E F G#)\n"
              "  Melodic minor (ascending): raise 6th and 7th\n"
              "- Chromatic notes are ONLY acceptable when:\n"
              "  (a) They are passing tones connecting two diatonic notes stepwise (e.g. F-F#-G in C major).\n"
              "  (b) They are part of a secondary dominant or borrowed chord (e.g. E/G# resolving to Am in C major).\n"
              "  (c) The user explicitly requests chromatic or atonal writing.\n"
              "  (d) They are approach notes (half-step below a chord tone, on a weak beat, resolving immediately).\n"
              "- For chords, use diatonic triads/sevenths of the key unless a chromatic chord is musically justified:\n"
              "  C major diatonic triads: C Dm Em F G Am Bdim\n"
              "  A minor diatonic triads: Am Bdim C Dm Em F G\n"
              "- Double-check every note number against the key before submitting. If in doubt, stay diatonic.\n"
              "- When writing drums (channel 10 / GM percussion), key constraints do not apply.\n";

    if (previewMixPlanMode()) {
        prompt += "- Preview mode is ON: call preview_mix_plan before mutating tools and wait for user confirmation intent.\n";
    }

    return prompt;
}

// ── Conversation ────────────────────────────────────────────────────────────

void AiService::clearConversation()
{
    abortCurrentReply();
    conversationHistory_.clear();
    toolRoundCount_ = 0;
    executedToolIds_.clear();
    setBusy(false);
}

void AiService::abortCurrentReply()
{
    // abort() emits finished() synchronously; disconnect first so the stream
    // handlers don't process (or null out) a reply we are throwing away.
    if (auto* reply = std::exchange(currentReply_, nullptr)) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

void AiService::loadConversation(const QVector<AiMessage>& messages)
{
    clearConversation();
    conversationHistory_ = messages;
}

void AiService::requestSessionTitle(const QString& userMessage)
{
    QString key = apiKey();
    if (key.isEmpty()) return;

    QJsonObject body;
    body["model"] = model();
    body["max_tokens"] = 2048;
    body["system"] = QString("Generate a concise 3-6 word title for this chat. "
                             "Reply with ONLY the title, no quotes or punctuation.");
    QByteArray betas = applyModelOptions(body, model(), "low", false);
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    QJsonArray contentArr;
    QJsonObject textBlock;
    textBlock["type"] = "text";
    textBlock["text"] = userMessage;
    contentArr.append(textBlock);
    userMsg["content"] = contentArr;
    messages.append(userMsg);
    body["messages"] = messages;

    QUrl apiUrl("https://api.anthropic.com/v1/messages");
    QNetworkRequest request{apiUrl};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", key.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    if (!betas.isEmpty())
        request.setRawHeader("anthropic-beta", betas);
    request.setTransferTimeout(30000);

    auto* reply = nam_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        auto doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull()) return;
        auto obj = doc.object();
        QString title;
        for (auto val : obj["content"].toArray()) {
            auto blockObj = val.toObject();
            if (blockObj["type"].toString() == "text") {
                title = blockObj["text"].toString().trimmed();
                break;
            }
        }
        if (title.isEmpty()) return;

        if (title.length() > 60)
            title = title.left(57) + "...";

        emit sessionTitleGenerated(title);
    });
}

void AiService::pruneHistory()
{
    if (conversationHistory_.size() <= 50) return;
    while (conversationHistory_.size() > 40)
        conversationHistory_.removeFirst();
}

void AiService::sendMessage(const QString& userText)
{
    if (busy_) return;

    QString key = apiKey();
    if (key.isEmpty()) {
        emit errorOccurred("No API key set. Open AI settings to configure your Anthropic API key.");
        return;
    }

    AiMessage userMsg = AiMessage::userMessage(userText);
    conversationHistory_.append(userMsg);
    toolRoundCount_ = 0;

    sendToApi();
}

// ── API call ────────────────────────────────────────────────────────────────

QJsonObject AiService::messageToJson(const AiMessage& msg) const
{
    QJsonObject obj;
    obj["role"] = (msg.role == AiRole::User) ? "user" : "assistant";

    QJsonArray contentArr;
    for (auto& block : msg.content) {
        QJsonObject blockObj;
        if (block.type == "text") {
            blockObj["type"] = "text";
            blockObj["text"] = block.text;
        } else if (block.type == "thinking") {
            // Thinking blocks must be passed back unmodified.
            blockObj["type"] = "thinking";
            blockObj["thinking"] = block.text;
            blockObj["signature"] = block.signature;
        } else if (block.type == "redacted_thinking") {
            blockObj["type"] = "redacted_thinking";
            blockObj["data"] = block.data;
        } else if (block.type == "tool_use") {
            blockObj["type"] = "tool_use";
            blockObj["id"] = block.toolCall.id;
            blockObj["name"] = block.toolCall.name;
            blockObj["input"] = block.toolCall.input;
        } else if (block.type == "tool_result") {
            blockObj["type"] = "tool_result";
            blockObj["tool_use_id"] = block.toolResult.toolUseId;
            blockObj["content"] = block.toolResult.content;
            if (block.toolResult.isError)
                blockObj["is_error"] = true;
        } else {
            continue;
        }
        contentArr.append(blockObj);
    }
    obj["content"] = contentArr;
    return obj;
}

void AiService::sendToApi()
{
    setBusy(true);
    streamBuffer_.clear();
    currentAssistantText_.clear();
    currentContentBlocks_.clear();
    currentToolInputStr_.clear();
    inToolInput_ = false;
    currentBlockIndex_ = -1;
    currentStopReason_.clear();
    currentRefusalCategory_.clear();

    QJsonObject body;
    body["model"] = model();
    body["max_tokens"] = kMaxTokens;
    body["stream"] = true;
    body["system"] = buildSystemPrompt();
    body["tools"] = AiToolDefs::allTools();
    QByteArray betas = applyModelOptions(body, model(), "high", true);

    QJsonArray messages;
    for (auto& msg : conversationHistory_)
        messages.append(messageToJson(msg));
    body["messages"] = messages;

    QUrl apiUrl(kApiUrl);
    QNetworkRequest request{apiUrl};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", apiKey().toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    if (!betas.isEmpty())
        request.setRawHeader("anthropic-beta", betas);

    request.setTransferTimeout(60000);

    pruneHistory();

    QByteArray bodyData = QJsonDocument(body).toJson(QJsonDocument::Compact);
    abortCurrentReply();
    currentReply_ = nam_->post(request, bodyData);

    connect(currentReply_, &QNetworkReply::readyRead,
            this, &AiService::handleStreamChunk);
    connect(currentReply_, &QNetworkReply::finished,
            this, &AiService::handleStreamFinished);
}

// ── SSE stream parsing ──────────────────────────────────────────────────────

void AiService::handleStreamChunk()
{
    if (!currentReply_ || sender() != currentReply_) return;
    streamBuffer_.append(currentReply_->readAll());

    while (true) {
        int nlPos = streamBuffer_.indexOf('\n');
        if (nlPos < 0) break;

        QByteArray line = streamBuffer_.left(nlPos).trimmed();
        streamBuffer_.remove(0, nlPos + 1);

        if (line.isEmpty() || line.startsWith("event:")) continue;
        if (!line.startsWith("data: ")) continue;

        QByteArray jsonData = line.mid(6);
        if (jsonData == "[DONE]") continue;

        QJsonParseError parseErr;
        auto doc = QJsonDocument::fromJson(jsonData, &parseErr);
        if (parseErr.error != QJsonParseError::NoError) continue;

        auto obj = doc.object();
        QString type = obj["type"].toString();

        if (type == "content_block_start") {
            auto blockObj = obj["content_block"].toObject();
            QString blockType = blockObj["type"].toString();
            currentBlockIndex_ = obj["index"].toInt();

            AiContentBlock block;
            block.type = blockType;

            if (blockType == "tool_use") {
                block.toolCall.id = blockObj["id"].toString();
                block.toolCall.name = blockObj["name"].toString();
                inToolInput_ = true;
                currentToolInputStr_.clear();
                emit toolCallStarted(block.toolCall.name, block.toolCall.id);
            } else if (blockType == "thinking") {
                block.text = blockObj["thinking"].toString();
                block.signature = blockObj["signature"].toString();
            } else if (blockType == "redacted_thinking") {
                block.data = blockObj["data"].toString();
            }

            currentContentBlocks_.append(block);
        }
        else if (type == "content_block_delta") {
            auto delta = obj["delta"].toObject();
            QString deltaType = delta["type"].toString();

            if (deltaType == "text_delta") {
                QString text = delta["text"].toString();
                currentAssistantText_ += text;
                if (currentBlockIndex_ >= 0 && currentBlockIndex_ < currentContentBlocks_.size())
                    currentContentBlocks_[currentBlockIndex_].text += text;
                emit tokenReceived(text);
            }
            else if (deltaType == "input_json_delta") {
                currentToolInputStr_ += delta["partial_json"].toString();
            }
            else if (deltaType == "thinking_delta") {
                if (currentBlockIndex_ >= 0 && currentBlockIndex_ < currentContentBlocks_.size())
                    currentContentBlocks_[currentBlockIndex_].text += delta["thinking"].toString();
            }
            else if (deltaType == "signature_delta") {
                if (currentBlockIndex_ >= 0 && currentBlockIndex_ < currentContentBlocks_.size())
                    currentContentBlocks_[currentBlockIndex_].signature += delta["signature"].toString();
            }
        }
        else if (type == "message_delta") {
            auto delta = obj["delta"].toObject();
            if (delta.contains("stop_reason"))
                currentStopReason_ = delta["stop_reason"].toString();
            auto details = delta["stop_details"].toObject();
            if (!details.isEmpty())
                currentRefusalCategory_ = details["category"].toString();
        }
        else if (type == "content_block_stop") {
            if (inToolInput_ && currentBlockIndex_ >= 0
                && currentBlockIndex_ < currentContentBlocks_.size()) {
                auto& block = currentContentBlocks_[currentBlockIndex_];
                if (block.type == "tool_use") {
                    auto inputDoc = QJsonDocument::fromJson(currentToolInputStr_.toUtf8());
                    block.toolCall.input = inputDoc.object();
                }
                inToolInput_ = false;
                currentToolInputStr_.clear();
            }
            currentBlockIndex_ = -1;
        }
        else if (type == "message_stop") {
            // handled in handleStreamFinished
        }
        else if (type == "error") {
            auto errObj = obj["error"].toObject();
            emit errorOccurred(errObj["message"].toString());
            // Drop the reply so the partial response isn't processed (and its
            // tool calls run) when the stream closes.
            abortCurrentReply();
            setBusy(false);
            return;
        }
    }
}

void AiService::handleStreamFinished()
{
    if (!currentReply_ || sender() != currentReply_) {
        if (auto* stale = qobject_cast<QNetworkReply*>(sender()))
            stale->deleteLater();
        return;
    }

    // OperationCanceledError here means the transfer timeout fired (deliberate
    // aborts disconnect first), so the partial stream must not be treated as complete.
    if (currentReply_->error() != QNetworkReply::NoError) {
        QByteArray responseBody = currentReply_->readAll();
        QString errorMsg = currentReply_->errorString();

        auto doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject()) {
            auto errObj = doc.object()["error"].toObject();
            if (!errObj.isEmpty())
                errorMsg = errObj["message"].toString();
        }

        emit errorOccurred(errorMsg);
        setBusy(false);
        currentReply_->deleteLater();
        currentReply_ = nullptr;
        return;
    }

    currentReply_->deleteLater();
    currentReply_ = nullptr;

    processAssistantResponse();
}

// ── Process response & agentic loop ─────────────────────────────────────────

void AiService::processAssistantResponse()
{
    AiMessage assistantMsg;
    assistantMsg.role = AiRole::Assistant;
    assistantMsg.timestamp = QDateTime::currentDateTime();

    {
        QStringList types;
        for (auto& block : currentContentBlocks_)
            types << block.type;
        qDebug() << "[AI] response stop_reason=" << currentStopReason_
                 << "round=" << toolRoundCount_ << "blocks=" << types.join(',');
    }

    // A server-side refusal fallback marks its switch point with a "fallback" block.
    // Thinking and tool_use blocks from the declined attempt before the last marker
    // must not be echoed back; text blocks are kept.
    int lastFallback = -1;
    for (int i = 0; i < currentContentBlocks_.size(); ++i) {
        if (currentContentBlocks_[i].type == "fallback")
            lastFallback = i;
    }
    for (int i = 0; i < currentContentBlocks_.size(); ++i) {
        const auto& block = currentContentBlocks_[i];
        if (block.type == "fallback")
            continue;
        if (i < lastFallback && block.type != "text")
            continue;
        assistantMsg.content.append(block);
    }

    if (currentStopReason_ == "refusal") {
        // Keep any partial text, but never run tools from a declined response.
        QVector<AiContentBlock> textBlocks;
        for (auto& block : assistantMsg.content) {
            if (block.type == "text" && !block.text.isEmpty())
                textBlocks.append(block);
        }
        assistantMsg.content = textBlocks;
        if (!textBlocks.isEmpty()) {
            conversationHistory_.append(assistantMsg);
            emit responseComplete(assistantMsg);
        }
        QString msg = "Claude declined this request";
        if (!currentRefusalCategory_.isEmpty())
            msg += QString(" (%1)").arg(currentRefusalCategory_);
        emit errorOccurred(msg + ". Try rephrasing it.");
        setBusy(false);
        return;
    }

    conversationHistory_.append(assistantMsg);

    if (assistantMsg.hasToolUse() && toolRoundCount_ < MAX_TOOL_ROUNDS) {
        toolRoundCount_++;
        QVector<AiToolCall> calls;
        for (auto& block : assistantMsg.content) {
            if (block.type == "tool_use")
                calls.append(block.toolCall);
        }
        executeToolCalls(calls);
    } else {
        emit responseComplete(assistantMsg);
        setBusy(false);
    }
}

void AiService::executeToolCalls(const QVector<AiToolCall>& calls)
{
    executedToolIds_.clear();
    QVector<AiToolResult> results;

    toolExecutor_->beginBatch();

    for (auto& call : calls) {
        if (confirmDestructive() && toolExecutor_->isDestructive(call.name)) {
            toolExecutor_->endBatch();
            emit confirmDestructiveAction(call.name, call);
            return;
        }

        qDebug() << "[AI] tool start" << call.name
                 << QJsonDocument(call.input).toJson(QJsonDocument::Compact).left(300);
        auto result = toolExecutor_->execute(call);
        qDebug() << "[AI] tool done" << call.name << (result.isError ? "ERROR" : "ok")
                 << result.content.left(200);
        results.append(result);
        executedToolIds_.insert(call.id);

        emit toolCallFinished(call.name, call.id, result.content, result.isError);
    }

    toolExecutor_->endBatch();

    AiMessage assistantMsg = conversationHistory_.last();
    continueAfterToolResults(results, assistantMsg);
}

void AiService::executeConfirmedTool(const AiToolCall& call)
{
    auto result = toolExecutor_->execute(call);
    emit toolCallFinished(call.name, call.id, result.content, result.isError);
    executedToolIds_.insert(call.id);

    AiMessage assistantMsg = conversationHistory_.last();
    QVector<AiToolResult> allResults;

    for (auto& block : assistantMsg.content) {
        if (block.type == "tool_use") {
            if (block.toolCall.id == call.id) {
                allResults.append(result);
            } else if (executedToolIds_.contains(block.toolCall.id)) {
                continue;
            } else if (confirmDestructive() && toolExecutor_->isDestructive(block.toolCall.name)) {
                emit confirmDestructiveAction(block.toolCall.name, block.toolCall);
                return;
            } else {
                auto existingResult = toolExecutor_->execute(block.toolCall);
                allResults.append(existingResult);
                executedToolIds_.insert(block.toolCall.id);
                emit toolCallFinished(block.toolCall.name, block.toolCall.id,
                                     existingResult.content, existingResult.isError);
            }
        }
    }

    continueAfterToolResults(allResults, assistantMsg);
}

void AiService::cancelPendingTool(const AiToolCall& call)
{
    AiToolResult cancelResult;
    cancelResult.toolUseId = call.id;
    cancelResult.content = "User cancelled this action.";
    cancelResult.isError = true;

    AiMessage assistantMsg = conversationHistory_.last();
    QVector<AiToolResult> results;
    for (auto& block : assistantMsg.content) {
        if (block.type == "tool_use") {
            if (block.toolCall.id == call.id)
                results.append(cancelResult);
            else {
                AiToolResult skipResult;
                skipResult.toolUseId = block.toolCall.id;
                skipResult.content = "Skipped because another tool was cancelled.";
                skipResult.isError = true;
                results.append(skipResult);
            }
        }
    }
    continueAfterToolResults(results, assistantMsg);
}

void AiService::continueAfterToolResults(const QVector<AiToolResult>& results,
                                          const AiMessage& /*assistantMsg*/)
{
    AiMessage toolResultMsg;
    toolResultMsg.role = AiRole::User;
    toolResultMsg.timestamp = QDateTime::currentDateTime();

    for (auto& result : results) {
        AiContentBlock block;
        block.type = "tool_result";
        block.toolResult = result;
        toolResultMsg.content.append(block);
    }

    conversationHistory_.append(toolResultMsg);

    sendToApi();
}

} // namespace OpenDaw
