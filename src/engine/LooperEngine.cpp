#include "LooperEngine.h"
#include "EditManager.h"
#include <QSettings>
#include <algorithm>
#include <cmath>

namespace OpenDaw {

namespace {
LooperEngine* g_engine = nullptr;

const juce::Identifier kLooperTree("OPENDAW_LOOPER");
const juce::Identifier kPadNode("PAD");
const juce::Identifier kIndexProp("index");
const juce::Identifier kTrackProp("track");
const juce::Identifier kFileProp("file");
const juce::Identifier kLengthProp("length");
const juce::Identifier kAnchorProp("anchor");
const juce::Identifier kOneShotProp("oneShot");
const juce::Identifier kGroupProp("group");
const juce::Identifier kPadCountProp("pads");
const juce::Identifier kMasterProp("master");
const juce::Identifier kMasterAnchorProp("masterAnchor");
const juce::Identifier kMasterSecsProp("masterSecs");

constexpr double kOpenEnd = 1.0e300;
constexpr double kGraceSecs = 0.15;       // a tap this late still counts as on the beat
constexpr double kQueuedMinSecs = 0.03;   // shorter waits aren't shown as queued
constexpr double kMaxRecordSecs = 120.0;
constexpr double kMinFreeLoopSecs = 0.2;
constexpr double kPrintFadeSecs = 0.004;
constexpr int kPollMs = 30;

bool isSounding(PadState s) { return s == PadState::Playing || s == PadState::Overdubbing; }
}

LooperPadProcessor* findLooperPad(int index)
{
    return g_engine ? g_engine->processor(index) : nullptr;
}

LooperEngine::LooperEngine(EditManager& editMgr) : em_(editMgr)
{
    for (int i = 0; i < kMaxPads; ++i)
        processors_[size_t(i)] = std::make_unique<LooperPadProcessor>(i, hub_);
    g_engine = this;

    QSettings s;
    input_ = s.value("looper/input").toString();
    // Off by default when tracks don't monitor either (a mic near speakers howls).
    monitorOn_ = s.value("looper/monitor", s.value("audio/monitorMode", 1).toInt() != 0).toBool();
    quantize_ = Quantize(std::clamp(s.value("looper/quantize", int(Quantize::Bar)).toInt(), 0, 2));
    recordBars_ = s.value("looper/recordBars", 0).toInt();
    freeTempo_ = s.value("looper/firstLoopSetsTempo", true).toBool();
    countIn_ = s.value("looper/countIn", true).toBool();
    padCount_ = std::clamp(s.value("looper/pads", 8).toInt(), 4, kMaxPads);
    monitor_.enabled.store(monitorOn_);

    writerThread_.startThread();
    connect(&timer_, &QTimer::timeout, this, &LooperEngine::poll);
    timer_.start(kPollMs);
}

LooperEngine::~LooperEngine()
{
    timer_.stop();
    if (auto* tap = em_.audioEngine().loudnessTap())
        tap->setMonitor(nullptr);
    detach();
    writerThread_.stopThread(2000);
    if (g_engine == this) g_engine = nullptr;
}

LooperPadProcessor* LooperEngine::processor(int index)
{
    if (index < 0 || index >= kMaxPads) return nullptr;
    return processors_[size_t(index)].get();
}

// ── Edit lifetime ────────────────────────────────────────────────────────────

void LooperEngine::attach()
{
    edit_ = em_.edit();
    if (!edit_) return;

    // Pads keep their tracks: each looper track's plugin knows its pad.
    for (auto* track : te::getAudioTracks(*edit_))
        for (auto* p : track->pluginList.getPlugins())
            if (auto* lp = dynamic_cast<LooperPlugin*>(p)) {
                const int idx = lp->padIndex();
                if (idx >= 0 && idx < kMaxPads) {
                    pads_[size_t(idx)].track = track->itemID;
                    padCount_ = std::max(padCount_, std::min(kMaxPads, (idx / 4 + 1) * 4));
                }
            }

    auto node = edit_->state.getChildWithName(kLooperTree);
    if (node.isValid()) {
        padCount_ = std::clamp(int(node.getProperty(kPadCountProp, padCount_)), padCount_, kMaxPads);
        master_ = bool(node.getProperty(kMasterProp, false));
        masterAnchor_ = double(node.getProperty(kMasterAnchorProp, 0.0));
        masterSecs_ = double(node.getProperty(kMasterSecsProp, 0.0));
        if (masterSecs_ <= 0.0) master_ = false;

        auto& fm = edit_->engine.getAudioFileFormatManager().readFormatManager;
        for (auto child : node) {
            if (!child.hasType(kPadNode)) continue;
            const int idx = int(child.getProperty(kIndexProp, -1));
            if (idx < 0 || idx >= kMaxPads) continue;
            auto& pad = pads_[size_t(idx)];
            pad.track = te::EditItemID::fromVar(child.getProperty(kTrackProp));
            pad.oneShot = bool(child.getProperty(kOneShotProp, false));
            pad.group = int(child.getProperty(kGroupProp, 0));
            if (!padTrack(idx)) { pad.track = {}; continue; }

            const juce::File file(child.getProperty(kFileProp).toString());
            std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
            const int64_t length = juce::int64(child.getProperty(kLengthProp, 0));
            if (!reader || length <= 0) continue;
            const int n = int(std::min<int64_t>(length, reader->lengthInSamples));
            auto buf = std::make_shared<juce::AudioBuffer<float>>(2, std::max(1, n));
            buf->clear();
            reader->read(buf.get(), 0, n, 0, true, true);
            if (reader->numChannels == 1) buf->copyFrom(1, 0, *buf, 0, 0, n);

            pad.versions = { buf };
            pad.length = n;
            pad.anchor = double(child.getProperty(kAnchorProp, 0.0));
            LooperCommand c;
            c.type = LooperCommand::SetBuffer;
            c.buffer = buf.get();
            c.length = n;
            c.anchor = pad.anchor;
            post(idx, c);
            processors_[size_t(idx)]->state.store(int(PadState::Stopped));
        }
    }
    bindInput();
    emit changed();
}

void LooperEngine::detach()
{
    if (!edit_) return;

    // The edit is going away: drop the performance in progress.
    for (int i = 0; i < kMaxPads; ++i) {
        auto& pad = pads_[size_t(i)];
        if (pad.writer) {
            processors_[size_t(i)]->setPrintWriter(nullptr);
            pad.writer.reset();
            pad.printFile.deleteFile();
        }
    }
    unbindInput();
    for (int i = 0; i < kMaxPads; ++i) {
        LooperEvent ev;
        while (processors_[size_t(i)]->popEvent(ev)) {}
        auto& pad = pads_[size_t(i)];
        LooperCommand c;
        c.type = LooperCommand::Clear;
        post(i, c);
        for (auto& b : pad.versions) bury(i, b);
        for (auto& b : pad.redo) bury(i, b);
        bury(i, pad.recording);
        bury(i, pad.overdubTarget);
        pad = Pad();
        processors_[size_t(i)]->state.store(int(PadState::Empty));
    }
    master_ = false;
    sessionActive_ = false;
    clickOffAt_ = -1.0;
    lastPad_ = -1;
    edit_ = nullptr;
}

void LooperEngine::saveState(const juce::File& editFile)
{
    if (!edit_ || editFile == juce::File()) return;

    auto old = edit_->state.getChildWithName(kLooperTree);
    if (old.isValid()) edit_->state.removeChild(old, nullptr);

    juce::ValueTree node(kLooperTree);
    node.setProperty(kPadCountProp, padCount_, nullptr);
    node.setProperty(kMasterProp, master_, nullptr);
    node.setProperty(kMasterAnchorProp, masterAnchor_, nullptr);
    node.setProperty(kMasterSecsProp, masterSecs_, nullptr);

    const auto folder = editFile.getParentDirectory()
                            .getChildFile(editFile.getFileNameWithoutExtension() + " Looper");
    for (int i = 0; i < kMaxPads; ++i) {
        auto& pad = pads_[size_t(i)];
        if (pad.versions.empty() || pad.length <= 0 || !padTrack(i)) continue;

        const auto file = folder.getChildFile("Pad " + juce::String(i + 1) + ".wav");
        if (pad.dirty || !file.existsAsFile()) {
            folder.createDirectory();
            juce::WavAudioFormat wav;
            file.deleteFile();
            auto out = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
            if (out) {
                auto opts = juce::AudioFormatWriterOptions{}
                                .withSampleRate(sampleRate())
                                .withNumChannels(2)
                                .withBitsPerSample(24);
                if (auto writer = wav.createWriterFor(out, opts))
                    writer->writeFromAudioSampleBuffer(*pad.versions.back(), 0, int(pad.length));
            }
            pad.dirty = false;
        }

        juce::ValueTree p(kPadNode);
        p.setProperty(kIndexProp, i, nullptr);
        p.setProperty(kTrackProp, pad.track.toString(), nullptr);
        p.setProperty(kFileProp, file.getFullPathName(), nullptr);
        p.setProperty(kLengthProp, juce::int64(pad.length), nullptr);
        p.setProperty(kAnchorProp, pad.anchor, nullptr);
        p.setProperty(kOneShotProp, pad.oneShot, nullptr);
        p.setProperty(kGroupProp, pad.group, nullptr);
        node.addChild(p, -1, nullptr);
    }
    edit_->state.addChild(node, -1, nullptr);
}

// ── Plumbing ─────────────────────────────────────────────────────────────────

void LooperEngine::post(int index, LooperCommand cmd)
{
    cmd.seq = ++seq_;
    padSeq_[size_t(index)] = cmd.seq;
    processors_[size_t(index)]->post(cmd);
}

void LooperEngine::bury(int index, BufferPtr buffer)
{
    // The audio thread may still be using it until it has read the commands
    // posted so far.
    if (buffer)
        graveyard_.push_back({ index, padSeq_[size_t(index)], juce::Time::getMillisecondCounter(),
                               std::move(buffer) });
}

te::AudioTrack* LooperEngine::padTrack(int index) const
{
    const auto& pad = pads_[size_t(index)];
    if (!edit_ || !pad.track.isValid()) return nullptr;
    return te::findAudioTrackForID(*edit_, pad.track);
}

te::AudioTrack* LooperEngine::ensureTrack(int index)
{
    if (auto* t = padTrack(index)) return t;
    auto* track = em_.addAudioTrack();
    if (!track) return nullptr;
    track->setName("Loop " + juce::String(index + 1));
    if (auto p = edit_->getPluginCache().createNewPlugin(LooperPlugin::create(index)))
        track->pluginList.insertPlugin(p, 0, nullptr);
    pads_[size_t(index)].track = track->itemID;
    emit em_.tracksChanged();
    return track;
}

void LooperEngine::bindInput()
{
    unbindInput();
    if (!edit_) return;

    if (input_.isEmpty()) {
        const auto sources = em_.getAvailableInputSources();
        if (!sources.isEmpty()) input_ = QString::fromStdString(sources.first().deviceName.toStdString());
    }
    for (auto* idi : edit_->getAllInputDevices()) {
        auto& dev = idi->getInputDevice();
        if (dev.getDeviceType() != te::InputDevice::waveDevice) continue;
        if (QString::fromStdString(dev.getName().toStdString()) != input_) continue;
        idi->addConsumer(&hub_);
        boundInput_ = idi;
        break;
    }
    const double rt = roundTrip();
    for (auto& p : processors_) p->setRoundTripSeconds(rt);
    monitor_.active.store(boundInput_ != nullptr);
}

void LooperEngine::unbindInput()
{
    monitor_.active.store(false);
    if (boundInput_ && edit_)
        for (auto* idi : edit_->getAllInputDevices())
            if (idi == boundInput_) { idi->removeConsumer(&hub_); break; }
    boundInput_ = nullptr;
}

double LooperEngine::roundTrip() const
{
    // The device's input + output latency, plus the user's recording offset
    // (Audio Settings), as Tracktion's own recordings use.
    auto& dm = em_.audioEngine().engine().getDeviceManager();
    const double offsetMs = QSettings().value("audio/recordOffsetMs", 0.0).toDouble();
    return std::clamp(0.001 * (dm.getRecordAdjustmentMs() + offsetMs), 0.0, 0.45);
}

double LooperEngine::sampleRate() const
{
    const double sr = em_.audioEngine().engine().getDeviceManager().getSampleRate();
    return sr > 0.0 ? sr : 48000.0;
}

// ── Timing ───────────────────────────────────────────────────────────────────

double LooperEngine::now() const
{
    return edit_ ? edit_->getTransport().getPosition().inSeconds() : 0.0;
}

double LooperEngine::beatsPerBar() const
{
    return std::max(1, em_.getTimeSigNumerator());
}

double LooperEngine::toBeats(double t) const
{
    return edit_->tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(t)).inBeats();
}

