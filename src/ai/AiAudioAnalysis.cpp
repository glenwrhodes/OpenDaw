#include "AiAudioAnalysis.h"
#include <QJsonArray>
#include <cmath>

namespace OpenDaw {

AiAudioAnalysis::AiAudioAnalysis(EditManager* editMgr)
    : editMgr_(editMgr)
{
    if (editMgr_) {
        // Item IDs restart in every Edit, so entries can't survive a New/Open.
        aboutToChangeEditConn_ = QObject::connect(
            editMgr_, &EditManager::aboutToChangeEdit, [this]() { detachAll(); });
        tracksChangedConn_ = QObject::connect(editMgr_, &EditManager::tracksChanged, [this]() {
            if (!editMgr_) return;
            auto tracks = editMgr_->getAudioTracks();
            std::unordered_set<qulonglong> liveIds;
            for (auto* t : tracks)
                liveIds.insert(static_cast<qulonglong>(t->itemID.getRawID()));

            for (auto it = meterClients_.begin(); it != meterClients_.end();) {
                if (liveIds.count(it->first) == 0) {
                    detachEntry(it->second);
                    it = meterClients_.erase(it);
                } else {
                    ++it;
                }
            }
        });
    }
}

AiAudioAnalysis::~AiAudioAnalysis()
{
    QObject::disconnect(tracksChangedConn_);
    QObject::disconnect(aboutToChangeEditConn_);
    detachAll();
}

void AiAudioAnalysis::detachEntry(MeterClientEntry& entry)
{
    if (entry.plugin && entry.client)
        entry.plugin->measurer.removeClient(*entry.client);
    entry.plugin = nullptr;
}

void AiAudioAnalysis::detachAll()
{
    for (auto& [id, entry] : meterClients_)
        detachEntry(entry);
    meterClients_.clear();
}

AiAudioAnalysis::MeterClientEntry* AiAudioAnalysis::ensureClient(te::AudioTrack* track)
{
    if (!track) return nullptr;
    auto* plugin = track->getLevelMeterPlugin();
    if (!plugin) return nullptr;

    const qulonglong key = static_cast<qulonglong>(track->itemID.getRawID());
    auto it = meterClients_.find(key);
    if (it == meterClients_.end()) {
        MeterClientEntry entry;
        entry.client = std::make_unique<te::LevelMeasurer::Client>();
        entry.plugin = plugin;
        plugin->measurer.addClient(*entry.client);
        it = meterClients_.emplace(key, std::move(entry)).first;
    } else if (it->second.plugin.get() != plugin) {
        detachEntry(it->second);
        it->second.plugin = plugin;
        if (!it->second.client)
            it->second.client = std::make_unique<te::LevelMeasurer::Client>();
        plugin->measurer.addClient(*it->second.client);
    }
    return &it->second;
}

std::pair<float, float> AiAudioAnalysis::readTrackLevels(te::AudioTrack* track)
{
    auto* entry = ensureClient(track);
    if (!entry || !entry->client)
        return {0.0f, 0.0f};

    const auto l = entry->client->getAndClearAudioLevel(0);
    const auto r = entry->client->getAndClearAudioLevel(1);
    const float lLin = static_cast<float>(std::pow(10.0, l.dB / 20.0));
    const float rLin = static_cast<float>(std::pow(10.0, r.dB / 20.0));
    return {lLin, rLin};
}

double AiAudioAnalysis::linearToDb(double linear)
{
    if (linear <= 0.000001) return -120.0;
    return 20.0 * std::log10(linear);
}

double AiAudioAnalysis::clamp01(double v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

QString AiAudioAnalysis::inferRoleFromName(const QString& trackName)
{
    const QString n = trackName.toLower();
    if (n.contains("kick") || n.contains("snare") || n.contains("drum")
        || n.contains("tom") || n.contains("hat") || n.contains("perc")
        || n.contains("cymbal") || n.contains("overhead") || n.contains("oh"))
        return "drums";
    if (n.contains("bass") || n.contains("808") || n.contains("sub"))
        return "bass";
    if (n.contains("vocal") || n.contains("vox") || n.contains("voice")
        || n.contains("bgv") || n.contains("choir") || n.contains("harmony"))
        return "vocals";
    if (n.contains("guitar") || n.contains("piano") || n.contains("keys")
        || n.contains("synth") || n.contains("organ") || n.contains("pad")
        || n.contains("string") || n.contains("violin") || n.contains("cello")
        || n.contains("brass") || n.contains("trumpet") || n.contains("horn")
        || n.contains("flute") || n.contains("oboe") || n.contains("clarinet"))
        return "music";
    if (n.contains("fx") || n.contains("sfx") || n.contains("foley")
        || n.contains("ambient") || n.contains("noise"))
        return "fx";
    if (n.contains("bus") || n.contains("group") || n.contains("submix"))
        return "bus";
    return "unknown";
}

// ── Level analysis (uses real meter data) ───────────────────────────────────

QJsonObject AiAudioAnalysis::analyzeTrackLevels(te::AudioTrack* track, double windowSeconds)
{
    QJsonObject out;
    if (!track) {
        out["error"] = "Track not found.";
        return out;
    }

    const auto [l, r] = readTrackLevels(track);
    const double peakLin = std::max<double>(l, r);
    const double peakDb = linearToDb(peakLin);

    out["track"] = QString::fromStdString(track->getName().toStdString());
    out["window_seconds"] = windowSeconds;
    out["peak_left_linear"] = static_cast<double>(l);
    out["peak_right_linear"] = static_cast<double>(r);
    out["peak_dbfs"] = peakDb;
    out["true_peak_est_dbfs"] = peakDb + 0.3;
    out["is_clipping"] = peakDb > -0.3;
    out["is_silent"] = peakDb < -60.0;
    out["data_source"] = "LevelMeterPlugin (real-time meter reading)";
    out["limitations"] = "This is an instantaneous reading, not an integrated measurement. "
                         "For best results, call this while audio is playing.";
    return out;
}

QJsonObject AiAudioAnalysis::analyzeMasterLevels(double windowSeconds)
{
    QJsonObject out;
    if (!editMgr_ || !editMgr_->edit()) {
        out["error"] = "No project loaded.";
        return out;
    }

    auto tracks = editMgr_->getAudioTracks();
    double peakLin = 0.0;
    int contributing = 0;
    QJsonArray perTrack;

    for (auto* t : tracks) {
        if (!t || t->isMuted(false)) continue;
        const auto [tl, tr] = readTrackLevels(t);
        const double trPeak = std::max<double>(tl, tr);
        if (trPeak > 0.000001) {
            peakLin = std::max(peakLin, trPeak);
            ++contributing;
            QJsonObject ti;
            ti["track"] = QString::fromStdString(t->getName().toStdString());
            ti["peak_dbfs"] = linearToDb(trPeak);
            perTrack.append(ti);
        }
    }

    const double peakDb = linearToDb(std::max(peakLin, 0.000001));

    out["window_seconds"] = windowSeconds;
    out["contributing_tracks"] = contributing;
    out["peak_dbfs"] = peakDb;
    out["true_peak_est_dbfs"] = peakDb + 0.3;
    out["is_clipping"] = peakDb > -0.3;
    out["per_track_peaks"] = perTrack;
    out["data_source"] = "Aggregated LevelMeterPlugin readings (real-time)";
    out["limitations"] = "LUFS and RMS require integrated measurement over time, which is not yet "
                         "available. Use peak data and track-level data to make gain staging decisions.";
    return out;
}

// ── Frequency/spectral (cannot measure -- returns what we know) ─────────────

QJsonObject AiAudioAnalysis::analyzeFrequencyBalance(te::AudioTrack* track)
{
    QJsonObject out;
    if (!track) {
        out["error"] = "Track not found.";
        return out;
    }

    const QString name = QString::fromStdString(track->getName().toStdString());
    const QString role = inferRoleFromName(name);

    out["track"] = name;
    out["inferred_role"] = role;
    out["has_eq"] = false;

    for (auto* p : track->pluginList.getPlugins()) {
        if (dynamic_cast<te::EqualiserPlugin*>(p)) {
            out["has_eq"] = true;
            QJsonArray eqParams;
            for (auto* param : p->getAutomatableParameters()) {
                QJsonObject po;
                po["name"] = QString::fromStdString(param->getParameterName().toStdString());
                po["value"] = static_cast<double>(param->getCurrentValue());
                eqParams.append(po);
            }
            out["eq_parameters"] = eqParams;
            break;
        }
    }

    out["data_source"] = "No FFT/spectral analysis available. Only track name, inferred role, "
                         "and existing EQ state are returned. Use your audio engineering knowledge "
                         "to make assumptions about likely frequency content based on the role.";
    return out;
}

QJsonObject AiAudioAnalysis::analyzeMasterFrequencyBalance()
{
    QJsonObject out;
    auto tracks = editMgr_ ? editMgr_->getAudioTracks() : juce::Array<te::AudioTrack*>();
    if (tracks.isEmpty()) {
        out["error"] = "No tracks available.";
        return out;
    }

    QJsonArray trackRoles;
    for (auto* t : tracks) {
        if (t->isMuted(false)) continue;
        QJsonObject tr;
        tr["track"] = QString::fromStdString(t->getName().toStdString());
        tr["inferred_role"] = inferRoleFromName(QString::fromStdString(t->getName().toStdString()));
        trackRoles.append(tr);
    }
    out["active_tracks_by_role"] = trackRoles;
    out["data_source"] = "No FFT/spectral analysis available. Track roles are inferred from names. "
                         "Use this to understand the ensemble composition and plan EQ decisions.";
    return out;
}

// ── Stereo image (uses real pan/mono data) ──────────────────────────────────

QJsonObject AiAudioAnalysis::analyzeStereoImage(te::AudioTrack* track)
{
    QJsonObject out;
    if (!track) {
        out["error"] = "Track not found.";
        return out;
    }

    double pan = 0.0;
    bool mono = editMgr_ ? editMgr_->isTrackMono(track) : false;
    for (auto* p : track->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(p)) {
            pan = vp->pan.get();
            break;
        }
    }

    out["track"] = QString::fromStdString(track->getName().toStdString());
    out["pan"] = pan;
    out["is_mono"] = mono;
    out["data_source"] = "Real pan and mono settings from VolumeAndPanPlugin.";
    return out;
}

QJsonObject AiAudioAnalysis::analyzeMasterStereoImage()
{
    QJsonObject out;
    auto tracks = editMgr_ ? editMgr_->getAudioTracks() : juce::Array<te::AudioTrack*>();
    if (tracks.isEmpty()) {
        out["error"] = "No tracks available.";
        return out;
    }

    QJsonArray panMap;
    int monoCount = 0;
    for (auto* t : tracks) {
        if (t->isMuted(false)) continue;
        auto img = analyzeStereoImage(t);
        if (img.contains("error")) continue;
        panMap.append(img);
        if (img["is_mono"].toBool()) ++monoCount;
    }
    out["tracks"] = panMap;
    out["mono_track_count"] = monoCount;
    out["data_source"] = "Real pan and mono settings from each track.";
    return out;
}

// ── Transients (uses real level + effect chain data) ────────────────────────

QJsonObject AiAudioAnalysis::analyzeTransients(te::AudioTrack* track)
{
    QJsonObject out;
    if (!track) {
        out["error"] = "Track not found.";
        return out;
    }

    auto level = analyzeTrackLevels(track, 1.0);

    bool hasCompressor = false;
    QJsonArray compParams;
    for (auto* p : track->pluginList.getPlugins()) {
        if (auto* comp = dynamic_cast<te::CompressorPlugin*>(p)) {
            hasCompressor = true;
            for (auto* param : comp->getAutomatableParameters()) {
                QJsonObject po;
                po["name"] = QString::fromStdString(param->getParameterName().toStdString());
                po["value"] = static_cast<double>(param->getCurrentValue());
                compParams.append(po);
            }
            break;
        }
    }

    const QString name = QString::fromStdString(track->getName().toStdString());
    out["track"] = name;
    out["inferred_role"] = inferRoleFromName(name);
    out["peak_dbfs"] = level["peak_dbfs"];
    out["has_compressor"] = hasCompressor;
    if (hasCompressor) out["compressor_parameters"] = compParams;
    out["data_source"] = "Peak level is real (from meter). Compressor state is real. "
                         "Transient character must be inferred from role, peak behavior, "
                         "and compressor settings using audio engineering knowledge.";
    return out;
}

// ── Masking (returns factual data for AI to reason about) ───────────────────

QJsonObject AiAudioAnalysis::analyzeMasking(te::AudioTrack* trackA, te::AudioTrack* trackB)
{
    QJsonObject out;
    if (!trackA || !trackB) {
        out["error"] = "Both tracks are required.";
        return out;
    }

    const QString nameA = QString::fromStdString(trackA->getName().toStdString());
    const QString nameB = QString::fromStdString(trackB->getName().toStdString());

    out["track_a"] = nameA;
    out["track_b"] = nameB;
    out["role_a"] = inferRoleFromName(nameA);
    out["role_b"] = inferRoleFromName(nameB);

    auto levelA = analyzeTrackLevels(trackA, 1.0);
    auto levelB = analyzeTrackLevels(trackB, 1.0);
    out["peak_a_dbfs"] = levelA["peak_dbfs"];
    out["peak_b_dbfs"] = levelB["peak_dbfs"];

    double panA = 0.0, panB = 0.0;
    for (auto* p : trackA->pluginList.getPlugins())
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(p)) { panA = vp->pan.get(); break; }
    for (auto* p : trackB->pluginList.getPlugins())
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(p)) { panB = vp->pan.get(); break; }

    out["pan_a"] = panA;
    out["pan_b"] = panB;
    out["pan_separation"] = std::abs(panA - panB);

    out["data_source"] = "Roles are inferred from track names. Levels and pans are real. "
                         "No spectral overlap measurement is available. Use track roles, "
                         "relative levels, and pan positions to assess masking risk.";
    return out;
}

} // namespace OpenDaw
