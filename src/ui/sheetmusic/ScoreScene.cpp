#include "ui/sheetmusic/ScoreScene.h"
#include "utils/IconFont.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QTimer>
#include <cmath>

namespace OpenDaw {

// ── SMuFL codepoints (Bravura) ──────────────────────────────────────────────

namespace smufl {
    const QChar trebleClef     {0xE050};
    const QChar bassClef       {0xE062};
    const QChar noteheadWhole  {0xE0A2};
    const QChar noteheadHalf   {0xE0A3};
    const QChar noteheadFilled {0xE0A4};
    const QChar restWhole      {0xE4E3};
    const QChar restHalf       {0xE4E4};
    const QChar restQuarter    {0xE4E5};
    const QChar restEighth     {0xE4E6};
    const QChar rest16th       {0xE4E7};
    const QChar flag8thUp      {0xE240};
    const QChar flag8thDown    {0xE241};
    const QChar flag16thUp     {0xE242};
    const QChar flag16thDown   {0xE243};
    const QChar accSharp       {0xE262};
    const QChar accFlat        {0xE260};
    const QChar accNatural     {0xE261};
    const QChar timeSig0       {0xE080};
}

static const QColor kPaperColor     {255, 255, 252};
static const QColor kStaffLineColor { 60,  60,  60};
static const QColor kBarLineColor   { 50,  50,  50};
static const QColor kNoteColor      { 20,  20,  20};
static const QColor kRestColor      { 40,  40,  40};
static const QColor kMeasNumColor   {130, 130, 130};
static const QColor kBraceColor     { 50,  50,  50};
static const QColor kSlurColor      { 40,  40, 100};
static const QColor kPageGapColor   {190, 190, 185};

static constexpr double kNoteHeadHalfW = 6.0;

// ── ScoreSheetItem ──────────────────────────────────────────────────────────

class ScoreSheetItem : public QGraphicsItem {
public:
    explicit ScoreSheetItem(ScoreScene* scene) : scene_(scene) {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, false);
    }
    QRectF boundingRect() const override { return scene_->sceneRect(); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        scene_->paintScore(painter);
    }
private:
    ScoreScene* scene_;
};

// ── NoteHeadItem (interactive overlay for each notehead) ────────────────────

class NoteHeadItem : public QGraphicsObject {
    Q_OBJECT
public:
    NoteHeadItem(te::MidiNote* note, te::MidiClip* clip, ScoreScene* scene,
                 StaffKind staff, int staffPos, int keySig, double x, double y)
        : QGraphicsObject(), note_(note), clip_(clip), scene_(scene),
          staff_(staff), staffPos_(staffPos), keySig_(keySig), noteX_(x), noteY_(y)
    {
        setPos(x, y);
        setFlags(ItemIsSelectable | ItemIsFocusable | ItemSendsGeometryChanges);
        setAcceptHoverEvents(true);
        setCursor(Qt::ArrowCursor);
        setZValue(10.0);
        setData(0, QVariant::fromValue(static_cast<void*>(note)));
    }

    te::MidiNote* engineNote() const { return note_; }
    te::MidiClip* engineClip() const { return clip_; }
    StaffKind noteStaff() const { return staff_; }
    int noteStaffPos() const { return staffPos_; }
    int noteKeySig() const { return keySig_; }

    void setDragPreviewDy(double dy) {
        if (!showingPreview_) { prepareGeometryChange(); showingPreview_ = true; }
        previewDy_ = dy;
        update();
    }
    void clearDragPreview() {
        if (showingPreview_) { prepareGeometryChange(); showingPreview_ = false; previewDy_ = 0; }
        update();
    }

