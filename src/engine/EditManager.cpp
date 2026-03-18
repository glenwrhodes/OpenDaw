#include "EditManager.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>
#include <QDebug>
#include <QPointF>

namespace freedaw {

namespace {
constexpr const char* kMonoUtilityPluginType = "airwindows_monoam";
constexpr float kMonoUtilityModeValue = 0.1875f; // Mo Noam: (int)(0.1875*7.999)=1 => kMONO
}

static bool hasInstrumentPlugin(te::AudioTrack* track)
{
    if (!track) return false;
    for (auto* p : track->pluginList.getPlugins()) {
        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p))
            if (ext->isSynth())
                return true;
    }
    return false;
}

EditManager::EditManager(AudioEngine& engine, QObject* parent)
    : QObject(parent), audioEngine_(engine)
{
    createDefaultEdit();
}

EditManager::~EditManager() = default;

void EditManager::teardownCurrentEdit()
{
    if (!edit_) return;

    qDebug() << "[teardown] emitting aboutToChangeEdit";
    emit aboutToChangeEdit();

    qDebug() << "[teardown] stopping transport";
    auto& transport = edit_->getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);

    qDebug() << "[teardown] freeing playback context";
    transport.freePlaybackContext();

    midiTrackIds_.clear();
    inputDisplayNames_.clear();
    frozenTracks_.clear();
    qDebug() << "[teardown] complete";
}

void EditManager::createDefaultEdit()
{
    teardownCurrentEdit();
    auto& eng = audioEngine_.engine();
    edit_ = te::createEmptyEdit(eng, juce::File());
    edit_->ensureNumberOfAudioTracks(4);
    ensureLevelMetersOnAllTracks();
    enableAllWaveInputDevices();
    edit_->getTransport().ensureContextAllocated();
    currentFile_ = juce::File();
    emit editChanged();
    emit tracksChanged();
}

void EditManager::newEdit()
{
    createDefaultEdit();
}

bool EditManager::loadEdit(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    qDebug() << "[loadEdit] teardown start";
    teardownCurrentEdit();
    qDebug() << "[loadEdit] teardown done, loading file...";

    auto& eng = audioEngine_.engine();
    auto newEdit = te::loadEditFromFile(eng, file);
    if (!newEdit)
        return false;

    qDebug() << "[loadEdit] file loaded, assigning edit";
    edit_ = std::move(newEdit);

    qDebug() << "[loadEdit] ensuring level meters";
    ensureLevelMetersOnAllTracks();

    qDebug() << "[loadEdit] detecting MIDI tracks";
    for (auto* track : te::getAudioTracks(*edit_)) {
        if (hasInstrumentPlugin(track))
            midiTrackIds_.insert(track->itemID.getRawID());
    }

    currentFile_ = file;

    qDebug() << "[loadEdit] loading input display names";
    loadInputDisplayNames();

    qDebug() << "[loadEdit] enabling wave input devices";
    enableAllWaveInputDevices();

    qDebug() << "[loadEdit] allocating playback context";
    edit_->getTransport().ensureContextAllocated();
    qDebug() << "[loadEdit] playback context ready";

    qDebug() << "[loadEdit] restoring disconnected outputs";
    for (auto* track : te::getAudioTracks(*edit_)) {
        if (track->state.getProperty(juce::Identifier("outputDisconnected"), false))
            track->getOutput().setOutputToTrack(nullptr);
    }

    qDebug() << "[loadEdit] emitting editChanged";
    emit editChanged();
    qDebug() << "[loadEdit] emitting tracksChanged";
    emit tracksChanged();
    qDebug() << "[loadEdit] done";
    return true;
}

bool EditManager::saveEdit()
{
    if (!edit_ || currentFile_ == juce::File())
        return false;
    te::EditFileOperations(*edit_).save(true, true, false);
    return true;
}

bool EditManager::saveEditAs(const juce::File& file)
{
    if (!edit_)
        return false;
    edit_->editFileRetriever = [file]() { return file; };
    te::EditFileOperations(*edit_).save(true, true, false);
    currentFile_ = file;
    return true;
}

te::TransportControl& EditManager::transport()
{
    jassert(edit_ != nullptr);
    return edit_->getTransport();
}

te::AudioTrack* EditManager::addAudioTrack()
{
    if (!edit_)
        return nullptr;
    edit_->ensureNumberOfAudioTracks(trackCount() + 1);
    auto tracks = getAudioTracks();
    auto* newTrack = tracks.getLast();
    if (newTrack) {
        bool hasLevelMeter = false;
        for (auto* p : newTrack->pluginList.getPlugins())
            if (dynamic_cast<te::LevelMeterPlugin*>(p))
                hasLevelMeter = true;
        if (!hasLevelMeter) {
            if (auto p = edit_->getPluginCache().createNewPlugin(
                    juce::String(te::LevelMeterPlugin::xmlTypeName), {}))
                newTrack->pluginList.insertPlugin(p, -1, nullptr);
        }
    }
    emit tracksChanged();
    return newTrack;
}

