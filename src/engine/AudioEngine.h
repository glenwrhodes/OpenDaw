#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <memory>

namespace te = tracktion::engine;

namespace OpenDaw {

class LoudnessTap;

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    te::Engine&       engine()       { return *engine_; }
    const te::Engine& engine() const { return *engine_; }

    te::DeviceManager& deviceManager();

    void setDefaultAudioDevice();
    void restoreSavedAudioSettings();
    juce::StringArray getAvailableInputDevices() const;
    juce::StringArray getAvailableOutputDevices() const;

    juce::StringArray getAvailableMidiInputDevices() const;
    juce::StringArray getAvailableMidiOutputDevices() const;
    void enableAllMidiInputDevices();

    // Input monitoring ("audio/monitorMode": 0 off, 1 while armed, 2 always)
    // and the recording offset ("audio/recordOffsetMs") from the app settings,
    // applied to every audio input.
    void applyInputSettings();

    // Loudness of what the audio device plays, installed with the device.
    LoudnessTap* loudnessTap() const { return loudnessTap_; }

private:
    std::unique_ptr<te::Engine> engine_;
    LoudnessTap* loudnessTap_ = nullptr;   // owned by the device manager
};

} // namespace OpenDaw
