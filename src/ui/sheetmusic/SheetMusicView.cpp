#include "ui/sheetmusic/SheetMusicView.h"
#include "engine/EditManager.h"
#include "utils/IconFont.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QToolBar>
#include <QPushButton>
#include <QGraphicsView>
#include <QScrollBar>
#include <QPrinter>
#include <QPrintDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QWheelEvent>

namespace OpenDaw {

static const juce::Identifier kPhrasesId("OpenDaw_PHRASES");
static const juce::Identifier kArticulationsId("OpenDaw_ARTICULATIONS");
static const juce::Identifier kSpellingsId("OpenDaw_SPELLINGS");
static const juce::Identifier kKeySigId("OpenDaw_KEY_SIGNATURE");

SheetMusicView::SheetMusicView(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Sheet Music View");
    nam_ = new QNetworkAccessManager(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buildToolbar();
    layout->addWidget(toolbar_);

    buildEditToolbar();
    layout->addWidget(editToolbar_);

    scene_ = new ScoreScene(this);

    connect(scene_, &ScoreScene::noteChanged, this, &SheetMusicView::onNoteChanged);
    connect(scene_, &ScoreScene::articulationToggled, this, &SheetMusicView::onArticulationToggled);
    connect(scene_, &ScoreScene::spellingOverrideRequested, this, &SheetMusicView::onSpellingOverride);

    view_ = new QGraphicsView(scene_, this);
    view_->setAccessibleName("Sheet Music Score Area");
    view_->setRenderHint(QPainter::Antialiasing, true);
    view_->setRenderHint(QPainter::TextAntialiasing, true);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    view_->setDragMode(QGraphicsView::RubberBandDrag);
    view_->setRubberBandSelectionMode(Qt::IntersectsItemBoundingRect);
    view_->setBackgroundBrush(QColor(180, 180, 178));

    view_->installEventFilter(this);

    layout->addWidget(view_);
}

bool SheetMusicView::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != view_) return QWidget::eventFilter(obj, event);

    auto isNoteEditKey = [](int key) {
        return key == Qt::Key_S || key == Qt::Key_F ||
               key == Qt::Key_N || key == Qt::Key_Period ||
               key == Qt::Key_Delete ||
               key == Qt::Key_1 || key == Qt::Key_2 || key == Qt::Key_3 ||
               key == Qt::Key_4 || key == Qt::Key_5 || key == Qt::Key_6 ||
               key == Qt::Key_0;
    };

    auto getSelectedNoteItems = [&]() {
        QList<QGraphicsObject*> notes;
        for (auto* item : scene_->selectedItems()) {
            auto* nh = dynamic_cast<QGraphicsObject*>(item);
            if (nh) notes.append(nh);
        }
        return notes;
    };

    // Ctrl+Wheel → zoom
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            int delta = we->angleDelta().y();
            int step = (delta > 0) ? 5 : -5;
            zoomSlider_->setValue(zoomSlider_->value() + step);
            return true;
        }
    }

    // ShortcutOverride: claim S/F/N/Period/Delete when notes are selected
    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (isNoteEditKey(ke->key()) && !getSelectedNoteItems().isEmpty()) {
            event->accept();
            return true;
        }
    }

    // KeyPress: apply to ALL selected notes
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        auto selected = getSelectedNoteItems();
        if (selected.isEmpty()) return QWidget::eventFilter(obj, event);

        if (ke->key() == Qt::Key_Delete) {
            if (clip_) {
                auto* um = &clip_->edit.getUndoManager();
                auto& seq = clip_->getSequence();
                QList<te::MidiNote*> toRemove;
                for (auto* item : scene_->selectedItems()) {
                    auto prop = item->data(0);
                    if (prop.isValid())
                        toRemove.append(static_cast<te::MidiNote*>(prop.value<void*>()));
                }
                for (auto* n : toRemove)
                    if (n) seq.removeNote(*n, um);
                QTimer::singleShot(0, this, [this]() { rebuildScore(); });
            }
            return true;
        }

        if (isNoteEditKey(ke->key())) {
            applyKeyToSelectedNote(ke->key());
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}

