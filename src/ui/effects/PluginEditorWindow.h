#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <QObject>

namespace te = tracktion::engine;

namespace OpenDaw {

class PluginEditorWindow : public juce::DocumentWindow {
public:
    PluginEditorWindow(te::ExternalPlugin& plugin);
    ~PluginEditorWindow() override;

    void closeButtonPressed() override;

    te::ExternalPlugin& getPlugin() { return plugin_; }

    static void showForPlugin(te::ExternalPlugin& plugin);
    static void closeForPlugin(te::ExternalPlugin& plugin);
    static void closeAll();
    // Closes editors whose plugin was removed from its Edit (effect removed,
    // instrument replaced, track deleted).
    static void closeOrphaned();

private:
    te::ExternalPlugin& plugin_;
    // The editor component belongs to the plugin instance, so the plugin must
    // outlive this window. Callers must closeAll() before its Edit is destroyed.
    te::Plugin::Ptr keepAlive_;

    static std::vector<std::unique_ptr<PluginEditorWindow>> openWindows_;
};

} // namespace OpenDaw