double LooperEngine::toTime(double beats) const
{
    return edit_->tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats)).inSeconds();
}

double LooperEngine::boundaryAfter(double t, Quantize q) const
{
    if (q == Quantize::Loop && master_) {
        const double k = (t - masterAnchor_) / masterSecs_;
        return masterAnchor_ + std::ceil(k - 1e-9) * masterSecs_;
    }
    const double unit = q == Quantize::Beat ? 1.0 : beatsPerBar();
    return toTime(std::ceil(toBeats(t) / unit - 1e-6) * unit);
}

double LooperEngine::boundaryBefore(double t, Quantize q) const
{
    if (q == Quantize::Loop && master_) {
        const double k = (t - masterAnchor_) / masterSecs_;
        return masterAnchor_ + std::floor(k + 1e-9) * masterSecs_;
    }
    const double unit = q == Quantize::Beat ? 1.0 : beatsPerBar();
    return toTime(std::floor(toBeats(t) / unit + 1e-6) * unit);
}

double LooperEngine::actionTime(double t, bool forRecording) const
{
    // A tap just after the beat counts as on it: a recording reaches back to
    // the beat (its input is still buffered); play and stop happen at once.
    const double before = boundaryBefore(t, quantize_);
    if (t - before < kGraceSecs)
        return forRecording ? before : t;
    return boundaryAfter(t, quantize_);
}