void SheetMusicView::buildToolbar()
{
    toolbar_ = new QToolBar(this);
    toolbar_->setAccessibleName("Sheet Music Toolbar");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(18, 18));

    auto* label = new QLabel(" Sheet Music ", toolbar_);
    label->setStyleSheet("color: #ccc; font-weight: bold; padding: 0 8px;");
    toolbar_->addWidget(label);

    clipNameLabel_ = new QLabel(toolbar_);
    clipNameLabel_->setAccessibleName("Sheet Music Clip Name");
    clipNameLabel_->setStyleSheet("color: #999; padding: 0 12px;");
    toolbar_->addWidget(clipNameLabel_);

    toolbar_->addSeparator();

    auto* keyLabel = new QLabel("Key:", toolbar_);
    keyLabel->setStyleSheet("color: #aaa; padding: 0 4px;");
    toolbar_->addWidget(keyLabel);

    keySigCombo_ = new QComboBox(toolbar_);
    keySigCombo_->setAccessibleName("Key Signature");

    struct KeyEntry { int val; const char* name; };
    static const KeyEntry keys[] = {
        {-7, "C\u266D maj / A\u266D min (7\u266D)"},
        {-6, "G\u266D maj / E\u266D min (6\u266D)"},
        {-5, "D\u266D maj / B\u266D min (5\u266D)"},
        {-4, "A\u266D maj / F min (4\u266D)"},
        {-3, "E\u266D maj / C min (3\u266D)"},
        {-2, "B\u266D maj / G min (2\u266D)"},
        {-1, "F maj / D min (1\u266D)"},
        { 0, "C maj / A min"},
        { 1, "G maj / E min (1\u266F)"},
        { 2, "D maj / B min (2\u266F)"},
        { 3, "A maj / F\u266F min (3\u266F)"},
        { 4, "E maj / C\u266F min (4\u266F)"},
        { 5, "B maj / G\u266F min (5\u266F)"},
        { 6, "F\u266F maj / D\u266F min (6\u266F)"},
        { 7, "C\u266F maj / A\u266F min (7\u266F)"},
    };
    for (auto& k : keys)
        keySigCombo_->addItem(QString::fromUtf8(k.name), k.val);
    keySigCombo_->setCurrentIndex(7);

    connect(keySigCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        keySig_ = keySigCombo_->currentData().toInt();
        if (editMgr_ && editMgr_->edit())
            editMgr_->edit()->state.setProperty(kKeySigId, keySig_, nullptr);
        rebuildScore();
    });

    toolbar_->addWidget(keySigCombo_);

    toolbar_->addSeparator();

    printBtn_ = new QPushButton("Print", toolbar_);
    printBtn_->setAccessibleName("Print Sheet Music");
    printBtn_->setToolTip("Print sheet music");
    connect(printBtn_, &QPushButton::clicked, this, &SheetMusicView::onPrint);
    toolbar_->addWidget(printBtn_);

    pdfBtn_ = new QPushButton("Export PDF", toolbar_);
    pdfBtn_->setAccessibleName("Export Sheet Music PDF");
    pdfBtn_->setToolTip("Export sheet music to PDF");
    connect(pdfBtn_, &QPushButton::clicked, this, &SheetMusicView::onExportPdf);
    toolbar_->addWidget(pdfBtn_);

    toolbar_->addSeparator();

    phrasingBtn_ = new QPushButton("Generate Phrasing", toolbar_);
    phrasingBtn_->setAccessibleName("Generate Phrasing");
    phrasingBtn_->setToolTip("Use AI to generate phrase markings");
    connect(phrasingBtn_, &QPushButton::clicked, this, &SheetMusicView::onGeneratePhrasing);
    toolbar_->addWidget(phrasingBtn_);

    clearPhrasingBtn_ = new QPushButton("Clear Phrasing", toolbar_);
    clearPhrasingBtn_->setAccessibleName("Clear Phrasing");
    clearPhrasingBtn_->setToolTip("Remove all phrase markings");
    connect(clearPhrasingBtn_, &QPushButton::clicked, this, &SheetMusicView::onClearPhrasing);
    toolbar_->addWidget(clearPhrasingBtn_);

    auto* spacer = new QWidget(toolbar_);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);

    auto* zoomLabel = new QLabel("Zoom:", toolbar_);
    zoomLabel->setStyleSheet("color: #aaa; padding: 0 4px;");
    toolbar_->addWidget(zoomLabel);

    zoomSlider_ = new QSlider(Qt::Horizontal, toolbar_);
    zoomSlider_->setAccessibleName("Sheet Music Zoom");
    zoomSlider_->setRange(20, 150);
    zoomSlider_->setValue(50);
    zoomSlider_->setFixedWidth(120);
    toolbar_->addWidget(zoomSlider_);

    connect(zoomSlider_, &QSlider::valueChanged, this, &SheetMusicView::onZoomChanged);
}

