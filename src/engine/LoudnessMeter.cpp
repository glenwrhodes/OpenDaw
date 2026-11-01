#include "LoudnessMeter.h"
#include "LooperPlugin.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace OpenDaw {

namespace {
constexpr double kAbsoluteGate = -70.0;

// 4x interpolation for true peak: 48 windowed-sinc taps, 12 per phase.
struct TruePeakFilter {
    std::array<std::array<float, 12>, 4> phase {};
    TruePeakFilter() {
        constexpr int taps = 48;
        constexpr double centre = (taps - 1) / 2.0;
        for (int p = 0; p < 4; ++p) {
            double sum = 0.0;
            for (int k = 0; k < 12; ++k) {
                const int i = p + 4 * k;
                const double x = (i - centre) / 4.0;
                const double sinc = std::abs(x) < 1e-9 ? 1.0
                    : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double window = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * (i + 0.5) / taps);
                phase[size_t(p)][size_t(k)] = float(sinc * window);
                sum += sinc * window;
            }
            for (auto& c : phase[size_t(p)]) c = float(c / sum);
        }
    }
};
const TruePeakFilter& truePeakFilter()
{
    static const TruePeakFilter f;
    return f;
}

double energyOf(double lufs) { return std::pow(10.0, (lufs + 0.691) / 10.0); }
}

double LoudnessMeter::toLufs(double meanSquare)
{
    return meanSquare > 1e-12 ? -0.691 + 10.0 * std::log10(meanSquare) : kSilence;
}

void LoudnessMeter::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double pi = juce::MathConstants<double>::pi;

    // K-weighting, stage 1: high shelf (+4 dB above ~1.5 kHz).
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(pi * f0 / sampleRate_);
        const double vh = std::pow(10.0, gainDb / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        for (auto& c : ch_) {
            c.shelf.b0 = (vh + vb * k / q + k * k) / a0;
            c.shelf.b1 = 2.0 * (k * k - vh) / a0;
            c.shelf.b2 = (vh - vb * k / q + k * k) / a0;
            c.shelf.a1 = 2.0 * (k * k - 1.0) / a0;
            c.shelf.a2 = (1.0 - k / q + k * k) / a0;
        }
    }
    // Stage 2: high pass (RLB weighting) at ~38 Hz.
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(pi * f0 / sampleRate_);
        const double a0 = 1.0 + k / q + k * k;
        for (auto& c : ch_) {
            c.highPass.b0 = 1.0;
            c.highPass.b1 = -2.0;
            c.highPass.b2 = 1.0;
            c.highPass.a1 = 2.0 * (k * k - 1.0) / a0;
            c.highPass.a2 = (1.0 - k / q + k * k) / a0;
        }
    }
    subBlockLength_ = std::max(1, int(std::round(sampleRate_ * 0.1)));
    reset();
}

void LoudnessMeter::reset()
{
    for (auto& c : ch_) {
        c.shelf.z1 = c.shelf.z2 = 0.0;
        c.highPass.z1 = c.highPass.z2 = 0.0;
        c.history.fill(0.0f);
        c.historyPos = 0;
    }
    subBlockCount_ = 0;
    subBlockSum_ = 0.0;
    subBlocks_.assign(30, 0.0);
    subBlockWrite_ = 0;
    subBlocksFilled_ = 0;
    momentary_ = shortTerm_ = maxMomentary_ = maxShortTerm_ = kSilence;
    truePeak_ = samplePeak_ = 0.0f;
    steps_.clear();
    steps_.reserve(256);
}

void LoudnessMeter::process(const float* const* channels, int numChannels, int numSamples)
{
    if (numChannels <= 0 || numSamples <= 0) return;
    const auto& tp = truePeakFilter();

    for (int i = 0; i < numSamples; ++i) {
        for (int c = 0; c < 2; ++c) {
            const float x = channels[std::min(c, numChannels - 1)][i];
            auto& chan = ch_[size_t(c)];

            const double weighted = chan.highPass.process(chan.shelf.process(x));
            subBlockSum_ += weighted * weighted;

            samplePeak_ = std::max(samplePeak_, std::abs(x));
            chan.history[size_t(chan.historyPos)] = x;
            for (int p = 0; p < 4; ++p) {
                float y = 0.0f;
                int idx = chan.historyPos;
                for (int k = 0; k < 12; ++k) {
                    y += tp.phase[size_t(p)][size_t(k)] * chan.history[size_t(idx)];
                    idx = idx == 0 ? 11 : idx - 1;
                }
                truePeak_ = std::max(truePeak_, std::abs(y));
            }
            chan.historyPos = (chan.historyPos + 1) % 12;
        }
        if (++subBlockCount_ >= subBlockLength_)
            finishSubBlock();
    }
}

