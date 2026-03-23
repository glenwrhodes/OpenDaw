#pragma once

#include "NotationModel.h"
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QFont>
#include <QPen>

namespace OpenDaw {

class ScoreScene : public QGraphicsScene {
    Q_OBJECT

public:
    explicit ScoreScene(QObject* parent = nullptr);

    void renderScore(const NotationModel& model);
    void clearScore();

    void setPixelsPerBeat(double ppb);
    double pixelsPerBeat() const { return pixelsPerBeat_; }

    void setPageWidth(double w);
    void setTitle(const QString& title);
    void setClip(te::MidiClip* clip);

    void paintScore(QPainter* p);
    void paintPage(QPainter* p, int pageIndex);
    int pageCount() const;

    const NotationModel& model() const { return model_; }
    double staffLineSpacing() const { return met_.staffLineSpacing; }

signals:
    void noteChanged();
    void articulationToggled(double beat, int midiNote, StaffKind staff, int artType);
    void spellingOverrideRequested(double beat, int newMidiNote, Accidental forced);

private:
    friend class NoteHeadItem;

    struct SystemLayout {
        int firstMeasure = 0;
        int measureCount = 0;
        double yOffset = 0.0;
        bool showTimeSig = false;
        bool showKeySig = false;
    };

    struct StaffMetrics {
        double staffLineSpacing = 10.0;
        double trebleTopY = 40.0;
        double bassTopY = 0.0;
        double staffGap = 50.0;
        double braceX = 20.0;
        double clefX = 42.0;
        double keySigX = 80.0;
        double timeSigX = 80.0;
        double leftMargin = 112.0;
        double measureStartX = 0.0;
        int timeSigNum = 4;
        int timeSigDen = 4;
        int keySig = 0;
        int currentSystemFirst = 0;
    };

    void computeMetrics();
    void buildSystems(int sysOnFirstPage);
    int glyphFontSize() const;
    double titleHeightForPage(int page) const;
    int sysOnFirstPage() const;
    void drawTitle(QPainter* p, double pageTopY);
    double staffLineY(StaffKind staff, int line, double systemYOff) const;
    double staffPositionY(StaffKind staff, int pos, double systemYOff) const;
    double beatToX(int localMeasure, double beatInMeasure) const;

    double systemHeight() const;
    double leftMarginForSystem(const SystemLayout& sys) const;
    void setupSystemMetrics(const SystemLayout& sys);

    void paintSystem(QPainter* p, const SystemLayout& sys);
    void createNoteOverlays();
    void drawPaper(QPainter* p, double x, double y, double w, double h);
    void drawStaffLines(QPainter* p, double startX, double endX, double sysY);
    void drawBrace(QPainter* p, double x, double sysY);
    void drawClefs(QPainter* p, double x, double sysY);
    void drawKeySig(QPainter* p, double x, double sysY);
    void drawTimeSig(QPainter* p, double x, double sysY);
    void drawBarLine(QPainter* p, double x, double sysY);
    void drawMeasureNumber(QPainter* p, int num, double x, double sysY);

    void drawEvent(QPainter* p, const NotationEvent& evt,
                   int localMeasure, StaffKind staff, double sysY);
    void drawNoteHead(QPainter* p, const NotationNote& note,
                      double x, double y);
    void drawStem(QPainter* p, double x, double noteY,
                  int stemDir, NoteValue value);
    void drawFlag(QPainter* p, double x, double stemTopY, int stemDir, NoteValue value);
    void drawAccidental(QPainter* p, Accidental acc, double x, double y);
    void drawLedgerLines(QPainter* p, StaffKind staff, int staffPos,
                         double x, double sysY);
    void drawDot(QPainter* p, double x, double y, int staffPos);
    void drawArticulation(QPainter* p, double x, double y, int stemDir,
                          ArticulationMarking::Type type);
    void drawRest(QPainter* p, NoteValue value, bool dotted,
                  double x, StaffKind staff, double sysY);
    void drawBeamGroup(QPainter* p, const BeamGroup& bg,
                       const std::vector<NotationEvent>& events,
                       int localMeasureOffset, double sysY);
    void drawSlur(QPainter* p, double startX, double startY,
                  double endX, double endY, int direction);
    void drawPhrases(QPainter* p, const SystemLayout& sys);

    StaffMetrics met_;
    double pixelsPerBeat_ = 50.0;
    double pageWidth_ = 900.0;
    double pageHeight_ = 0.0;
    double totalWidth_ = 800.0;
    double totalHeight_ = 300.0;
    double systemSpacing_ = 30.0;
    int systemsPerPage_ = 4;

    std::vector<double> measureWidths_;
    std::vector<SystemLayout> systems_;

    QFont musicFont_;
    QFont textFont_;

    NotationModel model_;
    te::MidiClip* clip_ = nullptr;
    QString title_;
    QFont titleFont_;
    bool hasScore_ = false;
};

} // namespace OpenDaw
