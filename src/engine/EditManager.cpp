#include "EditManager.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>
#include <QDebug>
#include <QPointF>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QCryptographicHash>
#include <QDateTime>

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

static bool hasMidiClips(te::AudioTrack* track)
{
    if (!track) return false;
    for (auto* clip : track->getClips())
        if (dynamic_cast<te::MidiClip*>(clip) != nullptr)
            return true;
    return false;
}

EditManager::EditManager(AudioEngine& engine, QObject* parent)
    : QObject(parent), audioEngine_(engine)
{
    connect(&autosaveTimer_, &QTimer::timeout, this, &EditManager::performAutosave);
    createDefaultEdit();
}

EditManager::~EditManager()
{
    stopAutosave();
}

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

    for (auto& [id, state] : frozenTracks_) {
        if (state.freezeFile.existsAsFile())
            state.freezeFile.deleteFile();
    }
    frozenTracks_.clear();

    midiTrackIds_.clear();
    inputDisplayNames_.clear();
    qDebug() << "[teardown] complete";
}

void EditManager::markDirtyAndNotify()
{
    hasUnsavedChanges_ = true;
    emit editChanged();
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

    auto& arm = edit_->getAutomationRecordManager();
    arm.setReadingAutomation(true);
    arm.setWritingAutomation(false);

    currentFile_ = juce::File();
    edit_->getUndoManager().clearUndoHistory();
    hasUnsavedChanges_ = false;
    emit editChanged();
    emit tracksChanged();
    startAutosave();
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
        if (hasInstrumentPlugin(track) || hasMidiClips(track))
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

    auto& arm = edit_->getAutomationRecordManager();
    arm.setReadingAutomation(true);
    arm.setWritingAutomation(false);

    qDebug() << "[loadEdit] restoring disconnected outputs";
    for (auto* track : te::getAudioTracks(*edit_)) {
        if (track->state.getProperty(juce::Identifier("outputDisconnected"), false))
            track->getOutput().setOutputToTrack(nullptr);
    }

    edit_->getUndoManager().clearUndoHistory();
    hasUnsavedChanges_ = false;
    qDebug() << "[loadEdit] emitting editChanged";
    emit editChanged();
    qDebug() << "[loadEdit] emitting tracksChanged";
    emit tracksChanged();
    startAutosave();
    qDebug() << "[loadEdit] done";
    return true;
}

bool EditManager::saveEdit()
{
    if (!edit_ || currentFile_ == juce::File())
        return false;
    unfreezeAllTracks();
    te::EditFileOperations(*edit_).save(true, true, false);
    clearAutosave();
    hasUnsavedChanges_ = false;
    return true;
}

bool EditManager::saveEditAs(const juce::File& file)
{
    if (!edit_)
        return false;
    unfreezeAllTracks();
    edit_->editFileRetriever = [file]() { return file; };
    te::EditFileOperations(*edit_).save(true, true, false);
    currentFile_ = file;
    clearAutosave();
    hasUnsavedChanges_ = false;
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

    if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(track))
        cleanupFreezeState(*audioTrack);

    edit_->deleteTrack(track);
    emit tracksChanged();
    emit routingChanged();
    markDirtyAndNotify();
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
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(audioFile));
    if (!reader) {
        qWarning() << "[addAudioClipToTrack] cannot read file:"
                    << QString::fromStdString(audioFile.getFullPathName().toStdString());
        return;
    }
    double fileDurationSecs = double(reader->lengthInSamples) / reader->sampleRate;
    if (fileDurationSecs <= 0.0) {
        qWarning() << "[addAudioClipToTrack] file has zero duration";
        return;
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

    markDirtyAndNotify();
}

void EditManager::midiPanic()
{
    if (!edit_ || engineSuspended_) return;

    transport().stop(false, false);

    for (auto* track : getAudioTracks()) {
        for (int ch = 1; ch <= 16; ++ch) {
            track->injectLiveMidiMessage(
                juce::MidiMessage::allSoundOff(ch), 0);
            track->injectLiveMidiMessage(
                juce::MidiMessage::allNotesOff(ch), 0);
        }
    }
}