void EditManager::removeTrack(te::Track* track)
{
    if (!edit_ || !track)
        return;
    edit_->deleteTrack(track);
    emit tracksChanged();
    emit routingChanged();
    emit editChanged();
}

int EditManager::trackCount() const
{
    return edit_ ? getAudioTracks().size() : 0;
}

te::AudioTrack* EditManager::getAudioTrack(int index) const
{
    auto tracks = getAudioTracks();
    if (index >= 0 && index < tracks.size())
        return tracks[index];
    return nullptr;
}

juce::Array<te::AudioTrack*> EditManager::getAudioTracks() const
{
    juce::Array<te::AudioTrack*> result;
    if (!edit_)
        return result;
    for (auto* track : te::getAudioTracks(*edit_))
        result.add(track);
    return result;
}

void EditManager::addAudioClipToTrack(te::AudioTrack& track,
                                       const juce::File& audioFile,
                                       double startBeat)
{
    double fileDurationSecs = 5.0;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager.createReaderFor(audioFile))) {
        fileDurationSecs = double(reader->lengthInSamples) / reader->sampleRate;
    }

    auto& ts = edit_->tempoSequence;
    auto startTime = ts.toTime(tracktion::BeatPosition::fromBeats(startBeat));
    auto endTime   = tracktion::TimePosition::fromSeconds(
                         startTime.inSeconds() + fileDurationSecs);

    auto clipRef = te::insertWaveClip(track,
                                       audioFile.getFileNameWithoutExtension(),
                                       audioFile,
                                       {{startTime, endTime}},
                                       te::DeleteExistingClips::no);
    if (clipRef != nullptr) {
        clipRef->setOffset(tracktion::TimeDuration::fromSeconds(0.0));
        clipRef->setSpeedRatio(1.0);

        // If the file looks like a loop (tempo/beat metadata), enable beat-based looping.
        const auto& loopInfo = clipRef->getLoopInfo();
        if (loopInfo.isLoopable() && !loopInfo.isOneShot() && loopInfo.getNumBeats() > 0.0) {
            clipRef->setAutoTempo(true);
            clipRef->setLoopRangeBeats({
                tracktion::BeatPosition::fromBeats(0.0),
                tracktion::BeatDuration::fromBeats(loopInfo.getNumBeats())
            });
        } else {
            // Fallback: treat full source length as the repeat segment.
            clipRef->setLoopRange({
                tracktion::TimePosition::fromSeconds(0.0),
                clipRef->getSourceLength()
            });
        }
    }

    emit editChanged();
}

void EditManager::undo()
{
    if (!edit_) return;
    edit_->getUndoManager().undo();
    emit editChanged();
    emit tracksChanged();
}

void EditManager::redo()
{
    if (!edit_) return;
    edit_->getUndoManager().redo();
    emit editChanged();
    emit tracksChanged();
}

double EditManager::getBpm() const
{
    if (!edit_)
        return 120.0;
    return edit_->tempoSequence.getTempo(0)->getBpm();
}

void EditManager::setBpm(double bpm)
{
    if (!edit_)
        return;
    edit_->tempoSequence.getTempo(0)->setBpm(bpm);
    emit editChanged();
}

int EditManager::getTimeSigNumerator() const
{
    if (!edit_)
        return 4;
    return edit_->tempoSequence.getTimeSig(0)->numerator;
}

int EditManager::getTimeSigDenominator() const
{
    if (!edit_)
        return 4;
    return edit_->tempoSequence.getTimeSig(0)->denominator;
}

void EditManager::setTimeSignature(int num, int den)
{
    if (!edit_)
        return;
    auto* ts = edit_->tempoSequence.getTimeSig(0);
    ts->numerator   = num;
    ts->denominator = den;
    emit editChanged();
}

te::AudioTrack* EditManager::addMidiTrack()
{
    auto* track = addAudioTrack();
    if (track)
        markAsMidiTrack(track);
    return track;
}

te::MidiClip* EditManager::addMidiClipToTrack(te::AudioTrack& track,
                                               double startBeat,
                                               double lengthBeats)
{
    if (!edit_) return nullptr;

    auto& ts = edit_->tempoSequence;
    auto startTime = ts.toTime(tracktion::BeatPosition::fromBeats(startBeat));
    auto endTime   = ts.toTime(tracktion::BeatPosition::fromBeats(startBeat + lengthBeats));

    auto clipRef = track.insertMIDIClip(
        "MIDI Clip",
        tracktion::TimeRange(startTime, endTime),
        nullptr);

    emit editChanged();
    return clipRef.get();
}

