#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QDateTime>

namespace OpenDaw {

enum class AiRole { User, Assistant, System };

struct AiToolCall {
    QString id;
    QString name;
    QJsonObject input;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["input"] = input;
        return obj;
    }

    static AiToolCall fromJson(const QJsonObject& obj) {
        AiToolCall tc;
        tc.id = obj["id"].toString();
        tc.name = obj["name"].toString();
        tc.input = obj["input"].toObject();
        return tc;
    }
};

struct AiToolResult {
    QString toolUseId;
    QString content;
    bool isError = false;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["tool_use_id"] = toolUseId;
        obj["content"] = content;
        obj["is_error"] = isError;
        return obj;
    }

    static AiToolResult fromJson(const QJsonObject& obj) {
        AiToolResult tr;
        tr.toolUseId = obj["tool_use_id"].toString();
        tr.content = obj["content"].toString();
        tr.isError = obj["is_error"].toBool(false);
        return tr;
    }
};

struct AiContentBlock {
    QString type;          // "text", "tool_use", "tool_result", "thinking" or "redacted_thinking"
    QString text;          // for "text" and "thinking"
    QString signature;     // for "thinking"
    QString data;          // for "redacted_thinking"
    AiToolCall toolCall;   // for "tool_use"
    AiToolResult toolResult; // for "tool_result"

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["type"] = type;
        if (type == "text") {
            obj["text"] = text;
        } else if (type == "thinking") {
            obj["thinking"] = text;
            obj["signature"] = signature;
        } else if (type == "redacted_thinking") {
            obj["data"] = data;
        } else if (type == "tool_use") {
            obj["tool_call"] = toolCall.toJson();
        } else if (type == "tool_result") {
            obj["tool_result"] = toolResult.toJson();
        }
        return obj;
    }

    static AiContentBlock fromJson(const QJsonObject& obj) {
        AiContentBlock block;
        block.type = obj["type"].toString();
        if (block.type == "text") {
            block.text = obj["text"].toString();
        } else if (block.type == "thinking") {
            block.text = obj["thinking"].toString();
            block.signature = obj["signature"].toString();
        } else if (block.type == "redacted_thinking") {
            block.data = obj["data"].toString();
        } else if (block.type == "tool_use") {
            block.toolCall = AiToolCall::fromJson(obj["tool_call"].toObject());
        } else if (block.type == "tool_result") {
            block.toolResult = AiToolResult::fromJson(obj["tool_result"].toObject());
        }
        return block;
    }
};

inline QString aiRoleToString(AiRole role) {
    switch (role) {
    case AiRole::User:      return "user";
    case AiRole::Assistant: return "assistant";
    case AiRole::System:    return "system";
    }
    return "user";
}

inline AiRole aiRoleFromString(const QString& s) {
    if (s == "assistant") return AiRole::Assistant;
    if (s == "system")    return AiRole::System;
    return AiRole::User;
}

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

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["role"] = aiRoleToString(role);
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        QJsonArray contentArr;
        for (auto& block : content)
            contentArr.append(block.toJson());
        obj["content"] = contentArr;
        return obj;
    }

    static AiMessage fromJson(const QJsonObject& obj) {
        AiMessage msg;
        msg.role = aiRoleFromString(obj["role"].toString());
        msg.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        for (auto val : obj["content"].toArray())
            msg.content.append(AiContentBlock::fromJson(val.toObject()));
        return msg;
    }
};

} // namespace OpenDaw