    QRectF boundingRect() const override {
        if (dragging_ || showingPreview_)
            return QRectF(-kNoteHeadHalfW - 2, -300, kNoteHeadHalfW * 2 + 4, 600);
        return QRectF(-kNoteHeadHalfW - 2, -7, kNoteHeadHalfW * 2 + 4, 14);
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        if (isSelected() && !dragging_ && !showingPreview_) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(70, 130, 220, 80));
            p->drawEllipse(QRectF(-kNoteHeadHalfW - 2, -7, kNoteHeadHalfW * 2 + 4, 14));
            p->setBrush(Qt::NoBrush);
        }
        if (dragging_) {
            double dy = dragY_ - noteY_;
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(220, 70, 70, 120));
            p->drawEllipse(QPointF(0, dy), kNoteHeadHalfW, 5.0);
            p->setBrush(Qt::NoBrush);
        } else if (showingPreview_) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(220, 70, 70, 120));
            p->drawEllipse(QPointF(0, previewDy_), kNoteHeadHalfW, 5.0);
            p->setBrush(Qt::NoBrush);
        }
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            bool multiSelect = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
            if (!multiSelect && !isSelected())
                scene_->clearSelection();
            setSelected(true);
            setFocus();
            origPitch_ = note_->getNoteNumber();
            dragStartY_ = e->scenePos().y();
            dragging_ = false;
        }
        e->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) {
            if (!dragging_) {
                prepareGeometryChange();
                dragging_ = true;
            }
            double halfSpace = scene_->staffLineSpacing() / 2.0;
            double dy = e->scenePos().y() - dragStartY_;
            int posDelta = -static_cast<int>(std::round(dy / halfSpace));
            double snappedDy = -posDelta * halfSpace;
            dragY_ = noteY_ + snappedDy;
            update();

            for (auto* item : scene_->selectedItems()) {
                auto* peer = dynamic_cast<NoteHeadItem*>(item);
                if (peer && peer != this)
                    peer->setDragPreviewDy(snappedDy);
            }
        }
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override {
        bool changed = false;
        if (dragging_ && note_ && clip_) {
            double halfSpace = scene_->staffLineSpacing() / 2.0;
            double dy = e->scenePos().y() - dragStartY_;
            int posDelta = -static_cast<int>(std::round(dy / halfSpace));
            if (posDelta != 0) {
                auto* um = &clip_->edit.getUndoManager();
                // apply same interval to all selected NoteHeadItems
                for (auto* item : scene_->selectedItems()) {
                    auto* nh = dynamic_cast<NoteHeadItem*>(item);
                    if (!nh || !nh->engineNote()) continue;
                    int np = nh->noteStaffPos() + posDelta;
                    int nm = std::clamp(staffPositionToMidi(np, nh->noteStaff(), nh->noteKeySig()), 0, 127);
                    nh->engineNote()->setNoteNumber(nm, um);
                }
                changed = true;
            }
        }
        for (auto* item : scene_->selectedItems()) {
            auto* peer = dynamic_cast<NoteHeadItem*>(item);
            if (peer && peer != this) peer->clearDragPreview();
        }
        prepareGeometryChange();
        dragging_ = false;
        update();
        QGraphicsObject::mouseReleaseEvent(e);
        if (changed)
            QTimer::singleShot(0, scene_, [s = scene_]() { emit s->noteChanged(); });
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (!note_ || !clip_) { QGraphicsObject::keyPressEvent(e); return; }

        int pitch = note_->getNoteNumber();
        int chroma = pitch % 12;
        bool isBlack = (chroma == 1 || chroma == 3 || chroma == 6 ||
                        chroma == 8 || chroma == 10);
        auto* um = &clip_->edit.getUndoManager();
        double absBeat = note_->getStartBeat().inBeats();

        if (e->key() == Qt::Key_S) {
            if (!isBlack) {
                int newPitch = std::min(127, pitch + 1);
                note_->setNoteNumber(newPitch, um);
                QTimer::singleShot(0, scene_, [s = scene_, absBeat, newPitch]() {
                    emit s->spellingOverrideRequested(absBeat, newPitch, Accidental::Sharp);
                });
            }
        } else if (e->key() == Qt::Key_F) {
            if (!isBlack) {
                int newPitch = std::max(0, pitch - 1);
                note_->setNoteNumber(newPitch, um);
                QTimer::singleShot(0, scene_, [s = scene_, absBeat, newPitch]() {
                    emit s->spellingOverrideRequested(absBeat, newPitch, Accidental::Flat);
                });
            }
        } else if (e->key() == Qt::Key_N) {
            // naturalize: find which diatonic degree is altered by the key sig
            // to produce this pitch, then set it to the unaltered (natural) version
            static const int kDiaBase[7] = {0, 2, 4, 5, 7, 9, 11};
            static const int kSharpOrd[7] = {3, 0, 4, 1, 5, 2, 6};
            static const int kFlatOrd[7]  = {6, 2, 5, 1, 4, 0, 3};

            int keyAlts[7] = {};
            if (keySig_ > 0)
                for (int i = 0; i < std::min(keySig_, 7); i++) keyAlts[kSharpOrd[i]] = +1;
            else if (keySig_ < 0)
                for (int i = 0; i < std::min(-keySig_, 7); i++) keyAlts[kFlatOrd[i]] = -1;

            // find which diatonic degree, when altered by keySig, produces this pitch class
            int octave = pitch / 12;
            int pc = pitch % 12;
            int targetDiatonic = -1;
            for (int d = 0; d < 7; d++) {
                int alteredPC = (kDiaBase[d] + keyAlts[d] + 12) % 12;
                if (alteredPC == pc) { targetDiatonic = d; break; }
            }

            if (targetDiatonic >= 0 && keyAlts[targetDiatonic] != 0) {
                // this note IS altered by the key sig -- naturalize it
                int naturalPitch = octave * 12 + kDiaBase[targetDiatonic];
                // handle octave boundary (e.g. Cb in octave 5 → B in octave 4)
                if (naturalPitch > pitch + 6) naturalPitch -= 12;
                if (naturalPitch < pitch - 6) naturalPitch += 12;
                note_->setNoteNumber(std::clamp(naturalPitch, 0, 127), um);
                double beat = absBeat;
                int np = std::clamp(naturalPitch, 0, 127);
                QTimer::singleShot(0, scene_, [s = scene_, beat, np]() {
                    emit s->spellingOverrideRequested(beat, np, Accidental::Natural);
                });
            } else if (isBlack) {
                // note is a chromatic accidental (not from key sig) -- move to natural
                static const int kDiaFromChroma[12] = {0,0,1,1,2,3,3,4,4,5,5,6};
                int dia = kDiaFromChroma[pc];
                int naturalPitch = octave * 12 + kDiaBase[dia];
                note_->setNoteNumber(std::clamp(naturalPitch, 0, 127), um);
                QTimer::singleShot(0, scene_, [s = scene_]() {
                    emit s->noteChanged();
                });
            }
        } else if (e->key() == Qt::Key_Period || e->key() == Qt::Key_1) {
            int mNote = note_->getNoteNumber();
            StaffKind sk = staff_;
            QTimer::singleShot(0, scene_, [s = scene_, absBeat, mNote, sk]() {
                emit s->articulationToggled(absBeat, mNote, sk, ArticulationMarking::Staccato);
            });
        } else if (e->key() == Qt::Key_2) {
            int mNote = note_->getNoteNumber(); StaffKind sk = staff_;
            QTimer::singleShot(0, scene_, [s = scene_, absBeat, mNote, sk]() {
                emit s->articulationToggled(absBeat, mNote, sk, ArticulationMarking::Tenuto);
            });
        } else if (e->key() == Qt::Key_3) {
            int mNote = note_->getNoteNumber(); StaffKind sk = staff_;
            QTimer::singleShot(0, scene_, [s = scene_, absBeat, mNote, sk]() {
                emit s->articulationToggled(absBeat, mNote, sk, ArticulationMarking::Accent);
            });
        } else if (e->key() == Qt::Key_4) {
            int mNote = note_->getNoteNumber(); StaffKind sk = staff_;
            QTimer::singleShot(0, scene_, [s = scene_, absBeat, mNote, sk]() {
                emit s->articulationToggled(absBeat, mNote, sk, ArticulationMarking::Marcato);
            });
        } else if (e->key() == Qt::Key_5) {
            int mNote = note_->getNoteNumber(); StaffKind sk = staff_;
            QTimer::singleShot(0, scene_, [s = scene_, absBeat, mNote, sk]() {
                emit s->articulationToggled(absBeat, mNote, sk, ArticulationMarking::Fermata);
            });
        } else if (e->key() == Qt::Key_6) {
            int mNote = note_->getNoteNumber(); StaffKind sk = staff_;
            QTimer::singleShot(0, scene_, [s = scene_, absBeat, mNote, sk]() {
                emit s->articulationToggled(absBeat, mNote, sk, ArticulationMarking::Staccatissimo);
            });
        } else {
            QGraphicsObject::keyPressEvent(e);
        }
    }

private:
    te::MidiNote* note_;
    te::MidiClip* clip_;
    ScoreScene* scene_;
    StaffKind staff_;
    int staffPos_;
    int keySig_;
    double noteX_, noteY_;
    int origPitch_ = 0;
    double dragStartY_ = 0;
    double dragY_ = 0;
    bool dragging_ = false;
    bool showingPreview_ = false;
    double previewDy_ = 0;
};