te::MidiClip* EditManager::importMidiFileToTrack(te::AudioTrack& track,
                                                  const juce::File& midiFile,
                                                  double startBeat)
{
    if (!edit_) return nullptr;

    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk()) return nullptr;

    juce::MidiFile midi;
    if (!midi.readFrom(stream)) return nullptr;

    midi.convertTimestampTicksToSeconds();

    double totalDuration = 0.0;
    for (int t = 0; t < midi.getNumTracks(); ++t) {
        auto* midiTrack = midi.getTrack(t);
        if (!midiTrack) continue;
        if (midiTrack->getNumEvents() > 0) {
            double lastTime = midiTrack->getEventTime(midiTrack->getNumEvents() - 1);
            totalDuration = std::max(totalDuration, lastTime);
        }
    }

    if (totalDuration <= 0.0)
        totalDuration = 4.0;

    double bpm = getBpm();
    double lengthBeats = (totalDuration / 60.0) * bpm;
    lengthBeats = std::max(lengthBeats, 1.0);

    auto* clip = addMidiClipToTrack(track, startBeat, lengthBeats);
    if (!clip) return nullptr;

    auto& seq = clip->getSequence();
    seq.removeAllNotes(nullptr);

    for (int t = 0; t < midi.getNumTracks(); ++t) {
        auto* midiTrack = midi.getTrack(t);
        if (!midiTrack) continue;

        for (int ei = 0; ei < midiTrack->getNumEvents(); ++ei) {
            auto msg = midiTrack->getEventPointer(ei)->message;
            if (!msg.isNoteOn()) continue;

            int noteNum = msg.getNoteNumber();
            int velocity = msg.getVelocity();
            double noteOnSecs = msg.getTimeStamp();

            double noteOffSecs = noteOnSecs + 0.25;
            int noteOffIdx = midiTrack->getIndexOfMatchingKeyUp(ei);
            if (noteOffIdx >= 0)
                noteOffSecs = midiTrack->getEventTime(noteOffIdx);

            double noteStartBeat = (noteOnSecs / 60.0) * bpm;
            double noteLengthBeats = ((noteOffSecs - noteOnSecs) / 60.0) * bpm;
            if (noteLengthBeats <= 0.0) noteLengthBeats = 0.25;

            seq.addNote(noteNum,
                        tracktion::BeatPosition::fromBeats(noteStartBeat),
                        tracktion::BeatDuration::fromBeats(noteLengthBeats),
                        velocity, 0, nullptr);
        }
    }

    if (!isMidiTrack(&track))
        markAsMidiTrack(&track);

    emit editChanged();
    return clip;
}

bool EditManager::isMidiTrack(te::AudioTrack* track) const
{
    if (!track) return false;
    if (midiTrackIds_.count(track->itemID.getRawID()) > 0)
        return true;
    return hasInstrumentPlugin(track);
}

bool EditManager::isTrackMono(te::AudioTrack* track) const
{
    if (!track)
        return false;

    for (auto* plugin : track->pluginList.getPlugins()) {
        if (plugin && plugin->getPluginType() == kMonoUtilityPluginType)
            return true;
    }

    return false;
}

void EditManager::setTrackMono(te::AudioTrack& track, bool mono)
{
    if (!edit_)
        return;

    te::Plugin* monoPlugin = nullptr;
    for (auto* plugin : track.pluginList.getPlugins()) {
        if (plugin && plugin->getPluginType() == kMonoUtilityPluginType) {
            monoPlugin = plugin;
            break;
        }
    }

    bool changed = false;

    if (mono) {
        if (!monoPlugin) {
            if (auto p = edit_->getPluginCache().createNewPlugin(
                    juce::String(kMonoUtilityPluginType), {})) {
                int insertIndex = track.pluginList.size();
                for (int i = 0; i < track.pluginList.size(); ++i) {
                    if (dynamic_cast<te::LevelMeterPlugin*>(track.pluginList[i])) {
                        insertIndex = i;
                        break;
                    }
                }
                track.pluginList.insertPlugin(p, insertIndex, nullptr);
                monoPlugin = p.get();
                changed = true;
            }
        }

        if (monoPlugin) {
            if (auto param = monoPlugin->getAutomatableParameter(0)) {
                param->setParameter(kMonoUtilityModeValue, juce::sendNotificationSync);
                changed = true;
            }
        }
    } else if (monoPlugin) {
        monoPlugin->deleteFromParent();
        changed = true;
    }

    if (changed) {
        emit editChanged();
        emit tracksChanged();
    }
}

te::Plugin* EditManager::getTrackInstrument(te::AudioTrack* track) const
{
    if (!track) return nullptr;
    for (auto* p : track->pluginList.getPlugins()) {
        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p))
            if (ext->isSynth())
                return ext;
    }
    return nullptr;
}

