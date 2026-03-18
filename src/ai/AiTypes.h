#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QDateTime>

namespace freedaw {

enum class AiRole { User, Assistant, System };

struct AiToolCall {
    QString id;
    QString name;
    QJsonObject input;
};

struct AiToolResult {
    QString toolUseId;
    QString content;
    bool isError = false;
};

struct AiContentBlock {
    QString type;          // "text" or "tool_use" or "tool_result"
    QString text;          // for "text"
    AiToolCall toolCall;   // for "tool_use"
    AiToolResult toolResult; // for "tool_result"
};

struct AiMessage {
    AiRole role = AiRole::User;
    QVector<AiContentBlock> content;
    QDateTime timestamp;

    static AiMessage userMessage(const QString& text) {
        AiMessage msg;
        msg.role = AiRole::User;
        msg.timestamp = QDateTime::currentDateTime();
        AiContentBlock block;
        block.type = "text";
        block.text = text;
        msg.content.append(block);
        return msg;
    }

    static AiMessage assistantText(const QString& text) {
        AiMessage msg;
        msg.role = AiRole::Assistant;
        msg.timestamp = QDateTime::currentDateTime();
        AiContentBlock block;
        block.type = "text";
        block.text = text;
        msg.content.append(block);
        return msg;
    }

    QString plainText() const {
        QString result;
        for (auto& block : content) {
            if (block.type == "text")
                result += block.text;
        }
        return result;
    }

    bool hasToolUse() const {
        for (auto& block : content) {
            if (block.type == "tool_use") return true;
        }
        return false;
    }
};

} // namespace freedaw