// ── ScoreScene ──────────────────────────────────────────────────────────────

ScoreScene::ScoreScene(QObject* parent)
    : QGraphicsScene(parent)
{
    musicFont_ = icons::bravuraMusic(40);
    textFont_ = QFont("Segoe UI");
    textFont_.setPixelSize(11);
    titleFont_ = QFont("Segoe UI");
    titleFont_.setPixelSize(22);
    titleFont_.setBold(true);
    setBackgroundBrush(QColor(180, 180, 178));
}

void ScoreScene::clearScore()
{
    QGraphicsScene::clear();
    hasScore_ = false;
    model_.clear();
    systems_.clear();
    setSceneRect(0, 0, 0, 0);
}

void ScoreScene::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat_ = ppb;
    if (hasScore_) {
        computeMetrics();
        QGraphicsScene::clear();
        setSceneRect(0, 0, totalWidth_, totalHeight_);
        addItem(new ScoreSheetItem(this));
    }
}

void ScoreScene::setPageWidth(double w)
{
    pageWidth_ = w;
}

void ScoreScene::setTitle(const QString& title)
{
    title_ = title;
}

void ScoreScene::setClip(te::MidiClip* clip)
{
    clip_ = clip;
}

void ScoreScene::renderScore(const NotationModel& model)
{
    QGraphicsScene::clear();
    model_ = model;
    hasScore_ = true;

    computeMetrics();
    setSceneRect(0, 0, totalWidth_, totalHeight_);
    addItem(new ScoreSheetItem(this));
    createNoteOverlays();
}

// ── layout ──────────────────────────────────────────────────────────────────

double ScoreScene::systemHeight() const
{
    return met_.trebleTopY + 4 * met_.staffLineSpacing
           + met_.staffGap + 4 * met_.staffLineSpacing + 40.0;
}

double ScoreScene::leftMarginForSystem(const SystemLayout& sys) const
{
    double margin = met_.braceX;
    margin += 22.0; // brace to clef
    margin += 38.0; // clef width

    if (sys.showKeySig && met_.keySig != 0) {
        int n = std::abs(met_.keySig);
        margin += n * 10.0 + 8.0;
    }

    if (sys.showTimeSig)
        margin += 32.0;
    else if (!sys.showKeySig || met_.keySig == 0)
        margin += 8.0;
    else
        margin += 4.0;

    return margin;
}

double ScoreScene::titleHeightForPage(int page) const
{
    return (page == 0 && !title_.isEmpty()) ? 45.0 : 0.0;
}

void ScoreScene::computeMetrics()
{
    met_.staffLineSpacing = 10.0;
    met_.trebleTopY = 40.0;
    met_.staffGap = 50.0;
    met_.bassTopY = met_.trebleTopY + 4 * met_.staffLineSpacing + met_.staffGap;
    met_.timeSigNum = model_.timeSigNum();
    met_.timeSigDen = model_.timeSigDen();
    met_.keySig = model_.keySig();

    // A4 portrait aspect ratio: height ≈ 1.414 * width
    pageHeight_ = pageWidth_ * 1.414;

    double sysH = systemHeight();
    double sysSlot = sysH + systemSpacing_;

    // systems per page: page 1 has less room due to title
    double page0Available = pageHeight_ - 20.0 - titleHeightForPage(0);
    double pageNAvailable = pageHeight_ - 20.0;

    int sysPage0 = std::max(1, static_cast<int>(page0Available / sysSlot));
    systemsPerPage_ = std::max(1, static_cast<int>(pageNAvailable / sysSlot));

    buildSystems(sysPage0);

    int pages = pageCount();
    double pageGap = 20.0;
    totalWidth_ = pageWidth_ + 40.0;
    totalHeight_ = pages * (pageHeight_ + pageGap);
}

void ScoreScene::buildSystems(int sysOnFirstPage)
{
    systems_.clear();
    int numMeasures = model_.measureCount();
    if (numMeasures == 0) return;

    double beatsPerMeasure = met_.timeSigNum * (4.0 / met_.timeSigDen);
    auto& measures = model_.measures();

    // compute per-measure widths based on note density
    // minimum width per note event ensures dense measures get more space
    constexpr double minNoteSlot = 18.0;
    double baseMeasureWidth = beatsPerMeasure * pixelsPerBeat_ + 20.0;

    measureWidths_.resize(numMeasures);
    for (int i = 0; i < numMeasures; ++i) {
        int evtCount = 0;
        if (i < static_cast<int>(measures.size())) {
            evtCount = std::max(
                static_cast<int>(measures[i].trebleEvents.size()),
                static_cast<int>(measures[i].bassEvents.size()));
        }
        double densityWidth = evtCount * minNoteSlot + 30.0;
        measureWidths_[i] = std::max(baseMeasureWidth, densityWidth);
    }

    SystemLayout firstProto;
    firstProto.showTimeSig = true;
    firstProto.showKeySig = true;
    double firstLeft = leftMarginForSystem(firstProto);

    SystemLayout contProto;
    contProto.showTimeSig = false;
    contProto.showKeySig = false;
    double contLeft = leftMarginForSystem(contProto);

    double sysH = systemHeight();
    double sysSlot = sysH + systemSpacing_;
    double pageGap = 20.0;
    int m = 0;

    while (m < numMeasures) {
        bool isFirst = systems_.empty();
        double lm = isFirst ? firstLeft : contLeft;
        double available = pageWidth_ - lm - 10.0;
        int count = 0;
        double used = 0.0;

        while (m + count < numMeasures) {
            double w = measureWidths_[m + count];
            if (used + w > available + 0.5 && count > 0) break;
            used += w;
            count++;
        }
        if (count == 0) count = 1;

        int sysIdx = static_cast<int>(systems_.size());
        int page, sysOnPage;
        if (sysIdx < sysOnFirstPage) {
            page = 0;
            sysOnPage = sysIdx;
        } else {
            int afterFirst = sysIdx - sysOnFirstPage;
            page = 1 + afterFirst / systemsPerPage_;
            sysOnPage = afterFirst % systemsPerPage_;
        }

        double pageTopY = page * (pageHeight_ + pageGap);
        double titleH = titleHeightForPage(page);

        SystemLayout sys;
        sys.firstMeasure = m;
        sys.measureCount = count;
        sys.showTimeSig = isFirst;
        sys.showKeySig = isFirst;
        sys.yOffset = pageTopY + 15.0 + titleH + sysOnPage * sysSlot;

        systems_.push_back(sys);
        m += count;
    }

    if (systems_.empty()) {
        SystemLayout sys;
        sys.firstMeasure = 0;
        sys.measureCount = 0;
        sys.showTimeSig = true;
        sys.showKeySig = true;
        sys.yOffset = 15.0 + titleHeightForPage(0);
        systems_.push_back(sys);
    }
}

