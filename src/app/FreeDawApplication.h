#pragma once

#include "engine/AudioEngine.h"
#include "engine/EditManager.h"
#include "engine/PluginScanner.h"
#include "JuceQtBridge.h"
#include <QObject>
#include <memory>

namespace freedaw {

class MainWindow;

class FreeDawApplication : public QObject {
    Q_OBJECT

public:
    explicit FreeDawApplication(QObject* parent = nullptr);
    ~FreeDawApplication() override;

    bool initialize();
    void showMainWindow();

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

} // namespace freedaw
