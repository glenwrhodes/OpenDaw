#include "PluginScanner.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace freedaw {

PluginScanWorker::PluginScanWorker(te::Engine& engine)
    : engine_(engine)
{
}

void PluginScanWorker::doScan()
{
    auto& kpl = engine_.getPluginManager().knownPluginList;
    juce::VST3PluginFormat vst3;

    auto defaultPaths = vst3.getDefaultLocationsToSearch();
    auto filesToScan = vst3.searchPathsForPlugins(defaultPaths, true, true);

    int total = filesToScan.size();
    for (int i = 0; i < total; ++i) {
        juce::OwnedArray<juce::PluginDescription> descriptions;
        vst3.findAllTypesForFile(descriptions, filesToScan[i]);

        for (auto* desc : descriptions) {
            kpl.addType(*desc);
            emit scanProgress(QString::fromStdString(desc->name.toStdString()),
                              i + 1, total);
        }
    }

    emit scanFinished();
}

PluginScanner::PluginScanner(AudioEngine& engine, QObject* parent)
    : QObject(parent), audioEngine_(engine)
{
    loadCachedList();
}

PluginScanner::~PluginScanner()
{
    workerThread_.quit();
    workerThread_.wait();
}

void PluginScanner::startScan()
{
    if (scanning_)
        return;

    scanning_ = true;

    auto* worker = new PluginScanWorker(audioEngine_.engine());
    worker->moveToThread(&workerThread_);

    connect(&workerThread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &PluginScanWorker::scanProgress,
            this, &PluginScanner::scanProgress);
    connect(worker, &PluginScanWorker::scanFinished, this, [this]() {
        scanning_ = false;
        saveCachedList();
        emit scanFinished();
    });

    workerThread_.start();
    QMetaObject::invokeMethod(worker, &PluginScanWorker::doScan);
}

juce::KnownPluginList& PluginScanner::getPluginList()
{
    return audioEngine_.engine().getPluginManager().knownPluginList;
}

juce::File PluginScanner::getCacheFile() const
{
    auto appData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    return appData.getChildFile("FreeDaw").getChildFile("plugin-cache.xml");
}

void PluginScanner::loadCachedList()
{
    auto cacheFile = getCacheFile();
    if (!cacheFile.existsAsFile()) return;

    if (auto xml = juce::XmlDocument::parse(cacheFile)) {
        getPluginList().recreateFromXml(*xml);
    }
}

void PluginScanner::saveCachedList()
{
    auto cacheFile = getCacheFile();
    cacheFile.getParentDirectory().createDirectory();

    if (auto xml = getPluginList().createXml()) {
        xml->writeTo(cacheFile);
    }
}

} // namespace freedaw
