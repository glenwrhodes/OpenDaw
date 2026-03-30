#pragma once

#include "app/OpenDawApplication.h"
#include "ui/timeline/TimelineView.h"
#include "ui/mixer/MixerView.h"
#include "ui/transport/TransportBar.h"
#include "ui/effects/EffectChainWidget.h"
#include "ui/browser/FileBrowserPanel.h"
#include "ui/routing/RoutingView.h"
#include "ai/AiChatWidget.h"
#include "ai/AiQuickPrompt.h"
#include <QMainWindow>
#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QCloseEvent>
#include <QLabel>
#include <QTimer>

namespace OpenDaw {

class PianoRollEditor;
class AudioClipEditor;
class SheetMusicView;
class VideoPlayerWidget;

class ShortcutFilter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(OpenDawApplication& app, QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    bool maybeSaveBeforeAction();
    void updateWindowTitle();
    void updateStatusBar();
    void createMenus();
    void createToolBar();
    void createDocks();
    void createStatusBar();
    void applyGlobalStyle();

    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onExportAudio();
    void onAddTrack();
    void onAddMidiTrack();
    void onRemoveTrack();
    void onSplitClipAtPlayhead();
    void onScanVstPlugins();
    void onEffectInsertRequested(te::AudioTrack* track, int slotIndex);
    void onInstrumentSelectRequested(te::AudioTrack* track);
    void onMidiClipDoubleClicked(te::MidiClip* clip);
    void onAudioClipDoubleClicked(te::WaveAudioClip* clip);

    OpenDawApplication& app_;
    EditManager& editMgr_;

    TransportBar* transportBar_       = nullptr;
    TimelineView* timelineView_       = nullptr;
    MixerView*    mixerView_          = nullptr;
    EffectChainWidget* effectChain_   = nullptr;
    FileBrowserPanel*  fileBrowser_   = nullptr;
    PianoRollEditor*   pianoRoll_     = nullptr;
    AudioClipEditor*   audioClipEditor_ = nullptr;
    RoutingView*       routingView_   = nullptr;
    SheetMusicView*    sheetMusicView_ = nullptr;
    AiChatWidget*      aiChatWidget_  = nullptr;
    AiQuickPrompt*     aiQuickPrompt_ = nullptr;
    VideoPlayerWidget* videoPlayer_   = nullptr;

    QDockWidget* mixerDock_           = nullptr;
    QDockWidget* effectsDock_         = nullptr;
    QDockWidget* browserDock_         = nullptr;
    QDockWidget* pianoRollDock_       = nullptr;
    QDockWidget* audioClipDock_      = nullptr;
    QDockWidget* routingDock_         = nullptr;
    QDockWidget* sheetMusicDock_     = nullptr;
    QDockWidget* videoPlayerDock_    = nullptr;
    QDockWidget* aiDock_              = nullptr;

    QToolBar* transportToolBar_       = nullptr;
    QToolBar* mainToolBar_            = nullptr;
    QAction* splitClipAction_         = nullptr;

    QLabel* sampleRateLabel_          = nullptr;
    QLabel* bufferLabel_              = nullptr;
    QLabel* cpuLabel_                 = nullptr;
    QTimer* statusBarTimer_           = nullptr;
};

} // namespace OpenDaw