double LooperEngine::recordingEnd(double t, double start) const
{
    // Loops are whole beats, bars or master loops long, ending at the next
    // one after the tap (or the one just passed, for a late tap).
    if (quantize_ == Quantize::Loop && master_) {
        const double k = (t - start) / masterSecs_;
        const double kf = std::floor(k);
        if (kf >= 1.0 && t - (start + kf * masterSecs_) < kGraceSecs)
            return start + kf * masterSecs_;
        return start + std::max(1.0, std::ceil(k)) * masterSecs_;
    }
    const double unit = quantize_ == Quantize::Beat ? 1.0 : beatsPerBar();
    const double sb = toBeats(start);
    const double k = (toBeats(t) - sb) / unit;
    const double kf = std::floor(k);
    if (kf >= 1.0 && t - toTime(sb + kf * unit) < kGraceSecs)
        return toTime(sb + kf * unit);
    return toTime(sb + std::max(1.0, std::ceil(k)) * unit);
}

bool LooperEngine::startTransport(bool forRecording, bool freeStart, double& at)
{
    auto& tc = edit_->getTransport();
    if (tc.isPlaying()) {
        sessionActive_ = true;
        return false;
    }
    // Start on the bar the playhead is in, with a bar of count-in before a
    // recording (not before a free first loop: it has no tempo yet).
    const double bpb = beatsPerBar();
    const double barBeat = std::floor(toBeats(tc.getPosition().inSeconds()) / bpb + 1e-6) * bpb;
    double startBeat = barBeat, atBeat = barBeat;
    if (forRecording && countIn_ && !freeStart) {
        startBeat = barBeat - bpb;
        if (startBeat < 0.0) { startBeat = 0.0; atBeat = bpb; }
        if (!edit_->clickTrackEnabled.get()) {
            edit_->clickTrackEnabled = true;
            clickOffAt_ = toTime(atBeat);
        }
    }
    tc.looping = false;
    if (em_.isPunchEnabled()) em_.setPunchEnabled(false);
    tc.setPosition(tracktion::TimePosition::fromSeconds(toTime(startBeat)));
    tc.play(false);
    sessionActive_ = true;
    at = toTime(atBeat);
    return true;
}