void SheetMusicView::buildEditToolbar()
{
    editToolbar_ = new QToolBar(this);
    editToolbar_->setAccessibleName("Sheet Music Edit Toolbar");
    editToolbar_->setMovable(false);
    editToolbar_->setIconSize(QSize(16, 16));
    editToolbar_->setStyleSheet("QToolBar { spacing: 2px; padding: 1px 4px; }");

    auto makeBtn = [&](const QString& label, const QString& tooltip,
                       const QString& accessible, int key) {
        auto* btn = new QPushButton(label, editToolbar_);
        btn->setAccessibleName(accessible);
        btn->setToolTip(tooltip);
        btn->setFixedSize(28, 24);
        btn->setStyleSheet("QPushButton { font-size: 14px; padding: 0; }");
        connect(btn, &QPushButton::clicked, this, [this, key]() {
            applyKeyToSelectedNote(key);
        });
        editToolbar_->addWidget(btn);
        return btn;
    };

    makeBtn(QString::fromUtf8("\u266F"), "Sharp (S)", "Sharp Button", Qt::Key_S);
    makeBtn(QString::fromUtf8("\u266D"), "Flat (F)", "Flat Button", Qt::Key_F);
    makeBtn(QString::fromUtf8("\u266E"), "Natural (N)", "Natural Button", Qt::Key_N);

    editToolbar_->addSeparator();

    makeBtn(QString::fromUtf8("\u00B7"),  "Staccato (. or 1)",     "Staccato Button",      Qt::Key_Period);
    makeBtn(QString::fromUtf8("\u2013"),  "Tenuto (2)",            "Tenuto Button",         Qt::Key_2);
    makeBtn(QString::fromUtf8(">"),       "Accent (3)",            "Accent Button",         Qt::Key_3);
    makeBtn(QString::fromUtf8("\u005E"),  "Marcato (4)",           "Marcato Button",        Qt::Key_4);
    makeBtn(QString::fromUtf8("\U0001D110"), "Fermata (5)",        "Fermata Button",        Qt::Key_5);
    makeBtn(QString::fromUtf8("\u25B4"),  "Staccatissimo (6)",     "Staccatissimo Button",  Qt::Key_6);
    makeBtn(QString::fromUtf8("\u00D7"),  "Clear Articulations (0)", "Clear Articulations Button", Qt::Key_0);

    editToolbar_->addSeparator();

    preferFlatsBtn_ = new QPushButton(QString::fromUtf8("Prefer \u266D"), editToolbar_);
    preferFlatsBtn_->setAccessibleName("Prefer Flats Toggle");
    preferFlatsBtn_->setToolTip("When in C major, display accidentals as flats instead of sharps");
    preferFlatsBtn_->setCheckable(true);
    preferFlatsBtn_->setChecked(preferFlats_);
    preferFlatsBtn_->setFixedHeight(24);
    preferFlatsBtn_->setStyleSheet(
        "QPushButton { font-size: 12px; padding: 0 6px; }"
        "QPushButton:checked { background: #556; color: #eee; }");
    connect(preferFlatsBtn_, &QPushButton::toggled, this, [this](bool checked) {
        preferFlats_ = checked;
        if (editMgr_ && editMgr_->edit())
            editMgr_->edit()->state.setProperty(
                juce::Identifier("OpenDaw_PREFER_FLATS"), preferFlats_, nullptr);
        rebuildScore();
    });
    editToolbar_->addWidget(preferFlatsBtn_);
}