void EditManager::suspendEngine()
{
    if (engineSuspended_ || !edit_) return;

    auto& t = edit_->getTransport();
    if (t.isPlaying())
        t.stop(false, false);

    t.freePlaybackContext();
    audioEngine_.deviceManager().deviceManager.closeAudioDevice();
    engineSuspended_ = true;
}

void EditManager::resumeEngine()
{
    if (!engineSuspended_) return;

    audioEngine_.setDefaultAudioDevice();
    audioEngine_.restoreSavedAudioSettings();
    audioEngine_.enableAllMidiInputDevices();

    if (edit_) {
        enableAllWaveInputDevices();
        edit_->getTransport().ensureContextAllocated();
    }

    engineSuspended_ = false;
}

void EditManager::undo()
{
    if (!edit_) return;
    edit_->getUndoManager().undo();
    markDirtyAndNotify();
    emit tracksChanged();
}

void EditManager::redo()
{
    if (!edit_) return;
    edit_->getUndoManager().redo();
    markDirtyAndNotify();
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
    markDirtyAndNotify();
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
    markDirtyAndNotify();
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

    markDirtyAndNotify();
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

    auto importToTrack = [&](te::AudioTrack& destTrack, const juce::MidiMessageSequence* midiTrack) -> te::MidiClip* {
        auto* clip = addMidiClipToTrack(destTrack, startBeat, lengthBeats);
        if (!clip) return nullptr;

        auto& seq = clip->getSequence();
        seq.removeAllNotes(nullptr);

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

        if (!isMidiTrack(&destTrack))
            markAsMidiTrack(&destTrack);
        return clip;
    };

    int noteTrackCount = 0;
    for (int t = 0; t < midi.getNumTracks(); ++t) {
        auto* mt = midi.getTrack(t);
        if (!mt) continue;
        bool hasNotes = false;
        for (int ei = 0; ei < mt->getNumEvents(); ++ei) {
            if (mt->getEventPointer(ei)->message.isNoteOn()) { hasNotes = true; break; }
        }
        if (hasNotes) noteTrackCount++;
    }

    te::MidiClip* firstClip = nullptr;
    if (noteTrackCount <= 1) {
        for (int t = 0; t < midi.getNumTracks(); ++t) {
            auto* mt = midi.getTrack(t);
            if (!mt) continue;
            bool hasNotes = false;
            for (int ei = 0; ei < mt->getNumEvents(); ++ei) {
                if (mt->getEventPointer(ei)->message.isNoteOn()) { hasNotes = true; break; }
            }
            if (hasNotes) {
                firstClip = importToTrack(track, mt);
                break;
            }
        }
        if (!firstClip)
            firstClip = importToTrack(track, midi.getTrack(0));
    } else {
        bool usedFirst = false;
        for (int t = 0; t < midi.getNumTracks(); ++t) {
            auto* mt = midi.getTrack(t);
            if (!mt) continue;
            bool hasNotes = false;
            for (int ei = 0; ei < mt->getNumEvents(); ++ei) {
                if (mt->getEventPointer(ei)->message.isNoteOn()) { hasNotes = true; break; }
            }
            if (!hasNotes) continue;

            te::AudioTrack* destTrack;
            if (!usedFirst) {
                destTrack = &track;
                usedFirst = true;
            } else {
                destTrack = addMidiTrack();
                if (!destTrack) continue;
            }

            auto* clip = importToTrack(*destTrack, mt);
            if (clip && !firstClip)
                firstClip = clip;
        }
    }

    markDirtyAndNotify();
    return firstClip;
}

