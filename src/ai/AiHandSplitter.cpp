#include "AiHandSplitter.h"
#include "AiService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <utility>

namespace OpenDaw {

static const QString kApiUrl = "https://api.anthropic.com/v1/messages";
// Notes per request. Chunks are cut at bar lines, so one dense bar can exceed this.
static const int kMaxNotesPerChunk = 400;
static const int kMaxParallelRequests = 4;
static const int kMaxRetries = 2;
// Thinking shares max_tokens with the answer; the answer itself is ~3 tokens per note.
static const int kMaxTokens = 32000;
// Non-streaming: nothing arrives until the answer is complete.
static const int kTransferTimeoutMs = 300000;

static const char* kSystemPrompt =
    "You are an expert pianist and music engraver. You are given the notes of a "
    "piano performance that was recorded onto a single MIDI track. Work out which "
    "hand played each note, so the performance can be written on a grand staff: "
    "right hand on the treble staff, left hand on the bass staff.\n\n"
    "Do not use a fixed split point. Decide the way a pianist reading or "
    "transcribing the music would:\n"
    "- Playability: one hand covers about an octave comfortably and rarely more "
    "than a tenth (about 16 semitones) at once, and holds at most five notes. "
    "Notes still sounding from earlier onsets are still under that hand.\n"
    "- Hand position: each hand moves gradually and tends to stay in a region; "
    "follow both hands through time. A leap is plausible when there is time for "
    "it or the other hand could not have taken the note.\n"
    "- Texture and voices: the melody or top voice is usually the right hand; the "
    "bass line and accompaniment figures (Alberti bass, broken chords, stride, "
    "octave basses) are usually the left hand, even when they rise above middle C. "
    "The right hand can dip below middle C. Keep a voice, scale or arpeggio in the "
    "same hand until it clearly hands over.\n"
    "- Chords may be shared between the hands when one hand cannot reach them all. "
    "Hand crossings happen but are rare.\n\n"
    "Assign every note id exactly once, to either right_hand or left_hand.";

AiHandSplitter::AiHandSplitter(EditManager* editMgr, QObject* parent)
    : QObject(parent), editMgr_(editMgr), nam_(new QNetworkAccessManager(this))
{
}

AiHandSplitter::~AiHandSplitter()
{
    cancel();
}

bool AiHandSplitter::start(te::AudioTrack* track, QString& error)
{
    if (isRunning()) { error = "A hand split is already running."; return false; }
    if (!editMgr_ || !editMgr_->edit() || !track) { error = "No track selected."; return false; }
    if (editMgr_->isTrackFrozen(track)) { error = "Unfreeze the track first."; return false; }

    QSettings settings;
    apiKey_ = settings.value("ai/apiKey").toString().trimmed();
    if (apiKey_.isEmpty()) {
        error = "Set your Anthropic API key in Edit > AI Preferences first.";
        return false;
    }
    model_ = AiService::configuredModel();

    auto refs = editMgr_->getTrackMidiNotes(track);
    if (refs.size() < 2) {
        error = "This track has no MIDI notes to split.";
        return false;
    }

    auto& ts = editMgr_->edit()->tempoSequence;
    notes_.clear();
    notes_.reserve(static_cast<int>(refs.size()));
    for (auto& ref : refs) {
        auto range = ref.note->getEditTimeRange(*ref.clip);
        NoteInfo n;
        n.pitch = ref.note->getNoteNumber();
        n.velocity = ref.note->getVelocity();
        n.startBeat = ts.toBeats(range.getStart()).inBeats();
        n.lengthBeats = ts.toBeats(range.getEnd()).inBeats() - n.startBeat;
        notes_.append(n);
    }

    trackId_ = track->itemID;
    timeSigNum_ = editMgr_->getTimeSigNumerator();
    timeSigDen_ = editMgr_->getTimeSigDenominator();
    bpm_ = editMgr_->getBpm();
    assignment_ = QVector<int>(notes_.size(), -1);
    chunksDone_ = 0;

    buildChunks();
    retries_ = QVector<int>(chunks_.size(), 0);
    ++generation_;
    running_ = true;
    pending_.clear();
    for (int i = 0; i < chunks_.size(); ++i)
        pending_.append(i);

    emit progress(0, chunks_.size());
    sendNextChunks();
    return true;
}

void AiHandSplitter::cancel()
{
    ++generation_;
    running_ = false;
    pending_.clear();
    const auto replies = std::exchange(replies_, {});
    for (auto* reply : replies) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

bool AiHandSplitter::matchesTrack(te::AudioTrack* track) const
{
    if (!editMgr_ || !track || track->itemID != trackId_) return false;
    auto refs = editMgr_->getTrackMidiNotes(track);
    if (static_cast<int>(refs.size()) != notes_.size()) return false;
    for (int i = 0; i < notes_.size(); ++i) {
        if (refs[i].note->getNoteNumber() != notes_[i].pitch) return false;
        auto range = refs[i].note->getEditTimeRange(*refs[i].clip);
        double start = editMgr_->edit()->tempoSequence.toBeats(range.getStart()).inBeats();
        if (std::abs(start - notes_[i].startBeat) > 1e-6) return false;
    }
    return true;
}

// ── Chunking ────────────────────────────────────────────────────────────────

void AiHandSplitter::buildChunks()
{
    QVector<int> order(notes_.size());
    for (int i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        if (notes_[a].startBeat != notes_[b].startBeat)
            return notes_[a].startBeat < notes_[b].startBeat;
        return notes_[a].pitch < notes_[b].pitch;
    });

    const double beatsPerBar = timeSigNum_ * (4.0 / std::max(1, timeSigDen_));
    auto barOf = [beatsPerBar](double beat) {
        return static_cast<int>(std::floor(beat / beatsPerBar + 1e-9));
    };

    // Cut only where a new bar starts, so phrases and chords stay together.
    chunks_.clear();
    QVector<int> current;
    for (int i = 0; i < order.size(); ++i) {
        int id = order[i];
        bool newBar = i > 0 && barOf(notes_[id].startBeat) != barOf(notes_[order[i - 1]].startBeat);
        if (newBar && current.size() >= kMaxNotesPerChunk) {
            chunks_.append(current);
            current.clear();
        }
        current.append(id);
    }
    if (!current.isEmpty())
        chunks_.append(current);
}

QString AiHandSplitter::noteName(int pitch)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    return QString("%1%2").arg(names[((pitch % 12) + 12) % 12]).arg(pitch / 12 - 1);
}

