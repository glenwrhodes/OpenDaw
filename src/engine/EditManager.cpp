#include "EditManager.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace freedaw {

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

void EditManager::createDefaultEdit()
{
    auto& eng = audioEngine_.engine();
    edit_ = te::createEmptyEdit(eng, juce::File());
    edit_->ensureNumberOfAudioTracks(4);
    ensureLevelMetersOnAllTracks();
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

    auto& eng = audioEngine_.engine();
    edit_ = te::loadEditFromFile(eng, file);
    if (!edit_)
        return false;

    ensureLevelMetersOnAllTracks();
    currentFile_ = file;
    emit editChanged();
    emit tracksChanged();
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
}

} // namespace freedaw
