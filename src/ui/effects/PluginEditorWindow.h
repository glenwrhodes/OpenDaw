#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <QObject>

namespace te = tracktion::engine;

namespace freedaw {

class PluginEditorWindow : public juce::DocumentWindow {
public:
    PluginEditorWindow(te::ExternalPlugin& plugin);
    ~PluginEditorWindow() override;

    void closeButtonPressed() override;

    te::ExternalPlugin& getPlugin() { return plugin_; }

    static void showForPlugin(te::ExternalPlugin& plugin);
    static void closeForPlugin(te::ExternalPlugin& plugin);
    static void closeAll();

private:
    te::ExternalPlugin& plugin_;

    static std::vector<std::unique_ptr<PluginEditorWindow>> openWindows_;
};

} // namespace freedaw
