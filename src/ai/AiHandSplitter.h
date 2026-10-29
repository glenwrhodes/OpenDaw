#pragma once

#include "engine/EditManager.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVector>
#include <QList>

namespace OpenDaw {

// Asks Claude which hand played each note of a recorded piano track, so the
// track can be split into right-hand (treble) and left-hand (bass) tracks by
// playability and voice leading rather than a fixed split point.
//
// Long performances are cut at bar lines into chunks that are analysed in
// parallel. Any note the model leaves out gets a local fallback guess.
class AiHandSplitter : public QObject {
    Q_OBJECT
public:
    explicit AiHandSplitter(EditManager* editMgr, QObject* parent = nullptr);
    ~AiHandSplitter() override;

    // Snapshots the track's notes and sends the requests. Returns false and
    // sets `error` if the analysis cannot start.
    bool start(te::AudioTrack* track, QString& error);
    void cancel();
    bool isRunning() const { return running_; }

    te::EditItemID trackId() const { return trackId_; }
    // True if the track still holds exactly the notes that were analysed.
    bool matchesTrack(te::AudioTrack* track) const;

signals:
    void progress(int chunksDone, int chunkCount);
    // isLeftHand is indexed like EditManager::getTrackMidiNotes().
    void finished(const QVector<bool>& isLeftHand, int fallbackNotes);
    void failed(const QString& message);

private:
    struct NoteInfo {
        int pitch = 0;
        int velocity = 0;
        double startBeat = 0.0;
        double lengthBeats = 0.0;
    };

    void buildChunks();
    QString describeChunk(const QVector<int>& ids) const;
    void sendNextChunks();
    void sendChunk(int chunkIndex);
    void handleReply(QNetworkReply* reply, int chunkIndex);
    bool applyChunkResult(int chunkIndex, const QJsonObject& result, QString& error);
    void finishIfDone();
    void fail(const QString& message);
    bool guessLeftHand(int id) const;
    static QString noteName(int pitch);

    EditManager* editMgr_;
    QNetworkAccessManager* nam_;

    te::EditItemID trackId_;
    QString apiKey_;
    QString model_;
    int timeSigNum_ = 4;
    int timeSigDen_ = 4;
    double bpm_ = 120.0;

    QVector<NoteInfo> notes_;          // indexed like getTrackMidiNotes()
    QVector<QVector<int>> chunks_;      // note ids per chunk, in time order
    QVector<int> assignment_;           // -1 unknown, 0 right hand, 1 left hand
    QVector<int> retries_;              // per chunk
    QList<int> pending_;                // chunk indices not yet sent
    QList<QNetworkReply*> replies_;
    int chunksDone_ = 0;
    bool running_ = false;
    int generation_ = 0;                // bumped per run, so stale retry timers do nothing
};

} // namespace OpenDaw
