#pragma once

#include <QDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <juce_audio_processors/juce_audio_processors.h>

namespace freedaw {

class VstSelectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit VstSelectorDialog(juce::KnownPluginList& pluginList,
                               bool instrumentsOnly = true,
                               QWidget* parent = nullptr);

    bool hasSelection() const { return hasSelection_; }
    juce::PluginDescription selectedPlugin() const { return selectedDesc_; }

private:
    void populateList();
    void filterList(const QString& text);

    juce::KnownPluginList& pluginList_;
    bool instrumentsOnly_;

    QLineEdit* searchField_;
    QTreeWidget* treeWidget_;
    QDialogButtonBox* buttonBox_;

    juce::PluginDescription selectedDesc_;
    bool hasSelection_ = false;

    std::vector<juce::PluginDescription> allDescs_;
};

} // namespace freedaw
