#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace OpenDaw {

// Loudness to ITU-R BS.1770-4 / EBU R128: K-weighted momentary (400 ms),
// short-term (3 s) and gated integrated loudness in LUFS, loudness range
// (LRA) in LU, and true peak (4x oversampled) in dBTP. Stereo; a mono
// input counts as both channels.
class LoudnessMeter {
public:
    static constexpr double kSilence = -100.0;

    void prepare(double sampleRate);
    void reset();
    void process(const float* const* channels, int numChannels, int numSamples);

    double momentary() const { return momentary_; }
    double shortTerm() const { return shortTerm_; }
    double truePeakDb() const;
    double samplePeakDb() const;
    double maxMomentary() const { return maxMomentary_; }
    double maxShortTerm() const { return maxShortTerm_; }

    // Every 100 ms: the 400 ms block's loudness, and the short-term loudness
    // (kSilence until 3 s have been measured). Gating needs them all; the
    // caller drains them (see LoudnessGate). Draining doesn't allocate, so it
    // is safe on the audio thread.
    struct Step {
        float block;
        float shortTerm;
    };
    template <typename Fn> void drainSteps(Fn&& fn)
    {
        for (const auto& s : steps_) fn(s);
        steps_.clear();
    }

    struct FileResult {
        bool ok = false;
        double integrated = kSilence;
        double loudnessRange = 0.0;
        double truePeakDb = kSilence;
        double maxShortTerm = kSilence;
        double seconds = 0.0;
    };
    // Measures a whole audio file.
    static FileResult analyzeFile(const juce::File& file);

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        double process(double x) {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    struct Channel {
        Biquad shelf, highPass;
        std::array<float, 12> history {};   // for the true-peak interpolator
        int historyPos = 0;
    };

    void finishSubBlock();
    static double toLufs(double meanSquare);

    double sampleRate_ = 48000.0;
    std::array<Channel, 2> ch_;
    int subBlockLength_ = 4800;     // 100 ms
    int subBlockCount_ = 0;
    double subBlockSum_ = 0.0;      // summed over both channels
    std::vector<double> subBlocks_; // ring of the last 30 sub-block mean squares
    int subBlockWrite_ = 0;
    int subBlocksFilled_ = 0;

    double momentary_ = kSilence;
    double shortTerm_ = kSilence;
    double maxMomentary_ = kSilence;
    double maxShortTerm_ = kSilence;
    float truePeak_ = 0.0f;
    float samplePeak_ = 0.0f;
    std::vector<Step> steps_;
};

// Integrated loudness and LRA from a stream of block and short-term values.
class LoudnessGate {
public:
    void add(const LoudnessMeter::Step& step);
    void reset();
    double integrated() const;
    double loudnessRange() const;
    double measuredSeconds() const { return blocks_.size() * 0.1; }

private:
    std::vector<float> blocks_;
    std::vector<float> shortTerm_;
};

// Measures what the audio device plays (the first two output channels, after
// everything including Master). Runs on the audio thread; the readings are
// read from the message thread.
class LooperMonitor;

class LoudnessTap : public juce::AudioProcessor {
public:
    LoudnessTap();

    // Mixes the looper's live input into the output (before it is measured).
    // Setting nullptr waits until the audio thread has let go of the old one.
    void setMonitor(LooperMonitor* monitor);

    // Message thread: current readings, integrated values over everything
    // played since the last reset.
    struct Readings {
        double momentary = LoudnessMeter::kSilence;
        double shortTerm = LoudnessMeter::kSilence;
        double integrated = LoudnessMeter::kSilence;
        double loudnessRange = 0.0;
        double truePeakDb = LoudnessMeter::kSilence;
        double maxShortTerm = LoudnessMeter::kSilence;
        double seconds = 0.0;
    };
    Readings readings();
    void requestReset();

    const juce::String getName() const override { return "OpenDaw Loudness"; }
    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    LoudnessMeter meter_;
    std::atomic<LooperMonitor*> monitor_ { nullptr };
    std::atomic<bool> inBlock_ { false };
    std::atomic<bool> resetRequested_ { false };
    std::atomic<double> momentary_ { LoudnessMeter::kSilence };
    std::atomic<double> shortTerm_ { LoudnessMeter::kSilence };
    std::atomic<double> truePeak_ { LoudnessMeter::kSilence };
    std::atomic<double> maxShortTerm_ { LoudnessMeter::kSilence };

    // Steps handed from the audio thread.
    juce::AbstractFifo fifo_ { 4096 };
    std::vector<LoudnessMeter::Step> fifoSteps_ = std::vector<LoudnessMeter::Step>(4096);
    LoudnessGate gate_;   // message thread only
};

} // namespace OpenDaw
