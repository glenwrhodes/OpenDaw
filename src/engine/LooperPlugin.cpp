#include "LooperPlugin.h"
#include <algorithm>
#include <cmath>

namespace OpenDaw {

namespace {
constexpr int64_t kRingMask = LooperInputHub::kRingSize - 1;
constexpr int kMaxDelaySamples = 96000;     // round trip up to 0.5 s at 192 kHz
constexpr int kPrintBlock = 16384;
constexpr double kRampStep = 1.0 / 128.0;   // de-click ramp, ~3 ms
constexpr double kMaxLateSecs = 10.0;       // a start this late (a stalled graph) begins now
constexpr double kOpenEnd = 1.0e299;
}

// ── Input hub ────────────────────────────────────────────────────────────────

LooperInputHub::LooperInputHub()
{
    ring_[0].assign(kRingSize, 0.0f);
    ring_[1].assign(kRingSize, 0.0f);
}

void LooperInputHub::acceptInputBuffer(choc::buffer::ChannelArrayView<float> block)
{
    const auto channels = block.getNumChannels();
    const auto frames = block.getNumFrames();
    if (channels == 0 || frames == 0) return;

    const int64_t w = written_.load(std::memory_order_relaxed);
    float pk = 0.0f;
    for (choc::buffer::FrameCount i = 0; i < frames; ++i) {
        const float l = block.getSample(0, i);
        const float r = channels > 1 ? block.getSample(1, i) : l;
        const auto idx = size_t((w + int64_t(i)) & kRingMask);
        ring_[0][idx] = l;
        ring_[1][idx] = r;
        pk = std::max(pk, std::max(std::abs(l), std::abs(r)));
    }
    stereo_.store(channels > 1, std::memory_order_relaxed);
    if (pk > peak_.load(std::memory_order_relaxed))
        peak_.store(pk, std::memory_order_relaxed);
    written_.store(w + int64_t(frames), std::memory_order_release);
}

float LooperInputHub::sample(int channel, int64_t index) const
{
    return ring_[channel & 1][size_t(index & kRingMask)];
}

// ── Monitor ──────────────────────────────────────────────────────────────────

void LooperMonitor::addTo(juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int64_t w = hub_.written();
    if (!enabled.load() || !active.load() || buffer.getNumChannels() == 0) {
        readPos_ = w;
        return;
    }
    const int64_t avail = w - readPos_;
    if (avail > n + 4096 || avail < 0)
        readPos_ = std::max<int64_t>(0, w - n);

    auto* l = buffer.getWritePointer(0);
    auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < n; ++i) {
        const int64_t idx = readPos_ + i;
        if (idx >= w) break;
        l[i] += hub_.sample(0, idx);
        if (r) r[i] += hub_.sample(1, idx);
    }
    readPos_ = std::min(readPos_ + n, w);
}

// ── Pad ──────────────────────────────────────────────────────────────────────

LooperPadProcessor::LooperPadProcessor(int index, LooperInputHub& hub)
    : index_(index), hub_(hub)
{
    // Sized for the worst case up front: the audio thread never allocates.
    delay_[0].assign(kMaxDelaySamples, 0.0f);
    delay_[1].assign(kMaxDelaySamples, 0.0f);
    print_.setSize(2, kPrintBlock);
}

void LooperPadProcessor::prepare(double sampleRate)
{
    if (sampleRate > 0.0) sampleRate_ = sampleRate;
}

bool LooperPadProcessor::post(const LooperCommand& cmd)
{
    int s1, n1, s2, n2;
    commandFifo_.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return false;
    commands_[size_t(n1 > 0 ? s1 : s2)] = cmd;
    commandFifo_.finishedWrite(1);
    return true;
}