// ── Pad actions ──────────────────────────────────────────────────────────────

void LooperEngine::tapPad(int index)
{
    if (!edit_ || index < 0 || index >= padCount_) return;
    lastPad_ = index;
    auto& pad = pads_[size_t(index)];
    const auto st = PadState(processors_[size_t(index)]->state.load());

    if (pad.queued == Queued::Record || pad.queued == Queued::Play || pad.queued == Queued::Stop) {
        // Tapping a pad that is waiting for its beat cancels what it was waiting to do.
        LooperCommand c;
        c.type = LooperCommand::CancelPending;
        post(index, c);
        if (pad.queued == Queued::Record) {
            bury(index, pad.recording);
            pad.recording.reset();
            pad.recordingOpen = false;
            pad.firstLoop = false;
        }
        pad.queued = Queued::None;
        emit changed();
        return;
    }

    if (pad.recording) {
        if (pad.recordingOpen) finishRecording(index);
    } else if (pad.overdubbing) {
        endOverdub(index);
    } else if (pad.versions.empty()) {
        startRecording(index);
    } else if (isSounding(st)) {
        stopPad(index);
    } else {
        playPad(index);
    }
    emit changed();
}

void LooperEngine::startRecording(int index)
{
    auto* track = ensureTrack(index);
    if (!track) return;
    if (!boundInput_) bindInput();

    auto& pad = pads_[size_t(index)];
    const bool free = !master_ && freeTempo_;
    double at = 0.0;
    if (!startTransport(true, free, at))
        at = free ? now() : actionTime(now(), true);

    double endAt = kOpenEnd;
    if (!free && recordBars_ > 0)
        endAt = toTime(toBeats(at) + recordBars_ * beatsPerBar());

    const double sr = sampleRate();
    const double secs = endAt < kOpenEnd ? endAt - at + 1.0 : kMaxRecordSecs;
    auto buf = std::make_shared<juce::AudioBuffer<float>>(2, int(std::ceil(secs * sr)));
    buf->clear();

    pad.recording = buf;
    pad.recordingOpen = endAt >= kOpenEnd;
    pad.firstLoop = free;
    pad.recStart = at;
    ensureWriter(index);

    LooperCommand c;
    c.type = LooperCommand::Record;
    c.at = at;
    c.endAt = endAt;
    c.buffer = buf.get();
    c.capacity = buf->getNumSamples();
    post(index, c);

    pad.queued = at > now() + kQueuedMinSecs ? Queued::Record : Queued::None;
    pad.queuedAt = at;
}

void LooperEngine::finishRecording(int index)
{
    auto& pad = pads_[size_t(index)];
    const double t = now();
    const double end = pad.firstLoop ? std::max(t, pad.recStart + kMinFreeLoopSecs)
                                     : recordingEnd(t, pad.recStart);
    LooperCommand c;
    c.type = LooperCommand::SetRecordEnd;
    c.endAt = end;
    post(index, c);
    pad.recordingOpen = false;
    pad.queued = end > t + kQueuedMinSecs ? Queued::EndRecord : Queued::None;
    pad.queuedAt = end;
}

void LooperEngine::playPad(int index, double at)
{
    auto& pad = pads_[size_t(index)];
    if (pad.versions.empty() || !padTrack(index)) return;
    const double t = now();
    if (at < 0.0 && !startTransport(false, false, at))
        at = actionTime(t, false);

    // Pads in a group take turns: starting one stops the others.
    if (pad.group > 0)
        for (int j = 0; j < padCount_; ++j) {
            if (j == index || pads_[size_t(j)].group != pad.group) continue;
            const auto sj = PadState(processors_[size_t(j)]->state.load());
            if (isSounding(sj) || pads_[size_t(j)].queued == Queued::Play)
                stopPad(j, at);
        }

    ensureWriter(index);
    LooperCommand c;
    c.type = LooperCommand::Play;
    c.at = at;
    c.oneShot = pad.oneShot;
    post(index, c);
    pad.queued = at > t + kQueuedMinSecs ? Queued::Play : Queued::None;
    pad.queuedAt = at;
}

void LooperEngine::stopPad(int index, double at)
{
    auto& pad = pads_[size_t(index)];
    const double t = now();
    if (at < 0.0) at = actionTime(t, false);
    LooperCommand c;
    c.type = LooperCommand::Stop;
    c.at = at;
    post(index, c);
    pad.queued = at > t + kQueuedMinSecs ? Queued::Stop : Queued::None;
    pad.queuedAt = at;
}

