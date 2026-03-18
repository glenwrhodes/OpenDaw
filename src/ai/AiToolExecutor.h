#pragma once

#include "AiTypes.h"
#include "engine/AudioEngine.h"
#include "engine/EditManager.h"
#include "engine/PluginScanner.h"
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <functional>

namespace freedaw {

class AiToolExecutor : public QObject {
    Q_OBJECT
public:
    explicit AiToolExecutor(EditManager* editMgr, AudioEngine* audioEngine,
                            PluginScanner* pluginScanner, QObject* parent = nullptr);

    AiToolResult execute(const AiToolCall& call);
    bool isDestructive(const QString& toolName) const;

private:
    te::AudioTrack* resolveTrack(const QJsonValue& trackRef) const;
    te::Plugin* resolveEffect(te::AudioTrack* track, const QJsonValue& effectRef) const;
    te::AutomatableParameter* resolveParameter(te::Plugin* plugin, const QJsonValue& paramRef) const;

    QVector<te::Plugin*> getUserPlugins(te::AudioTrack* track) const;

    AiToolResult ok(const QString& toolUseId, const QJsonObject& data) const;
    AiToolResult ok(const QString& toolUseId, const QJsonArray& data) const;
    AiToolResult ok(const QString& toolUseId, const QString& message) const;
    AiToolResult err(const QString& toolUseId, const QString& message) const;

    using Handler = std::function<AiToolResult(const QJsonObject&, const QString&)>;
    QMap<QString, Handler> handlers_;
    void registerHandlers();

    EditManager* editMgr_;
    AudioEngine* audioEngine_;
    PluginScanner* pluginScanner_;
};

} // namespace freedaw
