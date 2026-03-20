#include "AiToolExecutor.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <algorithm>

namespace freedaw {

AiToolExecutor::AiToolExecutor(EditManager* editMgr, AudioEngine* audioEngine,
                               PluginScanner* pluginScanner, QObject* parent)
    : QObject(parent), editMgr_(editMgr), audioEngine_(audioEngine),
      pluginScanner_(pluginScanner), audioAnalysis_(editMgr)
{
    registerHandlers();
}

// ── Resolution helpers ──────────────────────────────────────────────────────

te::AudioTrack* AiToolExecutor::resolveTrack(const QJsonValue& trackRef) const
{
    if (!editMgr_ || !editMgr_->edit()) return nullptr;

    if (trackRef.isDouble()) {
        int idx = trackRef.toInt();
        auto* track = editMgr_->getAudioTrack(idx);
        if (!track)
            qWarning() << "[resolveTrack] no track at index" << idx;
        return track;
    }

    if (trackRef.isObject()) {
        auto obj = trackRef.toObject();
        int idx = obj["index"].toInt(-1);
        QString name = obj["name"].toString();
        auto* track = (idx >= 0) ? editMgr_->getAudioTrack(idx) : nullptr;
        if (track && !name.isEmpty()) {
            auto trackName = QString::fromStdString(track->getName().toStdString());
            if (trackName.compare(name, Qt::CaseInsensitive) != 0)
                qWarning() << "[resolveTrack] index/name mismatch: idx=" << idx
                           << "expected=" << name << "actual=" << trackName;
        }
        if (track) return track;
        if (!name.isEmpty()) {
            for (auto* t : editMgr_->getAudioTracks())
                if (QString::fromStdString(t->getName().toStdString())
                        .compare(name, Qt::CaseInsensitive) == 0)
                    return t;
        }
        return nullptr;
    }

    QString name = trackRef.toString();
    for (auto* t : editMgr_->getAudioTracks()) {
        if (QString::fromStdString(t->getName().toStdString())
                .compare(name, Qt::CaseInsensitive) == 0)
            return t;
    }
    return nullptr;
}

QVector<te::Plugin*> AiToolExecutor::getUserPlugins(te::AudioTrack* track) const
{
    QVector<te::Plugin*> result;
    if (!track) return result;
    for (auto* p : track->pluginList.getPlugins()) {
        if (dynamic_cast<te::VolumeAndPanPlugin*>(p)) continue;
        if (dynamic_cast<te::LevelMeterPlugin*>(p)) continue;
        result.append(p);
    }
    return result;
}

te::Plugin* AiToolExecutor::resolveEffect(te::AudioTrack* track, const QJsonValue& effectRef) const
{
    auto plugins = getUserPlugins(track);
    if (plugins.isEmpty()) return nullptr;

    if (effectRef.isDouble()) {
        int idx = effectRef.toInt();
        if (idx >= 0 && idx < plugins.size())
            return plugins[idx];
        return nullptr;
    }

    QString name = effectRef.toString();
    for (auto* p : plugins) {
        if (QString::fromStdString(p->getName().toStdString())
                .compare(name, Qt::CaseInsensitive) == 0)
            return p;
    }
    return nullptr;
}

te::AutomatableParameter* AiToolExecutor::resolveParameter(te::Plugin* plugin, const QJsonValue& paramRef) const
{
    if (!plugin) return nullptr;
    auto params = plugin->getAutomatableParameters();

    if (paramRef.isDouble()) {
        int idx = paramRef.toInt();
        if (idx >= 0 && idx < params.size())
            return params[idx];
        return nullptr;
    }

    QString name = paramRef.toString();
    for (auto* p : params) {
        if (QString::fromStdString(p->getParameterName().toStdString())
                .compare(name, Qt::CaseInsensitive) == 0)
            return p;
    }
    for (auto* p : params) {
        if (QString::fromStdString(p->getParameterName().toStdString())
                .contains(name, Qt::CaseInsensitive))
            return p;
    }
    return nullptr;
}

// ── Result helpers ──────────────────────────────────────────────────────────

AiToolResult AiToolExecutor::ok(const QString& toolUseId, const QJsonObject& data) const
{
    AiToolResult r;
    r.toolUseId = toolUseId;
    r.content = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
    r.isError = false;
    return r;
}

AiToolResult AiToolExecutor::ok(const QString& toolUseId, const QJsonArray& data) const
{
    AiToolResult r;
    r.toolUseId = toolUseId;
    r.content = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
    r.isError = false;
    return r;
}

AiToolResult AiToolExecutor::ok(const QString& toolUseId, const QString& message) const
{
    AiToolResult r;
    r.toolUseId = toolUseId;
    r.content = message;
    r.isError = false;
    return r;
}

AiToolResult AiToolExecutor::err(const QString& toolUseId, const QString& message) const
{
    AiToolResult r;
    r.toolUseId = toolUseId;
    r.content = message;
    r.isError = true;
    return r;
}

// ── Track info helper ───────────────────────────────────────────────────────

static QJsonObject trackToJson(EditManager* mgr, te::AudioTrack* t, int index)
{
    QJsonObject obj;
    obj["index"] = index;
    obj["name"] = QString::fromStdString(t->getName().toStdString());

    if (mgr->isBusTrack(t))
        obj["type"] = "bus";
    else if (mgr->isMidiTrack(t))
        obj["type"] = "midi";
    else
        obj["type"] = "audio";

    obj["muted"] = t->isMuted(false);
    obj["solo"] = t->isSolo(false);
    obj["armed"] = mgr->isTrackRecordEnabled(t);
    obj["mono"] = mgr->isTrackMono(t);

    for (auto* p : t->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(p)) {
            float volPos = vp->volParam->getCurrentValue();
            float dB = te::volumeFaderPositionToDB(volPos);
            obj["volume_db"] = static_cast<double>(dB);
            obj["pan"] = static_cast<double>(vp->pan.get());
            break;
        }
    }

