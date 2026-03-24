#pragma once

#include "AudioEngine.h"
#include <tracktion_engine/tracktion_engine.h>
#include <QObject>
#include <QString>
#include <QList>
#include <QVector>
#include <QTimer>
#include <unordered_set>
#include <map>
#include <string>
#include <memory>
#include <functional>

namespace OpenDaw {

struct InputSource {
    juce::String deviceName;
    QString displayName;
};

enum class ExportFormat { WAV, FLAC, OGG };

struct ExportSettings {
    juce::File destFile;
    double sampleRate = 44100.0;
    int bitDepth = 24;
    bool normalize = false;
    ExportFormat format = ExportFormat::WAV;
    int oggQuality = 5;
};

class EditManager : public QObject {
    Q_OBJECT

public:
    explicit EditManager(AudioEngine& engine, QObject* parent = nullptr);
    ~EditManager() override;

    void newEdit();
    bool loadEdit(const juce::File& file);
    bool saveEdit();
    bool saveEditAs(const juce::File& file);

    te::Edit*      edit()      { return edit_.get(); }
    te::TransportControl& transport();

    te::AudioTrack* addAudioTrack();
    te::AudioTrack* addMidiTrack();
    void removeTrack(te::Track* track);
    int  trackCount() const;
    te::AudioTrack* getAudioTrack(int index) const;
    juce::Array<te::AudioTrack*> getAudioTracks() const;

    void addAudioClipToTrack(te::AudioTrack& track,
                             const juce::File& audioFile,
                             double startBeat);

    te::MidiClip* addMidiClipToTrack(te::AudioTrack& track,
                                     double startBeat, double lengthBeats);
    te::MidiClip* importMidiFileToTrack(te::AudioTrack& track,
                                        const juce::File& midiFile,
                                        double startBeat);

    bool isMidiTrack(te::AudioTrack* track) const;
    bool isTrackMono(te::AudioTrack* track) const;
    void setTrackMono(te::AudioTrack& track, bool mono);
    te::Plugin* getTrackInstrument(te::AudioTrack* track) const;
    void setTrackInstrument(te::AudioTrack& track,
                            const juce::PluginDescription& desc);
    void removeTrackInstrument(te::AudioTrack& track);

    void markAsMidiTrack(te::AudioTrack* track);

    void trimNotesToClipBounds(te::MidiClip& clip);
    bool isClipValid(te::Clip* clip) const;

    QList<InputSource> getAvailableInputSources() const;
    QList<InputSource> getAvailableMidiInputSources() const;
    void assignInputToTrack(te::AudioTrack& track, const juce::String& deviceName);
    void assignMidiInputToTrack(te::AudioTrack& track, const juce::String& deviceName);
    void clearTrackInput(te::AudioTrack& track);
    QString getTrackInputName(te::AudioTrack* track) const;
    void setTrackRecordEnabled(te::AudioTrack& track, bool enabled);
    bool isTrackRecordEnabled(te::AudioTrack* track) const;

    // Output routing
    bool wouldCreateCycle(te::AudioTrack* src, te::AudioTrack* dest) const;
    void setTrackOutputToMaster(te::AudioTrack& track);
    void setTrackOutputToTrack(te::AudioTrack& src, te::AudioTrack& dest);
    void clearTrackOutput(te::AudioTrack& track);
    te::AudioTrack* getTrackOutputDestination(te::AudioTrack* track) const;
    bool isTrackOutputDisconnected(te::AudioTrack* track) const;
    QString getTrackOutputName(te::AudioTrack* track) const;

    // Bus tracks
    te::AudioTrack* addBusTrack();
    bool isBusTrack(te::AudioTrack* track) const;
    juce::Array<te::AudioTrack*> getBusTracks() const;
    juce::Array<te::AudioTrack*> getNonBusAudioTracks() const;

    // Input device renaming (persisted in edit state)
    void setInputDisplayName(const juce::String& deviceName, const QString& customName);
    QString getInputDisplayName(const juce::String& deviceName) const;

    // Routing layout persistence
    void saveRoutingLayout(const QMap<QString, QPointF>& positions);
    QMap<QString, QPointF> loadRoutingLayout() const;

    // Track display order (decoupled from engine order)
    void saveTrackDisplayOrder(const QVector<te::EditItemID>& order);
    QVector<te::EditItemID> loadTrackDisplayOrder() const;
    juce::Array<te::AudioTrack*> getAudioTracksInDisplayOrder();

    // Track folders (organizational grouping)
    struct FolderInfo {
        int id = 0;
        QString name;
        bool collapsed = false;
    };

    struct DisplayItem {
        enum Type { Folder, Track };
        Type type = Track;
        te::AudioTrack* track = nullptr;
        int folderId = 0;
        int parentFolderId = 0;
    };