void SheetMusicView::applyKeyToSelectedNote(int qtKey)
{
    auto selected = scene_->selectedItems();
    if (selected.isEmpty() || !clip_) return;

    // clear all articulations on selected notes
    if (qtKey == Qt::Key_0) {
        bool changed = false;
        for (auto* item : selected) {
            auto prop = item->data(0);
            if (!prop.isValid()) continue;
            auto* note = static_cast<te::MidiNote*>(prop.value<void*>());
            if (!note) continue;
            double beat = note->getStartBeat().inBeats();
            int midiNote = note->getNoteNumber();

            articulations_.erase(
                std::remove_if(articulations_.begin(), articulations_.end(),
                    [&](const ArticulationMarking& a) {
                        return std::abs(a.beat - beat) < 0.05 && a.midiNote == midiNote;
                    }),
                articulations_.end());
            changed = true;
        }
        if (changed) {
            saveAnnotationsToClip();
            rebuildScore();
        }
        return;
    }

    // map articulation keys to types
    int artType = -1;
    if (qtKey == Qt::Key_Period || qtKey == Qt::Key_1) artType = ArticulationMarking::Staccato;
    else if (qtKey == Qt::Key_2) artType = ArticulationMarking::Tenuto;
    else if (qtKey == Qt::Key_3) artType = ArticulationMarking::Accent;
    else if (qtKey == Qt::Key_4) artType = ArticulationMarking::Marcato;
    else if (qtKey == Qt::Key_5) artType = ArticulationMarking::Fermata;
    else if (qtKey == Qt::Key_6) artType = ArticulationMarking::Staccatissimo;

    if (artType >= 0) {
        auto type = static_cast<ArticulationMarking::Type>(artType);
        for (auto* item : selected) {
            auto prop = item->data(0);
            if (!prop.isValid()) continue;
            auto* note = static_cast<te::MidiNote*>(prop.value<void*>());
            if (!note) continue;
            double beat = note->getStartBeat().inBeats();
            int midiNote = note->getNoteNumber();
            StaffKind staff = (midiNote >= 60) ? StaffKind::Treble : StaffKind::Bass;

            auto it = std::find_if(articulations_.begin(), articulations_.end(),
                [&](const ArticulationMarking& a) {
                    return a.type == type
                           && std::abs(a.beat - beat) < 0.05
                           && a.midiNote == midiNote;
                });
            if (it != articulations_.end())
                articulations_.erase(it);
            else
                articulations_.push_back({beat, midiNote, staff, type});
        }
        saveAnnotationsToClip();
        rebuildScore();
        return;
    }

    // S/F/N: apply pitch changes to all selected notes in batch, then rebuild once
    if (qtKey == Qt::Key_S || qtKey == Qt::Key_F || qtKey == Qt::Key_N) {
        auto* um = &clip_->edit.getUndoManager();
        bool changed = false;
        static const int kDiaBase[7] = {0, 2, 4, 5, 7, 9, 11};
        static const int kSharpOrd[7] = {3, 0, 4, 1, 5, 2, 6};
        static const int kFlatOrd[7]  = {6, 2, 5, 1, 4, 0, 3};
        static const bool kBlack[12] = {false,true,false,true,false,false,true,false,true,false,true,false};

        for (auto* item : selected) {
            auto prop = item->data(0);
            if (!prop.isValid()) continue;
            auto* note = static_cast<te::MidiNote*>(prop.value<void*>());
            if (!note) continue;
            int pitch = note->getNoteNumber();
            int chroma = pitch % 12;
            bool isBlack = kBlack[chroma];

            if (qtKey == Qt::Key_S && !isBlack) {
                int np = std::min(127, pitch + 1);
                note->setNoteNumber(np, um);
                double beat = note->getStartBeat().inBeats();
                auto it = std::find_if(spellingOverrides_.begin(), spellingOverrides_.end(),
                    [&](const SpellingOverride& s) { return std::abs(s.beat - beat) < 0.05 && s.midiNote == np; });
                if (it != spellingOverrides_.end()) it->forced = Accidental::Sharp;
                else spellingOverrides_.push_back({beat, np, Accidental::Sharp});
                changed = true;
            } else if (qtKey == Qt::Key_F && !isBlack) {
                int np = std::max(0, pitch - 1);
                note->setNoteNumber(np, um);
                double beat = note->getStartBeat().inBeats();
                auto it = std::find_if(spellingOverrides_.begin(), spellingOverrides_.end(),
                    [&](const SpellingOverride& s) { return std::abs(s.beat - beat) < 0.05 && s.midiNote == np; });
                if (it != spellingOverrides_.end()) it->forced = Accidental::Flat;
                else spellingOverrides_.push_back({beat, np, Accidental::Flat});
                changed = true;
            } else if (qtKey == Qt::Key_N) {
                int pc = pitch % 12;
                int keyAlts[7] = {};
                if (keySig_ > 0)
                    for (int i = 0; i < std::min(keySig_, 7); i++) keyAlts[kSharpOrd[i]] = +1;
                else if (keySig_ < 0)
                    for (int i = 0; i < std::min(-keySig_, 7); i++) keyAlts[kFlatOrd[i]] = -1;
                int octave = pitch / 12;
                int targetDia = -1;
                for (int d = 0; d < 7; d++)
                    if ((kDiaBase[d] + keyAlts[d] + 12) % 12 == pc) { targetDia = d; break; }
                if (targetDia >= 0 && keyAlts[targetDia] != 0) {
                    int np = octave * 12 + kDiaBase[targetDia];
                    if (np > pitch + 6) np -= 12;
                    if (np < pitch - 6) np += 12;
                    note->setNoteNumber(std::clamp(np, 0, 127), um);
                    double beat = note->getStartBeat().inBeats();
                    spellingOverrides_.push_back({beat, std::clamp(np, 0, 127), Accidental::Natural});
                    changed = true;
                } else if (isBlack) {
                    static const int kDiaFromChroma[12] = {0,0,1,1,2,3,3,4,4,5,5,6};
                    int np = octave * 12 + kDiaBase[kDiaFromChroma[pc]];
                    note->setNoteNumber(std::clamp(np, 0, 127), um);
                    changed = true;
                }
            }
        }
        if (changed) {
            saveAnnotationsToClip();
            rebuildScore();
        }
    }
}