    obj["input"] = mgr->getTrackInputName(t);
    obj["output"] = mgr->getTrackOutputName(t);

    return obj;
}

static QJsonObject trackToDetailedJson(EditManager* mgr, te::AudioTrack* t, int index)
{
    QJsonObject obj = trackToJson(mgr, t, index);

    QJsonArray effectsArr;
    for (auto* p : t->pluginList.getPlugins()) {
        if (dynamic_cast<te::VolumeAndPanPlugin*>(p)) continue;
        if (dynamic_cast<te::LevelMeterPlugin*>(p)) continue;

        QJsonObject fxObj;
        fxObj["name"] = QString::fromStdString(p->getName().toStdString());
        fxObj["enabled"] = p->isEnabled();

        QJsonArray paramsArr;
        for (auto* param : p->getAutomatableParameters()) {
            QJsonObject pObj;
            pObj["name"] = QString::fromStdString(param->getParameterName().toStdString());
            pObj["value"] = static_cast<double>(param->getCurrentValue());
            paramsArr.append(pObj);
        }
        fxObj["parameters"] = paramsArr;
        effectsArr.append(fxObj);
    }
    obj["effects"] = effectsArr;
    return obj;
}

// ── Effect name → xmlTypeName ───────────────────────────────────────────────

static const char* resolveEffectXmlType(const QString& name)
{
    QString lower = name.toLower().trimmed();
    if (lower == "reverb")                         return te::ReverbPlugin::xmlTypeName;
    if (lower == "eq" || lower == "equaliser"
        || lower == "equalizer")                   return te::EqualiserPlugin::xmlTypeName;
    if (lower == "compressor")                     return te::CompressorPlugin::xmlTypeName;
    if (lower == "delay")                          return te::DelayPlugin::xmlTypeName;
    if (lower == "chorus")                         return te::ChorusPlugin::xmlTypeName;
    if (lower == "phaser")                         return te::PhaserPlugin::xmlTypeName;
    if (lower == "low pass filter" || lower == "lowpass"
        || lower == "low pass")                    return te::LowPassPlugin::xmlTypeName;
    if (lower == "pitch shift" || lower == "pitchshift") return te::PitchShiftPlugin::xmlTypeName;
    return nullptr;
}

static te::VolumeAndPanPlugin* findVolumePanPlugin(te::AudioTrack* track)
{
    if (!track) return nullptr;
    for (auto* p : track->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(p))
            return vp;
    }
    return nullptr;
}

static te::Plugin* ensureBuiltInEffect(EditManager* editMgr, te::AudioTrack* track, const QString& effectName)
{
    if (!editMgr || !editMgr->edit() || !track) return nullptr;
    const char* xmlType = resolveEffectXmlType(effectName);
    if (!xmlType) return nullptr;

    juce::String resolvedType(xmlType);
    for (auto* p : track->pluginList.getPlugins()) {
        if (p->getPluginType() == resolvedType)
            return p;
        if (dynamic_cast<te::ReverbPlugin*>(p) && resolvedType == te::ReverbPlugin::xmlTypeName) return p;
        if (dynamic_cast<te::EqualiserPlugin*>(p) && resolvedType == te::EqualiserPlugin::xmlTypeName) return p;
        if (dynamic_cast<te::CompressorPlugin*>(p) && resolvedType == te::CompressorPlugin::xmlTypeName) return p;
        if (dynamic_cast<te::DelayPlugin*>(p) && resolvedType == te::DelayPlugin::xmlTypeName) return p;
        if (dynamic_cast<te::ChorusPlugin*>(p) && resolvedType == te::ChorusPlugin::xmlTypeName) return p;
        if (dynamic_cast<te::PhaserPlugin*>(p) && resolvedType == te::PhaserPlugin::xmlTypeName) return p;
        if (dynamic_cast<te::LowPassPlugin*>(p) && resolvedType == te::LowPassPlugin::xmlTypeName) return p;
        if (dynamic_cast<te::PitchShiftPlugin*>(p) && resolvedType == te::PitchShiftPlugin::xmlTypeName) return p;
    }

    auto plugin = editMgr->edit()->getPluginCache().createNewPlugin(juce::String(xmlType), {});
    if (!plugin) return nullptr;
    auto* raw = plugin.get();
    track->pluginList.insertPlugin(plugin, -1, nullptr);
    return raw;
}

