#pragma once

#include "AudioEngine.h"
#include <atomic>
#include <tracktion_engine/tracktion_engine.h>
#include <QObject>
#include <QColor>
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

enum class ExportFormat { WAV, FLAC, OGG, MP3 };

struct ExportSettings {
    juce::File destFile;
    double sampleRate = 44100.0;
    int bitDepth = 24;
    bool normalize = false;
    ExportFormat format = ExportFormat::WAV;
    int oggQuality = 5;
    int mp3Bitrate = 320;
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
    void removeClip(te::Clip* clip);

    // Track colours (headers, clips, mixer): a slot in the current theme's palette.
    QColor getTrackColour(te::Track* track) const;
    void setTrackColourSlot(te::Track& track, int slot);

    // Piano hand split. Notes of every MIDI clip on a track, in a stable order
    // (clip order, then sequence order) shared by the analysis and the split.
    struct TrackNoteRef {
        te::MidiClip* clip = nullptr;
        te::MidiNote* note = nullptr;
    };
    std::vector<TrackNoteRef> getTrackMidiNotes(te::AudioTrack* track) const;
    // Duplicates `track` just below itself (same instrument, effects and mix),
    // then keeps the right-hand notes on the original and the left-hand notes on
    // the copy. isLeftHand is indexed like getTrackMidiNotes(). One undo step.
    // Returns the left-hand track, or nullptr if the notes no longer match.
    te::AudioTrack* splitTrackByHands(te::AudioTrack& track,
                                      const std::vector<bool>& isLeftHand);

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
    // Direct outs: a track can skip Master and play straight out of another
    // hardware output (a cue mix, or one output of a multi-output interface).
    struct OutputDeviceInfo {
        juce::String id;
        QString name;
        bool isMain = false;   // the device Master plays through
    };
    QList<OutputDeviceInfo> getAudioOutputDevices() const;
    void setTrackOutputToDevice(te::AudioTrack& track, const juce::String& deviceID);
    // Empty unless the track goes straight to a device other than Master's.
    juce::String getTrackDirectOutputID(te::AudioTrack* track) const;
    // Every place a track's output can go, for menus and combo boxes. Keys are
    // "master", "none", "track:<id>" or "device:<id>".
    struct OutputChoice {
        QString label;
        QString key;
    };
    QList<OutputChoice> getOutputChoices(te::AudioTrack* track) const;
    QString currentOutputKey(te::AudioTrack* track) const;
    void setTrackOutputByKey(te::AudioTrack& track, const QString& key);

    // Bus tracks
    te::AudioTrack* addBusTrack();
    bool isBusTrack(te::AudioTrack* track) const;
    juce::Array<te::AudioTrack*> getBusTracks() const;
    juce::Array<te::AudioTrack*> getNonBusAudioTracks() const;
    // A bus named after `effectName` with that effect on it, ready to take sends.
    te::AudioTrack* addFxBus(const QString& effectName);

    // Aux sends: a copy of a track's signal, at its own level, into a bus, while
    // the track's output keeps going where it goes. Post-fader sends follow the
    // track's fader; pre-fader sends ignore it.
    struct SendInfo {
        te::AudioTrack* bus = nullptr;
        te::AuxSendPlugin* plugin = nullptr;
        float gainDb = 0.0f;
        bool preFader = false;
        bool muted = false;
        bool phaseInverted = false;
    };
    std::vector<SendInfo> getTrackSends(te::AudioTrack* track) const;
    bool canSendTo(te::AudioTrack* src, te::AudioTrack* bus) const;
    te::AuxSendPlugin* addSend(te::AudioTrack& src, te::AudioTrack& bus,
                               float gainDb = -6.0f, bool preFader = false);
    void removeSend(te::AudioTrack& src, te::AudioTrack& bus);
    void setSendGainDb(te::AudioTrack& src, te::AudioTrack& bus, float gainDb);
    void setSendPreFader(te::AudioTrack& src, te::AudioTrack& bus, bool preFader);
    void setSendMuted(te::AudioTrack& src, te::AudioTrack& bus, bool muted);
    void setSendPhaseInverted(te::AudioTrack& src, te::AudioTrack& bus, bool inverted);
    // Plugins the app manages itself and never lists as effects.
    static bool isInternalPlugin(te::Plugin* plugin);
    // Where a new effect goes: after the instrument and other effects, before
    // the fader, so the fader sets the level without changing the effect's input.
    static int effectInsertIndex(te::PluginList& list);

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

    // Set while playback has looping switched off until the playhead reaches the
    // loop start. Kept here, not in TransportBar, so a theme change's window
    // rebuild does not lose it.
    bool& loopActivationDeferred() { return loopActivationDeferred_; }
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
    // A send level or pre/post setting changed; cheaper than routingChanged.
    void sendChanged(te::AudioTrack* src, te::AudioTrack* bus);
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
    void assignMissingTrackColours();
    void enableAllWaveInputDevices();
    void clearTrackInputInternal(te::AudioTrack& track);

    void saveInputDisplayNames();
    void loadInputDisplayNames();
    void markDirtyAndNotify();

    void unfreezeAllTracks();
    void cleanupFreezeState(te::AudioTrack& track);
    void normalizePluginOrder();
    // Audio-flow edges out of a track: its output bus and the buses it sends to.
    std::vector<te::AudioTrack*> downstreamTracks(te::AudioTrack* track) const;
    int auxBusNumberFor(te::AudioTrack& bus);
    te::AudioTrack* busForAuxNumber(int busNumber) const;
    te::AuxSendPlugin* findSend(te::AudioTrack& src, te::AudioTrack& bus) const;
    static juce::String sanitizeForFilename(const juce::String& name);
    void performAutosave();
    QString autosaveFileId() const;

    AudioEngine& audioEngine_;
    std::unique_ptr<te::Edit> edit_;
    juce::File currentFile_;
    std::unordered_set<uint64_t> midiTrackIds_;
    std::map<std::string, QString> inputDisplayNames_;
    std::atomic<bool> renderInProgress_ { false };   // set from the export worker thread
    bool loopActivationDeferred_ = false;
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
