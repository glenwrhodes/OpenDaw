#include "NoteGrid.h"
#include "utils/ThemeManager.h"
#include <QScrollBar>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QTimer>
#include <QGraphicsSceneMouseEvent>
#include <cmath>

namespace freedaw {

NoteGridScene::NoteGridScene(QObject* parent) : QGraphicsScene(parent) {}

void NoteGridScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() & Qt::ControlModifier) {
        auto hitItems = this->items(event->scenePos());
        bool hitNote = false;
        for (auto* item : hitItems) {
            if (dynamic_cast<NoteItem*>(item)) { hitNote = true; break; }
        }
        if (!hitNote) {
            emit emptyAreaClicked(event->scenePos().x(), static_cast<int>(event->scenePos().y()));
            event->accept();
            return;
        }
    }
    QGraphicsScene::mousePressEvent(event);
}

void NoteGridScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        auto hitItems = this->items(event->scenePos());
        bool hitNote = false;
        for (auto* item : hitItems) {
            if (dynamic_cast<NoteItem*>(item)) { hitNote = true; break; }
        }
        if (!hitNote) {
            emit emptyAreaDoubleClicked(event->scenePos().x(), event->scenePos().y());
            event->accept();
            return;
        }
    }
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

    auto& theme = ThemeManager::instance().current();
    setBackgroundBrush(theme.pianoRollBackground);

    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &NoteGrid::verticalScrollChanged);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
            this, &NoteGrid::horizontalScrollChanged);

    auto addNoteAtScenePos = [this](double xPos, double yPos) {
        if (!clip_) return;
        double beat = xPos / pixelsPerBeat_;
        beat = snapper_.snapBeat(beat);
        if (beat < 0) beat = 0;

        int row = static_cast<int>(yPos / noteRowHeight_);
        int pitch = (TOTAL_NOTES - 1) - row;
        pitch = std::clamp(pitch, 0, 127);

        double length = std::max(0.25, snapper_.gridIntervalBeats());
        auto& seq = clip_->getSequence();
        auto* um = &clip_->edit.getUndoManager();
        seq.addNote(pitch,
                    tracktion::BeatPosition::fromBeats(beat),
                    tracktion::BeatDuration::fromBeats(length),
                    100, 0, um);
        rebuildNotes();
        emit notesChanged();
    };

    connect(scene_, &NoteGridScene::emptyAreaClicked,
            this, [addNoteAtScenePos](double xPos, int yPos) {
                addNoteAtScenePos(xPos, static_cast<double>(yPos));
            });

    connect(scene_, &NoteGridScene::emptyAreaDoubleClicked,
            this, [addNoteAtScenePos](double xPos, double yPos) {
                addNoteAtScenePos(xPos, yPos);
            });

    updateSceneSize();
}

void NoteGrid::setClip(te::MidiClip* clip)
{
    clip_ = clip;
    rebuildNotes();
}

void NoteGrid::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat_ = std::clamp(ppb, 10.0, 300.0);
    updateSceneSize();
    rebuildNotes();
}

void NoteGrid::setNoteRowHeight(double h)
{
    noteRowHeight_ = std::clamp(h, 6.0, 40.0);
    updateSceneSize();
    rebuildNotes();
}

void NoteGrid::updateSceneSize()
{
    double clipLenBeats = 16.0;
    if (clip_) {
        auto& ts = clip_->edit.tempoSequence;
        double startBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
        double endBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
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
    if (clip_) {
        auto& ts = clip_->edit.tempoSequence;
        double startBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
        double endBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
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
    if (!clip_) return;

    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double clipLenBeats = clipEndBeat - clipStartBeat;

    auto& seq = clip_->getSequence();
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
        clip_->setEnd(newEndTime, false);
        updateSceneSize();
    }
}

void NoteGrid::rebuildNotes()
{
    for (auto* item : noteItems_) scene_->removeItem(item);
    qDeleteAll(noteItems_);
    noteItems_.clear();

    expandClipToFitNotes();
    drawBackground();

    if (!clip_) return;

    auto& seq = clip_->getSequence();
    for (auto* note : seq.getNotes()) {
        auto* item = new NoteItem(note, clip_, pixelsPerBeat_, noteRowHeight_, 0);
        item->updateGeometry(pixelsPerBeat_, noteRowHeight_, 0, TOTAL_NOTES);
        item->setGridSnapper(&snapper_);
        item->setRefreshCallback([this]() {
            QTimer::singleShot(0, this, [this]() {
                expandClipToFitNotes();
                rebuildNotes();
                emit notesChanged();
            });
        });
        item->setZValue(2);
        scene_->addItem(item);
        noteItems_.push_back(item);
    }
}

void NoteGrid::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        double factor = std::pow(1.15, event->angleDelta().y() / 120.0);
        setPixelsPerBeat(pixelsPerBeat_ * factor);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void NoteGrid::deleteSelectedNotes()
{
    if (!clip_) return;
    auto& seq = clip_->getSequence();
    auto* um = &clip_->edit.getUndoManager();

    std::vector<te::MidiNote*> toDelete;
    for (auto* item : noteItems_) {
        if (item->isSelected())
            toDelete.push_back(item->note());
    }
    for (auto* note : toDelete)
        seq.removeNote(*note, um);

    rebuildNotes();
    emit notesChanged();
}

void NoteGrid::selectAllNotes()
{
    for (auto* item : noteItems_)
        item->setSelected(true);
}

void NoteGrid::quantizeNotes()
{
    if (!clip_) return;
    auto& seq = clip_->getSequence();
    auto* um = &clip_->edit.getUndoManager();

    double grid = snapper_.gridIntervalBeats();
    if (grid <= 0.0) grid = 0.25;

    for (auto* note : seq.getNotes()) {
        double startBeat = note->getStartBeat().inBeats();
        double snapped = std::round(startBeat / grid) * grid;
        double len = note->getLengthBeats().inBeats();
        double snappedLen = std::max(grid, std::round(len / grid) * grid);
        note->setStartAndLength(
            tracktion::BeatPosition::fromBeats(snapped),
            tracktion::BeatDuration::fromBeats(snappedLen), um);
    }

    rebuildNotes();
    emit notesChanged();
}

void NoteGrid::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedNotes();
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_A) {
        selectAllNotes();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void NoteGrid::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu;
    menu.setAccessibleName("Note Grid Context Menu");

    menu.addAction("Select All", this, &NoteGrid::selectAllNotes);
    menu.addAction("Delete Selected", this, &NoteGrid::deleteSelectedNotes);
    menu.addSeparator();
    menu.addAction("Quantize Notes", this, &NoteGrid::quantizeNotes);

    if (clip_) {
        menu.addSeparator();
        menu.addAction("Add Note Here", [this, event]() {
            double sceneX = mapToScene(event->pos()).x();
            double sceneY = mapToScene(event->pos()).y();
            double beat = sceneX / pixelsPerBeat_;
            beat = snapper_.snapBeat(beat);
            if (beat < 0) beat = 0;

            int row = static_cast<int>(sceneY / noteRowHeight_);
            int pitch = (TOTAL_NOTES - 1) - row;
            pitch = std::clamp(pitch, 0, 127);

            double length = std::max(0.25, snapper_.gridIntervalBeats());
            auto& seq = clip_->getSequence();
            auto* um = &clip_->edit.getUndoManager();
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

} // namespace freedaw