void SheetMusicView::setClip(te::MidiClip* clip, EditManager* editMgr)
{
    clip_ = clip;
    editMgr_ = editMgr;

    if (editMgr_ && editMgr_->edit()) {
        int savedKey = editMgr_->edit()->state.getProperty(kKeySigId, 0);
        keySig_ = std::clamp(savedKey, -7, 7);
        keySigCombo_->blockSignals(true);
        keySigCombo_->setCurrentIndex(keySig_ + 7);
        keySigCombo_->blockSignals(false);

        preferFlats_ = static_cast<bool>(
            editMgr_->edit()->state.getProperty(
                juce::Identifier("OpenDaw_PREFER_FLATS"), false));
        preferFlatsBtn_->blockSignals(true);
        preferFlatsBtn_->setChecked(preferFlats_);
        preferFlatsBtn_->blockSignals(false);
    }

    loadAnnotationsFromClip();
    rebuildScore();
}

void SheetMusicView::refresh()
{
    rebuildScore();
}

void SheetMusicView::rebuildScore()
{
    if (!clip_ || !editMgr_) {
        clipNameLabel_->setText("(no clip selected)");
        scene_->clearScore();
        return;
    }

    clipNameLabel_->setText(QString::fromStdString(clip_->getName().toStdString()));

    NotationModel model;
    model.buildFromClip(clip_,
                        editMgr_->getTimeSigNumerator(),
                        editMgr_->getTimeSigDenominator(),
                        keySig_, preferFlats_);
    model.setPhrases(phrases_);
    model.setArticulations(articulations_);
    model.setSpellingOverrides(spellingOverrides_);

    scene_->setPageWidth(900.0);
    scene_->setTitle(clipNameLabel_->text());
    scene_->setClip(clip_);
    scene_->setPixelsPerBeat(zoomSlider_->value());
    scene_->renderScore(model);

    view_->viewport()->update();
}

void SheetMusicView::onZoomChanged(int value)
{
    scene_->setPixelsPerBeat(value);
    if (clip_ && editMgr_)
        rebuildScore();
}

void SheetMusicView::onNoteChanged()
{
    rebuildScore();
}