void LoudnessMeter::finishSubBlock()
{
    // Channel weights are 1 for left and right, so the loudness uses the sum
    // of the channels' mean squares.
    subBlocks_[size_t(subBlockWrite_)] = subBlockSum_ / subBlockCount_;
    subBlockWrite_ = (subBlockWrite_ + 1) % 30;
    subBlocksFilled_ = std::min(30, subBlocksFilled_ + 1);
    subBlockSum_ = 0.0;
    subBlockCount_ = 0;

    auto meanOfLast = [this](int count) {
        double sum = 0.0;
        for (int j = 1; j <= count; ++j)
            sum += subBlocks_[size_t((subBlockWrite_ - j + 30) % 30)];
        return sum / count;
    };

    if (subBlocksFilled_ < 4) return;
    momentary_ = toLufs(meanOfLast(4));
    maxMomentary_ = std::max(maxMomentary_, momentary_);
    if (subBlocksFilled_ >= 30) {
        shortTerm_ = toLufs(meanOfLast(30));
        maxShortTerm_ = std::max(maxShortTerm_, shortTerm_);
    }
    // Capacity is reserved; a caller that never drains just stops recording.
    if (steps_.size() < steps_.capacity())
        steps_.push_back({ float(momentary_), subBlocksFilled_ >= 30 ? float(shortTerm_)
                                                                    : float(kSilence) });
}

double LoudnessMeter::truePeakDb() const
{
    return truePeak_ > 1e-9f ? 20.0 * std::log10(double(truePeak_)) : kSilence;
}

double LoudnessMeter::samplePeakDb() const
{
    return samplePeak_ > 1e-9f ? 20.0 * std::log10(double(samplePeak_)) : kSilence;
}

LoudnessMeter::FileResult LoudnessMeter::analyzeFile(const juce::File& file)
{
    FileResult result;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0.0) return result;

    LoudnessMeter meter;
    meter.prepare(reader->sampleRate);
    LoudnessGate gate;
    const int numChannels = int(std::min<unsigned int>(2, reader->numChannels));
    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer(std::max(1, numChannels), blockSize);

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += blockSize) {
        const int n = int(std::min<juce::int64>(blockSize, reader->lengthInSamples - pos));
        reader->read(&buffer, 0, n, pos, true, numChannels > 1);
        meter.process(buffer.getArrayOfReadPointers(), numChannels, n);
        meter.drainSteps([&gate](const Step& s) { gate.add(s); });
    }

    result.ok = true;
    result.integrated = gate.integrated();
    result.loudnessRange = gate.loudnessRange();
    result.truePeakDb = meter.truePeakDb();
    result.maxShortTerm = meter.maxShortTerm();
    result.seconds = reader->lengthInSamples / reader->sampleRate;
    return result;
}

// ── LoudnessGate ─────────────────────────────────────────────────────────────

void LoudnessGate::add(const LoudnessMeter::Step& step)
{
    blocks_.push_back(step.block);
    if (step.shortTerm > float(LoudnessMeter::kSilence))
        shortTerm_.push_back(step.shortTerm);
}

void LoudnessGate::reset()
{
    blocks_.clear();
    shortTerm_.clear();
}

double LoudnessGate::integrated() const
{
    // Absolute gate at -70 LUFS, then a relative gate 10 LU below the
    // loudness of what passed it.
    double sum = 0.0;
    size_t count = 0;
    for (float b : blocks_)
        if (b > kAbsoluteGate) { sum += energyOf(b); ++count; }
    if (count == 0) return LoudnessMeter::kSilence;
    const double relativeGate = -0.691 + 10.0 * std::log10(sum / count) - 10.0;

    sum = 0.0;
    count = 0;
    for (float b : blocks_)
        if (b > kAbsoluteGate && b > relativeGate) { sum += energyOf(b); ++count; }
    return count > 0 ? -0.691 + 10.0 * std::log10(sum / count) : LoudnessMeter::kSilence;
}

