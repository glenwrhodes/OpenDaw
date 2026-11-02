#include "SoundFontInstrument.h"

#define TSF_IMPLEMENTATION
#include "tsf.h"

#include <algorithm>
#include <map>

namespace OpenDaw {

namespace {
const juce::Identifier kBankProp("sfBank");
const juce::Identifier kProgramProp("sfProgram");
const juce::Identifier kFollowProp("sfFollowProgramChanges");
constexpr const char* kSoundFontName = "GeneralUser-GS.sf2";
constexpr int kMaxVoices = 128;
constexpr int kChannels = 16;
constexpr int kDrumChannel = 9;           // MIDI channel 10
constexpr int kRenderChunk = 4096;
constexpr float kOutputGainDb = -9.0f;    // GM sound sets are made hot; this matches other players

// The 16 General MIDI families, 8 programs each.
const char* const kFamilies[] = {
    "Piano", "Chromatic Percussion", "Organ", "Guitar", "Bass", "Strings",
    "Ensemble & Choir", "Brass", "Reed", "Pipe", "Synth Lead", "Synth Pad",
    "Synth Effects", "Ethnic", "Percussive", "Sound Effects",
};
const char* const kDrumKits = "Drum Kits";
}

// ── Library ──────────────────────────────────────────────────────────────────

SoundFontLibrary& SoundFontLibrary::instance()
{
    static SoundFontLibrary lib;
    return lib;
}

QStringList SoundFontLibrary::categories()
{
    QStringList out;
    for (auto* f : kFamilies) out << f;
    out << kDrumKits;
    return out;
}

void SoundFontLibrary::load()
{
    tried_ = true;
    // Next to the program (Windows, and builds), in the app bundle's
    // Resources (macOS), or in share/opendaw (Linux AppImage).
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto dir = exe.getParentDirectory();
    const juce::File candidates[] = {
        dir.getChildFile("soundfonts").getChildFile(kSoundFontName),
        dir.getParentDirectory().getChildFile("Resources/soundfonts").getChildFile(kSoundFontName),
        dir.getParentDirectory().getChildFile("share/opendaw/soundfonts").getChildFile(kSoundFontName),
    };
    for (const auto& f : candidates)
        if (f.existsAsFile()) { file_ = f; break; }
    if (file_ == juce::File()) return;

    juce::MemoryBlock data;
    if (!file_.loadFileAsData(data)) return;
    master_ = tsf_load_memory(data.getData(), int(data.getSize()));
    if (!master_) return;

    for (int i = 0; i < master_->presetNum; ++i) {
        const auto& p = master_->presets[i];
        Preset preset;
        preset.bank = p.bank;
        preset.program = p.preset;
        preset.name = QString::fromLatin1(p.presetName).simplified();
        if (preset.bank == 128) {
            if (preset.program == 127) continue;   // a CM-64 sound-effects map, not a kit
            preset.category = kDrumKits;
        } else if (preset.bank < 120 && preset.program < 128) {
            preset.category = kFamilies[preset.program / 8];
        } else {
            continue;                              // MT-32 compatibility banks
        }
        presets_.push_back(preset);
    }
    // General MIDI sounds first in each family, then the variations.
    std::stable_sort(presets_.begin(), presets_.end(), [](const Preset& a, const Preset& b) {
        const int fa = a.isDrumKit() ? 16 : a.program / 8, fb = b.isDrumKit() ? 16 : b.program / 8;
        if (fa != fb) return fa < fb;
        const bool ga = a.bank == 0 || a.isDrumKit(), gb = b.bank == 0 || b.isDrumKit();
        if (ga != gb) return ga;
        if (a.program != b.program) return a.program < b.program;
        return a.bank < b.bank;
    });
}

tsf* SoundFontLibrary::master()
{
    if (!tried_) load();
    return master_;
}

bool SoundFontLibrary::isAvailable()
{
    return master() != nullptr;
}

const std::vector<SoundFontLibrary::Preset>& SoundFontLibrary::presets()
{
    master();
    return presets_;
}

const SoundFontLibrary::Preset* SoundFontLibrary::find(int bank, int program)
{
    for (const auto& p : presets())
        if (p.bank == bank && p.program == program) return &p;
    return nullptr;
}

const SoundFontLibrary::Preset* SoundFontLibrary::findByName(const QString& text)
{
    const QString t = text.trimmed().toLower();
    if (t.isEmpty()) return nullptr;

    bool isNumber = false;
    const int number = t.toInt(&isNumber);
    if (isNumber && number >= 1 && number <= 128) return find(0, number - 1);
    if (t == "drums" || t == "drum kit" || t == "drum" || t == "kit") return find(128, 0);

    for (const auto& p : presets())
        if (p.name.toLower() == t) return &p;
    // Then the first sound whose name contains every word asked for.
    const auto words = t.split(' ', Qt::SkipEmptyParts);
    for (const auto& p : presets()) {
        const QString name = p.name.toLower();
        if (std::all_of(words.begin(), words.end(), [&](const QString& w) { return name.contains(w); }))
            return &p;
    }
    return nullptr;
}

// ── Plugin ───────────────────────────────────────────────────────────────────

const char* SoundFontPlugin::xmlTypeName = "opendawSoundFont";

SoundFontPlugin::SoundFontPlugin(te::PluginCreationInfo info) : te::Plugin(info)
{
    auto* um = getUndoManager();
    bank_.referTo(state, kBankProp, um, 0);
    program_.referTo(state, kProgramProp, um, 0);
    followProgramChanges_.referTo(state, kFollowProp, um, false);
    syncSettings();

    if (auto* master = SoundFontLibrary::instance().master()) {
        synth_ = tsf_copy(master);
        if (synth_) {
            // Everything that allocates happens here, not on the audio thread.
            tsf_set_max_voices(synth_, kMaxVoices);
            for (int ch = kChannels - 1; ch >= 0; --ch)
                tsf_channel_set_bank_preset(synth_, ch, 0, 0);
            tsf_set_output(synth_, TSF_STEREO_UNWEAVED, int(sampleRate_), kOutputGainDb);
        }
    }
    scratch_.assign(size_t(kRenderChunk) * 2, 0.0f);
}

SoundFontPlugin::~SoundFontPlugin()
{
    notifyListenersOfDeletion();
    const juce::SpinLock::ScopedLockType lock(synthLock_);
    if (synth_) tsf_close(synth_);
    synth_ = nullptr;
}

juce::ValueTree SoundFontPlugin::create(int bank, int program)
{
    juce::ValueTree v(te::IDs::PLUGIN);
    v.setProperty(te::IDs::type, xmlTypeName, nullptr);
    v.setProperty(kBankProp, bank, nullptr);
    v.setProperty(kProgramProp, program, nullptr);
    return v;
}

juce::String SoundFontPlugin::getName() const
{
    if (auto* p = SoundFontLibrary::instance().find(bank_.get(), program_.get()))
        return juce::String(p->name.toStdString());
    return getPluginName();
}

void SoundFontPlugin::setPreset(int bank, int program)
{
    if (bank == bank_.get() && program == program_.get()) return;
    bank_ = bank;
    program_ = program;
}

void SoundFontPlugin::setFollowsProgramChanges(bool on)
{
    followProgramChanges_ = on;
}

void SoundFontPlugin::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree == state && (id == kBankProp || id == kProgramProp || id == kFollowProp)) {
        syncSettings();
        presetChanged_.store(true);
    }
    te::Plugin::valueTreePropertyChanged(tree, id);
}

