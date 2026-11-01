#pragma once

#include "AudioEngine.h"
#include <array>
#include <atomic>
#include <vector>

namespace OpenDaw {

// The looper's audio side. Everything here runs on the audio threads, except
// where a comment says otherwise; the message thread drives it with commands
// and reads its state through atomics (see LooperEngine).

// Live input for the looper: the selected input's audio, kept in a ring so a
// pad can read the samples that match the block it is rendering, and look a
// few seconds back when a recording starts before its track is running.
class LooperInputHub : public te::InputDeviceInstance::Consumer {
public:
    static constexpr int kRingSize = 1 << 18;   // about 5 s at 48 kHz

    LooperInputHub();
    void acceptInputBuffer(choc::buffer::ChannelArrayView<float> block) override;

    // Total samples received so far. Samples [written - kRingSize/2, written)
    // are safe to read.
    int64_t written() const { return written_.load(std::memory_order_acquire); }
    float sample(int channel, int64_t index) const;
    bool isStereo() const { return stereo_.load(std::memory_order_relaxed); }
    // Highest input level since the last call (message thread).
    float takePeak() { return peak_.exchange(0.0f); }

private:
    std::vector<float> ring_[2];
    std::atomic<int64_t> written_ { 0 };
    std::atomic<bool> stereo_ { false };
    std::atomic<float> peak_ { 0.0f };
};

// Plays the live input out of the speakers (through the global output
// processor), so you hear yourself whether or not a pad is recording.
class LooperMonitor {
public:
    explicit LooperMonitor(LooperInputHub& hub) : hub_(hub) {}
    void addTo(juce::AudioBuffer<float>& buffer);   // audio device thread
    std::atomic<bool> enabled { true };
    std::atomic<bool> active { false };   // set while the looper has an input
private:
    LooperInputHub& hub_;
    int64_t readPos_ = 0;
};

enum class PadState : int { Empty, Recording, Playing, Stopped, Overdubbing };

// A command for one pad. Times are edit seconds; a time in the past means now.
struct LooperCommand {
    enum Type { Record, SetRecordEnd, Play, Stop, Overdub, SetOverdubEnd,
                SetBuffer, Clear, CancelPending };
    Type type = Stop;
    double at = -1.0e9;
    double endAt = 1.0e300;
    juce::AudioBuffer<float>* buffer = nullptr;
    int64_t length = 0;      // loop length in samples (SetBuffer)
    int64_t capacity = 0;    // usable samples in buffer (Record)
    double anchor = 0.0;     // edit time of the loop's first sample (SetBuffer)
    bool oneShot = false;    // Play: once from the start, then stop
    uint32_t seq = 0;
};

// Something that happened on the audio thread, for the message thread.
struct LooperEvent {
    enum Type { Audible, Silent, RecordDone, RecordAborted, OverdubDone };
    Type type = Audible;
    double time = 0.0;
    int64_t length = 0;
};

// One looper pad: its loop, recording and overdub capture, phase-locked
// playback, and the print stream that becomes clips on the timeline.
class LooperPadProcessor {
public:
    LooperPadProcessor(int index, LooperInputHub& hub);

    // Message thread.
    bool post(const LooperCommand& cmd);
    bool popEvent(LooperEvent& ev);
    // The print writer. Setting nullptr waits until the audio thread lets go.
    void setPrintWriter(juce::AudioFormatWriter::ThreadedWriter* writer);
    void setRoundTripSeconds(double s) { roundTrip_.store(s); }
    // Waits until no block is being processed (call after taking a buffer out of use).
    void waitUntilIdle() const;

    // Audio thread: renders one block of the pad's track.
    void process(const te::PluginRenderContext& fc);
    void prepare(double sampleRate);

    // Read by the message thread.
    std::atomic<int> state { int(PadState::Empty) };
    std::atomic<double> phase { 0.0 };          // 0..1 through the loop
    std::atomic<double> lastTime { 0.0 };       // end of the last block, edit seconds
    std::atomic<double> streamOrigin { 0.0 };   // edit time of the print stream's first sample
    std::atomic<bool> streamStarted { false };
    std::atomic<uint32_t> processedSeq { 0 };
    std::atomic<float> peak { 0.0f };

    int index() const { return index_; }

private:
    void applyNow(const LooperCommand& cmd, double t);
    void startCommand(const LooperCommand& cmd, double t);
    void pushEvent(LooperEvent::Type type, double time, int64_t length = 0);
    void stopAll(double t);
    void capture(double t0, double t1, double sr, int64_t hubAt0);
    float input(int ch, int64_t hubIndex, int64_t written) const;

    const int index_;
    LooperInputHub& hub_;

    juce::AbstractFifo commandFifo_ { 64 };
    std::array<LooperCommand, 64> commands_;
    juce::AbstractFifo eventFifo_ { 64 };
    std::array<LooperEvent, 64> events_;
    std::array<LooperCommand, 16> pending_;   // scheduled, in time order
    int pendingCount_ = 0;

    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> writer_ { nullptr };
    std::atomic<bool> busy_ { false };
    std::atomic<double> roundTrip_ { 0.0 };

    // Audio thread state.
    double sampleRate_ = 48000.0;
    PadState state_ = PadState::Empty;
    juce::AudioBuffer<float>* loop_ = nullptr;
    int64_t length_ = 0;
    double anchor_ = 0.0;
    bool oneShot_ = false;
    double oneShotStart_ = 0.0;
    double gain_ = 0.0;            // de-click ramp

    juce::AudioBuffer<float>* recBuffer_ = nullptr;
    int64_t recCapacity_ = 0;
    double recStart_ = 0.0, recEnd_ = 1.0e300;
    int64_t recCaptured_ = 0;
    bool recCapturing_ = false;

    double odStart_ = 0.0, odEnd_ = 1.0e300;
    int64_t odCaptured_ = 0;
    bool odCapturing_ = false;

    int64_t hubRead_ = 0;
    int64_t playIdx_ = 0;
    double lastBlockStart_ = -1.0;
    std::vector<float> delay_[2];
    int delayPos_ = 0;
    juce::AudioBuffer<float> print_;
    bool wasPlaying_ = false;
};

// The pad processor for a looper plugin's pad index (owned by LooperEngine).
LooperPadProcessor* findLooperPad(int index);

// The looper's plugin, first on each looper track. It renders its pad.
class LooperPlugin : public te::Plugin {
public:
    explicit LooperPlugin(te::PluginCreationInfo info);
    ~LooperPlugin() override;

    static const char* getPluginName() { return "Looper"; }
    static const char* xmlTypeName;
    static juce::ValueTree create(int padIndex);
    static const juce::Identifier padProperty;

    juce::String getName() const override { return "Looper"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return "Looper"; }
    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void applyToBuffer(const te::PluginRenderContext& fc) override;
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    bool canBeDisabled() override { return false; }

    int padIndex() const;

private:
    LooperPadProcessor* pad_ = nullptr;
};

} // namespace OpenDaw
