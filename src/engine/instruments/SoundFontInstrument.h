#pragma once

#include "engine/AudioEngine.h"
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <vector>

struct tsf;

namespace OpenDaw {

// The bundled General MIDI sound set (GeneralUser GS, played with TinySoundFont).
// Loaded once, on first use; every instrument shares its samples.
class SoundFontLibrary {
public:
    struct Preset {
        int bank = 0;
        int program = 0;
        QString name;
        QString category;     // a General MIDI family, or "Drum Kits"
        bool isDrumKit() const { return bank == 128; }
    };

    static SoundFontLibrary& instance();

    // Message thread. Loads the SoundFont the first time; nullptr if it's missing.
    tsf* master();
    bool isAvailable();
    const std::vector<Preset>& presets();
    const Preset* find(int bank, int program);
    // Matches a name ("Grand Piano", "fretless bass", "808") or a GM program
    // number ("1"-"128"); nullptr if nothing fits.
    const Preset* findByName(const QString& text);
    static QStringList categories();   // menu order
    juce::File file() const { return file_; }

private:
    SoundFontLibrary() = default;
    void load();

    bool tried_ = false;
    tsf* master_ = nullptr;
    juce::File file_;
    std::vector<Preset> presets_;
};

// OpenDaw's built-in sampled instrument: any sound from the General MIDI set,
// chosen by bank and program. Plays on every MIDI channel, or, with
// "follow program changes" on, behaves like a General MIDI module (program
// changes pick each channel's sound and channel 10 plays drums).
class SoundFontPlugin : public te::Plugin {
public:
    explicit SoundFontPlugin(te::PluginCreationInfo info);
    ~SoundFontPlugin() override;

    static const char* getPluginName() { return "OpenDaw Instrument"; }
    static const char* xmlTypeName;
    static juce::ValueTree create(int bank, int program);

    juce::String getName() const override;
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return "OpenDaw Instrument"; }
    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& fc) override;
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    bool isSynth() override { return true; }
    bool takesMidiInput() override { return true; }
    bool takesAudioInput() override { return false; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    bool noTail() override { return false; }
    double getTailLength() const override { return 2.0; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return true; }

    int bank() const { return bank_.get(); }
    int program() const { return program_.get(); }
    void setPreset(int bank, int program);   // message thread, undoable
    bool followsProgramChanges() const { return followProgramChanges_.get(); }
    void setFollowsProgramChanges(bool on);

private:
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id) override;
    void syncSettings();
    void applyPresetToChannels();
    void handleMidi(const juce::MidiMessage& m);
    void render(float* left, float* right, int numSamples);

    juce::CachedValue<int> bank_, program_;
    juce::CachedValue<bool> followProgramChanges_;

    tsf* synth_ = nullptr;          // this instrument's own voices; samples are shared
    juce::SpinLock synthLock_;      // held by the audio thread while rendering
    std::vector<float> scratch_;
    double sampleRate_ = 48000.0;
    // The settings, for the audio thread (the ValueTree is message-thread only).
    std::atomic<int> bankNow_ { 0 }, programNow_ { 0 };
    std::atomic<bool> followNow_ { false };
    std::atomic<bool> presetChanged_ { true };
    std::atomic<bool> resetRequested_ { false };
};

} // namespace OpenDaw