void SoundFontPlugin::syncSettings()
{
    bankNow_.store(int(state.getProperty(kBankProp, 0)));
    programNow_.store(int(state.getProperty(kProgramProp, 0)));
    followNow_.store(bool(state.getProperty(kFollowProp, false)));
}

void SoundFontPlugin::initialise(const te::PluginInitialisationInfo& info)
{
    const juce::SpinLock::ScopedLockType lock(synthLock_);
    sampleRate_ = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    if (synth_) tsf_set_output(synth_, TSF_STEREO_UNWEAVED, int(sampleRate_), kOutputGainDb);
    presetChanged_.store(true);
}

void SoundFontPlugin::reset()
{
    resetRequested_.store(true);
}

void SoundFontPlugin::applyPresetToChannels()
{
    const int bank = bankNow_.load();
    const int program = programNow_.load();
    const bool follow = followNow_.load();
    for (int ch = 0; ch < kChannels; ++ch) {
        if (follow && ch == kDrumChannel && bank != 128) {
            tsf_channel_set_bank_preset(synth_, ch, 128, 0);
            continue;
        }
        if (!tsf_channel_set_bank_preset(synth_, ch, bank, program))
            tsf_channel_set_bank_preset(synth_, ch, 0, std::clamp(program, 0, 127));
    }
}