void LooperEngine::overdubPad(int index)
{
    if (!edit_ || index < 0 || index >= padCount_) return;
    lastPad_ = index;
    auto& pad = pads_[size_t(index)];
    if (pad.recording || pad.versions.empty()) { tapPad(index); return; }
    if (pad.overdubbing) { endOverdub(index); emit changed(); return; }
    if (pad.oneShot) return;
    if (!boundInput_) bindInput();

    double at = 0.0;
    if (!startTransport(false, false, at)) at = now();

    // The overdub goes into a copy, so undo can go back to the loop before it.
    pad.overdubTarget = std::make_shared<juce::AudioBuffer<float>>(*pad.versions.back());
    pad.overdubbing = true;
    ensureWriter(index);

    LooperCommand c;
    c.type = LooperCommand::Overdub;
    c.at = at;
    c.buffer = pad.overdubTarget.get();
    post(index, c);
    pad.queued = Queued::None;
    emit changed();
}

void LooperEngine::endOverdub(int index)
{
    LooperCommand c;
    c.type = LooperCommand::SetOverdubEnd;
    c.endAt = now();
    post(index, c);
}

void LooperEngine::undoPad(int index)
{
    if (!edit_ || index < 0 || index >= kMaxPads) return;
    auto& pad = pads_[size_t(index)];
    if (pad.overdubbing) {
        pad.undoAfterOverdub = true;
        endOverdub(index);
        return;
    }
    if (pad.recording) { clearPad(index); return; }
    if (pad.versions.empty()) return;

    pad.redo.push_back(pad.versions.back());
    pad.versions.pop_back();
    LooperCommand c;
    c.type = LooperCommand::SetBuffer;
    c.buffer = pad.versions.empty() ? nullptr : pad.versions.back().get();
    c.length = pad.versions.empty() ? 0 : pad.length;
    c.anchor = pad.anchor;
    post(index, c);
    pad.dirty = true;
    if (pad.versions.empty()) resetClockIfIdle();
    emit changed();
}

void LooperEngine::redoPad(int index)
{
    if (!edit_ || index < 0 || index >= kMaxPads) return;
    auto& pad = pads_[size_t(index)];
    if (pad.redo.empty() || pad.overdubbing || pad.recording) return;
    pad.versions.push_back(pad.redo.back());
    pad.redo.pop_back();
    LooperCommand c;
    c.type = LooperCommand::SetBuffer;
    c.buffer = pad.versions.back().get();
    c.length = pad.length;
    c.anchor = pad.anchor;
    post(index, c);
    pad.dirty = true;
    if (!master_) establishClock(index);
    emit changed();
}

void LooperEngine::clearPad(int index)
{
    if (!edit_ || index < 0 || index >= kMaxPads) return;
    auto& pad = pads_[size_t(index)];
    LooperCommand c;
    c.type = LooperCommand::Clear;
    post(index, c);
    for (auto& b : pad.versions) bury(index, b);
    for (auto& b : pad.redo) bury(index, b);
    bury(index, pad.recording);
    bury(index, pad.overdubTarget);
    pad.versions.clear();
    pad.redo.clear();
    pad.recording.reset();
    pad.overdubTarget.reset();
    pad.length = 0;
    pad.queued = Queued::None;
    pad.recordingOpen = pad.firstLoop = pad.overdubbing = pad.undoAfterOverdub = false;
    pad.wasPlaying = false;
    pad.dirty = true;
    resetClockIfIdle();
    emit changed();
}

void LooperEngine::setPadOneShot(int index, bool oneShot)
{
    if (index < 0 || index >= kMaxPads) return;
    pads_[size_t(index)].oneShot = oneShot;
    emit changed();
}

void LooperEngine::setPadGroup(int index, int group)
{
    if (index < 0 || index >= kMaxPads) return;
    pads_[size_t(index)].group = std::clamp(group, 0, 4);
    emit changed();
}

void LooperEngine::setPadCount(int count)
{
    count = std::clamp(count, 4, kMaxPads);
    // Pads with something in them stay.
    for (int i = count; i < padCount_; ++i)
        if (!pads_[size_t(i)].versions.empty() || pads_[size_t(i)].recording)
            count = std::min(kMaxPads, (i / 4 + 1) * 4);
    padCount_ = count;
    QSettings().setValue("looper/pads", padCount_);
    emit changed();
}

void LooperEngine::play()
{
    if (!edit_ || isRunning()) return;
    double at = 0.0;
    startTransport(false, false, at);
    for (int i = 0; i < padCount_; ++i)
        if (pads_[size_t(i)].wasPlaying && !pads_[size_t(i)].versions.empty())
            playPad(i, at);
    emit changed();
}

void LooperEngine::stop()
{
    if (!edit_ || !isRunning()) return;
    edit_->getTransport().stop(false, false);
}

void LooperEngine::stopAllPads()
{
    if (!edit_) return;
    for (int i = 0; i < padCount_; ++i) {
        auto& pad = pads_[size_t(i)];
        if (pad.overdubbing) endOverdub(i);
        if (isSounding(PadState(processors_[size_t(i)]->state.load())) || pad.queued == Queued::Play)
            stopPad(i);
    }
    emit changed();
}