void EditManager::setTrackInstrument(te::AudioTrack& track,
                                      const juce::PluginDescription& desc)
{
    removeTrackInstrument(track);

    auto pluginState = te::ExternalPlugin::create(audioEngine_.engine(), desc);
    if (auto plugin = edit_->getPluginCache().createNewPlugin(pluginState)) {
        track.pluginList.insertPlugin(plugin, 0, nullptr);
        markAsMidiTrack(&track);
        emit editChanged();
        emit tracksChanged();
    }
}

void EditManager::removeTrackInstrument(te::AudioTrack& track)
{
    for (int i = track.pluginList.size() - 1; i >= 0; --i) {
        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(track.pluginList[i])) {
            if (ext->isSynth())
                ext->deleteFromParent();
        }
    }
}

void EditManager::markAsMidiTrack(te::AudioTrack* track)
{
    if (track) {
        midiTrackIds_.insert(track->itemID.getRawID());
        emit tracksChanged();
    }
}

void EditManager::trimNotesToClipBounds(te::MidiClip& clip)
{
    if (!edit_) return;

    auto& ts = edit_->tempoSequence;
    const double clipStartBeat = ts.toBeats(clip.getPosition().getStart()).inBeats();
    const double clipEndBeat   = ts.toBeats(clip.getPosition().getEnd()).inBeats();
    const double clipLenBeats  = clipEndBeat - clipStartBeat;

    auto& seq = clip.getSequence();
    auto* um = &edit_->getUndoManager();

    for (auto* note : seq.getNotes()) {
        const double ns = note->getStartBeat().inBeats();
        const double ne = ns + note->getLengthBeats().inBeats();
        if (ns < clipLenBeats && ne > clipLenBeats) {
            note->setStartAndLength(
                note->getStartBeat(),
                tracktion::BeatDuration::fromBeats(clipLenBeats - ns), um);
        }
    }

    std::vector<te::MidiNote*> toRemove;
    for (auto* note : seq.getNotes()) {
        if (note->getStartBeat().inBeats() >= clipLenBeats)
            toRemove.push_back(note);
    }
    for (auto* note : toRemove)
        seq.removeNote(*note, um);
}

bool EditManager::isClipValid(te::Clip* clip) const
{
    if (!edit_ || !clip) return false;
    for (auto* track : te::getAudioTracks(*edit_)) {
        for (auto* c : track->getClips())
            if (c == clip) return true;
    }
    return false;
}

void EditManager::enableAllWaveInputDevices()
{
    auto& dm = audioEngine_.engine().getDeviceManager();
    qDebug() << "[enableAllWaveInputDevices] found"
             << dm.getNumWaveInDevices() << "wave input devices";
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i) {
        if (auto* dev = dm.getWaveInDevice(i)) {
            qDebug() << "[enableAllWaveInputDevices] enabling:"
                     << QString::fromStdString(dev->getName().toStdString());
            dev->setEnabled(true);
        }
    }
}

QList<InputSource> EditManager::getAvailableInputSources() const
{
    QList<InputSource> sources;
    auto& dm = audioEngine_.engine().getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i) {
        if (auto* dev = dm.getWaveInDevice(i)) {
            InputSource src;
            src.deviceName = dev->getName();
            src.displayName = getInputDisplayName(dev->getName());
            sources.append(src);
        }
    }
    return sources;
}

void EditManager::clearTrackInputInternal(te::AudioTrack& track)
{
    if (!edit_) return;
    edit_->getEditInputDevices().clearAllInputs(track, &edit_->getUndoManager());
}

void EditManager::assignInputToTrack(te::AudioTrack& track, const juce::String& deviceName)
{
    if (!edit_) return;

    clearTrackInputInternal(track);
    edit_->getTransport().ensureContextAllocated();

    for (auto* instance : edit_->getAllInputDevices()) {
        if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice
            && instance->getInputDevice().getName() == deviceName) {
            auto result = instance->setTarget(track.itemID, true,
                                              &edit_->getUndoManager(), 0);
            (void)result;
            break;
        }
    }

    emit editChanged();
    emit routingChanged();
}

void EditManager::clearTrackInput(te::AudioTrack& track)
{
    clearTrackInputInternal(track);
    emit editChanged();
    emit routingChanged();
}

QString EditManager::getTrackInputName(te::AudioTrack* track) const
{
    if (!edit_ || !track) return {};

    auto devices = edit_->getEditInputDevices().getDevicesForTargetTrack(*track);
    for (auto* instance : devices) {
        if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
            return QString::fromStdString(
                instance->getInputDevice().getName().toStdString());
    }
    return {};
}

