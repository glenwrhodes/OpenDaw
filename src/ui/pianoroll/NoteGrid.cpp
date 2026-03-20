#include "NoteGrid.h"
#include "utils/ThemeManager.h"
#include <QScrollBar>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QShowEvent>
#include <QTimer>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <cmath>
#include <random>
#include <algorithm>
#include <map>
#include <unordered_set>

namespace freedaw {

std::vector<ClipboardNote> NoteGrid::clipboard_;

NoteGridScene::NoteGridScene(QObject* parent) : QGraphicsScene(parent) {}

void NoteGridScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsScene::mousePressEvent(event);
}

void NoteGridScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsScene::mouseDoubleClickEvent(event);
}

NoteGrid::NoteGrid(QWidget* parent) : QGraphicsView(parent)
{
    setAccessibleName("Note Grid");
    scene_ = new NoteGridScene(this);
    setScene(scene_);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setRenderHint(QPainter::Antialiasing, false);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setFocusPolicy(Qt::StrongFocus);

    auto& theme = ThemeManager::instance().current();
    setBackgroundBrush(theme.pianoRollBackground);

    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &NoteGrid::verticalScrollChanged);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
            this, &NoteGrid::horizontalScrollChanged);

    setEditMode(EditMode::Edit);
    updateSceneSize();
}

void NoteGrid::setClip(te::MidiClip* clip)
{
    stopAllPreviews();
    primaryClip_ = clip;
    allClips_.clear();
    if (clip)
        allClips_.push_back(clip);
    rebuildNotes();
}

void NoteGrid::setClips(te::MidiClip* primary, const std::vector<te::MidiClip*>& all)
{
    stopAllPreviews();
    primaryClip_ = primary;
    allClips_ = all;
    rebuildNotes();
}

void NoteGrid::setPrimaryClip(te::MidiClip* clip)
{
    primaryClip_ = clip;
    rebuildNotes();
}

void NoteGrid::setChannelVisible(int ch, bool visible)
{
    if (visible)
        hiddenChannels_.erase(ch);
    else
        hiddenChannels_.insert(ch);
    rebuildNotes();
}

void NoteGrid::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat_ = std::clamp(ppb, 10.0, 300.0);
    updateSceneSize();
    rebuildNotes();
    emit zoomChanged();
}

void NoteGrid::setNoteRowHeight(double h)
{
    noteRowHeight_ = std::clamp(h, 6.0, 40.0);
    updateSceneSize();
    rebuildNotes();
}

void NoteGrid::setEditMode(EditMode mode)
{
    editMode_ = mode;
    setDragMode(editMode_ == EditMode::Edit ? QGraphicsView::RubberBandDrag
                                            : QGraphicsView::NoDrag);
    if (editMode_ != EditMode::Draw)
        clearDrawPreview();
}

void NoteGrid::updateSceneSize()
{
    double clipLenBeats = 16.0;
    if (primaryClip_) {
        auto& ts = primaryClip_->edit.tempoSequence;
        double startBeat = ts.toBeats(primaryClip_->getPosition().getStart()).inBeats();
        double endBeat = ts.toBeats(primaryClip_->getPosition().getEnd()).inBeats();
        clipLenBeats = endBeat - startBeat;
    }
    double totalBeats = std::max(clipLenBeats + 16.0, 32.0);
    double w = totalBeats * pixelsPerBeat_;
    double h = TOTAL_NOTES * noteRowHeight_;
    scene_->setSceneRect(0, 0, w, h);
}

void NoteGrid::drawBackground()
{
    auto& theme = ThemeManager::instance().current();
    double sceneW = scene_->sceneRect().width();
    double sceneH = TOTAL_NOTES * noteRowHeight_;

    for (auto* item : bgItems_) scene_->removeItem(item);
    for (auto* item : gridLines_) scene_->removeItem(item);
    qDeleteAll(bgItems_);
    qDeleteAll(gridLines_);
    bgItems_.clear();
    gridLines_.clear();

    if (clipRegionLeft_) { scene_->removeItem(clipRegionLeft_); delete clipRegionLeft_; clipRegionLeft_ = nullptr; }
    if (clipRegionRight_) { scene_->removeItem(clipRegionRight_); delete clipRegionRight_; clipRegionRight_ = nullptr; }

    double clipLenBeats = 4.0;
    if (primaryClip_) {
        auto& ts = primaryClip_->edit.tempoSequence;
        double startBeat = ts.toBeats(primaryClip_->getPosition().getStart()).inBeats();
        double endBeat = ts.toBeats(primaryClip_->getPosition().getEnd()).inBeats();
        clipLenBeats = endBeat - startBeat;
    }
    double clipEndX = clipLenBeats * pixelsPerBeat_;

    for (int note = 0; note < TOTAL_NOTES; ++note) {
        int row = (TOTAL_NOTES - 1) - note;
        double y = row * noteRowHeight_;
        bool black = (note % 12 == 1 || note % 12 == 3 || note % 12 == 6
                     || note % 12 == 8 || note % 12 == 10);
        QColor bg = black ? theme.pianoRollBlackKey : theme.pianoRollWhiteKey;
        auto* rect = scene_->addRect(0, y, sceneW, noteRowHeight_,
                                     QPen(Qt::NoPen), QBrush(bg));
        rect->setZValue(-2);
        bgItems_.push_back(rect);

        if (note % 12 == 0) {
            auto* line = scene_->addLine(0, y + noteRowHeight_, sceneW, y + noteRowHeight_,
                                         QPen(theme.pianoRollGrid.lighter(120), 0.8));
            line->setZValue(-1);
            gridLines_.push_back(line);
        }
    }

    QColor dimOverlay(0, 0, 0, 100);
    if (clipEndX < sceneW) {
        clipRegionRight_ = scene_->addRect(clipEndX, 0, sceneW - clipEndX, sceneH,
                                           QPen(Qt::NoPen), QBrush(dimOverlay));
        clipRegionRight_->setZValue(0);
    }

    auto* rightBorder = scene_->addLine(clipEndX, 0, clipEndX, sceneH,
                                        QPen(QColor(255, 255, 255, 60), 1.5));
    rightBorder->setZValue(0);
    gridLines_.push_back(rightBorder);

    double beatsPerBar = 4.0;
    double totalBeats = sceneW / pixelsPerBeat_;
    for (double beat = 0; beat < totalBeats; beat += 1.0) {
        double x = beat * pixelsPerBeat_;
        bool isMajor = (std::fmod(beat, beatsPerBar) < 0.01);
        QPen pen(isMajor ? theme.pianoRollGrid.lighter(130) : theme.pianoRollGrid,
                 isMajor ? 0.8 : 0.4);
        auto* line = scene_->addLine(x, 0, x, sceneH, pen);
        line->setZValue(-1);
        gridLines_.push_back(line);
    }
}