int ScoreScene::glyphFontSize() const
{
    return static_cast<int>(4.0 * met_.staffLineSpacing);
}

double ScoreScene::staffLineY(StaffKind staff, int line, double sysY) const
{
    double topY = (staff == StaffKind::Treble) ? met_.trebleTopY : met_.bassTopY;
    return sysY + topY + line * met_.staffLineSpacing;
}

double ScoreScene::staffPositionY(StaffKind staff, int pos, double sysY) const
{
    double bottomY = staffLineY(staff, 4, sysY);
    return bottomY - pos * (met_.staffLineSpacing / 2.0);
}

double ScoreScene::beatToX(int localMeasure, double beatInMeasure) const
{
    double beatsPerMeasure = met_.timeSigNum * (4.0 / met_.timeSigDen);

    // sum widths of preceding local measures to get X offset
    // met_.currentSystemFirst_ is set by paintSystem before drawing
    double xOff = met_.measureStartX;
    for (int i = 0; i < localMeasure; ++i) {
        int gm = met_.currentSystemFirst + i;
        if (gm < static_cast<int>(measureWidths_.size()))
            xOff += measureWidths_[gm];
        else
            xOff += beatsPerMeasure * pixelsPerBeat_ + 20.0;
    }

    // within this measure, position proportionally
    int gm = met_.currentSystemFirst + localMeasure;
    double mw = (gm < static_cast<int>(measureWidths_.size()))
                    ? measureWidths_[gm]
                    : beatsPerMeasure * pixelsPerBeat_ + 20.0;
    double beatFraction = beatInMeasure / beatsPerMeasure;
    double noteAreaWidth = mw - 20.0;

    return xOff + 10.0 + beatFraction * noteAreaWidth;
}

int ScoreScene::pageCount() const
{
    int n = static_cast<int>(systems_.size());
    if (n == 0) return 1;

    // page 0 may hold fewer systems due to title
    double sysSlot = systemHeight() + systemSpacing_;
    double page0Available = pageHeight_ - 20.0 - titleHeightForPage(0);
    int sysPage0 = std::max(1, static_cast<int>(page0Available / sysSlot));

    if (n <= sysPage0) return 1;
    int remaining = n - sysPage0;
    return 1 + (remaining + systemsPerPage_ - 1) / systemsPerPage_;
}

// ── page helpers ────────────────────────────────────────────────────────────

static void getSystemRangeForPage(int pageIndex, int totalSystems,
                                   int sysPage0, int sysPerPage,
                                   int& outFirst, int& outLast)
{
    if (pageIndex == 0) {
        outFirst = 0;
        outLast = std::min(sysPage0, totalSystems);
    } else {
        outFirst = sysPage0 + (pageIndex - 1) * sysPerPage;
        outLast = std::min(outFirst + sysPerPage, totalSystems);
    }
}

int ScoreScene::sysOnFirstPage() const
{
    double sysSlot = systemHeight() + systemSpacing_;
    double avail = pageHeight_ - 20.0 - titleHeightForPage(0);
    return std::max(1, static_cast<int>(avail / sysSlot));
}

void ScoreScene::drawTitle(QPainter* p, double pageTopY)
{
    if (title_.isEmpty()) return;
    p->setPen(kNoteColor);
    p->setFont(titleFont_);
    double titleY = pageTopY + 30.0;
    QRectF titleRect(0, pageTopY + 8.0, pageWidth_, 35.0);
    p->drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, title_);
    (void)titleY;
}

// ── main paint entry point (all pages stacked for on-screen view) ───────────

void ScoreScene::paintScore(QPainter* painter)
{
    if (!hasScore_) return;
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    int pages = pageCount();
    double pageGap = 20.0;

    for (int pg = 0; pg < pages; ++pg) {
        double pageTopY = pg * (pageHeight_ + pageGap);
        drawPaper(painter, 0, pageTopY, pageWidth_, pageHeight_);

        if (pg == 0)
            drawTitle(painter, pageTopY);
    }

    for (auto& sys : systems_)
        paintSystem(painter, sys);
}

void ScoreScene::paintPage(QPainter* p, int pageIndex)
{
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);

    int n = static_cast<int>(systems_.size());
    int sp0 = sysOnFirstPage();

    int first, last;
    getSystemRangeForPage(pageIndex, n, sp0, systemsPerPage_, first, last);
    if (first >= n) return;

    // white page
    p->setPen(Qt::NoPen);
    p->setBrush(kPaperColor);
    p->drawRect(QRectF(0, 0, pageWidth_, pageHeight_));
    p->setBrush(Qt::NoBrush);

    if (pageIndex == 0)
        drawTitle(p, 0);

    double pageGap = 20.0;
    double baseY = pageIndex * (pageHeight_ + pageGap);

    for (int i = first; i < last; ++i) {
        SystemLayout localSys = systems_[i];
        localSys.yOffset = localSys.yOffset - baseY;
        paintSystem(p, localSys);
    }
}

// ── system metrics setup (shared by paintSystem and createNoteOverlays) ─────

void ScoreScene::setupSystemMetrics(const SystemLayout& sys)
{
    met_.currentSystemFirst = sys.firstMeasure;

    double leftFurniture = 22.0 + 38.0;
    if (sys.showKeySig && met_.keySig != 0)
        leftFurniture += std::abs(met_.keySig) * 10.0 + 8.0;
    if (sys.showTimeSig)
        leftFurniture += 32.0;
    else
        leftFurniture += 8.0;

    double measTotal = 0.0;
    for (int i = 0; i < sys.measureCount; ++i) {
        int gm = sys.firstMeasure + i;
        if (gm < static_cast<int>(measureWidths_.size()))
            measTotal += measureWidths_[gm];
    }

    double systemWidth = leftFurniture + measTotal;
    double xOffset = std::max(10.0, (pageWidth_ - systemWidth) / 2.0);

    met_.braceX = xOffset;
    met_.clefX = met_.braceX + 22.0;

    double x = met_.clefX + 38.0;
    if (sys.showKeySig && met_.keySig != 0) {
        met_.keySigX = x;
        x += std::abs(met_.keySig) * 10.0 + 8.0;
    } else {
        met_.keySigX = x;
    }

    if (sys.showTimeSig) {
        met_.timeSigX = x;
        x += 32.0;
    } else {
        met_.timeSigX = x;
        x += 8.0;
    }

    met_.leftMargin = x;
    met_.measureStartX = met_.leftMargin;
}