QString AiHandSplitter::describeChunk(const QVector<int>& ids) const
{
    const double beatsPerBar = timeSigNum_ * (4.0 / std::max(1, timeSigDen_));

    QString text;
    text += QString("Time signature %1/%2, tempo %3 BPM. Middle C is C4 (MIDI 60).\n")
                .arg(timeSigNum_).arg(timeSigDen_).arg(bpm_, 0, 'f', 1);
    text += "Positions and durations are in quarter-note beats; bar and beat are 1-based.\n";
    text += "Notes are sorted by onset, then pitch. Columns: id, bar, beat in bar, "
            "pitch, MIDI number, duration, velocity.\n\n";

    for (int id : ids) {
        const auto& n = notes_[id];
        double bar = std::floor(n.startBeat / beatsPerBar + 1e-9);
        double beatInBar = n.startBeat - bar * beatsPerBar + 1.0;
        text += QString("%1 bar%2 b%3 %4 %5 d%6 v%7\n")
                    .arg(id)
                    .arg(static_cast<int>(bar) + 1)
                    .arg(beatInBar, 0, 'f', 2)
                    .arg(noteName(n.pitch))
                    .arg(n.pitch)
                    .arg(n.lengthBeats, 0, 'f', 2)
                    .arg(n.velocity);
    }
    return text;
}

// ── Requests ────────────────────────────────────────────────────────────────

void AiHandSplitter::sendNextChunks()
{
    while (running_ && !pending_.isEmpty() && replies_.size() < kMaxParallelRequests)
        sendChunk(pending_.takeFirst());
}

void AiHandSplitter::sendChunk(int chunkIndex)
{
    const auto& ids = chunks_[chunkIndex];

    QJsonObject body;
    body["model"] = model_;
    body["max_tokens"] = kMaxTokens;
    body["system"] = QString(kSystemPrompt);

    QString userText = describeChunk(ids);
    if (chunks_.size() > 1)
        userText = QString("This is part %1 of %2 of the piece.\n")
                       .arg(chunkIndex + 1).arg(chunks_.size()) + userText;

    const bool structured = AiService::supportsStructuredOutputs(model_);
    if (!structured)
        userText += "\nReply with only a JSON object of the form "
                    "{\"right_hand\": [ids], \"left_hand\": [ids]}.";

    QJsonObject textBlock;
    textBlock["type"] = "text";
    textBlock["text"] = userText;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = QJsonArray{ textBlock };
    body["messages"] = QJsonArray{ userMsg };

    QByteArray betas = AiService::applyModelOptions(body, model_, "high", true);

    if (structured) {
        QJsonObject idArray;
        idArray["type"] = "array";
        idArray["items"] = QJsonObject{ { "type", "integer" } };

        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject{ { "right_hand", idArray }, { "left_hand", idArray } };
        schema["required"] = QJsonArray{ "right_hand", "left_hand" };
        schema["additionalProperties"] = false;

        QJsonObject format;
        format["type"] = "json_schema";
        format["schema"] = schema;

        auto outputConfig = body["output_config"].toObject();
        outputConfig["format"] = format;
        body["output_config"] = outputConfig;
    }

    QNetworkRequest request{QUrl(kApiUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", apiKey_.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    if (!betas.isEmpty())
        request.setRawHeader("anthropic-beta", betas);
    request.setTransferTimeout(kTransferTimeoutMs);

    auto* reply = nam_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    replies_.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, chunkIndex]() {
        handleReply(reply, chunkIndex);
    });
}