bool LooperPadProcessor::popEvent(LooperEvent& ev)
{
    int s1, n1, s2, n2;
    eventFifo_.prepareToRead(1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return false;
    ev = events_[size_t(n1 > 0 ? s1 : s2)];
    eventFifo_.finishedRead(1);
    return true;
}

void LooperPadProcessor::pushEvent(LooperEvent::Type type, double time, int64_t length)
{
    int s1, n1, s2, n2;
    eventFifo_.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return;
    events_[size_t(n1 > 0 ? s1 : s2)] = { type, time, length };
    eventFifo_.finishedWrite(1);
}

void LooperPadProcessor::setPrintWriter(juce::AudioFormatWriter::ThreadedWriter* writer)
{
    writer_.store(writer);
    if (writer == nullptr) waitUntilIdle();
    else streamStarted.store(false);
}

void LooperPadProcessor::waitUntilIdle() const
{
    // A block takes a few milliseconds at most; don't hang if the device stalls.
    for (int i = 0; i < 200 && busy_.load(); ++i)
        juce::Thread::sleep(1);
}

float LooperPadProcessor::input(int ch, int64_t hubIndex, int64_t written) const
{
    if (hubIndex < 0 || hubIndex >= written
        || hubIndex < written - LooperInputHub::kRingSize / 2)
        return 0.0f;
    return hub_.sample(ch, hubIndex);
}

void LooperPadProcessor::applyNow(const LooperCommand& cmd, double t)
{
    switch (cmd.type) {
    case LooperCommand::SetRecordEnd:
        recEnd_ = cmd.endAt;
        break;
    case LooperCommand::SetOverdubEnd:
        odEnd_ = cmd.endAt;
        break;
    case LooperCommand::SetBuffer:
        loop_ = cmd.buffer;
        length_ = cmd.length;
        anchor_ = cmd.anchor;
        odCapturing_ = false;
        if (!loop_ || length_ <= 0) {
            if (state_ == PadState::Playing || state_ == PadState::Overdubbing)
                pushEvent(LooperEvent::Silent, t);
            state_ = PadState::Empty;
        } else if (state_ == PadState::Empty) {
            state_ = PadState::Stopped;
        }
        break;
    case LooperCommand::Clear:
        if (state_ == PadState::Playing || state_ == PadState::Overdubbing
            || state_ == PadState::Recording)
            pushEvent(LooperEvent::Silent, t);
        state_ = PadState::Empty;
        loop_ = nullptr;
        length_ = 0;
        recBuffer_ = nullptr;
        recCapturing_ = odCapturing_ = false;
        pendingCount_ = 0;
        break;
    case LooperCommand::CancelPending:
        pendingCount_ = 0;
        break;
    default:
        break;
    }
}

void LooperPadProcessor::startCommand(const LooperCommand& cmd, double t)
{
    const bool wasAudible = state_ == PadState::Playing || state_ == PadState::Overdubbing
                         || state_ == PadState::Recording;
    switch (cmd.type) {
    case LooperCommand::Record:
        if (!cmd.buffer) break;
        recBuffer_ = cmd.buffer;
        recCapacity_ = std::min<int64_t>(cmd.capacity, cmd.buffer->getNumSamples());
        // A start the plugin reached late (a new track's plugin starts when
        // the graph is rebuilt) keeps its time, so the loop stays on the
        // beat: its input comes from the hub, or is silence if already gone.
        recStart_ = std::max(cmd.at, t - kMaxLateSecs);
        recEnd_ = cmd.endAt;
        recCaptured_ = 0;
        recCapturing_ = true;
        state_ = PadState::Recording;
        if (!wasAudible) pushEvent(LooperEvent::Audible, recStart_);
        break;
    case LooperCommand::Play:
        if (!loop_ || length_ <= 0) break;
        oneShot_ = cmd.oneShot;
        oneShotStart_ = t;
        if (state_ != PadState::Overdubbing) state_ = PadState::Playing;
        if (!wasAudible) pushEvent(LooperEvent::Audible, t);
        break;
    case LooperCommand::Stop:
        if (state_ == PadState::Overdubbing && odEnd_ > t) odEnd_ = t;
        if (state_ == PadState::Playing || state_ == PadState::Overdubbing) {
            state_ = PadState::Stopped;
            pushEvent(LooperEvent::Silent, t);
        }
        break;
    case LooperCommand::Overdub:
        if (!cmd.buffer || !loop_ || length_ <= 0) break;
        loop_ = cmd.buffer;   // a copy of the current loop, which the overdub adds to
        odStart_ = std::max(cmd.at, t - kMaxLateSecs);
        odEnd_ = cmd.endAt;
        odCaptured_ = 0;
        odCapturing_ = true;
        oneShot_ = false;
        state_ = PadState::Overdubbing;
        if (!wasAudible) pushEvent(LooperEvent::Audible, t);
        break;
    default:
        applyNow(cmd, t);
        break;
    }
}

void LooperPadProcessor::stopAll(double t)
{
    if (state_ == PadState::Recording) {
        // An unfinished recording is dropped.
        recCapturing_ = false;
        recBuffer_ = nullptr;
        pushEvent(LooperEvent::Silent, t);
        pushEvent(LooperEvent::RecordAborted, t);
        state_ = loop_ ? PadState::Stopped : PadState::Empty;
    }
    if (recCapturing_) {
        recCapturing_ = false;
        pushEvent(LooperEvent::RecordDone, recEnd_, length_);
    }
    if (odCapturing_) {
        odCapturing_ = false;
        pushEvent(LooperEvent::OverdubDone, t);
    }
    if (state_ == PadState::Playing || state_ == PadState::Overdubbing) {
        state_ = PadState::Stopped;
        pushEvent(LooperEvent::Silent, t);
    }
    gain_ = 0.0;
    pendingCount_ = 0;
}

void LooperPadProcessor::capture(double t0, double t1, double sr, int64_t hubAt0)
{
    const int64_t w = hub_.written();
    const double rt = std::round(roundTrip_.load() * sr) / sr;
    const double availableUntil = t1 - rt;   // input has arrived for loop times before this
    auto hubIndex = [&](double loopTime) {
        return hubAt0 + std::llround((loopTime + rt - t0) * sr);
    };

    if (recCapturing_ && recBuffer_) {
        const double until = std::min(recEnd_, availableUntil);
        int64_t kEnd = std::llround((until - recStart_) * sr);
        kEnd = std::min(kEnd, recCapacity_);
        for (int64_t k = recCaptured_; k < kEnd; ++k) {
            const int64_t h = hubIndex(recStart_ + double(k) / sr);
            recBuffer_->setSample(0, int(k), input(0, h, w));
            recBuffer_->setSample(1, int(k), input(1, h, w));
        }
        recCaptured_ = std::max(recCaptured_, kEnd);

        if (recCaptured_ >= recCapacity_ && recEnd_ > kOpenEnd) {
            // Out of room: end the recording here.
            recEnd_ = recStart_ + double(recCapacity_) / sr;
        }
        if (recEnd_ < kOpenEnd) {
            const int64_t total = std::min<int64_t>(recCapacity_, std::llround((recEnd_ - recStart_) * sr));
            if (recCaptured_ >= total && state_ != PadState::Recording) {
                recCapturing_ = false;
                pushEvent(LooperEvent::RecordDone, recEnd_, length_);
            }
        }
    }

    if (odCapturing_ && loop_ && length_ > 0) {
        const double until = std::min(odEnd_, availableUntil);
        const int64_t kEnd = std::llround((until - odStart_) * sr);
        for (int64_t k = odCaptured_; k < kEnd; ++k) {
            const double loopTime = odStart_ + double(k) / sr;
            int64_t idx = std::llround((loopTime - anchor_) * sr) % length_;
            if (idx < 0) idx += length_;
            const int64_t h = hubIndex(loopTime);
            loop_->addSample(0, int(idx), input(0, h, w));
            loop_->addSample(1, int(idx), input(1, h, w));
        }
        odCaptured_ = std::max(odCaptured_, kEnd);
        if (odEnd_ < kOpenEnd && odCaptured_ >= std::llround((odEnd_ - odStart_) * sr)) {
            odCapturing_ = false;
            pushEvent(LooperEvent::OverdubDone, odEnd_);
        }
    }
}

void LooperPadProcessor::process(const te::PluginRenderContext& fc)
{
    struct Busy {
        std::atomic<bool>& b;
        explicit Busy(std::atomic<bool>& f) : b(f) { b.store(true); }
        ~Busy() { b.store(false); }
    } busy(busy_);

    auto* dest = fc.destBuffer;
    const int n = fc.bufferNumSamples;
    if (!dest || n <= 0 || dest->getNumChannels() == 0) return;
    const double t0 = fc.editTime.getStart().inSeconds();

    // Commands: changes apply now, actions wait for their time.
    {
        int s1, n1, s2, n2;
        const int ready = commandFifo_.getNumReady();
        commandFifo_.prepareToRead(ready, s1, n1, s2, n2);
        auto take = [&](const LooperCommand& cmd) {
            switch (cmd.type) {
            case LooperCommand::Record: case LooperCommand::Play:
            case LooperCommand::Stop: case LooperCommand::Overdub: {
                if (pendingCount_ >= int(pending_.size())) break;
                int pos = pendingCount_;
                while (pos > 0 && pending_[size_t(pos - 1)].at > cmd.at) {
                    pending_[size_t(pos)] = pending_[size_t(pos - 1)];
                    --pos;
                }
                pending_[size_t(pos)] = cmd;
                ++pendingCount_;
                break;
            }
            default:
                applyNow(cmd, t0);
                break;
            }
            processedSeq.store(cmd.seq);
        };
        for (int i = 0; i < n1; ++i) take(commands_[size_t(s1 + i)]);
        for (int i = 0; i < n2; ++i) take(commands_[size_t(s2 + i)]);
        commandFifo_.finishedRead(n1 + n2);
    }

    // Exports render the timeline; the looper is a live instrument.
    if (fc.isRendering) return;

    if (!fc.isPlaying) {
        if (wasPlaying_) stopAll(lastTime.load());
        wasPlaying_ = false;
        hubRead_ = hub_.written();
        lastBlockStart_ = -1.0;
        state.store(int(state_));
        return;
    }
    // A second plugin for the same pad (a duplicated track) stays quiet.
    if (t0 == lastBlockStart_) return;
    lastBlockStart_ = t0;
    wasPlaying_ = true;

    const double sr = sampleRate_;
    const double t1 = t0 + n / sr;

    // The input that arrived with this block.
    const int64_t w = hub_.written();
    const int64_t avail = w - hubRead_;
    if (avail > n + 4096 || avail < 0)
        hubRead_ = std::max<int64_t>(0, w - n);
    const int64_t hubAt0 = hubRead_;
    hubRead_ = std::min(hubRead_ + n, w);

    const int rtSamples = std::clamp(int(std::lround(roundTrip_.load() * sr)), 0, kMaxDelaySamples - 1);
    auto* writer = writer_.load();
    if (writer && !streamStarted.load()) {
        streamOrigin.store(t0 - rtSamples / sr);
        streamStarted.store(true);
    }
    const bool printing = writer != nullptr && n <= kPrintBlock;

    float* out0 = dest->getWritePointer(0, fc.bufferStartSample);
    float* out1 = dest->getNumChannels() > 1 ? dest->getWritePointer(1, fc.bufferStartSample) : nullptr;
    float pk = 0.0f;

    for (int i = 0; i < n; ++i) {
        const double t = t0 + i / sr;
        while (pendingCount_ > 0 && pending_[0].at <= t) {
            const LooperCommand cmd = pending_[0];
            for (int k = 1; k < pendingCount_; ++k) pending_[size_t(k - 1)] = pending_[size_t(k)];
            --pendingCount_;
            startCommand(cmd, t);
        }

        if (state_ == PadState::Recording && t >= recEnd_) {
            // The recording becomes the loop and plays on, in phase.
            length_ = std::max<int64_t>(1, std::min<int64_t>(recCapacity_, std::llround((recEnd_ - recStart_) * sr)));
            loop_ = recBuffer_;
            anchor_ = recStart_;
            oneShot_ = false;
            state_ = PadState::Playing;
        }
        if (state_ == PadState::Overdubbing && t >= odEnd_)
            state_ = PadState::Playing;

        bool playing = (state_ == PadState::Playing || state_ == PadState::Overdubbing)
                    && loop_ && length_ > 0;
        if (playing) {
            if (oneShot_) {
                playIdx_ = std::llround((t - oneShotStart_) * sr);
                if (playIdx_ >= length_) {
                    state_ = PadState::Stopped;
                    pushEvent(LooperEvent::Silent, t);
                    playing = false;
                }
            } else {
                playIdx_ = std::llround((t - anchor_) * sr) % length_;
                if (playIdx_ < 0) playIdx_ += length_;
            }
        } else {
            ++playIdx_;   // a stop ramps out from where it was
        }

        gain_ += std::clamp((playing ? 1.0 : 0.0) - gain_, -kRampStep, kRampStep);
        float o0 = 0.0f, o1 = 0.0f;
        if (gain_ > 0.0 && loop_ && length_ > 0) {
            const int idx = int(((playIdx_ % length_) + length_) % length_);
            o0 = float(loop_->getSample(0, idx) * gain_);
            o1 = float(loop_->getSample(1, idx) * gain_);
        }

        // While the pad sounds, it replaces what the track has on the timeline.
        const bool audible = playing || state_ == PadState::Recording;
        if (audible) {
            out0[i] = o0;
            if (out1) out1[i] = o1;
        } else {
            out0[i] += o0;
            if (out1) out1[i] += o1;
        }
        pk = std::max(pk, std::max(std::abs(o0), std::abs(o1)));

        if (printing) {
            // The print is what was heard: the loop delayed by the round trip,
            // plus the input being recorded, which arrives that much late.
            const int readPos = (delayPos_ - rtSamples + kMaxDelaySamples) % kMaxDelaySamples;
            float p0 = rtSamples > 0 ? delay_[0][size_t(readPos)] : o0;
            float p1 = rtSamples > 0 ? delay_[1][size_t(readPos)] : o1;
            delay_[0][size_t(delayPos_)] = o0;
            delay_[1][size_t(delayPos_)] = o1;
            delayPos_ = (delayPos_ + 1) % kMaxDelaySamples;

            const double loopTime = t - rtSamples / sr;
            const bool capturing =
                (recCapturing_ && loopTime >= recStart_ && loopTime < recEnd_)
                || (odCapturing_ && loopTime >= odStart_ && loopTime < odEnd_);
            if (capturing) {
                p0 += input(0, hubAt0 + i, w);
                p1 += input(1, hubAt0 + i, w);
            }
            print_.setSample(0, i, p0);
            print_.setSample(1, i, p1);
        }
    }

    if (printing)
        writer->write(print_.getArrayOfReadPointers(), n);

    capture(t0, t1, sr, hubAt0);

    if (state_ == PadState::Recording && recEnd_ < kOpenEnd && recEnd_ > recStart_)
        phase.store(std::clamp((t1 - recStart_) / (recEnd_ - recStart_), 0.0, 1.0));
    else if (state_ == PadState::Recording)
        phase.store(0.0);
    else if (loop_ && length_ > 0)
        phase.store(double((playIdx_ % length_ + length_) % length_) / double(length_));
    state.store(int(state_));
    lastTime.store(t1);
    if (pk > peak.load()) peak.store(pk);
}

// ── Plugin ───────────────────────────────────────────────────────────────────

const char* LooperPlugin::xmlTypeName = "opendawLooper";
const juce::Identifier LooperPlugin::padProperty("looperPad");

LooperPlugin::LooperPlugin(te::PluginCreationInfo info) : te::Plugin(info)
{
    pad_ = findLooperPad(padIndex());
}

LooperPlugin::~LooperPlugin()
{
    notifyListenersOfDeletion();
}

juce::ValueTree LooperPlugin::create(int padIndex)
{
    juce::ValueTree v(te::IDs::PLUGIN);
    v.setProperty(te::IDs::type, xmlTypeName, nullptr);
    v.setProperty(padProperty, padIndex, nullptr);
    return v;
}

int LooperPlugin::padIndex() const
{
    return int(state.getProperty(padProperty, -1));
}

void LooperPlugin::initialise(const te::PluginInitialisationInfo& info)
{
    if (pad_) pad_->prepare(info.sampleRate);
}

void LooperPlugin::applyToBuffer(const te::PluginRenderContext& fc)
{
    if (pad_) pad_->process(fc);
}

} // namespace OpenDaw