void EditManager::setTrackRecordEnabled(te::AudioTrack& track, bool enabled)
{
    if (!edit_) return;

    edit_->getTransport().ensureContextAllocated();

    auto devices = edit_->getEditInputDevices().getDevicesForTargetTrack(track);
    for (auto* instance : devices) {
        if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice) {
            instance->setRecordingEnabled(track.itemID, enabled);
            break;
        }
    }

    emit editChanged();
}

bool EditManager::isTrackRecordEnabled(te::AudioTrack* track) const
{
    if (!edit_ || !track) return false;

    auto devices = edit_->getEditInputDevices().getDevicesForTargetTrack(*track);
    for (auto* instance : devices) {
        if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
            return instance->isRecordingEnabled(track->itemID);
    }
    return false;
}

// ── Output routing ───────────────────────────────────────────────────────────

void EditManager::setTrackOutputToMaster(te::AudioTrack& track)
{
    if (!edit_) return;
    track.state.setProperty(juce::Identifier("outputDisconnected"), false,
                            &edit_->getUndoManager());
    track.getOutput().setOutputToDefaultDevice(false);
    emit routingChanged();
    emit editChanged();
}

void EditManager::setTrackOutputToTrack(te::AudioTrack& src, te::AudioTrack& dest)
{
    if (!edit_) return;
    src.state.setProperty(juce::Identifier("outputDisconnected"), false,
                          &edit_->getUndoManager());
    src.getOutput().setOutputToTrack(&dest);
    emit routingChanged();
    emit editChanged();
}

void EditManager::clearTrackOutput(te::AudioTrack& track)
{
    if (!edit_) return;
    track.state.setProperty(juce::Identifier("outputDisconnected"), true,
                            &edit_->getUndoManager());
    track.getOutput().setOutputToTrack(nullptr);
    emit routingChanged();
    emit editChanged();
}

te::AudioTrack* EditManager::getTrackOutputDestination(te::AudioTrack* track) const
{
    if (!track) return nullptr;
    return track->getOutput().getDestinationTrack();
}

bool EditManager::isTrackOutputDisconnected(te::AudioTrack* track) const
{
    if (!track) return true;
    return track->state.getProperty(juce::Identifier("outputDisconnected"), false);
}

QString EditManager::getTrackOutputName(te::AudioTrack* track) const
{
    if (!track) return {};
    return QString::fromStdString(
        track->getOutput().getDescriptiveOutputName().toStdString());
}

// ── Bus tracks ───────────────────────────────────────────────────────────────

te::AudioTrack* EditManager::addBusTrack()
{
    auto* track = addAudioTrack();
    if (track) {
        track->state.setProperty(juce::Identifier("isBus"), true,
                                 &edit_->getUndoManager());
        track->setName(juce::String("Bus " + juce::String(getBusTracks().size())));
        emit tracksChanged();
        emit routingChanged();
    }
    return track;
}

bool EditManager::isBusTrack(te::AudioTrack* track) const
{
    if (!track) return false;
    return track->state.getProperty(juce::Identifier("isBus"), false);
}

juce::Array<te::AudioTrack*> EditManager::getBusTracks() const
{
    juce::Array<te::AudioTrack*> result;
    for (auto* track : getAudioTracks())
        if (isBusTrack(track))
            result.add(track);
    return result;
}

juce::Array<te::AudioTrack*> EditManager::getNonBusAudioTracks() const
{
    juce::Array<te::AudioTrack*> result;
    for (auto* track : getAudioTracks())
        if (!isBusTrack(track))
            result.add(track);
    return result;
}

// ── Input device renaming ────────────────────────────────────────────────────

void EditManager::setInputDisplayName(const juce::String& deviceName,
                                       const QString& customName)
{
    inputDisplayNames_[deviceName.toStdString()] = customName;
    saveInputDisplayNames();
    emit routingChanged();
}

QString EditManager::getInputDisplayName(const juce::String& deviceName) const
{
    auto it = inputDisplayNames_.find(deviceName.toStdString());
    if (it != inputDisplayNames_.end() && !it->second.isEmpty())
        return it->second;
    return QString::fromStdString(deviceName.toStdString());
}

void EditManager::saveInputDisplayNames()
{
    if (!edit_) return;
    static const juce::Identifier kInputNamesId("FREEDAW_INPUT_NAMES");
    static const juce::Identifier kEntryId("INPUT");
    static const juce::Identifier kDeviceId("device");
    static const juce::Identifier kDisplayId("display");

    auto existing = edit_->state.getChildWithName(kInputNamesId);
    if (existing.isValid())
        edit_->state.removeChild(existing, nullptr);

    juce::ValueTree node(kInputNamesId);
    for (auto& [dev, display] : inputDisplayNames_) {
        if (display.isEmpty()) continue;
        juce::ValueTree entry(kEntryId);
        entry.setProperty(kDeviceId, juce::String(dev), nullptr);
        entry.setProperty(kDisplayId, juce::String(display.toStdString()), nullptr);
        node.appendChild(entry, nullptr);
    }
    edit_->state.appendChild(node, nullptr);
}

