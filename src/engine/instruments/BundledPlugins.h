#pragma once

#include "engine/AudioEngine.h"
#include <optional>

namespace OpenDaw {

// Plugins shipped with OpenDaw: today, the Surge XT synth.
class BundledPlugins {
public:
    // The folder they're installed in (beside the program, in the macOS app
    // bundle's PlugIns, or in the AppImage's share/opendaw); invalid if none.
    static juce::File folder();
    static juce::File surgeXt();     // the Surge XT VST3 bundle, or invalid
    static bool hasSurgeXt() { return surgeXt().exists(); }

    // Call once at startup, before any plugin loads: on macOS, points Surge XT
    // at the factory content in the app bundle unless Surge is installed.
    static void prepareEnvironment();

    // Surge XT's entry in the plugin list, adding it (scanning just that one
    // plugin) the first time. Message thread; nullopt if it isn't shipped.
    static std::optional<juce::PluginDescription> surgeXtDescription(juce::KnownPluginList& list);
    static bool isSurgeXt(te::Plugin* plugin);
};

} // namespace OpenDaw
