#pragma once

#include "AudioEngine.h"
#include <QObject>
#include <QThread>

namespace freedaw {

class PluginScanWorker : public QObject {
    Q_OBJECT
public:
    explicit PluginScanWorker(te::Engine& engine);

public slots:
    void doScan();

signals:
    void scanProgress(const QString& pluginName, int current, int total);
    void scanFinished();

private:
    te::Engine& engine_;
};

class PluginScanner : public QObject {
    Q_OBJECT

public:
    explicit PluginScanner(AudioEngine& engine, QObject* parent = nullptr);
    ~PluginScanner() override;

    void startScan();
    bool isScanning() const { return scanning_; }
    juce::KnownPluginList& getPluginList();

    void loadCachedList();
    void saveCachedList();
    juce::File getCacheFile() const;

signals:
    void scanProgress(const QString& pluginName, int current, int total);
    void scanFinished();

private:
    AudioEngine& audioEngine_;
    QThread workerThread_;
    bool scanning_ = false;
};

} // namespace freedaw