void EditManager::loadInputDisplayNames()
{
    inputDisplayNames_.clear();
    if (!edit_) return;
    static const juce::Identifier kInputNamesId("FREEDAW_INPUT_NAMES");
    static const juce::Identifier kDeviceId("device");
    static const juce::Identifier kDisplayId("display");

    auto node = edit_->state.getChildWithName(kInputNamesId);
    if (!node.isValid()) return;
    for (int i = 0; i < node.getNumChildren(); ++i) {
        auto entry = node.getChild(i);
        auto dev = entry.getProperty(kDeviceId).toString().toStdString();
        auto display = entry.getProperty(kDisplayId).toString().toStdString();
        if (!dev.empty() && !display.empty())
            inputDisplayNames_[dev] = QString::fromStdString(display);
    }
}

// ── Routing layout persistence ───────────────────────────────────────────────

void EditManager::saveRoutingLayout(const QMap<QString, QPointF>& positions)
{
    if (!edit_) return;
    static const juce::Identifier kLayoutId("FREEDAW_ROUTING_LAYOUT");
    static const juce::Identifier kNodeId("NODE");
    static const juce::Identifier kKeyId("key");
    static const juce::Identifier kXId("x");
    static const juce::Identifier kYId("y");

    auto existing = edit_->state.getChildWithName(kLayoutId);
    if (existing.isValid())
        edit_->state.removeChild(existing, nullptr);

    juce::ValueTree node(kLayoutId);
    for (auto it = positions.begin(); it != positions.end(); ++it) {
        juce::ValueTree entry(kNodeId);
        entry.setProperty(kKeyId, juce::String(it.key().toStdString()), nullptr);
        entry.setProperty(kXId, it.value().x(), nullptr);
        entry.setProperty(kYId, it.value().y(), nullptr);
        node.appendChild(entry, nullptr);
    }
    edit_->state.appendChild(node, nullptr);
}

QMap<QString, QPointF> EditManager::loadRoutingLayout() const
{
    QMap<QString, QPointF> positions;
    if (!edit_) return positions;
    static const juce::Identifier kLayoutId("FREEDAW_ROUTING_LAYOUT");
    static const juce::Identifier kKeyId("key");
    static const juce::Identifier kXId("x");
    static const juce::Identifier kYId("y");

    auto node = edit_->state.getChildWithName(kLayoutId);
    if (!node.isValid()) return positions;
    for (int i = 0; i < node.getNumChildren(); ++i) {
        auto entry = node.getChild(i);
        QString key = QString::fromStdString(
            entry.getProperty(kKeyId).toString().toStdString());
        double x = entry.getProperty(kXId);
        double y = entry.getProperty(kYId);
        if (!key.isEmpty())
            positions[key] = QPointF(x, y);
    }
    return positions;
}

// ── Export / Freeze / Bounce ──────────────────────────────────────────────────

bool EditManager::exportMix(const ExportSettings& settings,
                            std::function<void(float)> progressCallback)
{
    if (!edit_) return false;

    auto& transport = edit_->getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);

    auto tracks = te::getAudioTracks(*edit_);
    if (tracks.isEmpty()) return false;

    juce::BigInteger tracksToDo;
    for (auto* track : tracks) {
        int idx = track->getIndexInEditTrackList();
        if (idx >= 0)
            tracksToDo.setBit(idx);
    }

    auto editLength = edit_->getLength();
    if (editLength.inSeconds() <= 0.0) return false;

    te::Renderer::Parameters params(*edit_);
    params.destFile = settings.destFile;
    params.sampleRateForAudio = settings.sampleRate;
    params.bitDepth = settings.bitDepth;
    params.shouldNormalise = settings.normalize;
    params.tracksToDo = tracksToDo;
    params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds(editLength.inSeconds()) };
    params.usePlugins = true;
    params.useMasterPlugins = true;
    params.canRenderInMono = false;

    juce::WavAudioFormat wavFormat;
    params.audioFormat = &wavFormat;

    std::atomic<float> progress{0.0f};
    auto task = std::make_unique<te::Renderer::RenderTask>(
        "Exporting mix", params, &progress, nullptr);

    while (true) {
        auto status = task->runJob();
        float p = progress.load();
        if (progressCallback)
            progressCallback(p);
        if (status == juce::ThreadPoolJob::jobHasFinished)
            break;
    }

    if (!task->errorMessage.isEmpty()) {
        qWarning() << "[exportMix] render error:"
                    << QString::fromStdString(task->errorMessage.toStdString());
        return false;
    }

    return settings.destFile.existsAsFile();
}