    int addFolder(const QString& name, te::AudioTrack* insertBefore = nullptr);
    void removeFolder(int folderId);
    void renameFolder(int folderId, const QString& name);
    void setFolderCollapsed(int folderId, bool collapsed);
    bool isFolderCollapsed(int folderId) const;
    QString getFolderName(int folderId) const;
    void moveTrackToFolder(te::AudioTrack* track, int folderId);
    int getTrackFolderId(te::AudioTrack* track) const;
    QVector<FolderInfo> getFolders() const;
    QVector<DisplayItem> getDisplayItems();
    void saveDisplayItems(const QVector<DisplayItem>& items);
    void saveRawDisplayOrder(const QStringList& entries);
    QStringList loadRawDisplayOrder() const;

    void midiPanic();

    void suspendEngine();
    void resumeEngine();
    bool isEngineSuspended() const { return engineSuspended_; }

    void undo();
    void redo();

    double getBpm() const;
    void   setBpm(double bpm);
    int    getTimeSigNumerator() const;
    int    getTimeSigDenominator() const;
    void   setTimeSignature(int num, int den);

    juce::File currentFile() const { return currentFile_; }
    bool hasUnsavedChanges() const { return hasUnsavedChanges_; }

    void startAutosave();
    void stopAutosave();
    void clearAutosave();
    static QString autosaveDir();
    struct RecoveryInfo {
        QString autosavePath;
        QString originalPath;
        QString timestamp;
    };
    static QList<RecoveryInfo> findRecoveryFiles();

    // Linked MIDI channel clips
    te::MidiClip* addLinkedMidiChannel(te::AudioTrack& track,
                                        te::MidiClip& referenceClip,
                                        int midiChannel,
                                        const QString& displayName = {});
    std::vector<te::MidiClip*> getLinkedMidiClips(te::AudioTrack* track,
                                                   te::MidiClip* referenceClip) const;
    bool isLinkedSecondary(te::MidiClip* clip) const;
    int linkedChannelCount(te::MidiClip* clip) const;
    void propagateClipPosition(te::MidiClip& primary);
    void setChannelName(te::MidiClip& clip, const QString& name);
    QString getChannelName(te::MidiClip* clip) const;

    // Automation parameter access
    QVector<te::AutomatableParameter*> getAutomatableParamsForTrack(te::AudioTrack* track) const;
    te::AutomatableParameter* getVolumeParam(te::AudioTrack* track) const;
    te::AutomatableParameter* getPanParam(te::AudioTrack* track) const;

    // Markers
    void ensureMarkerTrack();
    te::MarkerClip* addMarker(const QString& name, tracktion::TimePosition position);
    void removeMarker(te::MarkerClip* marker);
    juce::Array<te::MarkerClip*> getMarkers() const;

    // Export / Freeze / Bounce
    bool isRenderInProgress() const { return renderInProgress_; }
    bool exportMix(const ExportSettings& settings,
                   std::function<void(float)> progressCallback);
    bool isTrackFrozen(te::AudioTrack* track) const;
    void freezeTrack(te::AudioTrack& track);
    void unfreezeTrack(te::AudioTrack& track);
    bool bounceTrackToAudio(te::AudioTrack& track);

signals:
    void trackFreezeStateChanged(te::AudioTrack* track);
    void aboutToChangeEdit();
    void editChanged();
    void tracksChanged();
    void transportStateChanged();
    void routingChanged();
    void midiClipDoubleClicked(te::MidiClip* clip);
    void midiClipSelected(te::MidiClip* clip);
    void midiClipModified(te::MidiClip* clip);
    void audioClipDoubleClicked(te::WaveAudioClip* clip);
    void audioClipSelected(te::WaveAudioClip* clip);
    void audioClipModified(te::WaveAudioClip* clip);

private:
    void teardownCurrentEdit();
    void createDefaultEdit();
    void ensureLevelMetersOnAllTracks();
    void enableAllWaveInputDevices();
    void clearTrackInputInternal(te::AudioTrack& track);

    void saveInputDisplayNames();
    void loadInputDisplayNames();
    void markDirtyAndNotify();

    void unfreezeAllTracks();
    void cleanupFreezeState(te::AudioTrack& track);
    static juce::String sanitizeForFilename(const juce::String& name);
    void performAutosave();
    QString autosaveFileId() const;

    AudioEngine& audioEngine_;
    std::unique_ptr<te::Edit> edit_;
    juce::File currentFile_;
    std::unordered_set<uint64_t> midiTrackIds_;
    std::map<std::string, QString> inputDisplayNames_;
    bool renderInProgress_ = false;
    bool hasUnsavedChanges_ = false;
    bool engineSuspended_ = false;
    QTimer autosaveTimer_;
    struct FreezeState {
        juce::File freezeFile;
        std::vector<te::EditItemID> mutedClipIds;
        std::vector<te::EditItemID> disabledPluginIds;
    };
    std::map<uint64_t, FreezeState> frozenTracks_;
};

} // namespace OpenDaw
