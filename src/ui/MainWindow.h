#pragma once

#include "app/FreeDawApplication.h"
#include "ui/timeline/TimelineView.h"
#include "ui/mixer/MixerView.h"
#include "ui/transport/TransportBar.h"
#include "ui/effects/EffectChainWidget.h"
#include "ui/browser/FileBrowserPanel.h"
#include <QMainWindow>
#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>

namespace freedaw {

class PianoRollEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(FreeDawApplication& app, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void createMenus();
    void createToolBar();
    void createDocks();
    void createStatusBar();
    void applyGlobalStyle();

    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onAddTrack();
    void onAddMidiTrack();
    void onRemoveTrack();
    void onSplitClipAtPlayhead();
    void onScanVstPlugins();
    void onEffectInsertRequested(te::AudioTrack* track, int slotIndex);
    void onInstrumentSelectRequested(te::AudioTrack* track);
    void onMidiClipDoubleClicked(te::MidiClip* clip);

    FreeDawApplication& app_;
    EditManager& editMgr_;

    TransportBar* transportBar_       = nullptr;
    TimelineView* timelineView_       = nullptr;
    MixerView*    mixerView_          = nullptr;
    EffectChainWidget* effectChain_   = nullptr;
    FileBrowserPanel*  fileBrowser_   = nullptr;
    PianoRollEditor*   pianoRoll_     = nullptr;

    QDockWidget* mixerDock_           = nullptr;
    QDockWidget* effectsDock_         = nullptr;
    QDockWidget* browserDock_         = nullptr;
    QDockWidget* pianoRollDock_       = nullptr;

    QToolBar* mainToolBar_            = nullptr;
    QAction* splitClipAction_         = nullptr;
};

} // namespace freedaw
