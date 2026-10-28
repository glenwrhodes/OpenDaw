#include "PluginEditorWindow.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace OpenDaw {

std::vector<std::unique_ptr<PluginEditorWindow>> PluginEditorWindow::openWindows_;

PluginEditorWindow::PluginEditorWindow(te::ExternalPlugin& plugin)
    : juce::DocumentWindow(plugin.getName(),
                           juce::Colours::darkgrey,
                           juce::DocumentWindow::allButtons),
      plugin_(plugin),
      keepAlive_(&plugin)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setAlwaysOnTop(false);

    if (auto* instance = plugin.getAudioPluginInstance()) {
        if (auto* editor = instance->createEditorIfNeeded()) {
            setContentOwned(editor, true);
        }
    }

    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    toFront(true);

#if JUCE_WINDOWS
    if (auto* peer = getPeer()) {
        if (auto hwnd = (HWND) peer->getNativeHandle()) {
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
        }
    }
#endif
}

PluginEditorWindow::~PluginEditorWindow()
{
    clearContentComponent();
}

void PluginEditorWindow::closeButtonPressed()
{
    for (auto it = openWindows_.begin(); it != openWindows_.end(); ++it) {
        if (it->get() == this) {
            auto moved = std::move(*it);
            openWindows_.erase(it);
            break;
        }
    }
}

void PluginEditorWindow::showForPlugin(te::ExternalPlugin& plugin)
{
    for (auto& w : openWindows_) {
        if (&w->getPlugin() == &plugin) {
            w->toFront(true);
            return;
        }
    }

    juce::ScopedDPIAwarenessDisabler dpiDisabler;
    openWindows_.push_back(std::make_unique<PluginEditorWindow>(plugin));
}

void PluginEditorWindow::closeForPlugin(te::ExternalPlugin& plugin)
{
    for (auto it = openWindows_.begin(); it != openWindows_.end(); ++it) {
        if (&(*it)->getPlugin() == &plugin) {
            openWindows_.erase(it);
            return;
        }
    }
}

void PluginEditorWindow::closeAll()
{
    openWindows_.clear();
}

void PluginEditorWindow::closeOrphaned()
{
    std::erase_if(openWindows_, [](const std::unique_ptr<PluginEditorWindow>& w) {
        auto& p = w->plugin_;
        return !te::getAllPlugins(p.edit, false).contains(&p);
    });
}

} // namespace OpenDaw