bool EditManager::isMidiTrack(te::AudioTrack* track) const
{
    if (!track) return false;
    if (midiTrackIds_.count(track->itemID.getRawID()) > 0)
        return true;
    return hasInstrumentPlugin(track) || hasMidiClips(track);
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
        markDirtyAndNotify();
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
        markDirtyAndNotify();
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

te::MidiClip* EditManager::addLinkedMidiChannel(te::AudioTrack& track,
                                                 te::MidiClip& referenceClip,
                                                 int midiChannel,
                                                 const QString& displayName)
{
    if (!edit_) return nullptr;

    auto startTime = referenceClip.getPosition().getStart();
    auto endTime = referenceClip.getPosition().getEnd();

    auto clipRef = track.insertMIDIClip(
        juce::String("MIDI Ch ") + juce::String(midiChannel),
        tracktion::TimeRange(startTime, endTime),
        nullptr);

    auto* newClip = clipRef.get();
    if (!newClip) return nullptr;

    newClip->setMidiChannel(te::MidiChannel(std::clamp(midiChannel, 1, 16)));

    if (!displayName.isEmpty())
        setChannelName(*newClip, displayName);

    markDirtyAndNotify();
    return newClip;
}

std::vector<te::MidiClip*> EditManager::getLinkedMidiClips(te::AudioTrack* track,
                                                            te::MidiClip* referenceClip) const
{
    std::vector<te::MidiClip*> result;
    if (!track || !referenceClip) return result;

    auto refStart = referenceClip->getPosition().getStart();
    auto refEnd = referenceClip->getPosition().getEnd();

    for (auto* clip : track->getClips()) {
        auto* mc = dynamic_cast<te::MidiClip*>(clip);
        if (!mc) continue;

        auto clipStart = mc->getPosition().getStart();
        auto clipEnd = mc->getPosition().getEnd();
        bool overlaps = clipStart < refEnd && clipEnd > refStart;
        if (overlaps)
            result.push_back(mc);
    }

    std::sort(result.begin(), result.end(), [](te::MidiClip* a, te::MidiClip* b) {
        return a->getMidiChannel().getChannelNumber()
             < b->getMidiChannel().getChannelNumber();
    });

    return result;
}

bool EditManager::isLinkedSecondary(te::MidiClip* clip) const
{
    if (!clip) return false;
    auto* track = clip->getAudioTrack();
    if (!track) return false;

    auto clipStart = clip->getPosition().getStart();
    auto clipEnd = clip->getPosition().getEnd();
    int clipCh = clip->getMidiChannel().getChannelNumber();

    for (auto* c : track->getClips()) {
        auto* mc = dynamic_cast<te::MidiClip*>(c);
        if (!mc || mc == clip) continue;

        auto otherStart = mc->getPosition().getStart();
        auto otherEnd = mc->getPosition().getEnd();
        bool overlaps = otherStart < clipEnd && otherEnd > clipStart;
        if (overlaps && mc->getMidiChannel().getChannelNumber() < clipCh)
            return true;
    }
    return false;
}

int EditManager::linkedChannelCount(te::MidiClip* clip) const
{
    if (!clip) return 1;
    auto* track = clip->getAudioTrack();
    if (!track) return 1;

    auto linked = getLinkedMidiClips(track, clip);
    return std::max(1, static_cast<int>(linked.size()));
}

void EditManager::propagateClipPosition(te::MidiClip& primary)
{
    auto* track = primary.getAudioTrack();
    if (!track) return;

    auto startTime = primary.getPosition().getStart();
    auto endTime = primary.getPosition().getEnd();

    for (auto* c : track->getClips()) {
        auto* mc = dynamic_cast<te::MidiClip*>(c);
        if (!mc || mc == &primary) continue;

        auto otherStart = mc->getPosition().getStart();
        auto otherEnd = mc->getPosition().getEnd();
        bool wasLinked = (otherStart < endTime && otherEnd > startTime)
                      || (std::abs((otherStart - startTime).inSeconds()) < 0.01);

        if (wasLinked) {
            mc->setStart(startTime, false, true);
            mc->setEnd(endTime, false);
        }
    }
}

void EditManager::setChannelName(te::MidiClip& clip, const QString& name)
{
    auto* um = edit_ ? &edit_->getUndoManager() : nullptr;
    clip.state.setProperty(juce::Identifier("channelDisplayName"),
                           juce::String(name.toStdString()), um);
}

QString EditManager::getChannelName(te::MidiClip* clip) const
{
    if (!clip) return {};
    int ch = clip->getMidiChannel().getChannelNumber();
    if (ch < 1) ch = 1;
    auto val = clip->state.getProperty(juce::Identifier("channelDisplayName"));
    if (val.isVoid() || val.toString().isEmpty())
        return QString("Ch %1").arg(ch);
    return QString("%1 (ch%2)")
        .arg(QString::fromStdString(val.toString().toStdString()))
        .arg(ch);
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

    markDirtyAndNotify();
    emit routingChanged();
}

void EditManager::clearTrackInput(te::AudioTrack& track)
{
    clearTrackInputInternal(track);
    markDirtyAndNotify();
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

    markDirtyAndNotify();
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

bool EditManager::wouldCreateCycle(te::AudioTrack* src, te::AudioTrack* dest) const
{
    if (!src || !dest) return false;
    if (src == dest) return true;

    std::unordered_set<uint64_t> visited;
    visited.insert(src->itemID.getRawID());
    auto* current = dest;
    while (current) {
        if (visited.count(current->itemID.getRawID()))
            return true;
        visited.insert(current->itemID.getRawID());
        current = current->getOutput().getDestinationTrack();
    }
    return false;
}

void EditManager::setTrackOutputToMaster(te::AudioTrack& track)
{
    if (!edit_) return;
    track.state.setProperty(juce::Identifier("outputDisconnected"), false,
                            &edit_->getUndoManager());
    track.getOutput().setOutputToDefaultDevice(false);
    emit routingChanged();
    markDirtyAndNotify();
}

void EditManager::setTrackOutputToTrack(te::AudioTrack& src, te::AudioTrack& dest)
{
    if (!edit_) return;
    if (wouldCreateCycle(&src, &dest)) {
        qWarning() << "[setTrackOutputToTrack] rejected: would create routing cycle";
        return;
    }
    src.state.setProperty(juce::Identifier("outputDisconnected"), false,
                          &edit_->getUndoManager());
    src.getOutput().setOutputToTrack(&dest);
    emit routingChanged();
    markDirtyAndNotify();
}

void EditManager::clearTrackOutput(te::AudioTrack& track)
{
    if (!edit_) return;
    track.state.setProperty(juce::Identifier("outputDisconnected"), true,
                            &edit_->getUndoManager());
    track.getOutput().setOutputToTrack(nullptr);
    emit routingChanged();
    markDirtyAndNotify();
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

// ── Automation parameter access ───────────────────────────────────────────────

QVector<te::AutomatableParameter*> EditManager::getAutomatableParamsForTrack(te::AudioTrack* track) const
{
    QVector<te::AutomatableParameter*> result;
    if (!track) return result;

    for (auto* plugin : track->pluginList.getPlugins()) {
        for (auto* param : plugin->getAutomatableParameters())
            result.append(param);
    }
    return result;
}

te::AutomatableParameter* EditManager::getVolumeParam(te::AudioTrack* track) const
{
    if (!track) return nullptr;
    if (auto* vp = track->getVolumePlugin())
        return vp->volParam.get();
    return nullptr;
}

te::AutomatableParameter* EditManager::getPanParam(te::AudioTrack* track) const
{
    if (!track) return nullptr;
    if (auto* vp = track->getVolumePlugin())
        return vp->panParam.get();
    return nullptr;
}

// ── Export / Freeze / Bounce ──────────────────────────────────────────────────

juce::String EditManager::sanitizeForFilename(const juce::String& name)
{
    juce::String safe = name;
    for (auto ch : { '/', '\\', ':', '*', '?', '"', '<', '>', '|' })
        safe = safe.replaceCharacter(ch, '_');
    return safe.isEmpty() ? juce::String("track") : safe;
}

void EditManager::unfreezeAllTracks()
{
    if (!edit_) return;
    auto trackIds = std::vector<uint64_t>();
    for (auto& [id, _] : frozenTracks_)
        trackIds.push_back(id);
    for (auto id : trackIds) {
        for (auto* track : te::getAudioTracks(*edit_)) {
            if (track->itemID.getRawID() == id) {
                unfreezeTrack(*track);
                break;
            }
        }
    }
}

void EditManager::cleanupFreezeState(te::AudioTrack& track)
{
    auto it = frozenTracks_.find(track.itemID.getRawID());
    if (it == frozenTracks_.end()) return;

    if (it->second.freezeFile.existsAsFile())
        it->second.freezeFile.deleteFile();
    frozenTracks_.erase(it);
}

bool EditManager::exportMix(const ExportSettings& settings,
                            std::function<void(float)> progressCallback)
{
    if (!edit_) return false;
    if (renderInProgress_) return false;

    auto& transport = edit_->getTransport();
    if (transport.isRecording()) {
        qWarning() << "[exportMix] cannot export while recording";
        return false;
    }

    if (!settings.destFile.getParentDirectory().isDirectory()) {
        qWarning() << "[exportMix] output directory does not exist";
        return false;
    }

    bool allMuted = true;
    for (auto* track : te::getAudioTracks(*edit_)) {
        if (!track->isMuted(true)) { allMuted = false; break; }
    }
    if (allMuted) {
        qWarning() << "[exportMix] all tracks are muted, export would be silent";
        return false;
    }

    renderInProgress_ = true;

    if (transport.isPlaying())
        transport.stop(false, false);

    auto tracks = te::getAudioTracks(*edit_);
    if (tracks.isEmpty()) { renderInProgress_ = false; return false; }

    juce::BigInteger tracksToDo;
    for (auto* track : tracks) {
        int idx = track->getIndexInEditTrackList();
        if (idx >= 0)
            tracksToDo.setBit(idx);
    }

    auto editLength = edit_->getLength();
    if (editLength.inSeconds() <= 0.0) { renderInProgress_ = false; return false; }

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

    renderInProgress_ = false;

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
    if (!edit_ || renderInProgress_) return;
    if (frozenTracks_.count(track.itemID.getRawID()) > 0) return;

    if (track.getClips().isEmpty()) {
        qDebug() << "[freezeTrack] track has no clips, nothing to freeze";
        return;
    }

    if (isBusTrack(&track)) {
        for (auto* other : getAudioTracks()) {
            if (getTrackOutputDestination(other) == &track
                && isTrackFrozen(other)) {
                qWarning() << "[freezeTrack] feeder track is already frozen, unfreeze it first";
                return;
            }
        }
    } else {
        auto* dest = getTrackOutputDestination(&track);
        if (dest && isTrackFrozen(dest)) {
            qWarning() << "[freezeTrack] destination bus is already frozen, unfreeze it first";
            return;
        }
    }

    renderInProgress_ = true;

    auto& transport = edit_->getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);

    int trackIdx = track.getIndexInEditTrackList();
    qDebug() << "[freezeTrack] track index:" << trackIdx
             << "name:" << QString::fromStdString(track.getName().toStdString());

    auto editLength = edit_->getLength();
    if (editLength.inSeconds() <= 0.0) {
        renderInProgress_ = false;
        return;
    }

    auto projectDir = currentFile_.getParentDirectory();
    if (projectDir == juce::File())
        projectDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

    auto freezeFile = projectDir.getChildFile(
        sanitizeForFilename(track.getName()) + "_freeze_" +
        juce::String(juce::Time::currentTimeMillis()) + ".wav");

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

    qDebug() << "[freezeTrack] starting render...";

    std::atomic<float> progress{0.0f};
    auto task = std::make_unique<te::Renderer::RenderTask>(
        "Freezing track: " + track.getName(), params, &progress, nullptr);

    if (!task->errorMessage.isEmpty()) {
        qWarning() << "[freezeTrack] RenderTask init error:"
                    << QString::fromStdString(task->errorMessage.toStdString());
        renderInProgress_ = false;
        return;
    }

    while (true) {
        auto status = task->runJob();
        if (status == juce::ThreadPoolJob::jobHasFinished)
            break;
    }

    renderInProgress_ = false;

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

    for (auto* plugin : track.pluginList.getPlugins()) {
        if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) continue;
        if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) continue;
        if (plugin->isEnabled()) {
            state.disabledPluginIds.push_back(plugin->itemID);
            plugin->setEnabled(false);
        }
    }

    addAudioClipToTrack(track, freezeFile, 0.0);

    frozenTracks_[track.itemID.getRawID()] = std::move(state);
    qDebug() << "[freezeTrack] SUCCESS - frozen, file:" << freezeFile.getSize() << "bytes";

    emit trackFreezeStateChanged(&track);
    emit tracksChanged();
    markDirtyAndNotify();
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

    for (auto* plugin : track.pluginList.getPlugins()) {
        for (auto& id : state.disabledPluginIds) {
            if (plugin->itemID == id) {
                plugin->setEnabled(true);
                break;
            }
        }
    }

    if (state.freezeFile.existsAsFile())
        state.freezeFile.deleteFile();

    qDebug() << "[unfreezeTrack] track unfrozen:"
             << QString::fromStdString(track.getName().toStdString());

    emit trackFreezeStateChanged(&track);
    emit tracksChanged();
    markDirtyAndNotify();
}

