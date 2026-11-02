#include "BundledPlugins.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdlib>

namespace OpenDaw {

namespace {
constexpr const char* kSurgeXtBundle = "Surge XT.vst3";
}

juce::File BundledPlugins::folder()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    const juce::File candidates[] = {
        dir.getChildFile("plugins"),
        dir.getParentDirectory().getChildFile("PlugIns"),
        dir.getParentDirectory().getChildFile("share/opendaw/plugins"),
    };
    for (const auto& f : candidates)
        if (f.getChildFile(kSurgeXtBundle).isDirectory()) return f;
    return {};
}

juce::File BundledPlugins::surgeXt()
{
    const auto dir = folder();
    return dir == juce::File() ? juce::File() : dir.getChildFile(kSurgeXtBundle);
}

void BundledPlugins::prepareEnvironment()
{
#if JUCE_MAC
    // Surge XT on macOS reads its content from Application Support; an
    // installed copy's content is fine for the bundled plugin too.
    const auto data = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                          .getParentDirectory().getParentDirectory().getChildFile("Resources/SurgeXTData");
    const auto userData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Application Support/Surge XT");
    const juce::File sharedData("/Library/Application Support/Surge XT");
    if (data.isDirectory() && !std::getenv("SURGE_DATA_HOME")
        && !userData.isDirectory() && !sharedData.isDirectory())
        setenv("SURGE_DATA_HOME", data.getFullPathName().toRawUTF8(), 0);
#endif
    // Windows and Linux: Surge XT finds the SurgeXTData folder beside it.
}

std::optional<juce::PluginDescription> BundledPlugins::surgeXtDescription(juce::KnownPluginList& list)
{
    const auto bundle = surgeXt();
    if (!bundle.exists()) return std::nullopt;

    // The list holds the bundle, or on Windows the binary inside it.
    auto isOurs = [&bundle](const juce::PluginDescription& d) {
        const juce::File f(d.fileOrIdentifier);
        return d.isInstrument && (f == bundle || f.isAChildOf(bundle));
    };
    for (const auto& d : list.getTypes())
        if (isOurs(d)) return d;

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> found;
    vst3.findAllTypesForFile(found, bundle.getFullPathName());
    std::optional<juce::PluginDescription> result;
    for (auto* d : found) {
        list.addType(*d);
        if (!result && isOurs(*d)) result = *d;
    }
    return result;
}

bool BundledPlugins::isSurgeXt(te::Plugin* plugin)
{
    auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin);
    if (!ext) return false;
    const auto bundle = surgeXt();
    const juce::File f(ext->desc.fileOrIdentifier);
    return bundle.exists() && (f == bundle || f.isAChildOf(bundle));
}

} // namespace OpenDaw
