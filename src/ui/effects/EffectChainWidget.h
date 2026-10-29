#pragma once

#include "engine/EditManager.h"
#include "ui/controls/RotaryKnob.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <juce_audio_processors/juce_audio_processors.h>
#include <tracktion_engine/tracktion_engine.h>

namespace OpenDaw {

class EffectSlotWidget : public QWidget {
    Q_OBJECT

public:
    EffectSlotWidget(te::Plugin* plugin, EditManager* editMgr = nullptr,
                     QWidget* parent = nullptr);

    te::Plugin* plugin() const { return plugin_; }
    void updateKnobsFromAutomation();

signals:
    void removeRequested(te::Plugin* plugin);

private:
    void buildControls();

    te::Plugin* plugin_;
    EditManager* editMgr_ = nullptr;
    QVBoxLayout* layout_;
    QLabel* nameLabel_;
    QPushButton* bypassBtn_;
    QPushButton* editBtn_ = nullptr;
    QPushButton* removeBtn_;
    QComboBox* sidechainCombo_ = nullptr;

    struct ParamKnob {
        te::AutomatableParameter* param;
        RotaryKnob* knob;
    };
    QVector<ParamKnob> paramKnobs_;
};

class EffectChainWidget : public QWidget {
    Q_OBJECT

public:
    explicit EffectChainWidget(EditManager* editMgr, QWidget* parent = nullptr);
    void setPluginList(juce::KnownPluginList* list) { knownPlugins_ = list; }

    void setTrack(te::AudioTrack* track);
    void setMasterMode();
    te::AudioTrack* currentTrack() const { return track_; }
    bool isMasterMode() const { return masterMode_; }

public slots:
    void rebuild();

private:
    void addEffect(const QString& effectName);
    void addVstEffect(const juce::PluginDescription& desc);
    void removeEffect(te::Plugin* plugin);

    te::PluginList* targetPluginList() const;

    void scheduleRebuild();
    void clearSlots();

    juce::KnownPluginList* knownPlugins_ = nullptr;

    EditManager* editMgr_;
    te::AudioTrack* track_ = nullptr;
    bool masterMode_ = false;

    QVBoxLayout* mainLayout_;
    QLabel* trackLabel_;
    QPushButton* addBtn_;
    QScrollArea* scrollArea_;
    QWidget* slotsContainer_;
    QVBoxLayout* slotsLayout_;
    QTimer rebuildTimer_;
    // Plugins waiting for their delayed removal. Released when the Edit changes,
    // since a plugin must not outlive its Edit.
    std::vector<te::Plugin::Ptr> pendingRemovals_;
    QTimer automationPollTimer_;
};

} // namespace OpenDaw