bool EditManager::bounceTrackToAudio(te::AudioTrack& track)
{
    if (!edit_ || renderInProgress_) return false;

    if (isTrackFrozen(&track)) {
        qWarning() << "[bounceTrack] track is frozen, unfreeze before bouncing";
        return false;
    }

    renderInProgress_ = true;

    auto& transport = edit_->getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);

    auto editLength = edit_->getLength();
    if (editLength.inSeconds() <= 0.0) { renderInProgress_ = false; return false; }

    juce::BigInteger trackBit;
    int idx = track.getIndexInEditTrackList();
    if (idx < 0) { renderInProgress_ = false; return false; }
    trackBit.setBit(idx);

    auto projectDir = currentFile_.getParentDirectory();
    if (projectDir == juce::File())
        projectDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

    auto bounceFile = projectDir.getChildFile(
        sanitizeForFilename(track.getName()) + "_bounce_" +
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

    renderInProgress_ = false;

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
    markDirtyAndNotify();
    return true;
}

// ── Autosave ─────────────────────────────────────────────────────────────────

QString EditManager::autosaveDir()
{
    auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + "/autosave";
    QDir().mkpath(dir);
    return dir;
}

QString EditManager::autosaveFileId() const
{
    QString source;
    if (currentFile_ != juce::File())
        source = QString::fromStdString(currentFile_.getFullPathName().toStdString());
    else
        source = "untitled";
    return QString(QCryptographicHash::hash(source.toUtf8(),
                                            QCryptographicHash::Md5).toHex());
}