static QString inferTrackRole(const QString& trackName)
{
    return AiAudioAnalysis::inferRoleFromName(trackName);
}

// ── Handler registration ────────────────────────────────────────────────────

void AiToolExecutor::registerHandlers()
{
    handlers_["get_project_info"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        if (!editMgr_ || !editMgr_->edit())
            return err(id, "No project loaded.");

        QJsonObject info;
        auto tracks = editMgr_->getAudioTracks();
        info["track_count"] = tracks.size();
        info["tempo_bpm"] = editMgr_->getBpm();
        info["time_sig_numerator"] = editMgr_->getTimeSigNumerator();
        info["time_sig_denominator"] = editMgr_->getTimeSigDenominator();

        auto& transport = editMgr_->transport();
        info["playing"] = transport.isPlaying();
        info["recording"] = transport.isRecording();
        info["position_seconds"] = transport.getPosition().inSeconds();
        info["looping"] = transport.looping.get();

        QJsonArray trackList;
        for (int i = 0; i < tracks.size(); ++i)
            trackList.append(trackToJson(editMgr_, tracks[i], i));
        info["tracks"] = trackList;

        return ok(id, info);
    };

    handlers_["get_track_list"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        if (!editMgr_) return err(id, "No project loaded.");
        auto tracks = editMgr_->getAudioTracks();
        QJsonArray arr;
        for (int i = 0; i < tracks.size(); ++i)
            arr.append(trackToJson(editMgr_, tracks[i], i));
        return ok(id, arr);
    };

    handlers_["get_track_info"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        auto tracks = editMgr_->getAudioTracks();
        int idx = tracks.indexOf(track);
        return ok(id, trackToDetailedJson(editMgr_, track, idx));
    };

    handlers_["create_audio_track"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = editMgr_->addAudioTrack();
        if (!track) return err(id, "Failed to create audio track.");
        if (input.contains("name"))
            track->setName(juce::String(input["name"].toString().toStdString()));
        emit editMgr_->tracksChanged();
        return ok(id, QString("Created audio track '%1'.")
                      .arg(QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["create_midi_track"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = editMgr_->addMidiTrack();
        if (!track) return err(id, "Failed to create MIDI track.");
        if (input.contains("name"))
            track->setName(juce::String(input["name"].toString().toStdString()));
        emit editMgr_->tracksChanged();
        return ok(id, QString("Created MIDI track '%1'.")
                      .arg(QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["create_bus_track"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = editMgr_->addBusTrack();
        if (!track) return err(id, "Failed to create bus track.");
        if (input.contains("name"))
            track->setName(juce::String(input["name"].toString().toStdString()));
        emit editMgr_->tracksChanged();
        return ok(id, QString("Created bus track '%1'.")
                      .arg(QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["delete_track"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        QString name = QString::fromStdString(track->getName().toStdString());
        editMgr_->removeTrack(track);
        emit editMgr_->tracksChanged();
        return ok(id, QString("Deleted track '%1'.").arg(name));
    };

    handlers_["rename_track"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        QString oldName = QString::fromStdString(track->getName().toStdString());
        QString newName = input["new_name"].toString();
        track->setName(juce::String(newName.toStdString()));
        emit editMgr_->editChanged();
        return ok(id, QString("Renamed '%1' to '%2'.").arg(oldName, newName));
    };

    handlers_["set_track_mute"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        bool muted = input["muted"].toBool();
        track->setMute(muted);
        emit editMgr_->editChanged();
        return ok(id, QString("Track '%1' %2.")
                      .arg(QString::fromStdString(track->getName().toStdString()),
                           muted ? "muted" : "unmuted"));
    };

    handlers_["set_track_solo"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        bool solo = input["solo"].toBool();
        track->setSolo(solo);
        emit editMgr_->editChanged();
        return ok(id, QString("Track '%1' solo %2.")
                      .arg(QString::fromStdString(track->getName().toStdString()),
                           solo ? "on" : "off"));
    };

    handlers_["set_track_volume"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        double db = input["volume_db"].toDouble();
        for (auto* p : track->pluginList.getPlugins()) {
            if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(p)) {
                float pos = te::decibelsToVolumeFaderPosition(static_cast<float>(db));
                vp->volParam->setParameter(pos, juce::sendNotification);
                emit editMgr_->editChanged();
                return ok(id, QString("Track '%1' volume set to %2 dB.")
                              .arg(QString::fromStdString(track->getName().toStdString()))
                              .arg(db, 0, 'f', 1));
            }
        }
        return err(id, "Volume plugin not found on track.");
    };

    handlers_["set_track_pan"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        double pan = input["pan"].toDouble();
        for (auto* p : track->pluginList.getPlugins()) {
            if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(p)) {
                vp->pan.setValue(static_cast<float>(pan), nullptr);
                emit editMgr_->editChanged();
                return ok(id, QString("Track '%1' pan set to %2.")
                              .arg(QString::fromStdString(track->getName().toStdString()))
                              .arg(pan, 0, 'f', 2));
            }
        }
        return err(id, "Volume/pan plugin not found on track.");
    };

    handlers_["set_track_mono"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        bool mono = input["mono"].toBool();
        editMgr_->setTrackMono(*track, mono);
        emit editMgr_->editChanged();
        return ok(id, QString("Track '%1' set to %2.")
                      .arg(QString::fromStdString(track->getName().toStdString()),
                           mono ? "mono" : "stereo"));
    };

    handlers_["set_track_record_enabled"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        bool enabled = input["enabled"].toBool();
        editMgr_->setTrackRecordEnabled(*track, enabled);
        emit editMgr_->editChanged();
        return ok(id, QString("Track '%1' record %2.")
                      .arg(QString::fromStdString(track->getName().toStdString()),
                           enabled ? "armed" : "disarmed"));
    };

    // ── Routing ─────────────────────────────────────────────────────────────

    handlers_["get_available_inputs"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        auto sources = editMgr_->getAvailableInputSources();
        QJsonArray arr;
        for (auto& src : sources) {
            QJsonObject obj;
            obj["device_name"] = QString::fromStdString(src.deviceName.toStdString());
            obj["display_name"] = src.displayName;
            arr.append(obj);
        }
        return ok(id, arr);
    };

    handlers_["assign_input_to_track"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        QString inputName = input["input_name"].toString();
        editMgr_->assignInputToTrack(*track, juce::String(inputName.toStdString()));
        emit editMgr_->routingChanged();
        return ok(id, QString("Assigned input '%1' to track '%2'.")
                      .arg(inputName, QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["clear_track_input"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        editMgr_->clearTrackInput(*track);
        emit editMgr_->routingChanged();
        return ok(id, QString("Cleared input on track '%1'.")
                      .arg(QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["set_track_output"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        QJsonValue dest = input["destination"];
        if (dest.isString() && dest.toString().compare("master", Qt::CaseInsensitive) == 0) {
            editMgr_->setTrackOutputToMaster(*track);
        } else {
            auto* destTrack = resolveTrack(dest);
            if (!destTrack) return err(id, "Destination track not found.");
            editMgr_->setTrackOutputToTrack(*track, *destTrack);
        }
        emit editMgr_->routingChanged();
        return ok(id, QString("Set output of '%1' to '%2'.")
                      .arg(QString::fromStdString(track->getName().toStdString()),
                           dest.isString() ? dest.toString() : QString::number(dest.toInt())));
    };

    handlers_["clear_track_output"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        editMgr_->clearTrackOutput(*track);
        emit editMgr_->routingChanged();
        return ok(id, QString("Disconnected output on track '%1'.")
                      .arg(QString::fromStdString(track->getName().toStdString())));
    };

    // ── Effects ─────────────────────────────────────────────────────────────

    handlers_["list_available_effects"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        QJsonArray arr;
        QStringList builtIn = {"Reverb", "EQ", "Compressor", "Delay", "Chorus",
                               "Phaser", "Low Pass Filter", "Pitch Shift"};
        for (auto& name : builtIn) {
            QJsonObject obj;
            obj["name"] = name;
            obj["type"] = "built-in";
            arr.append(obj);
        }
        if (pluginScanner_) {
            auto& list = pluginScanner_->getPluginList();
            for (auto& desc : list.getTypes()) {
                if (desc.isInstrument) continue;
                QJsonObject obj;
                obj["name"] = QString::fromStdString(desc.name.toStdString());
                obj["manufacturer"] = QString::fromStdString(desc.manufacturerName.toStdString());
                obj["type"] = "vst";
                arr.append(obj);
            }
        }
        return ok(id, arr);
    };

    handlers_["get_track_effects"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");

        auto plugins = getUserPlugins(track);
        QJsonArray arr;
        for (int i = 0; i < plugins.size(); ++i) {
            auto* p = plugins[i];
            QJsonObject fxObj;
            fxObj["index"] = i;
            fxObj["name"] = QString::fromStdString(p->getName().toStdString());
            fxObj["enabled"] = p->isEnabled();

            QJsonArray paramsArr;
            for (auto* param : p->getAutomatableParameters()) {
                QJsonObject pObj;
                pObj["name"] = QString::fromStdString(param->getParameterName().toStdString());
                pObj["value"] = static_cast<double>(param->getCurrentValue());
                paramsArr.append(pObj);
            }
            fxObj["parameters"] = paramsArr;
            arr.append(fxObj);
        }
        return ok(id, arr);
    };

    handlers_["add_effect_to_track"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        if (!editMgr_->edit()) return err(id, "No project loaded.");

        QString effectName = input["effect_name"].toString();
        const char* xmlType = resolveEffectXmlType(effectName);
        if (!xmlType) return err(id, QString("Unknown effect '%1'. Use list_available_effects to see options.").arg(effectName));

        auto& cache = editMgr_->edit()->getPluginCache();
        auto plugin = cache.createNewPlugin(juce::String(xmlType), {});
        if (!plugin) return err(id, "Failed to create effect plugin.");

        track->pluginList.insertPlugin(plugin, -1, nullptr);
        emit editMgr_->editChanged();
        return ok(id, QString("Added '%1' to track '%2'.")
                      .arg(effectName, QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["remove_effect_from_track"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");

        auto* plugin = resolveEffect(track, input["effect"]);
        if (!plugin) return err(id, "Effect not found.");

        QString name = QString::fromStdString(plugin->getName().toStdString());
        plugin->deleteFromParent();
        emit editMgr_->editChanged();
        return ok(id, QString("Removed '%1' from track '%2'.")
                      .arg(name, QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["set_effect_parameter"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        auto* plugin = resolveEffect(track, input["effect"]);
        if (!plugin) return err(id, "Effect not found.");
        auto* param = resolveParameter(plugin, input["parameter"]);
        if (!param) return err(id, "Parameter not found.");

        double value = input["value"].toDouble();
        param->setParameter(static_cast<float>(value), juce::sendNotification);
        emit editMgr_->editChanged();
        return ok(id, QString("Set '%1' on '%2' to %3.")
                      .arg(QString::fromStdString(param->getParameterName().toStdString()),
                           QString::fromStdString(plugin->getName().toStdString()))
                      .arg(value, 0, 'f', 3));
    };

    handlers_["set_effect_bypass"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        auto* plugin = resolveEffect(track, input["effect"]);
        if (!plugin) return err(id, "Effect not found.");

        bool bypassed = input["bypassed"].toBool();
        plugin->setEnabled(!bypassed);
        emit editMgr_->editChanged();
        return ok(id, QString("'%1' %2.")
                      .arg(QString::fromStdString(plugin->getName().toStdString()),
                           bypassed ? "bypassed" : "enabled"));
    };

    // ── Analysis / Mix Intelligence ────────────────────────────────────────

    handlers_["analyze_track_levels"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        const double windowSeconds = input.contains("window_seconds")
            ? input["window_seconds"].toDouble()
            : 2.0;
        return ok(id, audioAnalysis_.analyzeTrackLevels(track, windowSeconds));
    };

    handlers_["analyze_master_levels"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        const double windowSeconds = input.contains("window_seconds")
            ? input["window_seconds"].toDouble()
            : 4.0;
        return ok(id, audioAnalysis_.analyzeMasterLevels(windowSeconds));
    };

    handlers_["analyze_frequency_balance"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        const auto target = input["target"];
        if (target.isString() && target.toString().compare("master", Qt::CaseInsensitive) == 0)
            return ok(id, audioAnalysis_.analyzeMasterFrequencyBalance());
        auto* track = resolveTrack(target);
        if (!track) return err(id, "Track not found.");
        return ok(id, audioAnalysis_.analyzeFrequencyBalance(track));
    };

    handlers_["analyze_stereo_image"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        const auto target = input["target"];
        if (target.isString() && target.toString().compare("master", Qt::CaseInsensitive) == 0)
            return ok(id, audioAnalysis_.analyzeMasterStereoImage());
        auto* track = resolveTrack(target);
        if (!track) return err(id, "Track not found.");
        return ok(id, audioAnalysis_.analyzeStereoImage(track));
    };

    handlers_["analyze_transients"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        return ok(id, audioAnalysis_.analyzeTransients(track));
    };

    handlers_["analyze_masking"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* a = resolveTrack(input["track_a"]);
        auto* b = resolveTrack(input["track_b"]);
        if (!a || !b) return err(id, "Both tracks are required.");
        return ok(id, audioAnalysis_.analyzeMasking(a, b));
    };

    // ── Semantic Mix / Master Actions ───────────────────────────────────────

    handlers_["apply_mix_preset"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        const QString preset = input["preset_name"].toString().toLower().trimmed();

        if (preset == "vocal_clarity") {
            ensureBuiltInEffect(editMgr_, track, "EQ");
            auto* comp = ensureBuiltInEffect(editMgr_, track, "Compressor");
            if (comp) {
                auto params = comp->getAutomatableParameters();
                if (params.size() > 0) params[0]->setParameter(0.40f, juce::sendNotification);
                if (params.size() > 1) params[1]->setParameter(0.55f, juce::sendNotification);
            }
            if (auto* vp = findVolumePanPlugin(track))
                vp->volParam->setParameter(te::decibelsToVolumeFaderPosition(-8.0f), juce::sendNotification);
        } else if (preset == "drum_punch") {
            auto* comp = ensureBuiltInEffect(editMgr_, track, "Compressor");
            if (comp) {
                auto params = comp->getAutomatableParameters();
                if (params.size() > 0) params[0]->setParameter(0.58f, juce::sendNotification);
                if (params.size() > 1) params[1]->setParameter(0.62f, juce::sendNotification);
            }
        } else if (preset == "bass_control") {
            auto* comp = ensureBuiltInEffect(editMgr_, track, "Compressor");
            if (comp) {
                auto params = comp->getAutomatableParameters();
                if (params.size() > 0) params[0]->setParameter(0.52f, juce::sendNotification);
            }
            if (auto* vp = findVolumePanPlugin(track))
                vp->pan.setValue(0.0f, nullptr);
        } else {
            return err(id, QString("Unknown preset '%1'.").arg(preset));
        }

        emit editMgr_->editChanged();
        return ok(id, QString("Applied mix preset '%1' to '%2'.")
                      .arg(preset, QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["set_track_target_peak"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        const double targetDb = input["dbfs"].toDouble(-10.0);
        auto analysis = audioAnalysis_.analyzeTrackLevels(track, 1.5);
        const double currentPeak = analysis["peak_dbfs"].toDouble(-120.0);
        if (currentPeak <= -100.0)
            return err(id, "Track appears silent right now; play audio first for peak targeting.");

        const double delta = targetDb - currentPeak;
        auto* vp = findVolumePanPlugin(track);
        if (!vp) return err(id, "Volume plugin not found.");

        const double currentVolDb = te::volumeFaderPositionToDB(vp->volParam->getCurrentValue());
        const double nextDb = std::clamp(currentVolDb + delta, -60.0, 6.0);
        vp->volParam->setParameter(te::decibelsToVolumeFaderPosition(static_cast<float>(nextDb)),
                                   juce::sendNotification);
        emit editMgr_->editChanged();
        return ok(id, QString("Adjusted '%1' toward target peak %2 dBFS (current %3 dBFS, new volume %4 dB).")
                      .arg(QString::fromStdString(track->getName().toStdString()))
                      .arg(targetDb, 0, 'f', 1).arg(currentPeak, 0, 'f', 1).arg(nextDb, 0, 'f', 1));
    };

    handlers_["set_track_dynamic_goal"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        const QString goal = input["goal"].toString().toLower().trimmed();
        auto* comp = ensureBuiltInEffect(editMgr_, track, "Compressor");
        if (!comp) return err(id, "Could not create/find compressor.");
        auto params = comp->getAutomatableParameters();

        if (goal == "tighter") {
            if (params.size() > 0) params[0]->setParameter(0.60f, juce::sendNotification);
            if (params.size() > 1) params[1]->setParameter(0.65f, juce::sendNotification);
        } else if (goal == "more_open") {
            if (params.size() > 0) params[0]->setParameter(0.35f, juce::sendNotification);
            if (params.size() > 1) params[1]->setParameter(0.40f, juce::sendNotification);
        } else if (goal == "more_sustain") {
            if (params.size() > 0) params[0]->setParameter(0.50f, juce::sendNotification);
            if (params.size() > 2) params[2]->setParameter(0.70f, juce::sendNotification);
        } else {
            return err(id, QString("Unknown dynamic goal '%1'.").arg(goal));
        }

        emit editMgr_->editChanged();
        return ok(id, QString("Applied dynamic goal '%1' to '%2'.")
                      .arg(goal, QString::fromStdString(track->getName().toStdString())));
    };

    handlers_["set_reverb_character"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        const QString style = input["style"].toString().toLower().trimmed();
        const double amount = std::clamp(input.contains("amount") ? input["amount"].toDouble() : 0.5, 0.0, 1.0);
        auto* reverb = ensureBuiltInEffect(editMgr_, track, "Reverb");
        if (!reverb) return err(id, "Could not create/find reverb.");
        auto params = reverb->getAutomatableParameters();

        if (style == "ethereal") {
            if (params.size() > 0) params[0]->setParameter(static_cast<float>(0.75 + amount * 0.2), juce::sendNotification);
            if (params.size() > 2) params[2]->setParameter(static_cast<float>(0.55 + amount * 0.35), juce::sendNotification);
        } else if (style == "roomy") {
            if (params.size() > 0) params[0]->setParameter(static_cast<float>(0.55 + amount * 0.25), juce::sendNotification);
            if (params.size() > 2) params[2]->setParameter(static_cast<float>(0.40 + amount * 0.25), juce::sendNotification);
        } else if (style == "tight") {
            if (params.size() > 0) params[0]->setParameter(static_cast<float>(0.20 + amount * 0.20), juce::sendNotification);
            if (params.size() > 2) params[2]->setParameter(static_cast<float>(0.15 + amount * 0.20), juce::sendNotification);
        } else {
            return err(id, QString("Unknown reverb style '%1'.").arg(style));
        }

        emit editMgr_->editChanged();
        return ok(id, QString("Set reverb on '%1' to style '%2' (amount %3).")
                      .arg(QString::fromStdString(track->getName().toStdString()), style)
                      .arg(amount, 0, 'f', 2));
    };

    handlers_["set_bus_glue"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* bus = resolveTrack(input["bus"]);
        if (!bus) return err(id, "Bus track not found.");
        if (!editMgr_->isBusTrack(bus))
            return err(id, "Target track is not a bus.");
        const double intensity = std::clamp(input.contains("intensity") ? input["intensity"].toDouble() : 0.5, 0.0, 1.0);
        auto* comp = ensureBuiltInEffect(editMgr_, bus, "Compressor");
        if (!comp) return err(id, "Could not create/find compressor.");
        auto params = comp->getAutomatableParameters();
        if (params.size() > 0) params[0]->setParameter(static_cast<float>(0.40 + intensity * 0.30), juce::sendNotification);
        if (params.size() > 1) params[1]->setParameter(static_cast<float>(0.35 + intensity * 0.25), juce::sendNotification);
        emit editMgr_->editChanged();
        return ok(id, QString("Applied bus glue on '%1' with intensity %2.")
                      .arg(QString::fromStdString(bus->getName().toStdString()))
                      .arg(intensity, 0, 'f', 2));
    };

    handlers_["set_master_target"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        if (!editMgr_ || !editMgr_->edit()) return err(id, "No project loaded.");
        const QString profile = input["profile"].toString().toLower().trimmed();
        if (profile == "streaming_balanced") {
            editMgr_->edit()->setMasterVolumeSliderPos(0.72f);
        } else if (profile == "loud_pop") {
            editMgr_->edit()->setMasterVolumeSliderPos(0.82f);
        } else if (profile == "dynamic_film") {
            editMgr_->edit()->setMasterVolumeSliderPos(0.65f);
        } else {
            return err(id, QString("Unknown master profile '%1'.").arg(profile));
        }
        emit editMgr_->editChanged();
        return ok(id, QString("Set master target profile to '%1'.").arg(profile));
    };

    // ── Session Structure Helpers ────────────────────────────────────────────

    handlers_["group_tracks_by_inferred_role"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        QJsonObject out;
        QJsonArray drums, bass, vocals, music, fx;
        auto tracks = editMgr_->getAudioTracks();
        for (auto* t : tracks) {
            const QString name = QString::fromStdString(t->getName().toStdString());
            const QString role = inferTrackRole(name);
            if (role == "drums") drums.append(name);
            else if (role == "bass") bass.append(name);
            else if (role == "vocals") vocals.append(name);
            else if (role == "fx") fx.append(name);
            else music.append(name);
        }
        out["drums"] = drums;
        out["bass"] = bass;
        out["vocals"] = vocals;
        out["music"] = music;
        out["fx"] = fx;
        return ok(id, out);
    };

    handlers_["create_mix_buses_from_roles"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        QStringList targetBuses = {"Drums Bus", "Music Bus", "Vocals Bus", "FX Bus"};
        QJsonArray created;
        auto buses = editMgr_->getBusTracks();

        auto busExists = [&buses](const QString& busName) {
            for (auto* b : buses) {
                if (QString::fromStdString(b->getName().toStdString()).compare(busName, Qt::CaseInsensitive) == 0)
                    return true;
            }
            return false;
        };

        for (const auto& name : targetBuses) {
            if (!busExists(name)) {
                auto* bus = editMgr_->addBusTrack();
                if (bus) {
                    bus->setName(juce::String(name.toStdString()));
                    created.append(name);
                }
            }
        }
        emit editMgr_->tracksChanged();
        return ok(id, created);
    };

    handlers_["auto_route_by_name_patterns"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        auto tracks = editMgr_->getNonBusAudioTracks();
        auto buses = editMgr_->getBusTracks();
        QMap<QString, te::AudioTrack*> busByRole;
        for (auto* b : buses) {
            const QString n = QString::fromStdString(b->getName().toStdString()).toLower();
            if (n.contains("drum")) busByRole["drums"] = b;
            else if (n.contains("vocal")) busByRole["vocals"] = b;
            else if (n.contains("fx")) busByRole["fx"] = b;
            else if (n.contains("music")) busByRole["music"] = b;
        }

        QJsonArray routed;
        for (auto* t : tracks) {
            const QString role = inferTrackRole(QString::fromStdString(t->getName().toStdString()));
            if (busByRole.contains(role)) {
                editMgr_->setTrackOutputToTrack(*t, *busByRole[role]);
                QJsonObject row;
                row["track"] = QString::fromStdString(t->getName().toStdString());
                row["bus"] = QString::fromStdString(busByRole[role]->getName().toStdString());
                routed.append(row);
            }
        }
        emit editMgr_->routingChanged();
        return ok(id, routed);
    };

    // ── Mix Plan / Checkpoint Helpers ────────────────────────────────────────

    handlers_["preview_mix_plan"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        const QString req = input["request"].toString().trimmed();
        QJsonObject out;
        out["request"] = req;
        out["stages"] = QJsonArray{
            "1) Gain staging and peak targets",
            "2) Balance and pan pass",
            "3) Corrective EQ and masking checks",
            "4) Compression/dynamics shaping",
            "5) Space effects (reverb/delay)",
            "6) Bus glue + master target",
            "7) Verify against analysis metrics"
        };
        out["note"] = "Preview only - no project changes applied.";
        return ok(id, out);
    };

    handlers_["commit_last_mix_stage"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        return ok(id, "Last mix stage marked as committed.");
    };

    handlers_["revert_last_mix_stage"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        editMgr_->undo();
        return ok(id, "Reverted last mix stage via undo.");
    };

    // ── Transport ───────────────────────────────────────────────────────────

    handlers_["play"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        editMgr_->transport().play(false);
        return ok(id, "Playback started.");
    };

    handlers_["stop"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        editMgr_->transport().stop(false, false);
        return ok(id, "Playback stopped.");
    };

    handlers_["record"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        bool anyArmed = false;
        for (auto* track : editMgr_->getAudioTracks()) {
            if (editMgr_->isTrackRecordEnabled(track)) {
                anyArmed = true;
                break;
            }
        }
        if (!anyArmed)
            return err(id, "No tracks are armed for recording. Arm a track first.");
        editMgr_->transport().record(false);
        return ok(id, "Recording started.");
    };

    handlers_["set_position"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        double secs = input["seconds"].toDouble();
        editMgr_->transport().setPosition(tracktion::TimePosition::fromSeconds(secs));
        return ok(id, QString("Position set to %1 seconds.").arg(secs, 0, 'f', 2));
    };

    handlers_["get_transport_state"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        auto& transport = editMgr_->transport();
        QJsonObject state;
        state["playing"] = transport.isPlaying();
        state["recording"] = transport.isRecording();
        state["position_seconds"] = transport.getPosition().inSeconds();
        state["looping"] = transport.looping.get();
        return ok(id, state);
    };

    handlers_["set_tempo"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        double bpm = input["bpm"].toDouble();
        if (bpm < 20.0 || bpm > 999.0)
            return err(id, "BPM must be between 20 and 999.");
        editMgr_->setBpm(bpm);
        emit editMgr_->editChanged();
        return ok(id, QString("Tempo set to %1 BPM.").arg(bpm, 0, 'f', 1));
    };

    handlers_["set_time_signature"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        int num = input["numerator"].toInt();
        int den = input["denominator"].toInt();
        if (num < 1 || num > 32)
            return err(id, "Numerator must be between 1 and 32.");
        if (den < 1 || den > 32 || (den & (den - 1)) != 0)
            return err(id, "Denominator must be a power of 2 (1, 2, 4, 8, 16, 32).");
        editMgr_->setTimeSignature(num, den);
        emit editMgr_->editChanged();
        return ok(id, QString("Time signature set to %1/%2.").arg(num).arg(den));
    };

    // ── Project ─────────────────────────────────────────────────────────────

    handlers_["save_project"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        if (editMgr_->saveEdit())
            return ok(id, "Project saved.");
        return err(id, "Failed to save. The project may not have a file path yet.");
    };

    handlers_["undo"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        editMgr_->undo();
        return ok(id, "Undo performed.");
    };

    handlers_["redo"] = [this](const QJsonObject&, const QString& id) -> AiToolResult {
        editMgr_->redo();
        return ok(id, "Redo performed.");
    };

    handlers_["setup_midi_channels"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        if (!editMgr_->isMidiTrack(track))
            return err(id, "Track is not a MIDI track.");

        te::MidiClip* refClip = nullptr;
        for (auto* clip : track->getClips()) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                refClip = mc;
                break;
            }
        }
        if (!refClip) {
            refClip = editMgr_->addMidiClipToTrack(*track, 0.0, 16.0);
            if (!refClip) return err(id, "Failed to create reference MIDI clip.");
        }

        QJsonArray channels = input["channels"].toArray();
        QJsonArray created;
        for (const auto& chVal : channels) {
            QJsonObject chObj = chVal.toObject();
            int ch = chObj["channel"].toInt(1);
            QString name = chObj["name"].toString();

            if (ch == refClip->getMidiChannel().getChannelNumber()) {
                if (!name.isEmpty())
                    editMgr_->setChannelName(*refClip, name);
                QJsonObject entry;
                entry["channel"] = ch;
                entry["name"] = name;
                entry["status"] = "existing";
                created.append(entry);
                continue;
            }

            auto* newClip = editMgr_->addLinkedMidiChannel(*track, *refClip, ch, name);
            QJsonObject entry;
            entry["channel"] = ch;
            entry["name"] = name;
            entry["status"] = newClip ? "created" : "failed";
            created.append(entry);
        }

        emit editMgr_->editChanged();
        return ok(id, created);
    };

    handlers_["set_channel_name"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");
        int ch = input["channel"].toInt(0);
        QString name = input["name"].toString();
        if (ch < 1 || ch > 16) return err(id, "Channel must be 1-16.");
        if (name.isEmpty()) return err(id, "Name cannot be empty.");

        for (auto* clip : track->getClips()) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                if (mc->getMidiChannel().getChannelNumber() == ch) {
                    editMgr_->setChannelName(*mc, name);
                    return ok(id, QString("Channel %1 renamed to '%2'.").arg(ch).arg(name));
                }
            }
        }
        return err(id, QString("No MIDI clip on channel %1 found.").arg(ch));
    };

    handlers_["get_channel_names"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        auto* track = resolveTrack(input["track"]);
        if (!track) return err(id, "Track not found.");

        QJsonArray result;
        for (auto* clip : track->getClips()) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                QJsonObject entry;
                entry["channel"] = mc->getMidiChannel().getChannelNumber();
                entry["name"] = editMgr_->getChannelName(mc);
                result.append(entry);
            }
        }
        return ok(id, result);
    };
}

// ── Public API ──────────────────────────────────────────────────────────────

AiToolResult AiToolExecutor::execute(const AiToolCall& call)
{
    auto it = handlers_.find(call.name);
    if (it == handlers_.end())
        return err(call.id, QString("Unknown tool '%1'.").arg(call.name));
    return it.value()(call.input, call.id);
}

bool AiToolExecutor::isDestructive(const QString& toolName) const
{
    static const QSet<QString> destructive = {
        "delete_track", "remove_effect_from_track",
        "clear_track_output", "clear_track_input",
        "set_track_output",
        "undo", "redo", "revert_last_mix_stage"
    };
    return destructive.contains(toolName);
}

} // namespace freedaw
