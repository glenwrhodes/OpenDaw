#pragma once

#include "TimeRuler.h"
#include "GridSnapper.h"
#include "ClipItem.h"
#include "TrackHeaderWidget.h"
#include "engine/EditManager.h"
#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QScrollBar>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QEvent>
#include <vector>
#include <memory>

namespace freedaw {

class TimelineScene : public QGraphicsScene {
    Q_OBJECT

public:
    explicit TimelineScene(QObject* parent = nullptr);

protected:
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;
    void dropEvent(QGraphicsSceneDragDropEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void fileDropped(const QString& filePath, double beat, int trackIndex);
    void emptyAreaDoubleClicked(double sceneX, double sceneY);
    void backgroundClicked(double sceneX, double sceneY);
    void backgroundRightClicked(QPointF scenePos, QPoint screenPos);
};

class TimelineView : public QWidget {
    Q_OBJECT

public:
    explicit TimelineView(EditManager* editMgr, QWidget* parent = nullptr);

    void setPixelsPerBeat(double ppb);
    double pixelsPerBeat() const { return pixelsPerBeat_; }

    void setTrackHeight(double h);
    double trackHeight() const { return trackHeight_; }

    GridSnapper& snapper() { return snapper_; }
    const GridSnapper& snapper() const { return snapper_; }

    void zoomIn();
    void zoomOut();
    void zoomVerticalIn();
    void zoomVerticalOut();

    void rebuildClips();

signals:
    void snapModeChanged(SnapMode mode);
    void instrumentSelectRequested(te::AudioTrack* track);
    void trackSelected(te::AudioTrack* track);

public slots:
    void onTracksChanged();
    void onEditChanged();
    void onTransportPositionChanged();
    void splitSelectedClipsAtPlayhead();
    void deleteSelectedClips();
    void setSelectedTrack(te::AudioTrack* track);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void rebuildTrackHeaders();
    void drawGridLines();
    void updatePlayhead();
    void syncHeaderScroll();
    void handleFileDrop(const QString& path, double beat, int trackIndex);
    void handleEmptyAreaDoubleClick(double sceneX, double sceneY);
    void selectTrack(te::AudioTrack* track);

    EditManager* editMgr_;

    QHBoxLayout* topRowLayout_;
    QWidget*     headerCorner_;
    TimeRuler*   ruler_;

    QHBoxLayout* bodyLayout_;
    QScrollArea* headerScrollArea_;
    QWidget*     headerContainer_;
    QVBoxLayout* headerVLayout_;
    QGraphicsView*  graphicsView_;
    TimelineScene*  scene_;

    double pixelsPerBeat_ = 40.0;
    double trackHeight_   = 80.0;
    GridSnapper snapper_;

    QGraphicsLineItem* playheadLine_ = nullptr;
    QTimer playheadTimer_;

    std::vector<ClipItem*> clipItems_;
    std::vector<QGraphicsRectItem*> trackBgItems_;
    std::vector<QGraphicsLineItem*> gridLineItems_;
    std::vector<TrackHeaderWidget*> trackHeaders_;
    te::AudioTrack* selectedTrack_ = nullptr;

    static constexpr int HEADER_WIDTH = 140;
};

} // namespace freedaw