void EditManager::startAutosave()
{
    autosaveTimer_.start(60000);
}

void EditManager::stopAutosave()
{
    autosaveTimer_.stop();
}

void EditManager::performAutosave()
{
    if (!edit_) return;

    auto dir = autosaveDir();
    auto id = autosaveFileId();
    auto editPath = dir + "/" + id + ".tracktionedit";
    auto sidecarPath = dir + "/" + id + ".json";

    juce::File autosaveFile(juce::String(editPath.toUtf8().constData()));

    edit_->editFileRetriever = [currentFile = currentFile_, autosaveFile]() {
        return autosaveFile;
    };

    te::EditFileOperations(*edit_).save(true, true, false);

    if (currentFile_ != juce::File()) {
        edit_->editFileRetriever = [f = currentFile_]() { return f; };
    } else {
        edit_->editFileRetriever = []() { return juce::File(); };
    }

    QJsonObject sidecar;
    if (currentFile_ != juce::File())
        sidecar["originalPath"] = QString::fromStdString(
            currentFile_.getFullPathName().toStdString());
    else
        sidecar["originalPath"] = QString();
    sidecar["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile jsonFile(sidecarPath);
    if (jsonFile.open(QIODevice::WriteOnly))
        jsonFile.write(QJsonDocument(sidecar).toJson(QJsonDocument::Compact));

    qDebug() << "[autosave] saved to" << editPath;
}

void EditManager::clearAutosave()
{
    auto dir = autosaveDir();
    auto id = autosaveFileId();
    QFile::remove(dir + "/" + id + ".tracktionedit");
    QFile::remove(dir + "/" + id + ".json");
}

QList<EditManager::RecoveryInfo> EditManager::findRecoveryFiles()
{
    QList<RecoveryInfo> results;
    QDir dir(autosaveDir());
    auto entries = dir.entryList({"*.tracktionedit"}, QDir::Files);
    for (auto& entry : entries) {
        auto baseName = entry.left(entry.lastIndexOf('.'));
        auto sidecarPath = dir.filePath(baseName + ".json");
        RecoveryInfo info;
        info.autosavePath = dir.filePath(entry);

        QFile jsonFile(sidecarPath);
        if (jsonFile.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(jsonFile.readAll());
            auto obj = doc.object();
            info.originalPath = obj["originalPath"].toString();
            info.timestamp = obj["timestamp"].toString();
        }

        results.append(info);
    }
    return results;
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