// ── note overlays for interactivity ─────────────────────────────────────────

void ScoreScene::createNoteOverlays()
{
    if (!clip_ || !hasScore_) return;

    auto& measures = model_.measures();
    double beatsPerMeasure = met_.timeSigNum * (4.0 / met_.timeSigDen);

    for (const auto& sys : systems_) {
        setupSystemMetrics(sys);
        double sysY = sys.yOffset;

        for (int i = 0; i < sys.measureCount; ++i) {
            int gm = sys.firstMeasure + i;
            if (gm >= static_cast<int>(measures.size())) break;
            auto& meas = measures[gm];

            auto processEvents = [&](const std::vector<NotationEvent>& events, StaffKind /*sk*/) {
                for (const auto& evt : events) {
                    if (evt.isRest) continue;
                    for (const auto& note : evt.notes) {
                        if (!note.engineNote) continue;
                        double nx = beatToX(i, note.beatInMeasure);
                        double ny = staffPositionY(note.staff, note.staffPosition, sysY);
                        auto* item = new NoteHeadItem(
                            note.engineNote, clip_, this,
                            note.staff, note.staffPosition,
                            model_.keySig(), nx, ny);
                        addItem(item);
                    }
                }
            };

            processEvents(meas.trebleEvents, StaffKind::Treble);
            processEvents(meas.bassEvents, StaffKind::Bass);
        }
    }

    (void)beatsPerMeasure;
}

// ── articulation rendering (using SMuFL/Bravura glyphs) ─────────────────────

namespace smuflArt {
    // above / below pairs
    const QChar accentAbove     {0xE4A0};
    const QChar accentBelow     {0xE4A1};
    const QChar staccatoAbove   {0xE4A2};
    const QChar staccatoBelow   {0xE4A3};
    const QChar tenutoAbove     {0xE4A4};
    const QChar tenutoBelow     {0xE4A5};
    const QChar staccatissimoAbove {0xE4A6};
    const QChar staccatissimoBelow {0xE4A7};
    const QChar marcatoAbove    {0xE4AC};
    const QChar marcatoBelow    {0xE4AD};
    const QChar fermataAbove    {0xE4C0};
    const QChar fermataBelow    {0xE4C1};
}

void ScoreScene::drawArticulation(QPainter* p, double x, double y, int stemDir,
                                   ArticulationMarking::Type type)
{
    // articulations go on the opposite side from the stem (closest to notehead)
    // stem-up (stemDir>0) → articulation below → use "below" glyph
    // stem-down (stemDir<=0) → articulation above → use "above" glyph
    bool above = (stemDir <= 0);

    double offset = above ? -10.0 : 10.0;
    double ay = y + offset;

    QChar glyph;
    switch (type) {
    case ArticulationMarking::Staccato:
        glyph = above ? smuflArt::staccatoAbove : smuflArt::staccatoBelow; break;
    case ArticulationMarking::Tenuto:
        glyph = above ? smuflArt::tenutoAbove : smuflArt::tenutoBelow; break;
    case ArticulationMarking::Accent:
        glyph = above ? smuflArt::accentAbove : smuflArt::accentBelow; break;
    case ArticulationMarking::Marcato:
        glyph = above ? smuflArt::marcatoAbove : smuflArt::marcatoBelow; break;
    case ArticulationMarking::Fermata:
        glyph = above ? smuflArt::fermataAbove : smuflArt::fermataBelow; break;
    case ArticulationMarking::Staccatissimo:
        glyph = above ? smuflArt::staccatissimoAbove : smuflArt::staccatissimoBelow; break;
    }

    int artFontSize = static_cast<int>(glyphFontSize() * 0.8);
    p->setPen(kNoteColor);
    QFont f = musicFont_;
    f.setPixelSize(artFontSize);
    p->setFont(f);

    // center the glyph horizontally on the notehead
    QFontMetricsF fm(f);
    double glyphW = fm.horizontalAdvance(QString(glyph));
    p->drawText(QPointF(x - glyphW / 2.0, ay), QString(glyph));
}

// ── system painting ─────────────────────────────────────────────────────────

void ScoreScene::paintSystem(QPainter* p, const SystemLayout& sys)
{
    double sysY = sys.yOffset;
    setupSystemMetrics(sys);

    double measTotal = 0.0;
    for (int i = 0; i < sys.measureCount; ++i) {
        int gm = sys.firstMeasure + i;
        if (gm < static_cast<int>(measureWidths_.size()))
            measTotal += measureWidths_[gm];
    }
    double scoreEndX = met_.measureStartX + measTotal;

    drawStaffLines(p, met_.braceX + 10.0, scoreEndX, sysY);
    drawBrace(p, met_.braceX, sysY);
    drawBarLine(p, met_.braceX + 10.0, sysY);
    drawClefs(p, met_.clefX, sysY);

    if (sys.showKeySig)
        drawKeySig(p, met_.keySigX, sysY);
    if (sys.showTimeSig)
        drawTimeSig(p, met_.timeSigX, sysY);

    // bar lines between measures and at the end (skip the left edge of first measure)
    auto& measures = model_.measures();
    double bx = met_.measureStartX;
    for (int i = 0; i <= sys.measureCount; ++i) {
        if (i > 0)
            drawBarLine(p, bx, sysY);
        if (i < sys.measureCount) {
            int globalMeas = sys.firstMeasure + i;
            drawMeasureNumber(p, globalMeas + 1, bx, sysY);
            if (globalMeas < static_cast<int>(measureWidths_.size()))
                bx += measureWidths_[globalMeas];
        }
    }

    // notes and beams
    for (int i = 0; i < sys.measureCount; ++i) {
        int gm = sys.firstMeasure + i;
        if (gm >= static_cast<int>(measures.size())) break;
        auto& meas = measures[gm];

        for (auto& evt : meas.trebleEvents)
            drawEvent(p, evt, i, StaffKind::Treble, sysY);
        for (auto& evt : meas.bassEvents)
            drawEvent(p, evt, i, StaffKind::Bass, sysY);

        for (auto& bg : meas.trebleBeams)
            drawBeamGroup(p, bg, meas.trebleEvents, i, sysY);
        for (auto& bg : meas.bassBeams)
            drawBeamGroup(p, bg, meas.bassEvents, i, sysY);
    }

    drawPhrases(p, sys);
}

// ── drawing primitives ──────────────────────────────────────────────────────

