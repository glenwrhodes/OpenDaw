#pragma once

#include "NoteItem.h"
#include "ui/timeline/GridSnapper.h"
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsLineItem>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

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

class NoteGrid : public QGraphicsView {
    Q_OBJECT

public:
    explicit NoteGrid(QWidget* parent = nullptr);

    void setClip(te::MidiClip* clip);
    void setPixelsPerBeat(double ppb);
    void setNoteRowHeight(double h);
    double pixelsPerBeat() const { return pixelsPerBeat_; }
    double noteRowHeight() const { return noteRowHeight_; }

    GridSnapper& snapper() { return snapper_; }

    void rebuildNotes();
    void deleteSelectedNotes();
    void selectAllNotes();
    void quantizeNotes();

signals:
    void notesChanged();
    void verticalScrollChanged(int value);
    void horizontalScrollChanged(int value);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void drawBackground();
    void updateSceneSize();
    void expandClipToFitNotes();

    te::MidiClip* clip_ = nullptr;
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
};

} // namespace freedaw