void SoundFontPlugin::handleMidi(const juce::MidiMessage& m)
{
    const int ch = std::clamp(m.getChannel() - 1, 0, kChannels - 1);
    if (m.isNoteOn()) {
        tsf_channel_note_on(synth_, ch, m.getNoteNumber(), m.getVelocity() / 127.0f);
    } else if (m.isNoteOff()) {
        tsf_channel_note_off(synth_, ch, m.getNoteNumber());
    } else if (m.isPitchWheel()) {
        tsf_channel_set_pitchwheel(synth_, ch, m.getPitchWheelValue());
    } else if (m.isController()) {
        const int cc = m.getControllerNumber();
        // Bank select only matters to a General MIDI player.
        if ((cc == 0 || cc == 32) && !followNow_.load()) return;
        tsf_channel_midi_control(synth_, ch, cc, m.getControllerValue());
    } else if (m.isProgramChange()) {
        if (followNow_.load())
            tsf_channel_set_presetnumber(synth_, ch, m.getProgramChangeNumber(), ch == kDrumChannel);
    } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
        tsf_channel_note_off_all(synth_, ch);
    }
}

void SoundFontPlugin::render(float* left, float* right, int numSamples)
{
    while (numSamples > 0) {
        const int n = std::min(numSamples, kRenderChunk);
        tsf_render_float(synth_, scratch_.data(), n, 0);
        // Unweaved: n left samples, then n right samples.
        std::copy(scratch_.begin(), scratch_.begin() + n, left);
        if (right) {
            std::copy(scratch_.begin() + n, scratch_.begin() + 2 * n, right);
            right += n;
        }
        left += n;
        numSamples -= n;
    }
}

void SoundFontPlugin::applyToBuffer(const te::PluginRenderContext& fc)
{
    auto* dest = fc.destBuffer;
    if (!dest || fc.bufferNumSamples <= 0) return;
    const int start = fc.bufferStartSample;
    const int n = fc.bufferNumSamples;
    for (int c = 0; c < dest->getNumChannels(); ++c)
        dest->clear(c, start, n);

    const juce::SpinLock::ScopedTryLockType lock(synthLock_);
    if (!lock.isLocked() || !synth_) return;

    if (resetRequested_.exchange(false))
        for (int ch = 0; ch < kChannels; ++ch) {
            tsf_channel_sounds_off_all(synth_, ch);
            tsf_channel_set_sustain(synth_, ch, 0);
        }
    if (presetChanged_.exchange(false))
        applyPresetToChannels();

    float* left = dest->getWritePointer(0, start);
    float* right = dest->getNumChannels() > 1 ? dest->getWritePointer(1, start) : nullptr;

    int pos = 0;
    if (auto* midi = fc.bufferForMidiMessages) {
        if (midi->isAllNotesOff)
            for (int ch = 0; ch < kChannels; ++ch) {
                tsf_channel_note_off_all(synth_, ch);
                tsf_channel_set_sustain(synth_, ch, 0);
            }
        // Render up to each event, then apply it, so notes start on their sample.
        for (auto& m : *midi) {
            const int at = std::clamp(int(std::lround(m.getTimeStamp() * sampleRate_)), pos, n);
            if (at > pos) {
                render(left + pos, right ? right + pos : nullptr, at - pos);
                pos = at;
            }
            handleMidi(m);
        }
    }
    if (pos < n)
        render(left + pos, right ? right + pos : nullptr, n - pos);
}

} // namespace OpenDaw