void LooperEngine::clearAll()
{
    for (int i = 0; i < kMaxPads; ++i) clearPad(i);
    master_ = false;
    emit changed();
}

bool LooperEngine::isRunning() const
{
    return edit_ && edit_->getTransport().isPlaying();
}

void LooperEngine::establishClock(int index)
{
    auto& pad = pads_[size_t(index)];
    if (pad.length <= 0) return;
    const double sr = sampleRate();
    master_ = true;
    masterSecs_ = double(pad.length) / sr;
    masterAnchor_ = pad.anchor;

    if (!pad.firstLoop || !freeTempo_) return;
    pad.firstLoop = false;

    // The free first loop sets the tempo: a whole number of beats (1, 2, a
    // bar, or 2, 4, 8 or 16 bars), whichever lands nearest 70-140 BPM,
    // starting where it started.
    const double bpb = beatsPerBar();
    double best = 0.0, bestScore = 1.0e9;
    for (double beats : { 1.0, 2.0, bpb, 2 * bpb, 4 * bpb, 8 * bpb, 16 * bpb }) {
        const double bpm = beats * 60.0 / masterSecs_;
        const double score = bpm < 70.0 ? 70.0 - bpm : (bpm >= 140.0 ? bpm - 139.99 : 0.0);
        if (score < bestScore) { bestScore = score; best = bpm; }
    }
    best = std::clamp(best, 30.0, 300.0);
    const double startBeat = toBeats(masterAnchor_);
    if (startBeat < 0.001)
        em_.setBpm(best);
    else
        edit_->tempoSequence.insertTempo(tracktion::BeatPosition::fromBeats(startBeat), best, 0.0f);
}

void LooperEngine::resetClockIfIdle()
{
    for (auto& pad : pads_)
        if (!pad.versions.empty() || pad.recording) return;
    master_ = false;
}

void LooperEngine::setInputDevice(const QString& deviceName)
{
    input_ = deviceName;
    QSettings().setValue("looper/input", input_);
    bindInput();
    emit changed();
}

void LooperEngine::setMonitor(bool on)
{
    monitorOn_ = on;
    monitor_.enabled.store(on);
    QSettings().setValue("looper/monitor", on);
}

void LooperEngine::setQuantize(Quantize q)
{
    quantize_ = q;
    QSettings().setValue("looper/quantize", int(q));
}

void LooperEngine::setRecordBars(int bars)
{
    recordBars_ = std::max(0, bars);
    QSettings().setValue("looper/recordBars", recordBars_);
}

void LooperEngine::setFirstLoopSetsTempo(bool on)
{
    freeTempo_ = on;
    QSettings().setValue("looper/firstLoopSetsTempo", on);
}

void LooperEngine::setCountIn(bool on)
{
    countIn_ = on;
    QSettings().setValue("looper/countIn", on);
}

bool LooperEngine::click() const
{
    return edit_ && edit_->clickTrackEnabled.get() && clickOffAt_ < 0.0;
}

void LooperEngine::setClick(bool on)
{
    if (!edit_) return;
    clickOffAt_ = -1.0;
    edit_->clickTrackEnabled = on;
    QSettings().setValue("transport/metronomeEnabled", on);
    emit changed();
}

// ── Views ────────────────────────────────────────────────────────────────────

LooperEngine::PadView LooperEngine::padView(int index) const
{
    PadView v;
    if (index < 0 || index >= kMaxPads) return v;
    const auto& pad = pads_[size_t(index)];
    const auto& proc = *processors_[size_t(index)];
    v.state = PadState(proc.state.load());
    // Until the audio thread has caught up, show what was asked for.
    if (pad.recording && v.state == PadState::Empty) v.state = PadState::Recording;
    if (!pad.recording && pad.versions.empty()) v.state = PadState::Empty;
    v.queued = pad.queued;
    v.phase = proc.phase.load();
    v.undoDepth = int(pad.versions.size());
    v.redoDepth = int(pad.redo.size());
    v.oneShot = pad.oneShot;
    v.group = pad.group;
    v.overdubbing = pad.overdubbing;
    if (edit_ && pad.length > 0) {
        const double secs = double(pad.length) / sampleRate();
        v.beats = toBeats(pad.anchor + secs) - toBeats(pad.anchor);
        v.bars = v.beats / beatsPerBar();
    }
    if (auto* track = padTrack(index)) {
        v.hasTrack = true;
        v.name = QString::fromStdString(track->getName().toStdString());
        v.colour = em_.getTrackColour(track);
    }
    return v;
}

QString LooperEngine::statusText() const
{
    if (!edit_) return {};
    const double t = now();
    const double bpm = edit_->tempoSequence.getBpmAt(tracktion::TimePosition::fromSeconds(t));
    if (!master_) {
        if (freeTempo_)
            return "Tap a pad and play: the first loop sets the tempo";
        return QString("%1 BPM  ·  tap a pad to record").arg(bpm, 0, 'f', 1);
    }
    const double beats = toBeats(masterAnchor_ + masterSecs_) - toBeats(masterAnchor_);
    const double bars = beats / beatsPerBar();
    QString text = QString("Loop %1  ·  %2 BPM").arg(lengthText(bars, beats)).arg(bpm, 0, 'f', 1);
    if (isRunning() && bars >= 1.0) {
        double k = std::fmod((t - masterAnchor_) / masterSecs_, 1.0);
        if (k < 0.0) k += 1.0;
        text += QString("  ·  bar %1").arg(int(std::floor(k * bars + 1e-6)) + 1);
    }
    return text;
}