void SheetMusicView::onArticulationToggled(double beat, int midiNote, StaffKind staff, int artType)
{
    auto type = static_cast<ArticulationMarking::Type>(artType);

    auto it = std::find_if(articulations_.begin(), articulations_.end(),
        [&](const ArticulationMarking& a) {
            return a.type == type
                   && std::abs(a.beat - beat) < 0.05
                   && a.midiNote == midiNote;
        });

    if (it != articulations_.end()) {
        articulations_.erase(it);
    } else {
        ArticulationMarking am;
        am.beat = beat;
        am.midiNote = midiNote;
        am.staff = staff;
        am.type = type;
        articulations_.push_back(am);
    }

    saveAnnotationsToClip();
    rebuildScore();
}

void SheetMusicView::onSpellingOverride(double beat, int midiNote, Accidental forced)
{
    auto it = std::find_if(spellingOverrides_.begin(), spellingOverrides_.end(),
        [&](const SpellingOverride& s) {
            return std::abs(s.beat - beat) < 0.05 && s.midiNote == midiNote;
        });

    if (it != spellingOverrides_.end()) {
        it->forced = forced;
    } else {
        SpellingOverride so;
        so.beat = beat;
        so.midiNote = midiNote;
        so.forced = forced;
        spellingOverrides_.push_back(so);
    }

    saveAnnotationsToClip();
    rebuildScore();
}

// ── persistence ─────────────────────────────────────────────────────────────

void SheetMusicView::saveAnnotationsToClip()
{
    if (!clip_) return;

    // phrases
    auto existing = clip_->state.getChildWithName(kPhrasesId);
    if (existing.isValid())
        clip_->state.removeChild(existing, nullptr);

    if (!phrases_.empty()) {
        juce::ValueTree phNode(kPhrasesId);
        for (auto& ph : phrases_) {
            juce::ValueTree c("PHRASE");
            c.setProperty("startBeat", ph.startBeat, nullptr);
            c.setProperty("endBeat", ph.endBeat, nullptr);
            c.setProperty("staff", ph.staff == StaffKind::Bass ? "bass" : "treble", nullptr);
            phNode.appendChild(c, nullptr);
        }
        clip_->state.appendChild(phNode, nullptr);
    }

    // articulations
    auto existingArt = clip_->state.getChildWithName(kArticulationsId);
    if (existingArt.isValid())
        clip_->state.removeChild(existingArt, nullptr);

    if (!articulations_.empty()) {
        juce::ValueTree artNode(kArticulationsId);
        for (auto& a : articulations_) {
            juce::ValueTree c("ARTICULATION");
            c.setProperty("beat", a.beat, nullptr);
            c.setProperty("midiNote", a.midiNote, nullptr);
            c.setProperty("staff", a.staff == StaffKind::Bass ? "bass" : "treble", nullptr);
            const char* typeStr = "staccato";
            switch (a.type) {
            case ArticulationMarking::Staccato:      typeStr = "staccato"; break;
            case ArticulationMarking::Tenuto:        typeStr = "tenuto"; break;
            case ArticulationMarking::Marcato:       typeStr = "marcato"; break;
            case ArticulationMarking::Accent:        typeStr = "accent"; break;
            case ArticulationMarking::Fermata:       typeStr = "fermata"; break;
            case ArticulationMarking::Staccatissimo: typeStr = "staccatissimo"; break;
            }
            c.setProperty("type", typeStr, nullptr);
            artNode.appendChild(c, nullptr);
        }
        clip_->state.appendChild(artNode, nullptr);
    }

    // spelling overrides
    auto existingSpell = clip_->state.getChildWithName(kSpellingsId);
    if (existingSpell.isValid())
        clip_->state.removeChild(existingSpell, nullptr);

    if (!spellingOverrides_.empty()) {
        juce::ValueTree spNode(kSpellingsId);
        for (auto& s : spellingOverrides_) {
            juce::ValueTree c("SPELLING");
            c.setProperty("beat", s.beat, nullptr);
            c.setProperty("midiNote", s.midiNote, nullptr);
            auto accStr = (s.forced == Accidental::Sharp) ? "sharp"
                        : (s.forced == Accidental::Flat) ? "flat"
                        : (s.forced == Accidental::Natural) ? "natural" : "none";
            c.setProperty("forced", accStr, nullptr);
            spNode.appendChild(c, nullptr);
        }
        clip_->state.appendChild(spNode, nullptr);
    }
}