double LoudnessGate::loudnessRange() const
{
    // EBU Tech 3342: short-term values above -70 LUFS and above the relative
    // gate 20 LU below their mean; LRA is the spread from the 10th to the
    // 95th percentile.
    std::vector<float> gated;
    double sum = 0.0;
    for (float v : shortTerm_)
        if (v > kAbsoluteGate) { gated.push_back(v); sum += energyOf(v); }
    if (gated.size() < 2) return 0.0;
    const double relativeGate = -0.691 + 10.0 * std::log10(sum / gated.size()) - 20.0;
    gated.erase(std::remove_if(gated.begin(), gated.end(),
                               [relativeGate](float v) { return v <= relativeGate; }),
                gated.end());
    if (gated.size() < 2) return 0.0;
    std::sort(gated.begin(), gated.end());
    auto at = [&](double q) { return gated[size_t(std::round(q * (gated.size() - 1)))]; };
    return at(0.95) - at(0.10);
}

// ── LoudnessTap ──────────────────────────────────────────────────────────────

LoudnessTap::LoudnessTap()
    : juce::AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void LoudnessTap::prepareToPlay(double sampleRate, int)
{
    meter_.prepare(sampleRate);
}

void LoudnessTap::setMonitor(LooperMonitor* monitor)
{
    monitor_.store(monitor);
    if (monitor == nullptr)
        for (int i = 0; i < 200 && inBlock_.load(); ++i)
            juce::Thread::sleep(1);
}

void LoudnessTap::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    inBlock_.store(true);
    if (auto* monitor = monitor_.load())
        monitor->addTo(buffer);
    inBlock_.store(false);

    if (resetRequested_.exchange(false))
        meter_.reset();

    const int numChannels = std::min(2, buffer.getNumChannels());
    if (numChannels == 0) return;
    meter_.process(buffer.getArrayOfReadPointers(), numChannels, buffer.getNumSamples());

    momentary_.store(meter_.momentary(), std::memory_order_relaxed);
    shortTerm_.store(meter_.shortTerm(), std::memory_order_relaxed);
    truePeak_.store(meter_.truePeakDb(), std::memory_order_relaxed);
    maxShortTerm_.store(meter_.maxShortTerm(), std::memory_order_relaxed);

    // Hand the gating steps to the message thread (dropped if it falls far
    // behind, which only loses old history).
    meter_.drainSteps([this](const LoudnessMeter::Step& s) {
        int start1, size1, start2, size2;
        fifo_.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 > 0) fifoSteps_[size_t(start1)] = s;
        else if (size2 > 0) fifoSteps_[size_t(start2)] = s;
        fifo_.finishedWrite(size1 + size2);
    });
}

void LoudnessTap::requestReset()
{
    resetRequested_ = true;
    gate_.reset();
    // Drop values measured before the reset.
    fifo_.finishedRead(fifo_.getNumReady());
}

LoudnessTap::Readings LoudnessTap::readings()
{
    int start1, size1, start2, size2;
    fifo_.prepareToRead(fifo_.getNumReady(), start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) gate_.add(fifoSteps_[size_t(start1 + i)]);
    for (int i = 0; i < size2; ++i) gate_.add(fifoSteps_[size_t(start2 + i)]);
    fifo_.finishedRead(size1 + size2);

    Readings r;
    r.momentary = momentary_.load(std::memory_order_relaxed);
    r.shortTerm = shortTerm_.load(std::memory_order_relaxed);
    r.truePeakDb = truePeak_.load(std::memory_order_relaxed);
    r.maxShortTerm = maxShortTerm_.load(std::memory_order_relaxed);
    r.integrated = gate_.integrated();
    r.loudnessRange = gate_.loudnessRange();
    r.seconds = gate_.measuredSeconds();
    return r;
}

} // namespace OpenDaw