QString LooperEngine::lengthText(double bars, double beats)
{
    auto plural = [](double n, const char* unit) {
        return QString("%1 %2%3").arg(n, 0, 'g', 3).arg(unit).arg(std::abs(n - 1.0) < 0.01 ? "" : "s");
    };
    return bars < 0.99 ? plural(beats, "beat") : plural(bars, "bar");
}

std::vector<LooperEngine::LivePrint> LooperEngine::livePrints() const
{
    std::vector<LivePrint> out;
    const double rt = roundTrip();
    for (int i = 0; i < kMaxPads; ++i) {
        const auto& pad = pads_[size_t(i)];
        const auto& proc = *processors_[size_t(i)];
        if (!pad.writer || !pad.thumbnail || !proc.streamStarted.load()) continue;
        LivePrint lp;
        lp.track = pad.track;
        lp.thumbnail = pad.thumbnail;
        lp.origin = proc.streamOrigin.load();
        lp.segments = pad.segments;
        if (pad.openSince >= 0.0)
            lp.segments.push_back({ pad.openSince, std::max(pad.openSince, proc.lastTime.load() - rt) });
        if (!lp.segments.empty()) out.push_back(std::move(lp));
    }
    return out;
}

// ── Poll: events, transport, housekeeping ────────────────────────────────────

void LooperEngine::poll()
{
    if (!edit_) return;
    ++pollCount_;

    if (!monitorAttached_)
        if (auto* tap = em_.audioEngine().loudnessTap()) {
            tap->setMonitor(&monitor_);
            monitorAttached_ = true;
        }

    for (int i = 0; i < kMaxPads; ++i) {
        LooperEvent ev;
        while (processors_[size_t(i)]->popEvent(ev)) handleEvent(i, ev);
    }

    const bool running = isRunning();
    if (running) {
        sessionActive_ = true;
        for (int i = 0; i < kMaxPads; ++i)
            pads_[size_t(i)].wasPlaying = isSounding(PadState(processors_[size_t(i)]->state.load()));
    } else if (sessionActive_) {
        endSession();
    }

    if (clickOffAt_ >= 0.0 && (!running || now() >= clickOffAt_)) {
        edit_->clickTrackEnabled = false;
        clickOffAt_ = -1.0;
    }

    for (int i = 0; i < kMaxPads; ++i) {
        auto& pad = pads_[size_t(i)];
        if (pad.queued != Queued::None
            && (!running || processors_[size_t(i)]->lastTime.load() >= pad.queuedAt - 0.001))
            pad.queued = Queued::None;
        // A pad whose track was deleted (or undone) is empty again.
        if (pad.track.isValid() && !padTrack(i)) {
            clearPad(i);
            pad.track = {};
        }
    }

    // Free buffers the audio thread has let go of. One with no track to play
    // it is safe after a moment (the graph without it has taken over).
    const auto nowMs = juce::Time::getMillisecondCounter();
    graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(), [&](const Buried& b) {
        return processors_[size_t(b.pad)]->processedSeq.load() >= b.seq
            || (!padTrack(b.pad) && nowMs - b.since > 2000);
    }), graveyard_.end());

    // The input instance is replaced when the device setup changes.
    if (pollCount_ % 16 == 0) {
        bool stillThere = false;
        if (boundInput_)
            for (auto* idi : edit_->getAllInputDevices())
                if (idi == boundInput_) stillThere = true;
        if (!stillThere && !input_.isEmpty()) bindInput();
        else {
            const double rt = roundTrip();
            for (auto& p : processors_) p->setRoundTripSeconds(rt);
        }
    }
}

void LooperEngine::handleEvent(int index, const LooperEvent& ev)
{
    auto& pad = pads_[size_t(index)];
    switch (ev.type) {
    case LooperEvent::Audible:
        if (pad.writer && pad.openSince < 0.0) pad.openSince = ev.time;
        break;
    case LooperEvent::Silent:
        if (pad.openSince >= 0.0) {
            pad.segments.push_back({ pad.openSince, std::max(pad.openSince, ev.time) });
            pad.openSince = -1.0;
        }
        break;
    case LooperEvent::RecordDone:
        if (pad.recording) {
            pad.versions = { pad.recording };
            pad.recording.reset();
            pad.length = ev.length;
            pad.anchor = ev.time - double(ev.length) / sampleRate();
            pad.recordingOpen = false;
            pad.dirty = true;
            if (!master_) establishClock(index);
            pad.firstLoop = false;
            emit changed();
        }
        break;
    case LooperEvent::RecordAborted:
        bury(index, pad.recording);
        pad.recording.reset();
        pad.recordingOpen = pad.firstLoop = false;
        emit changed();
        break;
    case LooperEvent::OverdubDone:
        if (pad.overdubTarget) {
            for (auto& b : pad.redo) bury(index, b);
            pad.redo.clear();
            pad.versions.push_back(pad.overdubTarget);
            pad.overdubTarget.reset();
            pad.dirty = true;
        }
        pad.overdubbing = false;
        if (pad.undoAfterOverdub) {
            pad.undoAfterOverdub = false;
            undoPad(index);
        }
        emit changed();
        break;
    }
}