bool EditManager::isTrackFrozen(te::AudioTrack* track) const
{
    if (!track) return false;
    return frozenTracks_.count(track->itemID.getRawID()) > 0;
}

void EditManager::freezeTrack(te::AudioTrack& track)
{
    if (!edit_) {
        qWarning() << "[freezeTrack] no edit";
        return;
    }
    if (frozenTracks_.count(track.itemID.getRawID()) > 0) {
        qDebug() << "[freezeTrack] already frozen";
        return;
    }

    auto& transport = edit_->getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);

    int trackIdx = track.getIndexInEditTrackList();
    qDebug() << "[freezeTrack] track index:" << trackIdx
             << "name:" << QString::fromStdString(track.getName().toStdString());

    auto editLength = edit_->getLength();
    qDebug() << "[freezeTrack] edit length:" << editLength.inSeconds() << "sec";
    if (editLength.inSeconds() <= 0.0) {
        qWarning() << "[freezeTrack] edit length is 0, nothing to freeze";
        return;
    }

    auto projectDir = currentFile_.getParentDirectory();
    if (projectDir == juce::File())
        projectDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

    auto freezeFile = projectDir.getChildFile(
        track.getName() + "_freeze_" +
        juce::String(juce::Time::currentTimeMillis()) + ".wav");

    qDebug() << "[freezeTrack] freeze file:"
             << QString::fromStdString(freezeFile.getFullPathName().toStdString());

    juce::BigInteger trackNum;
    trackNum.setBit(trackIdx);

    auto& dm = audioEngine_.engine().getDeviceManager();

    te::Renderer::Parameters params(*edit_);
    params.tracksToDo = trackNum;
    params.destFile = freezeFile;
    params.bitDepth = 24;
    params.blockSizeForAudio = dm.getBlockSize();
    params.sampleRateForAudio = dm.getSampleRate();
    params.time = { tracktion::TimePosition(),
                    tracktion::TimePosition::fromSeconds(editLength.inSeconds()) };
    params.canRenderInMono = false;
    params.usePlugins = true;
    params.useMasterPlugins = false;
    params.checkNodesForAudio = false;

    juce::WavAudioFormat wavFormat;
    params.audioFormat = &wavFormat;

    qDebug() << "[freezeTrack] starting render: sr=" << params.sampleRateForAudio
             << "bs=" << params.blockSizeForAudio
             << "bits set=" << params.tracksToDo.countNumberOfSetBits();

    std::atomic<float> progress{0.0f};
    auto task = std::make_unique<te::Renderer::RenderTask>(
        "Freezing track: " + track.getName(), params, &progress, nullptr);

    if (!task->errorMessage.isEmpty()) {
        qWarning() << "[freezeTrack] RenderTask init error:"
                    << QString::fromStdString(task->errorMessage.toStdString());
        return;
    }

    qDebug() << "[freezeTrack] RenderTask created, running...";

    int iterations = 0;
    while (true) {
        auto status = task->runJob();
        iterations++;
        if (status == juce::ThreadPoolJob::jobHasFinished)
            break;
    }

    qDebug() << "[freezeTrack] render done, iterations:" << iterations
             << "progress:" << progress.load()
             << "error:" << QString::fromStdString(task->errorMessage.toStdString());

    qDebug() << "[freezeTrack] file exists:" << freezeFile.existsAsFile()
             << "size:" << freezeFile.getSize();

    if (!task->errorMessage.isEmpty()) {
        qWarning() << "[freezeTrack] render error:"
                    << QString::fromStdString(task->errorMessage.toStdString());
        freezeFile.deleteFile();
        return;
    }

    if (!freezeFile.existsAsFile()) {
        qWarning() << "[freezeTrack] render produced no file";
        return;
    }

    FreezeState state;
    state.freezeFile = freezeFile;

    for (auto* clip : track.getClips()) {
        if (!clip->isMuted()) {
            state.mutedClipIds.push_back(clip->itemID);
            clip->setMuted(true);
        }
    }
    qDebug() << "[freezeTrack] muted" << state.mutedClipIds.size() << "clips";

    for (int i = 0; i < track.pluginList.size(); ++i) {
        auto* plugin = track.pluginList[i];
        if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) continue;
        if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) continue;
        if (plugin->isEnabled()) {
            state.disabledPluginIndices.push_back(i);
            plugin->setEnabled(false);
        }
    }
    qDebug() << "[freezeTrack] disabled" << state.disabledPluginIndices.size() << "plugins";

    addAudioClipToTrack(track, freezeFile, 0.0);

    frozenTracks_[track.itemID.getRawID()] = std::move(state);
    qDebug() << "[freezeTrack] SUCCESS - track frozen with playback swap, file size:"
             << freezeFile.getSize();

    emit trackFreezeStateChanged(&track);
    emit tracksChanged();
    emit editChanged();
}