void NoteGrid::expandClipToFitNotes()
{
    if (!primaryClip_) return;

    auto& ts = primaryClip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(primaryClip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(primaryClip_->getPosition().getEnd()).inBeats();
    double clipLenBeats = clipEndBeat - clipStartBeat;

    auto& seq = primaryClip_->getSequence();
    double maxNoteEnd = clipLenBeats;
    bool needsExpand = false;

    for (auto* note : seq.getNotes()) {
        double noteEnd = note->getStartBeat().inBeats() + note->getLengthBeats().inBeats();
        if (noteEnd > clipLenBeats) {
            maxNoteEnd = std::max(maxNoteEnd, noteEnd);
            needsExpand = true;
        }
    }

    if (needsExpand) {
        double newEndAbsBeat = clipStartBeat + maxNoteEnd;
        auto newEndTime = ts.toTime(tracktion::BeatPosition::fromBeats(newEndAbsBeat));
        primaryClip_->setEnd(newEndTime, false);
        updateSceneSize();
    }
}

void NoteGrid::setPendingCopySelect(std::vector<ClipboardNote> notes)
{
    pendingCopySelect_ = std::move(notes);
}

// ── Rebuild with selection persistence ────────────────────────────────

void NoteGrid::rebuildNotes()
{
    // Save current selection (te::MidiNote pointers survive rebuild if notes aren't removed)
    std::unordered_set<te::MidiNote*> savedSel;
    for (auto* item : noteItems_) {
        if (item->isSelected())
            savedSel.insert(item->note());
    }

    for (auto* item : noteItems_) scene_->removeItem(item);
    qDeleteAll(noteItems_);
    noteItems_.clear();

    expandClipToFitNotes();
    drawBackground();

    if (!primaryClip_ || allClips_.empty()) return;

    auto& ts = primaryClip_->edit.tempoSequence;
    double primaryStartBeat = ts.toBeats(primaryClip_->getPosition().getStart()).inBeats();

    for (auto* mc : allClips_) {
        int ch = mc->getMidiChannel().getChannelNumber();
        if (ch < 1) ch = 1;
        if (hiddenChannels_.count(ch)) continue;

        double clipStartBeat = ts.toBeats(mc->getPosition().getStart()).inBeats();
        double beatOffset = clipStartBeat - primaryStartBeat;
        bool isPrimary = (mc == primaryClip_);

        auto& seq = mc->getSequence();
        for (auto* note : seq.getNotes()) {
            auto* item = new NoteItem(note, mc, pixelsPerBeat_, noteRowHeight_, 0, ch);
            item->updateGeometry(pixelsPerBeat_, noteRowHeight_, 0, TOTAL_NOTES, beatOffset);
            item->setGridSnapper(&snapper_);
            item->setActiveChannel(isPrimary);
            item->setRefreshCallback([this]() {
                QTimer::singleShot(0, this, [this]() {
                    expandClipToFitNotes();
                    rebuildNotes();
                    emit notesChanged();
                });
            });
            item->setZValue(isPrimary ? 2.0 : 1.5);
            scene_->addItem(item);
            noteItems_.push_back(item);
        }
    }

    // Restore selection: either from copy-drag pending, or from saved pointers
    if (!pendingCopySelect_.empty()) {
        for (auto* item : noteItems_) {
            auto* n = item->note();
            double nb = n->getStartBeat().inBeats();
            int np = n->getNoteNumber();
            double nl = n->getLengthBeats().inBeats();
            int nv = n->getVelocity();
            for (auto& cn : pendingCopySelect_) {
                if (cn.pitch == np && std::abs(cn.beatOffset - nb) < 0.001
                    && std::abs(cn.length - nl) < 0.001 && cn.velocity == nv) {
                    item->setSelected(true);
                    break;
                }
            }
        }
        pendingCopySelect_.clear();
    } else {
        for (auto* item : noteItems_) {
            if (savedSel.count(item->note()))
                item->setSelected(true);
        }
    }

    updateTypingCursorVisual();
}

// ── Mouse events ──────────────────────────────────────────────────────

void NoteGrid::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        setFocus(Qt::MouseFocusReason);

    if (editMode_ == EditMode::Draw && event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());
        const auto hitItems = scene_->items(scenePos);
        for (auto* item : hitItems) {
            if (auto* noteItem = dynamic_cast<NoteItem*>(item)) {
                scene_->clearSelection();
                noteItem->setSelected(true);
                setEditMode(EditMode::Edit);
                emit editModeRequested();
                event->accept();
                return;
            }
        }
        beginDrawPreview(scenePos);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void NoteGrid::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (editMode_ == EditMode::Edit && event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());
        const auto hitItems = scene_->items(scenePos);
        bool hitNote = false;
        for (auto* item : hitItems) {
            if (dynamic_cast<NoteItem*>(item)) {
                hitNote = true;
                break;
            }
        }
        if (!hitNote) {
            setEditMode(EditMode::Draw);
            emit drawModeRequested();
            beginDrawPreview(scenePos);
            event->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void NoteGrid::mouseMoveEvent(QMouseEvent* event)
{
    if (editMode_ == EditMode::Draw && isDrawingNote_) {
        updateDrawPreview(mapToScene(event->pos()));
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void NoteGrid::mouseReleaseEvent(QMouseEvent* event)
{
    if (editMode_ == EditMode::Draw && event->button() == Qt::LeftButton && isDrawingNote_) {
        commitDrawPreview(mapToScene(event->pos()));
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void NoteGrid::wheelEvent(QWheelEvent* event)
{
    if (editMode_ == EditMode::Draw && isDrawingNote_) {
        const int raw = event->angleDelta().y() / 120;
        const int velocityStep = (raw >= 0) ? std::max(1, raw) : std::min(-1, raw);
        drawVelocity_ = std::clamp(drawVelocity_ + velocityStep * 4, 1, 127);
        defaultDrawVelocity_ = drawVelocity_;
        updateDrawPreviewAppearance();
        event->accept();
        return;
    }

    if (event->modifiers() & Qt::ControlModifier) {
        double factor = std::pow(1.15, event->angleDelta().y() / 120.0);
        setPixelsPerBeat(pixelsPerBeat_ * factor);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

// ── Selection ─────────────────────────────────────────────────────────

void NoteGrid::deleteSelectedNotes()
{
    if (!primaryClip_) return;
    auto* um = &primaryClip_->edit.getUndoManager();

    std::map<te::MidiClip*, std::vector<te::MidiNote*>> clipNotes;
    for (auto* item : noteItems_) {
        if (item->isSelected())
            clipNotes[item->clip()].push_back(item->note());
    }
    for (auto& [clip, notes] : clipNotes) {
        auto& seq = clip->getSequence();
        for (auto* note : notes)
            seq.removeNote(*note, um);
    }

    rebuildNotes();
    emit notesChanged();
}

void NoteGrid::selectAllNotes()
{
    for (auto* item : noteItems_)
        item->setSelected(true);
}

void NoteGrid::deselectAllNotes()
{
    scene_->clearSelection();
}

// ── Clipboard ─────────────────────────────────────────────────────────

void NoteGrid::copySelectedNotes()
{
    clipboard_.clear();
    if (!primaryClip_) return;

    double minBeat = 1e9;
    std::vector<NoteItem*> selected;
    for (auto* item : noteItems_) {
        if (item->isSelected()) {
            selected.push_back(item);
            double b = item->note()->getStartBeat().inBeats();
            if (b < minBeat) minBeat = b;
        }
    }
    for (auto* item : selected) {
        auto* n = item->note();
        clipboard_.push_back({
            n->getNoteNumber(),
            n->getStartBeat().inBeats() - minBeat,
            n->getLengthBeats().inBeats(),
            n->getVelocity()
        });
    }
}

void NoteGrid::cutSelectedNotes()
{
    copySelectedNotes();
    deleteSelectedNotes();
}

void NoteGrid::pasteNotes(double atBeat)
{
    if (!primaryClip_ && ensureClipCb_)
        primaryClip_ = ensureClipCb_();
    if (!primaryClip_ || clipboard_.empty()) return;
    auto& seq = primaryClip_->getSequence();
    auto* um = &primaryClip_->edit.getUndoManager();

    um->beginNewTransaction("Paste Notes");

    std::vector<ClipboardNote> pasted;
    for (auto& cn : clipboard_) {
        double b = atBeat + cn.beatOffset;
        seq.addNote(cn.pitch,
                    tracktion::BeatPosition::fromBeats(b),
                    tracktion::BeatDuration::fromBeats(cn.length),
                    cn.velocity, 0, um);
        pasted.push_back({ cn.pitch, b, cn.length, cn.velocity });
    }

    setPendingCopySelect(std::move(pasted));
    expandClipToFitNotes();
    rebuildNotes();
    emit notesChanged();
}

void NoteGrid::duplicateSelectedNotes()
{
    if (!primaryClip_) return;
    copySelectedNotes();
    if (clipboard_.empty()) return;

    double maxEnd = 0;
    for (auto& cn : clipboard_)
        maxEnd = std::max(maxEnd, cn.beatOffset + cn.length);

    double grid = snapper_.gridIntervalBeats();
    if (grid <= 0.0) grid = 0.25;
    double offset = std::max(grid, std::ceil(maxEnd / grid) * grid);

    double minBeat = 1e9;
    for (auto* item : noteItems_) {
        if (item->isSelected()) {
            double b = item->note()->getStartBeat().inBeats();
            if (b < minBeat) minBeat = b;
        }
    }
    pasteNotes(minBeat + offset);
}

// ── Transpose ─────────────────────────────────────────────────────────

void NoteGrid::transposeSelected(int semitones)
{
    if (!primaryClip_) return;
    auto* um = &primaryClip_->edit.getUndoManager();
    um->beginNewTransaction("Transpose");

    bool any = false;
    for (auto* item : noteItems_) {
        if (item->isSelected()) {
            int newPitch = std::clamp(item->note()->getNoteNumber() + semitones, 0, 127);
            item->note()->setNoteNumber(newPitch, um);
            any = true;
        }
    }
    if (any) {
        rebuildNotes();
        emit notesChanged();
    }
}

void NoteGrid::showTransposeDialog()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Transpose");
    dlg->setAccessibleName("Transpose Dialog");
    dlg->setFixedWidth(280);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* form = new QFormLayout();
    form->setContentsMargins(12, 12, 12, 6);

    auto* semiSpin = new QSpinBox(dlg);
    semiSpin->setAccessibleName("Semitones");
    semiSpin->setRange(-48, 48);
    semiSpin->setValue(0);
    semiSpin->setSuffix(" semitones");
    form->addRow("Transpose:", semiSpin);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* okBtn = new QPushButton("OK", dlg);
    okBtn->setAccessibleName("OK");
    okBtn->setDefault(true);
    auto* cancelBtn = new QPushButton("Cancel", dlg);
    cancelBtn->setAccessibleName("Cancel");
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(dlg);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, dlg, [this, dlg, semiSpin]() {
        int val = semiSpin->value();
        dlg->accept();
        if (val != 0) transposeSelected(val);
    });
    dlg->exec();
}

// ── Quantize with strength ────────────────────────────────────────────

void NoteGrid::quantizeNotes()
{
    showQuantizeDialog();
}

void NoteGrid::showQuantizeDialog()
{
    if (!primaryClip_) return;

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Quantize");
    dlg->setAccessibleName("Quantize Dialog");
    dlg->setFixedWidth(320);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* form = new QFormLayout();
    form->setContentsMargins(12, 12, 12, 6);

    auto* gridCombo = new QComboBox(dlg);
    gridCombo->setAccessibleName("Grid Size");
    gridCombo->addItem("1/32", 0.125);
    gridCombo->addItem("1/16", 0.25);
    gridCombo->addItem("1/8", 0.5);
    gridCombo->addItem("1/4", 1.0);
    gridCombo->addItem("Bar", 4.0);
    double currentGrid = snapper_.gridIntervalBeats();
    for (int i = 0; i < gridCombo->count(); ++i) {
        if (std::abs(gridCombo->itemData(i).toDouble() - currentGrid) < 0.001) {
            gridCombo->setCurrentIndex(i);
            break;
        }
    }
    form->addRow("Grid:", gridCombo);

    auto* strengthSlider = new QSlider(Qt::Horizontal, dlg);
    strengthSlider->setAccessibleName("Quantize Strength");
    strengthSlider->setRange(0, 100);
    strengthSlider->setValue(100);
    auto* strengthLabel = new QLabel("100%", dlg);
    strengthLabel->setAccessibleName("Strength Value");
    connect(strengthSlider, &QSlider::valueChanged, strengthLabel,
            [strengthLabel](int val) { strengthLabel->setText(QString("%1%").arg(val)); });

    auto* strengthRow = new QHBoxLayout();
    strengthRow->addWidget(strengthSlider, 1);
    strengthRow->addWidget(strengthLabel);
    form->addRow("Strength:", strengthRow);

    auto* quantizeLengthCheck = new QCheckBox("Quantize note lengths", dlg);
    quantizeLengthCheck->setAccessibleName("Quantize Lengths");
    quantizeLengthCheck->setChecked(false);
    form->addRow(quantizeLengthCheck);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* okBtn = new QPushButton("OK", dlg);
    okBtn->setAccessibleName("OK");
    okBtn->setDefault(true);
    auto* cancelBtn = new QPushButton("Cancel", dlg);
    cancelBtn->setAccessibleName("Cancel");
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(dlg);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, dlg,
        [this, dlg, gridCombo, strengthSlider, quantizeLengthCheck]() {
        double grid = gridCombo->currentData().toDouble();
        double strength = strengthSlider->value() / 100.0;
        bool quantizeLen = quantizeLengthCheck->isChecked();
        dlg->accept();

        auto* um = &primaryClip_->edit.getUndoManager();
        um->beginNewTransaction("Quantize Notes");

        bool hasSelection = false;
        for (auto* item : noteItems_)
            if (item->isSelected()) { hasSelection = true; break; }

        for (auto* item : noteItems_) {
            if (hasSelection && !item->isSelected()) continue;
            auto* note = item->note();
            double orig = note->getStartBeat().inBeats();
            double snapped = std::round(orig / grid) * grid;
            double newBeat = orig + (snapped - orig) * strength;

            double len = note->getLengthBeats().inBeats();
            if (quantizeLen) {
                double snappedLen = std::max(grid, std::round(len / grid) * grid);
                len = len + (snappedLen - len) * strength;
            }
            note->setStartAndLength(
                tracktion::BeatPosition::fromBeats(newBeat),
                tracktion::BeatDuration::fromBeats(std::max(0.03125, len)), um);
        }
        rebuildNotes();
        emit notesChanged();
    });
    dlg->exec();
}

// ── Reverse ───────────────────────────────────────────────────────────

void NoteGrid::reverseSelectedNotes()
{
    if (!primaryClip_) return;

    struct NoteData { te::MidiNote* note; double start; double length; };
    std::vector<NoteData> sel;
    for (auto* item : noteItems_) {
        if (item->isSelected()) {
            auto* n = item->note();
            sel.push_back({ n, n->getStartBeat().inBeats(), n->getLengthBeats().inBeats() });
        }
    }
    if (sel.size() < 2) return;

    double minStart = 1e9, maxEnd = 0;
    for (auto& d : sel) {
        minStart = std::min(minStart, d.start);
        maxEnd = std::max(maxEnd, d.start + d.length);
    }

    auto* um = &primaryClip_->edit.getUndoManager();
    um->beginNewTransaction("Reverse Notes");
    for (auto& d : sel) {
        double newStart = maxEnd - (d.start - minStart) - d.length;
        d.note->setStartAndLength(
            tracktion::BeatPosition::fromBeats(std::max(0.0, newStart)),
            tracktion::BeatDuration::fromBeats(d.length), um);
    }
    rebuildNotes();
    emit notesChanged();
}

// ── Swing quantize ────────────────────────────────────────────────────

void NoteGrid::showSwingDialog()
{
    if (!primaryClip_) return;

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Swing");
    dlg->setAccessibleName("Swing Dialog");
    dlg->setFixedWidth(300);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* form = new QFormLayout();
    form->setContentsMargins(12, 12, 12, 6);

    auto* swingSlider = new QSlider(Qt::Horizontal, dlg);
    swingSlider->setAccessibleName("Swing Amount");
    swingSlider->setRange(0, 100);
    swingSlider->setValue(50);
    auto* swingLabel = new QLabel("50%", dlg);
    swingLabel->setAccessibleName("Swing Value");
    connect(swingSlider, &QSlider::valueChanged, swingLabel,
            [swingLabel](int val) { swingLabel->setText(QString("%1%").arg(val)); });

    auto* sliderRow = new QHBoxLayout();
    sliderRow->addWidget(swingSlider, 1);
    sliderRow->addWidget(swingLabel);
    form->addRow("Swing:", sliderRow);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* okBtn = new QPushButton("OK", dlg);
    okBtn->setAccessibleName("OK");
    okBtn->setDefault(true);
    auto* cancelBtn = new QPushButton("Cancel", dlg);
    cancelBtn->setAccessibleName("Cancel");
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(dlg);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, dlg, [this, dlg, swingSlider]() {
        double swingPct = swingSlider->value() / 100.0;
        dlg->accept();

        double grid = snapper_.gridIntervalBeats();
        if (grid <= 0.0) grid = 0.25;
        double swingOffset = grid * 0.5 * swingPct;

        auto* um = &primaryClip_->edit.getUndoManager();
        um->beginNewTransaction("Swing Quantize");

        bool hasSelection = false;
        for (auto* item : noteItems_)
            if (item->isSelected()) { hasSelection = true; break; }

        for (auto* item : noteItems_) {
            if (hasSelection && !item->isSelected()) continue;
            auto* note = item->note();
            double beat = note->getStartBeat().inBeats();
            double snapped = std::round(beat / grid) * grid;
            int gridIndex = static_cast<int>(std::round(snapped / grid));
            double finalBeat = snapped;
            if (gridIndex % 2 == 1)
                finalBeat += swingOffset;
            note->setStartAndLength(
                tracktion::BeatPosition::fromBeats(std::max(0.0, finalBeat)),
                note->getLengthBeats(), um);
        }
        rebuildNotes();
        emit notesChanged();
    });
    dlg->exec();
}

// ── Humanize ──────────────────────────────────────────────────────────

void NoteGrid::showHumanizeDialog()
{
    if (!primaryClip_) return;

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Humanize");
    dlg->setAccessibleName("Humanize Dialog");
    dlg->setFixedWidth(320);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* form = new QFormLayout();
    form->setContentsMargins(12, 12, 12, 6);

    auto* timingSpin = new QSpinBox(dlg);
    timingSpin->setAccessibleName("Timing Randomness");
    timingSpin->setRange(0, 100);
    timingSpin->setValue(20);
    timingSpin->setSuffix(" ticks");
    form->addRow("Timing:", timingSpin);

    auto* velSpin = new QSpinBox(dlg);
    velSpin->setAccessibleName("Velocity Randomness");
    velSpin->setRange(0, 40);
    velSpin->setValue(10);
    velSpin->setSuffix(" units");
    form->addRow("Velocity:", velSpin);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* okBtn = new QPushButton("OK", dlg);
    okBtn->setAccessibleName("OK");
    okBtn->setDefault(true);
    auto* cancelBtn = new QPushButton("Cancel", dlg);
    cancelBtn->setAccessibleName("Cancel");
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(dlg);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, dlg, [this, dlg, timingSpin, velSpin]() {
        int timingRange = timingSpin->value();
        int velRange = velSpin->value();
        dlg->accept();

        auto* um = &primaryClip_->edit.getUndoManager();
        um->beginNewTransaction("Humanize");

        std::mt19937 rng(std::random_device{}());
        double tickToBeats = 1.0 / 480.0;

        bool hasSelection = false;
        for (auto* item : noteItems_)
            if (item->isSelected()) { hasSelection = true; break; }

        for (auto* item : noteItems_) {
            if (hasSelection && !item->isSelected()) continue;
            auto* note = item->note();

            if (timingRange > 0) {
                std::uniform_int_distribution<int> timeDist(-timingRange, timingRange);
                double shift = timeDist(rng) * tickToBeats;
                double newBeat = std::max(0.0, note->getStartBeat().inBeats() + shift);
                note->setStartAndLength(
                    tracktion::BeatPosition::fromBeats(newBeat),
                    note->getLengthBeats(), um);
            }
            if (velRange > 0) {
                std::uniform_int_distribution<int> velDist(-velRange, velRange);
                int newVel = std::clamp(note->getVelocity() + velDist(rng), 1, 127);
                note->setVelocity(newVel, um);
            }
        }
        rebuildNotes();
        emit notesChanged();
    });
    dlg->exec();
}

// ── Legato ────────────────────────────────────────────────────────────

void NoteGrid::legatoSelectedNotes()
{
    if (!primaryClip_) return;

    struct NoteData { te::MidiNote* note; double start; };
    std::vector<NoteData> sel;
    for (auto* item : noteItems_) {
        if (item->isSelected())
            sel.push_back({ item->note(), item->note()->getStartBeat().inBeats() });
    }
    if (sel.size() < 2) return;

    std::sort(sel.begin(), sel.end(),
              [](auto& a, auto& b) { return a.start < b.start; });

    auto* um = &primaryClip_->edit.getUndoManager();
    um->beginNewTransaction("Legato");

    for (size_t i = 0; i + 1 < sel.size(); ++i) {
        double gap = sel[i + 1].start - sel[i].start;
        if (gap > 0.0) {
            sel[i].note->setStartAndLength(
                sel[i].note->getStartBeat(),
                tracktion::BeatDuration::fromBeats(gap), um);
        }
    }
    rebuildNotes();
    emit notesChanged();
}

// ── Note preview (audition) ───────────────────────────────────────────

void NoteGrid::playNotePreview(int noteNumber, int velocity)
{
    if (!primaryClip_) return;
    auto* track = primaryClip_->getAudioTrack();
    if (!track) return;

    noteNumber = std::clamp(noteNumber, 0, 127);
    velocity = std::clamp(velocity, 1, 127);

    if (activePreviewNotes_.count(noteNumber))
        stopNotePreview(noteNumber);

    auto msg = juce::MidiMessage::noteOn(1, noteNumber, static_cast<juce::uint8>(velocity));
    track->injectLiveMidiMessage(msg, 0);
    activePreviewNotes_.insert(noteNumber);
}

void NoteGrid::stopNotePreview(int noteNumber)
{
    if (!primaryClip_ || !activePreviewNotes_.count(noteNumber)) return;
    auto* track = primaryClip_->getAudioTrack();
    if (!track) return;

    auto msg = juce::MidiMessage::noteOff(1, noteNumber);
    track->injectLiveMidiMessage(msg, 0);
    activePreviewNotes_.erase(noteNumber);
}

void NoteGrid::stopAllPreviews()
{
    if (!primaryClip_) {
        activePreviewNotes_.clear();
        return;
    }
    auto* track = primaryClip_->getAudioTrack();
    if (!track) {
        activePreviewNotes_.clear();
        return;
    }
    for (int note : activePreviewNotes_) {
        auto msg = juce::MidiMessage::noteOff(1, note);
        track->injectLiveMidiMessage(msg, 0);
    }
    activePreviewNotes_.clear();
}

// ── Musical typing ────────────────────────────────────────────────────

void NoteGrid::setMusicalTypingEnabled(bool enabled)
{
    musicalTypingEnabled_ = enabled;
    if (!enabled) {
        stopAllPreviews();
        heldTypingKeys_.clear();
    }
    updateTypingCursorVisual();
    emit musicalTypingToggled(enabled);
}

void NoteGrid::setStepRecordEnabled(bool enabled)
{
    stepRecordEnabled_ = enabled;
    if (enabled && !musicalTypingEnabled_)
        setMusicalTypingEnabled(true);
    updateTypingCursorVisual();
    emit stepRecordToggled(enabled);
}

int NoteGrid::musicalTypingKeyToPitch(int qtKey) const
{
    int semitone = -1;
    switch (qtKey) {
    case Qt::Key_A: semitone = 0;  break;
    case Qt::Key_W: semitone = 1;  break;
    case Qt::Key_S: semitone = 2;  break;
    case Qt::Key_E: semitone = 3;  break;
    case Qt::Key_D: semitone = 4;  break;
    case Qt::Key_F: semitone = 5;  break;
    case Qt::Key_T: semitone = 6;  break;
    case Qt::Key_G: semitone = 7;  break;
    case Qt::Key_Y: semitone = 8;  break;
    case Qt::Key_H: semitone = 9;  break;
    case Qt::Key_U: semitone = 10; break;
    case Qt::Key_J: semitone = 11; break;
    case Qt::Key_K: semitone = 12; break;
    default: return -1;
    }
    int pitch = (typingOctave_ + 1) * 12 + semitone;
    return std::clamp(pitch, 0, 127);
}

void NoteGrid::insertNoteAtCursor(int pitch)
{
    if (!primaryClip_ && ensureClipCb_)
        primaryClip_ = ensureClipCb_();
    if (!primaryClip_) return;
    auto& seq = primaryClip_->getSequence();
    auto* um = &primaryClip_->edit.getUndoManager();

    double length = snapper_.gridIntervalBeats();
    if (length <= 0.0) length = 0.25;

    seq.addNote(pitch,
                tracktion::BeatPosition::fromBeats(typingCursorBeat_),
                tracktion::BeatDuration::fromBeats(length),
                typingVelocity_, 0, um);

    expandClipToFitNotes();
    rebuildNotes();
    emit notesChanged();
}

void NoteGrid::setTypingCursorBeat(double beat)
{
    typingCursorBeat_ = std::max(0.0, beat);
    updateTypingCursorVisual();
    emit typingCursorMoved(typingCursorBeat_);
}

void NoteGrid::advanceCursor()
{
    double step = snapper_.gridIntervalBeats();
    if (step <= 0.0) step = 0.25;
    typingCursorBeat_ += step;
    updateTypingCursorVisual();
    emit typingCursorMoved(typingCursorBeat_);
}

void NoteGrid::updateTypingCursorVisual()
{
    if (typingCursorLine_) {
        scene_->removeItem(typingCursorLine_);
        delete typingCursorLine_;
        typingCursorLine_ = nullptr;
    }
    if (typingCursorLabel_) {
        scene_->removeItem(typingCursorLabel_);
        delete typingCursorLabel_;
        typingCursorLabel_ = nullptr;
    }

    if (!musicalTypingEnabled_ && !stepRecordEnabled_) return;

    double x = typingCursorBeat_ * pixelsPerBeat_;
    double sceneH = TOTAL_NOTES * noteRowHeight_;

    typingCursorLine_ = scene_->addLine(x, 0, x, sceneH,
                                        QPen(QColor(0, 220, 110, 220), 2.5));
    typingCursorLine_->setZValue(5);

    QString label = QString("Oct %1  Vel %2").arg(typingOctave_).arg(typingVelocity_);
    typingCursorLabel_ = scene_->addSimpleText(label);
    typingCursorLabel_->setBrush(QColor(0, 220, 110));
    QFont labelFont;
    labelFont.setPixelSize(10);
    labelFont.setBold(true);
    typingCursorLabel_->setFont(labelFont);
    typingCursorLabel_->setPos(x + 4, 4);
    typingCursorLabel_->setZValue(5.1);
}

// ── Key press ─────────────────────────────────────────────────────────

void NoteGrid::keyPressEvent(QKeyEvent* event)
{
    const auto mods = event->modifiers();
    const int key = event->key();

    if (musicalTypingEnabled_ && !(mods & Qt::ControlModifier)) {
        if (key == Qt::Key_Z && !(mods & Qt::ShiftModifier)) {
            typingOctave_ = std::max(-1, typingOctave_ - 1);
            updateTypingCursorVisual();
            event->accept(); return;
        }
        if (key == Qt::Key_X) {
            typingOctave_ = std::min(8, typingOctave_ + 1);
            updateTypingCursorVisual();
            event->accept(); return;
        }
        if (key == Qt::Key_C) {
            typingVelocity_ = std::max(1, typingVelocity_ - 10);
            updateTypingCursorVisual();
            event->accept(); return;
        }
        if (key == Qt::Key_V) {
            typingVelocity_ = std::min(127, typingVelocity_ + 10);
            updateTypingCursorVisual();
            event->accept(); return;
        }

        int pitch = musicalTypingKeyToPitch(key);
        if (pitch >= 0 && !event->isAutoRepeat()) {
            heldTypingKeys_.insert(key);
            playNotePreview(pitch, typingVelocity_);
            insertNoteAtCursor(pitch);
            event->accept();
            return;
        }
    }

    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        deleteSelectedNotes();
        event->accept();
        return;
    }

    if ((mods & Qt::ControlModifier) && key == Qt::Key_A) {
        selectAllNotes();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_C) {
        copySelectedNotes();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_X) {
        cutSelectedNotes();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_V) {
        double pasteBeat = (musicalTypingEnabled_ || stepRecordEnabled_)
                           ? typingCursorBeat_ : lastContextMenuBeat_;
        pasteNotes(pasteBeat);
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_D) {
        duplicateSelectedNotes();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_Q) {
        showQuantizeDialog();
        event->accept();
        return;
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_L) {
        legatoSelectedNotes();
        event->accept();
        return;
    }

    if (key == Qt::Key_Up) {
        int delta = (mods & Qt::ShiftModifier) ? 12 : 1;
        transposeSelected(delta);
        event->accept();
        return;
    }
    if (key == Qt::Key_Down) {
        int delta = (mods & Qt::ShiftModifier) ? -12 : -1;
        transposeSelected(delta);
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void NoteGrid::keyReleaseEvent(QKeyEvent* event)
{
    if (musicalTypingEnabled_ && !event->isAutoRepeat()) {
        int key = event->key();
        if (heldTypingKeys_.count(key)) {
            int pitch = musicalTypingKeyToPitch(key);
            if (pitch >= 0)
                stopNotePreview(pitch);
            heldTypingKeys_.erase(key);
            if (heldTypingKeys_.empty())
                advanceCursor();
            event->accept();
            return;
        }
    }
    QGraphicsView::keyReleaseEvent(event);
}

// ── Context menu ──────────────────────────────────────────────────────

void NoteGrid::contextMenuEvent(QContextMenuEvent* event)
{
    QPointF scenePos = mapToScene(event->pos());
    lastContextMenuBeat_ = snapper_.snapBeat(scenePos.x() / pixelsPerBeat_);
    if (lastContextMenuBeat_ < 0) lastContextMenuBeat_ = 0;

    QMenu menu;
    menu.setAccessibleName("Note Grid Context Menu");

    auto* editMenu = menu.addMenu("Edit");
    editMenu->setAccessibleName("Edit Menu");
    editMenu->addAction("Select All\tCtrl+A", this, &NoteGrid::selectAllNotes);
    editMenu->addAction("Deselect All", this, &NoteGrid::deselectAllNotes);
    editMenu->addSeparator();
    editMenu->addAction("Cut\tCtrl+X", this, &NoteGrid::cutSelectedNotes);
    editMenu->addAction("Copy\tCtrl+C", this, &NoteGrid::copySelectedNotes);
    editMenu->addAction("Paste Here\tCtrl+V", this, [this]() {
        pasteNotes(lastContextMenuBeat_);
    });
    editMenu->addAction("Duplicate\tCtrl+D", this, &NoteGrid::duplicateSelectedNotes);
    editMenu->addSeparator();
    editMenu->addAction("Delete\tDel", this, &NoteGrid::deleteSelectedNotes);

    auto* transformMenu = menu.addMenu("Transform");
    transformMenu->setAccessibleName("Transform Menu");
    transformMenu->addAction("Quantize...\tCtrl+Q", this, &NoteGrid::showQuantizeDialog);
    transformMenu->addAction("Swing...", this, &NoteGrid::showSwingDialog);
    transformMenu->addAction("Humanize...", this, &NoteGrid::showHumanizeDialog);
    transformMenu->addSeparator();
    transformMenu->addAction("Transpose...", this, &NoteGrid::showTransposeDialog);
    transformMenu->addAction("Transpose Up\tUp", this, [this]() { transposeSelected(1); });
    transformMenu->addAction("Transpose Down\tDown", this, [this]() { transposeSelected(-1); });
    transformMenu->addAction("Octave Up\tShift+Up", this, [this]() { transposeSelected(12); });
    transformMenu->addAction("Octave Down\tShift+Down", this, [this]() { transposeSelected(-12); });
    transformMenu->addSeparator();
    transformMenu->addAction("Reverse", this, &NoteGrid::reverseSelectedNotes);
    transformMenu->addAction("Legato\tCtrl+L", this, &NoteGrid::legatoSelectedNotes);

    auto* inputMenu = menu.addMenu("Input");
    inputMenu->setAccessibleName("Input Menu");
    auto* typingAction = inputMenu->addAction("Musical Typing");
    typingAction->setCheckable(true);
    typingAction->setChecked(musicalTypingEnabled_);
    connect(typingAction, &QAction::toggled, this, &NoteGrid::setMusicalTypingEnabled);

    auto* stepAction = inputMenu->addAction("Step Record");
    stepAction->setCheckable(true);
    stepAction->setChecked(stepRecordEnabled_);
    connect(stepAction, &QAction::toggled, this, &NoteGrid::setStepRecordEnabled);

    if (primaryClip_) {
        menu.addSeparator();
        menu.addAction("Add Note Here", [this]() {
            double beat = lastContextMenuBeat_;
            QPointF cursorPos = mapFromGlobal(QCursor::pos());
            double sceneY = mapToScene(cursorPos.toPoint()).y();
            int row = static_cast<int>(sceneY / noteRowHeight_);
            int pitch = (TOTAL_NOTES - 1) - row;
            pitch = std::clamp(pitch, 0, 127);

            double length = std::max(0.25, snapper_.gridIntervalBeats());
            auto& seq = primaryClip_->getSequence();
            auto* um = &primaryClip_->edit.getUndoManager();
            seq.addNote(pitch,
                        tracktion::BeatPosition::fromBeats(beat),
                        tracktion::BeatDuration::fromBeats(length),
                        100, 0, um);
            rebuildNotes();
            emit notesChanged();
        });
    }

    menu.exec(event->globalPos());
}

void NoteGrid::scrollToMidiNote(int midiNote)
{
    midiNote = std::clamp(midiNote, 0, TOTAL_NOTES - 1);
    const int row = (TOTAL_NOTES - 1) - midiNote;
    const int targetY = static_cast<int>(row * noteRowHeight_ + noteRowHeight_ * 0.5);
    const int centeredValue = targetY - viewport()->height() / 2;
    verticalScrollBar()->setValue(std::max(0, centeredValue));
}

void NoteGrid::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);

    if (!initialVerticalScrollSet_) {
        scrollToMidiNote(60);
        initialVerticalScrollSet_ = true;
        emit verticalScrollChanged(verticalScrollBar()->value());
    }
}

// ── Draw preview ──────────────────────────────────────────────────────

void NoteGrid::beginDrawPreview(const QPointF& scenePos)
{
    if (!primaryClip_) return;

    double beat = scenePos.x() / pixelsPerBeat_;
    beat = snapper_.snapBeat(beat);
    if (beat < 0.0) beat = 0.0;

    const int row = static_cast<int>(scenePos.y() / noteRowHeight_);
    drawPitch_ = std::clamp((TOTAL_NOTES - 1) - row, 0, 127);
    drawVelocity_ = std::clamp(defaultDrawVelocity_, 1, 127);
    drawStartBeat_ = beat;
    drawCurrentBeat_ = beat;
    isDrawingNote_ = true;
    playNotePreview(drawPitch_, drawVelocity_);

    if (!drawPreviewItem_) {
        auto& theme = ThemeManager::instance().current();
        drawPreviewItem_ = scene_->addRect(0, 0, 1.0, std::max(1.0, noteRowHeight_ - 1.0),
                                           QPen(theme.pianoRollNoteSelected.lighter(120), 1.0, Qt::DashLine),
                                           QBrush(theme.pianoRollNote));
        drawPreviewItem_->setZValue(3.5);
    }
    if (!drawPreviewText_) {
        drawPreviewText_ = scene_->addSimpleText("");
        drawPreviewText_->setZValue(3.6);
    }

    updateDrawPreview(scenePos);
}

void NoteGrid::updateDrawPreview(const QPointF& scenePos)
{
    if (!isDrawingNote_ || !drawPreviewItem_) return;

    double beat = scenePos.x() / pixelsPerBeat_;
    beat = snapper_.snapBeat(beat);
    if (beat < 0.0) beat = 0.0;
    drawCurrentBeat_ = beat;

    double minLength = snapper_.gridIntervalBeats();
    if (minLength <= 0.0) minLength = 0.25;

    const double leftBeat = std::min(drawStartBeat_, drawCurrentBeat_);
    const double rightBeat = std::max(drawStartBeat_, drawCurrentBeat_);
    const double lengthBeats = std::max(minLength, rightBeat - leftBeat);

    const double x = leftBeat * pixelsPerBeat_;
    const int row = (TOTAL_NOTES - 1) - drawPitch_;
    const double y = row * noteRowHeight_;
    const double width = std::max(1.0, lengthBeats * pixelsPerBeat_);
    const double height = std::max(1.0, noteRowHeight_ - 1.0);

    drawPreviewItem_->setRect(0, 0, width, height);
    drawPreviewItem_->setPos(x, y);
    updateDrawPreviewAppearance();
}

void NoteGrid::commitDrawPreview(const QPointF& scenePos)
{
    if (!primaryClip_ && ensureClipCb_)
        primaryClip_ = ensureClipCb_();

    if (!primaryClip_ || !isDrawingNote_) {
        clearDrawPreview();
        return;
    }

    updateDrawPreview(scenePos);

    double minLength = snapper_.gridIntervalBeats();
    if (minLength <= 0.0) minLength = 0.25;

    const double leftBeat = std::min(drawStartBeat_, drawCurrentBeat_);
    const double rightBeat = std::max(drawStartBeat_, drawCurrentBeat_);
    const double lengthBeats = std::max(minLength, rightBeat - leftBeat);

    auto& seq = primaryClip_->getSequence();
    auto* um = &primaryClip_->edit.getUndoManager();
    seq.addNote(drawPitch_,
                tracktion::BeatPosition::fromBeats(leftBeat),
                tracktion::BeatDuration::fromBeats(lengthBeats),
                drawVelocity_, 0, um);

    clearDrawPreview();
    rebuildNotes();
    emit notesChanged();
}

void NoteGrid::clearDrawPreview()
{
    if (isDrawingNote_)
        stopNotePreview(drawPitch_);
    isDrawingNote_ = false;

    if (drawPreviewItem_) {
        scene_->removeItem(drawPreviewItem_);
        delete drawPreviewItem_;
        drawPreviewItem_ = nullptr;
    }
    if (drawPreviewText_) {
        scene_->removeItem(drawPreviewText_);
        delete drawPreviewText_;
        drawPreviewText_ = nullptr;
    }
}

QString NoteGrid::midiNoteName(int midiNote) const
{
    static const char* kNames[12] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    midiNote = std::clamp(midiNote, 0, 127);
    const int octave = (midiNote / 12) - 1;
    const int semitone = midiNote % 12;
    return QString("%1%2").arg(kNames[semitone]).arg(octave);
}

void NoteGrid::updateDrawPreviewAppearance()
{
    if (!drawPreviewItem_ || !drawPreviewText_) return;

    auto& theme = ThemeManager::instance().current();
    QColor noteColor = theme.pianoRollNoteSelected;
    noteColor.setAlphaF(0.35 + 0.55 * (drawVelocity_ / 127.0));

    drawPreviewItem_->setBrush(QBrush(noteColor));
    drawPreviewItem_->setPen(QPen(theme.pianoRollNoteSelected.lighter(130), 1.0, Qt::DashLine));

    const QString previewText = QString("%1  Vel %2")
        .arg(midiNoteName(drawPitch_))
        .arg(drawVelocity_);
    drawPreviewText_->setText(previewText);
    drawPreviewText_->setBrush(theme.text);
    drawPreviewText_->setPos(drawPreviewItem_->pos().x() + 4.0,
                             drawPreviewItem_->pos().y() - 16.0);
}

} // namespace freedaw
