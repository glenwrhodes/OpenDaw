#include "AudioEngine.h"

namespace freedaw {

AudioEngine::AudioEngine()
{
    engine_ = std::make_unique<te::Engine>("FreeDaw");
}

AudioEngine::~AudioEngine() = default;

te::DeviceManager& AudioEngine::deviceManager()
{
    return engine_->getDeviceManager();
}

void AudioEngine::setDefaultAudioDevice()
{
    auto& dm = engine_->getDeviceManager();
    dm.initialise(2, 2);
}

juce::StringArray AudioEngine::getAvailableInputDevices() const
{
    juce::StringArray result;
    auto& dm = engine_->getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* dev = dm.getWaveInDevice(i))
            result.add(dev->getName());
    return result;
}

juce::StringArray AudioEngine::getAvailableOutputDevices() const
{
    juce::StringArray result;
    auto& dm = engine_->getDeviceManager();
    for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
        if (auto* dev = dm.getWaveOutDevice(i))
            result.add(dev->getName());
    return result;
}

juce::StringArray AudioEngine::getAvailableMidiInputDevices() const
{
    juce::StringArray result;
    auto& dm = engine_->getDeviceManager();
    for (int i = 0; i < dm.getNumMidiInDevices(); ++i) {
        auto dev = dm.getMidiInDevice(i);
        if (dev)
            result.add(dev->getName());
    }
    return result;
}

juce::StringArray AudioEngine::getAvailableMidiOutputDevices() const
{
    juce::StringArray result;
    auto& dm = engine_->getDeviceManager();
    for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
        if (auto dev = dm.getMidiOutDevice(i))
            result.add(dev->getName());
    return result;
}

void AudioEngine::enableAllMidiInputDevices()
{
    auto& dm = engine_->getDeviceManager();
    for (int i = 0; i < dm.getNumMidiInDevices(); ++i) {
        auto dev = dm.getMidiInDevice(i);
        if (dev)
            dev->setEnabled(true);
    }
}

} // namespace freedaw