void ScoreScene::drawPaper(QPainter* p, double x, double y, double w, double h)
{
    p->setPen(Qt::NoPen);
    p->setBrush(kPaperColor);
    p->drawRect(QRectF(x, y, w, h));
    p->setBrush(Qt::NoBrush);
}

void ScoreScene::drawStaffLines(QPainter* p, double startX, double endX, double sysY)
{
    QPen pen(kStaffLineColor, 1.0);
    p->setPen(pen);
    for (int staff = 0; staff < 2; ++staff) {
        StaffKind sk = (staff == 0) ? StaffKind::Treble : StaffKind::Bass;
        for (int line = 0; line < 5; ++line) {
            double y = staffLineY(sk, line, sysY);
            p->drawLine(QPointF(startX, y), QPointF(endX, y));
        }
    }
}

void ScoreScene::drawBrace(QPainter* p, double x, double sysY)
{
    double topY = staffLineY(StaffKind::Treble, 0, sysY);
    double botY = staffLineY(StaffKind::Bass, 4, sysY);
    double braceH = botY - topY;

    QFont braceFont("Times New Roman", 10);
    braceFont.setPixelSize(static_cast<int>(braceH * 1.1));

    p->save();
    p->setPen(kBraceColor);
    p->setFont(braceFont);

    QFontMetricsF fm(braceFont);
    QRectF gb = fm.tightBoundingRect("{");
    double scaleY = braceH / gb.height();
    double scaleX = scaleY * 0.55;
    double centerY = (topY + botY) / 2.0;

    p->translate(x, centerY);
    p->scale(scaleX, scaleY);
    p->translate(0, -gb.center().y());
    p->drawText(QPointF(-gb.width(), 0), "{");
    p->restore();
}

void ScoreScene::drawClefs(QPainter* p, double x, double sysY)
{
    p->setPen(kNoteColor);
    QFont cf = musicFont_;
    cf.setPixelSize(glyphFontSize());
    p->setFont(cf);
    p->drawText(QPointF(x, staffLineY(StaffKind::Treble, 3, sysY)), QString(smufl::trebleClef));
    p->drawText(QPointF(x, staffLineY(StaffKind::Bass, 1, sysY)), QString(smufl::bassClef));
}

void ScoreScene::drawKeySig(QPainter* p, double x, double sysY)
{
    if (met_.keySig == 0) return;
    p->setPen(kNoteColor);
    QFont kf = musicFont_;
    kf.setPixelSize(glyphFontSize());
    p->setFont(kf);

    bool sharps = met_.keySig > 0;
    int count = std::abs(met_.keySig);
    QChar glyph = sharps ? smufl::accSharp : smufl::accFlat;

    static const int tSharps[7] = {8, 5, 9, 6, 3, 7, 4};
    static const int bSharps[7] = {6, 3, 7, 4, 1, 5, 2};
    static const int tFlats[7]  = {4, 7, 3, 6, 2, 5, 1};
    static const int bFlats[7]  = {2, 5, 1, 4, 0, 3, 6};
    const int* tp = sharps ? tSharps : tFlats;
    const int* bp = sharps ? bSharps : bFlats;

    for (int i = 0; i < count; i++) {
        double xp = x + i * 10.0;
        p->drawText(QPointF(xp, staffPositionY(StaffKind::Treble, tp[i], sysY)), QString(glyph));
        p->drawText(QPointF(xp, staffPositionY(StaffKind::Bass, bp[i], sysY)), QString(glyph));
    }
}

void ScoreScene::drawTimeSig(QPainter* p, double x, double sysY)
{
    p->setPen(kNoteColor);
    QFont tf = musicFont_;
    tf.setPixelSize(glyphFontSize());
    p->setFont(tf);

    auto draw = [&](int val, StaffKind sk, int baseLine) {
        QString d = QString::number(val);
        double xo = x;
        for (QChar ch : d) {
            int dig = ch.digitValue();
            if (dig >= 0) {
                p->drawText(QPointF(xo, staffLineY(sk, baseLine, sysY)),
                            QString(QChar(smufl::timeSig0.unicode() + dig)));
                xo += 14.0;
            }
        }
    };
    draw(met_.timeSigNum, StaffKind::Treble, 1);
    draw(met_.timeSigDen, StaffKind::Treble, 3);
    draw(met_.timeSigNum, StaffKind::Bass, 1);
    draw(met_.timeSigDen, StaffKind::Bass, 3);
}

void ScoreScene::drawBarLine(QPainter* p, double x, double sysY)
{
    double topY = staffLineY(StaffKind::Treble, 0, sysY);
    double botY = staffLineY(StaffKind::Bass, 4, sysY);
    p->setPen(QPen(kBarLineColor, 1.2));
    p->drawLine(QPointF(x, topY), QPointF(x, botY));
}

void ScoreScene::drawMeasureNumber(QPainter* p, int num, double x, double sysY)
{
    p->setPen(kMeasNumColor);
    p->setFont(textFont_);
    p->drawText(QPointF(x + 4, staffLineY(StaffKind::Treble, 0, sysY) - 8.0), QString::number(num));
}

// ── event rendering ─────────────────────────────────────────────────────────

void ScoreScene::drawEvent(QPainter* p, const NotationEvent& evt,
                           int localMeasure, StaffKind staff, double sysY)
{
    double x = beatToX(localMeasure, evt.beatInMeasure);

    if (evt.isRest) {
        drawRest(p, evt.value, evt.dotted, x, staff, sysY);
        return;
    }

    double beatsPerMeas = met_.timeSigNum * (4.0 / met_.timeSigDen);
    int globalMeasure = met_.currentSystemFirst + localMeasure;
    double absBeat = globalMeasure * beatsPerMeas + evt.beatInMeasure;

    for (auto& note : evt.notes) {
        double y = staffPositionY(note.staff, note.staffPosition, sysY);
        drawLedgerLines(p, note.staff, note.staffPosition, x, sysY);
        if (note.accidental != Accidental::None)
            drawAccidental(p, note.accidental, x - kNoteHeadHalfW - 12.0, y);
        drawNoteHead(p, note, x, y);
        if (note.dotted)
            drawDot(p, x + kNoteHeadHalfW + 4.0, y, note.staffPosition);
        for (auto artType : model_.getArticulations(absBeat, note.midiNote))
            drawArticulation(p, x, y, note.stemDirection, artType);
    }

    if (evt.beamed) return;

    if (!evt.notes.empty() && evt.value != NoteValue::Whole) {
        double topNoteY = 1e9, botNoteY = -1e9;
        int avgPos = 0;
        for (auto& n : evt.notes) {
            double ny = staffPositionY(n.staff, n.staffPosition, sysY);
            topNoteY = std::min(topNoteY, ny);
            botNoteY = std::max(botNoteY, ny);
            avgPos += n.staffPosition;
        }
        avgPos /= static_cast<int>(evt.notes.size());
        int stemDir = stemDirectionForPosition(avgPos);

        double stemX = (stemDir > 0) ? x + kNoteHeadHalfW : x - kNoteHeadHalfW;
        double ext = 3.0 * met_.staffLineSpacing;
        constexpr double stemInset = 2.5;
        double stemFromY, stemToY;
        if (stemDir > 0) {
            stemFromY = botNoteY - stemInset;
            stemToY = std::min(topNoteY, botNoteY) - ext;
        } else {
            stemFromY = topNoteY + stemInset;
            stemToY = std::max(botNoteY, topNoteY) + ext;
        }

        p->setPen(QPen(kNoteColor, 1.2));
        p->drawLine(QPointF(stemX, stemFromY), QPointF(stemX, stemToY));

        if (evt.value == NoteValue::Eighth || evt.value == NoteValue::Sixteenth)
            drawFlag(p, stemX, stemToY, stemDir, evt.value);
    }
}

