#include "AiToolExecutor.h"
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>

namespace freedaw {

AiToolExecutor::AiToolExecutor(EditManager* editMgr, AudioEngine* audioEngine,
                               PluginScanner* pluginScanner, QObject* parent)
    : QObject(parent), editMgr_(editMgr), audioEngine_(audioEngine),
      pluginScanner_(pluginScanner)
{
    registerHandlers();
}

// ── Resolution helpers ──────────────────────────────────────────────────────

te::AudioTrack* AiToolExecutor::resolveTrack(const QJsonValue& trackRef) const
{
    if (!editMgr_ || !editMgr_->edit()) return nullptr;

    if (trackRef.isDouble()) {
        int idx = trackRef.toInt();
        return editMgr_->getAudioTrack(idx);
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
        editMgr_->setBpm(bpm);
        emit editMgr_->editChanged();
        return ok(id, QString("Tempo set to %1 BPM.").arg(bpm, 0, 'f', 1));
    };

    handlers_["set_time_signature"] = [this](const QJsonObject& input, const QString& id) -> AiToolResult {
        int num = input["numerator"].toInt();
        int den = input["denominator"].toInt();
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
        "clear_track_output", "clear_track_input"
    };
    return destructive.contains(toolName);
}

} // namespace freedaw