void SheetMusicView::loadAnnotationsFromClip()
{
    phrases_.clear();
    articulations_.clear();
    spellingOverrides_.clear();
    if (!clip_) return;

    // phrases
    auto phNode = clip_->state.getChildWithName(kPhrasesId);
    if (phNode.isValid()) {
        for (int i = 0; i < phNode.getNumChildren(); ++i) {
            auto c = phNode.getChild(i);
            PhraseMarking pm;
            pm.startBeat = static_cast<double>(c.getProperty("startBeat", 0.0));
            pm.endBeat = static_cast<double>(c.getProperty("endBeat", 0.0));
            pm.staff = (c.getProperty("staff", "treble").toString() == "bass")
                           ? StaffKind::Bass : StaffKind::Treble;
            if (pm.endBeat > pm.startBeat)
                phrases_.push_back(pm);
        }
    }

    // articulations
    auto artNode = clip_->state.getChildWithName(kArticulationsId);
    if (artNode.isValid()) {
        for (int i = 0; i < artNode.getNumChildren(); ++i) {
            auto c = artNode.getChild(i);
            ArticulationMarking am;
            am.beat = static_cast<double>(c.getProperty("beat", 0.0));
            am.midiNote = static_cast<int>(c.getProperty("midiNote", 60));
            am.staff = (c.getProperty("staff", "treble").toString() == "bass")
                           ? StaffKind::Bass : StaffKind::Treble;
            auto ts = c.getProperty("type", "staccato").toString();
            if (ts == "tenuto")             am.type = ArticulationMarking::Tenuto;
            else if (ts == "marcato")       am.type = ArticulationMarking::Marcato;
            else if (ts == "accent")        am.type = ArticulationMarking::Accent;
            else if (ts == "fermata")       am.type = ArticulationMarking::Fermata;
            else if (ts == "staccatissimo") am.type = ArticulationMarking::Staccatissimo;
            else                            am.type = ArticulationMarking::Staccato;
            articulations_.push_back(am);
        }
    }

    // spelling overrides
    auto spNode = clip_->state.getChildWithName(kSpellingsId);
    if (spNode.isValid()) {
        for (int i = 0; i < spNode.getNumChildren(); ++i) {
            auto c = spNode.getChild(i);
            SpellingOverride so;
            so.beat = static_cast<double>(c.getProperty("beat", 0.0));
            so.midiNote = static_cast<int>(c.getProperty("midiNote", 60));
            auto fs = c.getProperty("forced", "none").toString();
            if (fs == "sharp") so.forced = Accidental::Sharp;
            else if (fs == "flat") so.forced = Accidental::Flat;
            else if (fs == "natural") so.forced = Accidental::Natural;
            else continue;
            spellingOverrides_.push_back(so);
        }
    }
}

// ── printing ────────────────────────────────────────────────────────────────

void SheetMusicView::onPrint()
{
    if (!clip_) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Portrait);

    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle("Print Sheet Music");
    if (dlg.exec() != QDialog::Accepted) return;

    QRectF pageRect = printer.pageRect(QPrinter::DevicePixel);
    double virtualW = 900.0;
    double virtualH = virtualW * 1.414;
    double scale = std::min(pageRect.width() / virtualW, pageRect.height() / virtualH);

    QPainter painter(&printer);
    painter.scale(scale, scale);

    int pages = scene_->pageCount();
    for (int pg = 0; pg < pages; ++pg) {
        if (pg > 0) printer.newPage();
        scene_->paintPage(&painter, pg);
    }
}

void SheetMusicView::onExportPdf()
{
    if (!clip_) return;

    QString path = QFileDialog::getSaveFileName(this, "Export Sheet Music PDF",
                                                 QString(), "PDF Files (*.pdf)");
    if (path.isEmpty()) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageOrientation(QPageLayout::Portrait);

    QRectF pageRect = printer.pageRect(QPrinter::DevicePixel);
    double virtualW = 900.0;
    double virtualH = virtualW * 1.414;
    double scale = std::min(pageRect.width() / virtualW, pageRect.height() / virtualH);

    QPainter painter(&printer);
    painter.scale(scale, scale);

    int pages = scene_->pageCount();
    for (int pg = 0; pg < pages; ++pg) {
        if (pg > 0) printer.newPage();
        scene_->paintPage(&painter, pg);
    }

    QMessageBox::information(this, "Export Complete",
                             QString("PDF saved to:\n%1").arg(path));
}

