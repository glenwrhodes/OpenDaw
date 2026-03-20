#pragma once

#include "TimeRuler.h"
#include "GridSnapper.h"
#include "ClipItem.h"
#include "TrackHeaderWidget.h"
#include "AutomationLaneItem.h"
#include "AutomationLaneHeader.h"
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
    void cancelBackgroundDrag();

protected:
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;
    void dropEvent(QGraphicsSceneDragDropEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void fileDropped(const QString& filePath, double beat, int trackIndex);
    void emptyAreaDoubleClicked(double sceneX, double sceneY);
    void backgroundClicked(double sceneX, double sceneY);
    void backgroundRightClicked(QPointF scenePos, QPoint screenPos);
    void backgroundDragStarted(QPointF startScenePos);
    void backgroundDragUpdated(QPointF startScenePos, QPointF currentScenePos);
    void backgroundDragFinished(QPointF startScenePos, QPointF endScenePos);

private:
    bool backgroundDragCandidate_ = false;
    bool backgroundDragging_ = false;
    QPointF backgroundDragStartScenePos_;
};

struct TrackLayoutInfo {
    int trackIndex = 0;
    double yOffset = 0.0;
    double clipRowHeight = 120.0;
    bool automationVisible = false;
    double automationLaneHeight = 80.0;
    te::AutomatableParameter* shownParam = nullptr;
    double totalHeight() const {
        return clipRowHeight + (automationVisible ? automationLaneHeight : 0.0);
    }
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

    te::AudioTrack* selectedTrack() const { return selectedTrack_; }
    void rebuildClips();

signals:
    void snapModeChanged(SnapMode mode);
    void instrumentSelectRequested(te::AudioTrack* track);
    void trackSelected(te::AudioTrack* track);
    void selectedClipsDeleted();

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
    void handleBackgroundDragStarted(QPointF startScenePos);
    void handleBackgroundDragUpdated(QPointF startScenePos, QPointF currentScenePos);
    void handleBackgroundDragFinished(QPointF startScenePos, QPointF endScenePos);
    void clearMidiClipDrawPreview();
    void selectClipItem(te::Clip* clip);
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
    double trackHeight_   = 120.0;
    GridSnapper snapper_;

    QGraphicsLineItem* playheadLine_ = nullptr;
    QTimer playheadTimer_;

    std::vector<ClipItem*> clipItems_;
    std::vector<QGraphicsRectItem*> trackBgItems_;
    std::vector<QGraphicsLineItem*> gridLineItems_;
    std::vector<QGraphicsLineItem*> trackSeparatorItems_;
    std::vector<TrackHeaderWidget*> trackHeaders_;
    QGraphicsRectItem* midiClipDrawPreviewItem_ = nullptr;
    te::AudioTrack* midiClipDrawTrack_ = nullptr;
    double midiClipDrawStartBeat_ = 0.0;
    bool isMidiClipDrawActive_ = false;
    te::AudioTrack* selectedTrack_ = nullptr;

    // Automation lane state
    std::vector<TrackLayoutInfo> layout_;
    std::vector<AutomationLaneItem*> automationLaneItems_;
    std::vector<AutomationLaneHeader*> automationLaneHeaders_;
    std::vector<QGraphicsRectItem*> laneResizeHandles_;

    void rebuildLayout();
    void toggleAutomation(te::AudioTrack* track, bool visible);
    void onAutomationParamChanged(int trackIndex, te::AutomatableParameter* param);
    int trackIndexAtSceneY(double sceneY) const;
    double trackYOffset(int trackIndex) const;
    void cleanupAutomationItems();
    void rebuildAutomationLanes(double sceneWidth);

    // Track transport state for automation refresh
    bool wasPlaying_ = false;

    // Lane resize
    bool laneResizing_ = false;
    int laneResizeTrackIndex_ = -1;
    double laneResizeStartHeight_ = 0.0;
    double laneResizeStartY_ = 0.0;

    static constexpr int HEADER_WIDTH = 140;
    static constexpr double kMinLaneHeight = 50.0;
    static constexpr double kMaxLaneHeight = 120.0;
    static constexpr double kDefaultLaneHeight = 80.0;
    static constexpr double kResizeHandleHeight = 4.0;
};

} // namespace freedaw