void ScoreScene::drawNoteHead(QPainter* p, const NotationNote& note, double x, double y)
{
    p->setPen(kNoteColor);
    QFont f = musicFont_; f.setPixelSize(glyphFontSize()); p->setFont(f);
    QChar g;
    switch (note.value) {
    case NoteValue::Whole: g = smufl::noteheadWhole; break;
    case NoteValue::Half:  g = smufl::noteheadHalf; break;
    default:               g = smufl::noteheadFilled; break;
    }
    p->drawText(QPointF(x - kNoteHeadHalfW, y), QString(g));
}

void ScoreScene::drawStem(QPainter* p, double x, double noteY, int stemDir, NoteValue value)
{
    if (value == NoteValue::Whole) return;
    double stemX = (stemDir > 0) ? x + kNoteHeadHalfW : x - kNoteHeadHalfW;
    double stemToY = noteY - stemDir * 3.0 * met_.staffLineSpacing;
    p->setPen(QPen(kNoteColor, 1.2));
    p->drawLine(QPointF(stemX, noteY), QPointF(stemX, stemToY));
}

void ScoreScene::drawFlag(QPainter* p, double x, double stemTopY, int stemDir, NoteValue value)
{
    p->setPen(kNoteColor);
    QFont f = musicFont_; f.setPixelSize(glyphFontSize()); p->setFont(f);
    QChar g;
    if (value == NoteValue::Eighth)
        g = (stemDir > 0) ? smufl::flag8thUp : smufl::flag8thDown;
    else
        g = (stemDir > 0) ? smufl::flag16thUp : smufl::flag16thDown;
    p->drawText(QPointF(x - 2, stemTopY), QString(g));
}

void ScoreScene::drawAccidental(QPainter* p, Accidental acc, double x, double y)
{
    if (acc == Accidental::None) return;
    p->setPen(kNoteColor);
    QFont f = musicFont_; f.setPixelSize(glyphFontSize()); p->setFont(f);
    QChar g;
    switch (acc) {
    case Accidental::Sharp:   g = smufl::accSharp; break;
    case Accidental::Flat:    g = smufl::accFlat; break;
    case Accidental::Natural: g = smufl::accNatural; break;
    default: return;
    }
    p->drawText(QPointF(x, y), QString(g));
}

void ScoreScene::drawLedgerLines(QPainter* p, StaffKind staff, int staffPos,
                                  double x, double sysY)
{
    p->setPen(QPen(kStaffLineColor, 1.0));
    double hw = kNoteHeadHalfW + 3.0;
    if (staffPos < 0)
        for (int pos = -2; pos >= staffPos; pos -= 2) {
            double y = staffPositionY(staff, pos, sysY);
            p->drawLine(QPointF(x - hw, y), QPointF(x + hw, y));
        }
    if (staffPos > 8)
        for (int pos = 10; pos <= staffPos; pos += 2) {
            double y = staffPositionY(staff, pos, sysY);
            p->drawLine(QPointF(x - hw, y), QPointF(x + hw, y));
        }
}

void ScoreScene::drawDot(QPainter* p, double x, double y, int staffPos)
{
    double dy = y;
    if (staffPos % 2 == 0) dy -= met_.staffLineSpacing / 4.0;
    p->setPen(Qt::NoPen); p->setBrush(kNoteColor);
    p->drawEllipse(QPointF(x, dy), 2.0, 2.0);
    p->setBrush(Qt::NoBrush);
}

void ScoreScene::drawRest(QPainter* p, NoteValue value, bool dotted,
                           double x, StaffKind staff, double sysY)
{
    p->setPen(kRestColor);
    QFont f = musicFont_; f.setPixelSize(glyphFontSize()); p->setFont(f);
    QChar g; int bl = 2;
    switch (value) {
    case NoteValue::Whole:     g = smufl::restWhole;   bl = 1; break;
    case NoteValue::Half:      g = smufl::restHalf;    bl = 2; break;
    case NoteValue::Quarter:   g = smufl::restQuarter; bl = 2; break;
    case NoteValue::Eighth:    g = smufl::restEighth;  bl = 2; break;
    case NoteValue::Sixteenth: g = smufl::rest16th;    bl = 2; break;
    }
    double y = staffLineY(staff, bl, sysY);
    p->drawText(QPointF(x - 4, y), QString(g));
    if (dotted) drawDot(p, x + 12.0, y, 3);
}

// ── beams ───────────────────────────────────────────────────────────────────