// ── AI phrasing ─────────────────────────────────────────────────────────────

void SheetMusicView::onGeneratePhrasing()
{
    if (!clip_ || !editMgr_) return;

    QSettings settings;
    QString apiKey = settings.value("ai/apiKey").toString();
    if (apiKey.isEmpty()) {
        QMessageBox::warning(this, "No API Key",
                             "Set your Anthropic API key in AI Preferences first.");
        return;
    }

    phrasingBtn_->setEnabled(false);
    phrasingBtn_->setText("Analyzing...");

    auto& seq = clip_->getSequence();
    QJsonArray notesArr;
    for (auto* note : seq.getNotes()) {
        QJsonObject n;
        n["note"] = note->getNoteNumber();
        n["start_beat"] = note->getStartBeat().inBeats();
        n["length_beats"] = note->getLengthBeats().inBeats();
        notesArr.append(n);
    }

    double beatsPerMeasure = editMgr_->getTimeSigNumerator()
                             * (4.0 / editMgr_->getTimeSigDenominator());

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = QString(
        "Given these MIDI notes:\n%1\n\n"
        "The time signature is %2/%3 (%4 beats per measure).\n\n"
        "Identify musical phrases and return ONLY a JSON array of phrase boundaries. "
        "Each element: {\"start_beat\": <number>, \"end_beat\": <number>, \"staff\": \"treble\" or \"bass\"}.\n\n"
        "Rules:\n"
        "- Notes with MIDI number >= 60 are treble staff, below 60 are bass staff\n"
        "- Phrases typically span 2-8 bars\n"
        "- Look for melodic contour, breathing points, rests, and large intervals\n"
        "- Provide separate phrasing for treble and bass\n"
        "- Return ONLY the JSON array, no other text"
    ).arg(QString(QJsonDocument(notesArr).toJson(QJsonDocument::Compact)))
     .arg(editMgr_->getTimeSigNumerator())
     .arg(editMgr_->getTimeSigDenominator())
     .arg(beatsPerMeasure);

    QJsonArray messages;
    messages.append(userMsg);

    QJsonObject body;
    body["model"] = "claude-sonnet-4-6";
    body["max_tokens"] = 14096;
    body["messages"] = messages;

    QNetworkRequest request{QUrl("https://api.anthropic.com/v1/messages")};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setTransferTimeout(30000);

    auto* reply = nam_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        phrasingBtn_->setEnabled(true);
        phrasingBtn_->setText("Generate Phrasing");

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "Phrasing Error",
                                 "API request failed: " + reply->errorString());
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto content = doc.object()["content"].toArray();
        QString text;
        for (const auto& block : content) {
            if (block.toObject()["type"].toString() == "text")
                text = block.toObject()["text"].toString();
        }

        int arrStart = text.indexOf('[');
        int arrEnd = text.lastIndexOf(']');
        if (arrStart < 0 || arrEnd < 0) {
            QMessageBox::warning(this, "Phrasing Error", "Could not parse AI response.");
            return;
        }

        auto arrDoc = QJsonDocument::fromJson(text.mid(arrStart, arrEnd - arrStart + 1).toUtf8());
        if (!arrDoc.isArray()) {
            QMessageBox::warning(this, "Phrasing Error", "AI response was not a valid JSON array.");
            return;
        }

        phrases_.clear();
        for (const auto& elem : arrDoc.array()) {
            auto obj = elem.toObject();
            PhraseMarking pm;
            pm.startBeat = obj["start_beat"].toDouble();
            pm.endBeat = obj["end_beat"].toDouble();
            pm.staff = (obj["staff"].toString() == "bass") ? StaffKind::Bass : StaffKind::Treble;
            if (pm.endBeat > pm.startBeat)
                phrases_.push_back(pm);
        }

        saveAnnotationsToClip();
        rebuildScore();

        QMessageBox::information(this, "Phrasing Complete",
                                 QString("Added %1 phrase markings.").arg(phrases_.size()));
    });
}

void SheetMusicView::onClearPhrasing()
{
    phrases_.clear();
    saveAnnotationsToClip();
    rebuildScore();
}

} // namespace OpenDaw
