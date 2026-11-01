#pragma once

#include "LooperPlugin.h"
#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>
#include <array>
#include <memory>
#include <vector>

namespace OpenDaw {

class EditManager;

// A live looper in the style of a looper pedal or app: a grid of pads, each a
// loop on its own track. Tap an empty pad to record, tap again to close the
// loop, then tap to stop and start it. Starts and stops land on the beat, bar
// or loop. The first loop can set the tempo. Overdubs add layers you can undo.
//
// Everything the pads play is printed to the timeline: while the transport
// runs, each pad's output is written to a file, and when it stops the parts
// where the pad was sounding become clips on its track. So the performance,
// including when each loop came in and out, is in the song afterwards.
//
// Message thread only. The audio runs in LooperPadProcessor.
class LooperEngine : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxPads = 16;
    enum class Quantize { Beat = 0, Bar = 1, Loop = 2 };
    // An action waiting for its beat, bar or loop.
    enum class Queued { None, Record, EndRecord, Play, Stop };

    struct PadView {
        PadState state = PadState::Empty;
        Queued queued = Queued::None;
        double phase = 0.0;        // 0..1 through the loop, or the recording
        double bars = 0.0;         // loop length
        double beats = 0.0;
        int undoDepth = 0;
        int redoDepth = 0;
        bool oneShot = false;
        int group = 0;             // 0: none; pads in a group play one at a time
        bool overdubbing = false;
        bool hasTrack = false;
        QString name;
        QColor colour;
    };

    explicit LooperEngine(EditManager& editMgr);
    ~LooperEngine() override;

    // EditManager calls these around each edit's lifetime and saves.
    void attach();
    void detach();
    void saveState(const juce::File& editFile);

    int padCount() const { return padCount_; }
    void setPadCount(int count);
    PadView padView(int index) const;
    int lastPad() const { return lastPad_; }

    // Record / close the loop / stop / play, depending on the pad's state.
    void tapPad(int index);
    // Starts an overdub (on a pad with a loop) or ends the one running.
    void overdubPad(int index);
    void undoPad(int index);
    void redoPad(int index);
    void clearPad(int index);
    void setPadOneShot(int index, bool oneShot);
    void setPadGroup(int index, int group);

    void play();          // starts the transport, bringing back the loops that were on
    void stop();          // stops it, which prints the performance
    void stopAllPads();
    void clearAll();
    bool isRunning() const;

    // Settings (kept between sessions).
    QString inputDevice() const { return input_; }
    void setInputDevice(const QString& deviceName);
    bool monitor() const { return monitorOn_; }
    void setMonitor(bool on);
    Quantize quantize() const { return quantize_; }
    void setQuantize(Quantize q);
    int recordBars() const { return recordBars_; }   // 0: until you tap
    void setRecordBars(int bars);
    bool firstLoopSetsTempo() const { return freeTempo_; }
    void setFirstLoopSetsTempo(bool on);
    bool countIn() const { return countIn_; }
    void setCountIn(bool on);
    bool click() const;
    void setClick(bool on);
    float takeInputPeak() { return hub_.takePeak(); }

    bool hasMasterLoop() const { return master_; }
    QString statusText() const;
    // "2 bars", or "2 beats" for a loop shorter than a bar.
    static QString lengthText(double bars, double beats);

    // What is being printed right now, for the timeline to draw.
    struct LivePrint {
        te::EditItemID track;
        std::shared_ptr<juce::AudioThumbnail> thumbnail;
        double origin = 0.0;   // edit time of the thumbnail's first sample
        std::vector<std::pair<double, double>> segments;
    };
    std::vector<LivePrint> livePrints() const;

    LooperPadProcessor* processor(int index);

signals:
    void changed();   // pads, settings or the loop clock changed
    void printed();   // a performance was added to the timeline

private:
    using BufferPtr = std::shared_ptr<juce::AudioBuffer<float>>;
    struct Pad {
        te::EditItemID track;
        std::vector<BufferPtr> versions;   // undo history; back() is playing
        std::vector<BufferPtr> redo;
        BufferPtr recording;               // being recorded into
        BufferPtr overdubTarget;           // the overdub's new version
        int64_t length = 0;                // samples
        double anchor = 0.0;               // edit time of the loop's first sample
        bool oneShot = false;
        int group = 0;
        Queued queued = Queued::None;
        double queuedAt = 0.0;
        bool recordingOpen = false;        // waiting for the tap that closes it
        bool firstLoop = false;            // free length; sets the clock
        double recStart = 0.0;
        bool overdubbing = false;
        bool undoAfterOverdub = false;
        bool wasPlaying = false;
        bool dirty = false;                // loop changed since it was saved
        // Print stream for the current session.
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        std::shared_ptr<juce::AudioThumbnail> thumbnail;
        juce::File printFile;
        std::vector<std::pair<double, double>> segments;
        double openSince = -1.0;
    };
    struct PrintJob {
        te::EditItemID track;
        juce::File file;
        double origin = 0.0;
        double end = 0.0;
        std::vector<std::pair<double, double>> segments;
        QString name;
    };

    void poll();
    void handleEvent(int index, const LooperEvent& ev);
    void post(int index, LooperCommand cmd);
    void bury(int index, BufferPtr buffer);
    te::AudioTrack* padTrack(int index) const;
    te::AudioTrack* ensureTrack(int index);
    void bindInput();
    void unbindInput();
    double roundTrip() const;
    double sampleRate() const;

    // Timing. Times are edit seconds.
    double now() const;
    double beatsPerBar() const;
    double toBeats(double t) const;
    double toTime(double beats) const;
    double boundaryAfter(double t, Quantize q) const;   // at or after t
    double boundaryBefore(double t, Quantize q) const;  // at or before t
    double actionTime(double t, bool allowLate) const;
    double recordingEnd(double t, double start) const;
    bool startTransport(bool forRecording, bool freeStart, double& at);

    void startRecording(int index);
    void finishRecording(int index);
    void playPad(int index, double at = -1.0);
    void stopPad(int index, double at = -1.0);
    void endOverdub(int index);
    void establishClock(int index);
    void resetClockIfIdle();

    void ensureWriter(int index);
    void endSession();
    void printPerformance(std::vector<PrintJob> jobs);
    juce::File printFolder() const;

    EditManager& em_;
    te::Edit* edit_ = nullptr;
    LooperInputHub hub_;
    LooperMonitor monitor_ { hub_ };
    std::array<std::unique_ptr<LooperPadProcessor>, kMaxPads> processors_;
    std::array<Pad, kMaxPads> pads_;
    int padCount_ = 8;
    int lastPad_ = -1;

    QString input_;
    bool monitorOn_ = true;
    Quantize quantize_ = Quantize::Bar;
    int recordBars_ = 0;
    bool freeTempo_ = true;
    bool countIn_ = true;

    bool master_ = false;
    double masterAnchor_ = 0.0;
    double masterSecs_ = 0.0;

    bool sessionActive_ = false;
    bool monitorAttached_ = false;
    double clickOffAt_ = -1.0;    // the count-in turned the click on until then
    te::InputDeviceInstance* boundInput_ = nullptr;
    int pollCount_ = 0;
    uint32_t seq_ = 0;
    std::array<uint32_t, kMaxPads> padSeq_ {};   // last command posted to each pad
    struct Buried { int pad; uint32_t seq; uint32_t since; BufferPtr buffer; };
    std::vector<Buried> graveyard_;

    juce::TimeSliceThread writerThread_ { "Looper print" };
    QTimer timer_;
};

} // namespace OpenDaw
