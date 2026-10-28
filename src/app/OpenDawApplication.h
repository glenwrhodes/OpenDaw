#pragma once

#include "engine/AudioEngine.h"
#include "engine/EditManager.h"
#include "engine/PluginScanner.h"
#include "JuceQtBridge.h"
#include <QObject>
#include <QWidget>
#include <memory>

namespace OpenDaw {

class MainWindow;

class OpenDawApplication : public QObject {
    Q_OBJECT

public:
    explicit OpenDawApplication(QObject* parent = nullptr);
    ~OpenDawApplication() override;

    bool initialize();
    void checkRecovery(QWidget* splashToHide = nullptr);
    void showMainWindow();
    // Recreates the main window (e.g. after a theme change). The project, engine
    // and playback live outside it and carry on. Must not be called from inside
    // the old window's event handling; queue it.
    void rebuildMainWindow();

    AudioEngine&  audioEngine()  { return *audioEngine_; }
    EditManager&  editManager()  { return *editManager_; }
    PluginScanner& pluginScanner() { return *pluginScanner_; }
    MainWindow*   mainWindow()   { return mainWindow_.get(); }

private:
    std::unique_ptr<JuceQtBridge>   bridge_;
    std::unique_ptr<AudioEngine>    audioEngine_;
    std::unique_ptr<EditManager>    editManager_;
    std::unique_ptr<PluginScanner>  pluginScanner_;
    std::unique_ptr<MainWindow>     mainWindow_;
};

} // namespace OpenDaw