// ── Printing the performance ─────────────────────────────────────────────────

juce::File LooperEngine::printFolder() const
{
    const auto file = em_.currentFile();
    if (file != juce::File())
        return file.getParentDirectory().getChildFile(file.getFileNameWithoutExtension() + " Looper");
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("OpenDaw").getChildFile("Looper");
}

void LooperEngine::ensureWriter(int index)
{
    auto& pad = pads_[size_t(index)];
    if (!sessionActive_ || pad.writer) return;
    auto* track = padTrack(index);
    if (!track) return;

    const auto folder = printFolder();
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile(
        EditManager::sanitizeForFilename(track->getName()) + " performance", ".wav", false);
    auto out = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    if (!out) return;
    auto opts = juce::AudioFormatWriterOptions{}
                    .withSampleRate(sampleRate())
                    .withNumChannels(2)
                    .withBitsPerSample(24);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(out, opts);
    if (!writer) { file.deleteFile(); return; }

    auto& eng = edit_->engine;
    pad.thumbnail = std::make_shared<juce::AudioThumbnail>(
        256, eng.getAudioFileFormatManager().readFormatManager,
        eng.getAudioFileManager().getAudioThumbnailCache());
    pad.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer.release(), writerThread_, 65536);
    pad.writer->setDataReceiver(pad.thumbnail.get());
    pad.printFile = file;
    pad.segments.clear();
    pad.openSince = -1.0;

    auto& proc = *processors_[size_t(index)];
    proc.setRoundTripSeconds(roundTrip());
    proc.setPrintWriter(pad.writer.get());
}

void LooperEngine::endSession()
{
    sessionActive_ = false;
    const double rt = roundTrip();
    std::vector<PrintJob> jobs;
    for (int i = 0; i < kMaxPads; ++i) {
        auto& pad = pads_[size_t(i)];
        if (!pad.writer) continue;
        auto& proc = *processors_[size_t(i)];
        const double end = proc.lastTime.load() - rt;
        if (pad.openSince >= 0.0) {
            pad.segments.push_back({ pad.openSince, std::max(pad.openSince, end) });
            pad.openSince = -1.0;
        }
        proc.setPrintWriter(nullptr);
        pad.writer.reset();   // flushes and closes the file
        pad.thumbnail.reset();

        PrintJob job;
        job.track = pad.track;
        job.file = pad.printFile;
        job.origin = proc.streamOrigin.load();
        job.end = end;
        job.segments = std::move(pad.segments);
        pad.segments.clear();
        if (auto* track = padTrack(i)) job.name = QString::fromStdString(track->getName().toStdString());
        if (proc.streamStarted.load()) jobs.push_back(std::move(job));
        else pad.printFile.deleteFile();
    }
    printPerformance(std::move(jobs));
    emit changed();
}

void LooperEngine::printPerformance(std::vector<PrintJob> jobs)
{
    bool any = false;
    for (auto& job : jobs) any = any || !job.segments.empty();
    if (!any || !edit_) {
        for (auto& job : jobs) job.file.deleteFile();
        return;
    }

    auto& um = edit_->getUndoManager();
    um.beginNewTransaction("Looper Performance");
    auto& fm = edit_->engine.getAudioFileFormatManager().readFormatManager;

    for (auto& job : jobs) {
        double fileEnd = job.origin;
        {
            std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(job.file));
            if (reader && reader->sampleRate > 0.0)
                fileEnd = job.origin + double(reader->lengthInSamples) / reader->sampleRate;
        }
        auto* track = te::findAudioTrackForID(*edit_, job.track);
        int added = 0;
        if (track) {
            for (auto [a, b] : job.segments) {
                a = std::max(a, job.origin);
                b = std::min(b, fileEnd);
                if (b - a < 0.02) continue;
                const tracktion::TimeRange range(tracktion::TimePosition::fromSeconds(a),
                                                 tracktion::TimePosition::fromSeconds(b));
                // The loop replaced what the track had there while it played.
                juce::Array<te::Clip*> covered;
                for (auto* c : track->getClips())
                    if (c->getPosition().time.overlaps(range)) covered.add(c);
                for (auto* c : covered) te::deleteRegion(*c, range);

                auto clip = te::insertWaveClip(*track, juce::String(job.name.toStdString()), job.file,
                                               { range, tracktion::TimeDuration::fromSeconds(a - job.origin) },
                                               te::DeleteExistingClips::no);
                if (!clip) continue;
                clip->setFadeIn(tracktion::TimeDuration::fromSeconds(kPrintFadeSecs));
                clip->setFadeOut(tracktion::TimeDuration::fromSeconds(kPrintFadeSecs));
                ++added;
            }
        }
        if (added == 0) job.file.deleteFile();
    }
    em_.notifyEdited();
    emit printed();
}

} // namespace OpenDaw
