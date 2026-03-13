#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <memory>

namespace te = tracktion::engine;

namespace freedaw {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    te::Engine&       engine()       { return *engine_; }
    const te::Engine& engine() const { return *engine_; }

    te::DeviceManager& deviceManager();

    void setDefaultAudioDevice();
    juce::StringArray getAvailableInputDevices() const;
    juce::StringArray getAvailableOutputDevices() const;

    juce::StringArray getAvailableMidiInputDevices() const;
    juce::StringArray getAvailableMidiOutputDevices() const;
    void enableAllMidiInputDevices();

private:
    std::unique_ptr<te::Engine> engine_;
};

} // namespace freedaw