void EditManager::unfreezeTrack(te::AudioTrack& track)
{
    if (!edit_) return;

    auto it = frozenTracks_.find(track.itemID.getRawID());
    if (it == frozenTracks_.end()) return;

    auto state = std::move(it->second);
    frozenTracks_.erase(it);

    for (auto* clip : track.getClips()) {
        if (clip->getCurrentSourceFile() == state.freezeFile) {
            clip->removeFromParent();
            break;
        }
    }

    for (auto* clip : track.getClips()) {
        for (auto& id : state.mutedClipIds) {
            if (clip->itemID == id) {
                clip->setMuted(false);
                break;
            }
        }
    }
    qDebug() << "[unfreezeTrack] unmuted" << state.mutedClipIds.size() << "clips";

    for (int idx : state.disabledPluginIndices) {
        if (idx < track.pluginList.size())
            track.pluginList[idx]->setEnabled(true);
    }
    qDebug() << "[unfreezeTrack] re-enabled" << state.disabledPluginIndices.size() << "plugins";

    if (state.freezeFile.existsAsFile())
        state.freezeFile.deleteFile();

    qDebug() << "[unfreezeTrack] track unfrozen:"
             << QString::fromStdString(track.getName().toStdString());

    emit trackFreezeStateChanged(&track);
    emit tracksChanged();
    emit editChanged();
}

bool EditManager::bounceTrackToAudio(te::AudioTrack& track)
{
    if (!edit_) return false;

    auto& transport = edit_->getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);

    auto editLength = edit_->getLength();
    if (editLength.inSeconds() <= 0.0) return false;

    juce::BigInteger trackBit;
    int idx = track.getIndexInEditTrackList();
    if (idx < 0) return false;
    trackBit.setBit(idx);

    auto projectDir = currentFile_.getParentDirectory();
    if (projectDir == juce::File())
        projectDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

    auto bounceFile = projectDir.getChildFile(
        track.getName() + "_bounce_" +
        juce::String(juce::Time::currentTimeMillis()) + ".wav");

    te::Renderer::Parameters params(*edit_);
    params.destFile = bounceFile;
    params.sampleRateForAudio = 44100.0;
    params.bitDepth = 24;
    params.tracksToDo = trackBit;
    params.time = { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds(editLength.inSeconds()) };
    params.usePlugins = true;
    params.useMasterPlugins = false;
    params.canRenderInMono = false;

    juce::WavAudioFormat wavFormat;
    params.audioFormat = &wavFormat;

    std::atomic<float> progress{0.0f};
    auto task = std::make_unique<te::Renderer::RenderTask>(
        "Bouncing track", params, &progress, nullptr);

    while (true) {
        auto status = task->runJob();
        if (status == juce::ThreadPoolJob::jobHasFinished)
            break;
    }

    if (!bounceFile.existsAsFile()) return false;

    auto& um = edit_->getUndoManager();
    um.beginNewTransaction("Bounce Track to Audio");

    auto clips = track.getClips();
    for (int i = clips.size() - 1; i >= 0; --i)
        clips[i]->removeFromParent();

    for (int i = track.pluginList.size() - 1; i >= 0; --i) {
        auto* plugin = track.pluginList[i];
        if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) continue;
        if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) continue;
        plugin->deleteFromParent();
    }

    addAudioClipToTrack(track, bounceFile, 0.0);

    emit tracksChanged();
    emit editChanged();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

void EditManager::ensureLevelMetersOnAllTracks()
{
    if (!edit_)
        return;

    for (auto* track : te::getAudioTracks(*edit_)) {
        bool hasLevelMeter = false;
        for (auto* plugin : track->pluginList.getPlugins()) {
            if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) {
                hasLevelMeter = true;
                break;
            }
        }
        if (!hasLevelMeter) {
            if (auto p = edit_->getPluginCache().createNewPlugin(
                    juce::String(te::LevelMeterPlugin::xmlTypeName), {}))
                track->pluginList.insertPlugin(p, -1, nullptr);
        }
    }

    bool hasMasterLevelMeter = false;
    for (auto* plugin : edit_->getMasterPluginList().getPlugins()) {
        if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) {
            hasMasterLevelMeter = true;
            break;
        }
    }

    if (!hasMasterLevelMeter) {
        if (auto p = edit_->getPluginCache().createNewPlugin(
                juce::String(te::LevelMeterPlugin::xmlTypeName), {}))
            edit_->getMasterPluginList().insertPlugin(p, -1, nullptr);
    }
}

} // namespace freedaw
