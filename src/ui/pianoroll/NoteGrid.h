#pragma once

#include "NoteItem.h"
#include "ui/timeline/GridSnapper.h"
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>
#include <unordered_set>
#include <set>

class QShowEvent;

namespace te = tracktion::engine;

namespace freedaw {

class NoteGridScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit NoteGridScene(QObject* parent = nullptr);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void emptyAreaClicked(double beat, int pitch);
    void emptyAreaDoubleClicked(double sceneX, double sceneY);
};

struct ClipboardNote {
    int pitch;
    double beatOffset;
    double length;
    int velocity;
};

class NoteGrid : public QGraphicsView {
    Q_OBJECT

public:
    enum class EditMode {
        Draw = 0,
        Edit = 1
    };

    explicit NoteGrid(QWidget* parent = nullptr);

    void setClip(te::MidiClip* clip);
    void setClips(te::MidiClip* primary, const std::vector<te::MidiClip*>& all);
    void setPrimaryClip(te::MidiClip* clip);
    void setChannelVisible(int ch, bool visible);
    te::MidiClip* clip() const { return primaryClip_; }

    void setPixelsPerBeat(double ppb);
    void setNoteRowHeight(double h);
    double pixelsPerBeat() const { return pixelsPerBeat_; }
    double noteRowHeight() const { return noteRowHeight_; }

    GridSnapper& snapper() { return snapper_; }
    void setEditMode(EditMode mode);
    EditMode editMode() const { return editMode_; }

    void rebuildNotes();
    void deleteSelectedNotes();
    void selectAllNotes();
    void deselectAllNotes();
    void scrollToMidiNote(int midiNote);

    // Clipboard
    void copySelectedNotes();
    void cutSelectedNotes();
    void pasteNotes(double atBeat);
    void duplicateSelectedNotes();

    // Transpose
    void transposeSelected(int semitones);
    void showTransposeDialog();

    // Transforms
    void quantizeNotes();
    void showQuantizeDialog();
    void reverseSelectedNotes();
    void showSwingDialog();
    void showHumanizeDialog();
    void legatoSelectedNotes();

    // Musical typing
    void setMusicalTypingEnabled(bool enabled);
    bool musicalTypingEnabled() const { return musicalTypingEnabled_; }

    // Step record
    void setStepRecordEnabled(bool enabled);
    bool stepRecordEnabled() const { return stepRecordEnabled_; }

    // Note preview (audition through virtual instrument)
    void playNotePreview(int noteNumber, int velocity = 100);
    void stopNotePreview(int noteNumber);
    void stopAllPreviews();

    // Copy-drag selection (called by NoteItem after copy-drag)
    void setPendingCopySelect(std::vector<ClipboardNote> notes);

    // Step cursor position (for ruler sync)
    void setTypingCursorBeat(double beat);
    double typingCursorBeat() const { return typingCursorBeat_; }

    void setEnsureClipCallback(std::function<te::MidiClip*()> cb) { ensureClipCb_ = std::move(cb); }

signals:
    void notesChanged();
    void zoomChanged();
    void verticalScrollChanged(int value);
    void horizontalScrollChanged(int value);
    void editModeRequested();
    void drawModeRequested();
    void musicalTypingToggled(bool enabled);
    void stepRecordToggled(bool enabled);
    void typingCursorMoved(double beat);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void drawBackground();
    void updateSceneSize();
    void expandClipToFitNotes();
    void beginDrawPreview(const QPointF& scenePos);
    void updateDrawPreview(const QPointF& scenePos);
    void commitDrawPreview(const QPointF& scenePos);
    void clearDrawPreview();
    QString midiNoteName(int midiNote) const;
    void updateDrawPreviewAppearance();

    // Musical typing helpers
    int musicalTypingKeyToPitch(int qtKey) const;
    void insertNoteAtCursor(int pitch);
    void advanceCursor();
    void updateTypingCursorVisual();

    te::MidiClip* primaryClip_ = nullptr;
    std::vector<te::MidiClip*> allClips_;
    std::set<int> hiddenChannels_;
    NoteGridScene* scene_;
    GridSnapper snapper_;

    double pixelsPerBeat_ = 60.0;
    double noteRowHeight_ = 14.0;
    static constexpr int TOTAL_NOTES = 128;

    std::vector<NoteItem*> noteItems_;
    std::vector<QGraphicsRectItem*> bgItems_;
    std::vector<QGraphicsLineItem*> gridLines_;
    QGraphicsRectItem* clipRegionLeft_ = nullptr;
    QGraphicsRectItem* clipRegionRight_ = nullptr;
    QGraphicsLineItem* playheadLine_ = nullptr;
    bool initialVerticalScrollSet_ = false;

    EditMode editMode_ = EditMode::Edit;
    bool isDrawingNote_ = false;
    int drawPitch_ = 60;
    int drawVelocity_ = 100;
    int defaultDrawVelocity_ = 100;
    double drawStartBeat_ = 0.0;
    double drawCurrentBeat_ = 0.0;
    QGraphicsRectItem* drawPreviewItem_ = nullptr;
    QGraphicsSimpleTextItem* drawPreviewText_ = nullptr;

    // Clipboard (shared across instances)
    static std::vector<ClipboardNote> clipboard_;
    double lastContextMenuBeat_ = 0.0;

    // Selection persistence
    std::vector<ClipboardNote> pendingCopySelect_;

    // Musical typing / step record
    bool musicalTypingEnabled_ = false;
    bool stepRecordEnabled_ = false;
    int typingOctave_ = 4;
    int typingVelocity_ = 100;
    double typingCursorBeat_ = 0.0;
    QGraphicsLineItem* typingCursorLine_ = nullptr;
    QGraphicsSimpleTextItem* typingCursorLabel_ = nullptr;
    std::set<int> heldTypingKeys_;

    // Note preview tracking
    std::set<int> activePreviewNotes_;

    std::function<te::MidiClip*()> ensureClipCb_;
};

} // namespace freedaw