void ScoreScene::drawBeamGroup(QPainter* p, const BeamGroup& bg,
                                const std::vector<NotationEvent>& events,
                                int localMeasureOffset, double sysY)
{
    if (bg.eventIndices.size() < 2) return;

    int totalPos = 0, noteCount = 0;
    for (int idx : bg.eventIndices)
        for (auto& n : events[idx].notes) { totalPos += n.staffPosition; noteCount++; }
    int stemDir = (noteCount > 0 && totalPos / noteCount >= 4) ? -1 : 1;

    double ext = 3.0 * met_.staffLineSpacing;
    double beamThick = 4.0;

    struct SI { double sx; double sy; };
    std::vector<SI> stems;
    for (int idx : bg.eventIndices) {
        auto& evt = events[idx];
        double x = beatToX(localMeasureOffset, evt.beatInMeasure);
        double topY = 1e9, botY = -1e9;
        for (auto& n : evt.notes) {
            double ny = staffPositionY(n.staff, n.staffPosition, sysY);
            topY = std::min(topY, ny); botY = std::max(botY, ny);
        }
        double stemX = (stemDir > 0) ? x + kNoteHeadHalfW : x - kNoteHeadHalfW;
        constexpr double inset = 2.5;
        double startY = (stemDir > 0) ? botY - inset : topY + inset;
        stems.push_back({stemX, startY});
    }

    double beamY;
    if (stemDir > 0) {
        beamY = 1e9;
        for (auto& s : stems) beamY = std::min(beamY, s.sy - ext);
    } else {
        beamY = -1e9;
        for (auto& s : stems) beamY = std::max(beamY, s.sy + ext);
    }

    // beam sits on the far side of beamY from the notes
    // stems should reach to the near edge of the beam, not through it
    double beamNearEdge = beamY + stemDir * beamThick;

    p->setPen(QPen(kNoteColor, 1.2));
    for (auto& s : stems)
        p->drawLine(QPointF(s.sx, s.sy), QPointF(s.sx, beamNearEdge));

    p->setPen(Qt::NoPen); p->setBrush(kNoteColor);
    double bL = stems.front().sx, bR = stems.back().sx;
    QPolygonF beam;
    beam << QPointF(bL, beamY) << QPointF(bR, beamY)
         << QPointF(bR, beamNearEdge) << QPointF(bL, beamNearEdge);
    p->drawPolygon(beam);

    bool all16 = true;
    for (int idx : bg.eventIndices) if (events[idx].value != NoteValue::Sixteenth) { all16 = false; break; }
    if (all16 && stems.size() >= 2) {
        double off = stemDir * (beamThick + 3.0);
        QPolygonF b2;
        b2 << QPointF(bL, beamY + off) << QPointF(bR, beamY + off)
           << QPointF(bR, beamY + off + stemDir * beamThick) << QPointF(bL, beamY + off + stemDir * beamThick);
        p->drawPolygon(b2);
    }
    p->setBrush(Qt::NoBrush);
}

// ── slurs / phrases ─────────────────────────────────────────────────────────

void ScoreScene::drawSlur(QPainter* p, double startX, double startY,
                           double endX, double endY, int direction)
{
    double span = endX - startX;
    if (span < 5.0) return;

    double arcH = std::min(22.0, std::max(10.0, span * 0.10));
    double dy = -direction * arcH;

    // outer curve (farther from staff)
    double thickness = std::min(2.5, std::max(1.2, span * 0.008));
    double innerDy = dy + direction * thickness;

    QPainterPath path;
    path.moveTo(startX, startY);
    path.cubicTo(startX + span * 0.2, startY + dy,
                 startX + span * 0.8, endY + dy,
                 endX, endY);
    // inner curve back (closer to staff) -- thinner at endpoints
    path.cubicTo(startX + span * 0.8, endY + innerDy,
                 startX + span * 0.2, startY + innerDy,
                 startX, startY);
    path.closeSubpath();

    p->setPen(Qt::NoPen);
    p->setBrush(kSlurColor);
    p->drawPath(path);
    p->setBrush(Qt::NoBrush);
}

void ScoreScene::drawPhrases(QPainter* p, const SystemLayout& sys)
{
    auto& phrases = model_.phrases();
    if (phrases.empty()) return;

    double beatsPerMeasure = met_.timeSigNum * (4.0 / met_.timeSigDen);
    double sysY = sys.yOffset;
    double sysStartBeat = sys.firstMeasure * beatsPerMeasure;
    double sysEndBeat = (sys.firstMeasure + sys.measureCount) * beatsPerMeasure;
    auto& measures = model_.measures();

    constexpr double phraseGap = 8.0;
    constexpr double slurClearance = 12.0;

    for (const auto& ph : phrases) {
        if (ph.endBeat <= sysStartBeat || ph.startBeat >= sysEndBeat) continue;

        double clampStart = std::max(ph.startBeat, sysStartBeat);
        double clampEnd = std::min(ph.endBeat, sysEndBeat);

        int startLocalMeas = static_cast<int>((clampStart - sysStartBeat) / beatsPerMeasure);
        double startBeatInMeas = clampStart - sysStartBeat - startLocalMeas * beatsPerMeasure;
        double sx = beatToX(startLocalMeas, startBeatInMeas) + phraseGap * 0.5;

        int endLocalMeas = std::min(static_cast<int>((clampEnd - sysStartBeat) / beatsPerMeasure),
                                     sys.measureCount - 1);
        double endBeatInMeas = clampEnd - sysStartBeat - endLocalMeas * beatsPerMeasure;
        double ex = beatToX(endLocalMeas, endBeatInMeas) - phraseGap * 0.5;

        if (ex <= sx + 10.0) continue;

        int dir = (ph.staff == StaffKind::Treble) ? 1 : -1;

        // scan notes in the phrase range to find the extreme Y position
        double extremeY;
        if (dir > 0)
            extremeY = staffLineY(StaffKind::Treble, 0, sysY);
        else
            extremeY = staffLineY(StaffKind::Bass, 4, sysY);

        for (int i = 0; i < sys.measureCount; ++i) {
            int gm = sys.firstMeasure + i;
            if (gm >= static_cast<int>(measures.size())) break;
            double measStartBeat = gm * beatsPerMeasure;

            const auto& evts = (ph.staff == StaffKind::Treble)
                                   ? measures[gm].trebleEvents
                                   : measures[gm].bassEvents;

            for (const auto& evt : evts) {
                if (evt.isRest) continue;
                double evtAbsBeat = measStartBeat + evt.beatInMeasure;
                if (evtAbsBeat < clampStart - 0.01 || evtAbsBeat > clampEnd + 0.01)
                    continue;

                for (const auto& note : evt.notes) {
                    double ny = staffPositionY(note.staff, note.staffPosition, sysY);
                    // also account for stem length
                    double stemTip = ny - stemDirectionForPosition(note.staffPosition)
                                     * 3.0 * met_.staffLineSpacing;
                    if (dir > 0) {
                        extremeY = std::min(extremeY, std::min(ny, stemTip));
                    } else {
                        extremeY = std::max(extremeY, std::max(ny, stemTip));
                    }
                }
            }
        }

        double slurY = extremeY - dir * slurClearance;
        drawSlur(p, sx, slurY, ex, slurY, dir);
    }
}

} // namespace OpenDaw

#include "ScoreScene.moc"