void AiHandSplitter::handleReply(QNetworkReply* reply, int chunkIndex)
{
    replies_.removeOne(reply);
    reply->deleteLater();
    if (!running_) return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        // Rate limits and overload are worth another try; anything else is not.
        const bool retryable = status == 429 || status == 529 || status >= 500;
        int& retries = retries_[chunkIndex];
        if (retryable && retries < kMaxRetries) {
            ++retries;
            QTimer::singleShot(3000 * retries, this, [this, chunkIndex, gen = generation_]() {
                if (running_ && gen == generation_) { pending_.prepend(chunkIndex); sendNextChunks(); }
            });
            return;
        }

        QString message = reply->errorString();
        auto errObj = QJsonDocument::fromJson(data).object()["error"].toObject();
        if (!errObj.isEmpty())
            message = errObj["message"].toString();
        fail("The AI request failed: " + message);
        return;
    }

    auto response = QJsonDocument::fromJson(data).object();
    const QString stopReason = response["stop_reason"].toString();

    QString text;
    for (const auto& block : response["content"].toArray()) {
        auto obj = block.toObject();
        if (obj["type"].toString() == "text")
            text += obj["text"].toString();
    }

    QString problem;
    if (stopReason == "refusal") {
        problem = "the model declined this part";
    } else if (stopReason == "max_tokens") {
        problem = "the answer was cut off";
    } else {
        int open = text.indexOf('{');
        int close = text.lastIndexOf('}');
        auto doc = (open >= 0 && close > open)
            ? QJsonDocument::fromJson(text.mid(open, close - open + 1).toUtf8())
            : QJsonDocument();
        if (!doc.isObject())
            problem = "the answer was not valid JSON";
        else
            applyChunkResult(chunkIndex, doc.object(), problem);
    }
    if (!problem.isEmpty())
        qWarning() << "[AiHandSplitter] chunk" << chunkIndex << ":" << problem
                   << "- using fallback guesses for its notes";

    ++chunksDone_;
    emit progress(chunksDone_, chunks_.size());
    sendNextChunks();
    finishIfDone();
}

bool AiHandSplitter::applyChunkResult(int chunkIndex, const QJsonObject& result, QString& error)
{
    QSet<int> inChunk(chunks_[chunkIndex].begin(), chunks_[chunkIndex].end());
    QSet<int> seen, conflicting;

    auto take = [&](const QJsonArray& ids, int hand) {
        for (const auto& v : ids) {
            int id = v.toInt(-1);
            if (!inChunk.contains(id)) continue;      // ignore ids from elsewhere
            if (seen.contains(id) && assignment_[id] != hand)
                conflicting.insert(id);
            seen.insert(id);
            assignment_[id] = hand;
        }
    };
    take(result["right_hand"].toArray(), 0);
    take(result["left_hand"].toArray(), 1);

    // A note listed in both hands is treated as unanswered.
    for (int id : conflicting)
        assignment_[id] = -1;

    if (seen.isEmpty()) {
        error = "the answer assigned no notes";
        return false;
    }
    return true;
}

void AiHandSplitter::finishIfDone()
{
    if (!running_ || chunksDone_ < chunks_.size()) return;
    running_ = false;

    QVector<bool> isLeftHand(notes_.size());
    int fallbackNotes = 0;
    for (int i = 0; i < notes_.size(); ++i) {
        if (assignment_[i] < 0) {
            isLeftHand[i] = guessLeftHand(i);
            ++fallbackNotes;
        } else {
            isLeftHand[i] = assignment_[i] == 1;
        }
    }
    emit finished(isLeftHand, fallbackNotes);
}

// For notes the model left out: side with whichever hand was playing nearer in
// pitch around the same time, else split at middle C.
bool AiHandSplitter::guessLeftHand(int id) const
{
    const auto& n = notes_[id];
    double rightSum = 0.0, leftSum = 0.0;
    int rightCount = 0, leftCount = 0;
    for (int i = 0; i < notes_.size(); ++i) {
        if (assignment_[i] < 0 || std::abs(notes_[i].startBeat - n.startBeat) > 2.0) continue;
        if (assignment_[i] == 1) { leftSum += notes_[i].pitch; ++leftCount; }
        else { rightSum += notes_[i].pitch; ++rightCount; }
    }
    if (leftCount > 0 && rightCount > 0)
        return std::abs(n.pitch - leftSum / leftCount) < std::abs(n.pitch - rightSum / rightCount);
    return n.pitch < 60;
}

void AiHandSplitter::fail(const QString& message)
{
    if (!running_) return;
    cancel();
    emit failed(message);
}

} // namespace OpenDaw
