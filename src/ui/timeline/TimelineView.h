#pragma once

#include "TimeRuler.h"
#include "GridSnapper.h"
#include "ClipItem.h"
#include "TrackHeaderWidget.h"
#include "FolderHeaderWidget.h"
#include "AutomationLaneItem.h"
#include "AutomationLaneHeader.h"
#include "TakeLanes.h"
#include "RecordingPreview.h"
#include "EditTool.h"
#include "MarkerTempoLane.h"
#include "VideoTrackLane.h"
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
#include <QLabel>
#include <vector>
#include <memory>

namespace OpenDaw {

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
    // Range, Razor and Eraser act on whatever is under the mouse, clips included.
    void toolDragStarted(QPointF startScenePos);
    void toolDragUpdated(QPointF startScenePos, QPointF currentScenePos);
    void toolDragFinished(QPointF startScenePos, QPointF endScenePos);
    void toolClicked(QPointF scenePos, Qt::KeyboardModifiers mods);

private:
    // True when the active tool handles this press instead of the items.
    bool toolTakesPress(QGraphicsSceneMouseEvent* event) const;
    bool toolPressed_ = false;
    bool toolDragging_ = false;
    QPointF toolStartScenePos_;
    bool backgroundDragCandidate_ = false;
    bool backgroundDragging_ = false;
    QPointF backgroundDragStartScenePos_;
};

struct TrackLayoutInfo {
    int trackIndex = 0;
    double yOffset = 0.0;
    double clipRowHeight = 96.0;
    bool automationVisible = false;
    bool collapsed = false;
    double automationLaneHeight = 80.0;
    te::AutomatableParameter* shownParam = nullptr;
    bool takesVisible = false;
    int takeLaneCount = 0;
    // Take lanes sit between the clip row and the automation lane.
    double takesHeight() const {
        return (takesVisible && !collapsed) ? takeLaneCount * TakeLaneItem::kLaneHeight : 0.0;
    }
    double automationLaneY() const { return yOffset + clipRowHeight + takesHeight(); }
    double totalHeight() const {
        return clipRowHeight + takesHeight()
             + ((automationVisible && !collapsed) ? automationLaneHeight : 0.0);
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
    void splitHandsRequested(te::AudioTrack* track);
    void trackSelected(te::AudioTrack* track);
    void selectedClipsDeleted();
    void editToolChanged(EditTool tool);

public slots:
    void onTracksChanged();
    void onEditChanged();
    void onTransportPositionChanged();
    void onLoopToggled(bool enabled);
    void splitSelectedClipsAtPlayhead();
    void deleteSelectedClips();
    void copySelectedClips();
    void cutSelectedClips();
    void pasteClips();
    void setSelectedTrack(te::AudioTrack* track);
    void clearTrackSelection();
    void toggleMarkerTempoLane();
    void toggleVideoLane();
    bool isMarkerTempoLaneVisible() const;
    bool isVideoLaneVisible() const;
    void selectAllClips();
    void selectClipsAfterPlayhead();
    // Deletes the time selection if there is one, else the selected clips.
    void deleteSelection(bool ripple);
    void setEditTool(EditTool tool);
    // Moves the selected clips (or the time selection) one grid step.
    void nudgeSelection(int direction, bool fine);
    bool hasTimeSelection() const { return selection_.active; }
    // Empty when nothing is selected.
    tracktion::TimeRange timeSelectionRange() const
    {
        return selection_.active ? selectionTimeRange() : tracktion::TimeRange();
    }
    void loopTimeSelection();
    VideoTrackLane* videoLane() const { return videoLane_; }

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void rebuildTrackHeaders();
    void drawGridLines();
    void updatePlayhead();
    void updateRecordingPreviews();
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

    MarkerTempoLane* markerTempoLane_ = nullptr;
    QWidget* markerTempoRow_ = nullptr;

    VideoTrackLane* videoLane_ = nullptr;
    QWidget* videoRow_ = nullptr;

    QHBoxLayout* bodyLayout_;
    QScrollArea* headerScrollArea_;
    QWidget*     headerContainer_;
    QVBoxLayout* headerVLayout_;
    QGraphicsView*  graphicsView_;
    TimelineScene*  scene_;

    double pixelsPerBeat_ = 40.0;
    double trackHeight_   = 96.0;
    GridSnapper snapper_;

    QGraphicsLineItem* playheadLine_ = nullptr;
    QGraphicsRectItem* loopOverlayItem_ = nullptr;
    QTimer playheadTimer_;

    std::vector<ClipItem*> clipItems_;
    std::vector<QGraphicsRectItem*> trackBgItems_;
    std::vector<QGraphicsLineItem*> gridLineItems_;
    std::vector<QGraphicsLineItem*> trackSeparatorItems_;
    std::vector<TrackHeaderWidget*> trackHeaders_;
    std::vector<FolderHeaderWidget*> folderHeaders_;
    QGraphicsRectItem* midiClipDrawPreviewItem_ = nullptr;
    te::AudioTrack* midiClipDrawTrack_ = nullptr;
    double midiClipDrawStartBeat_ = 0.0;
    bool isMidiClipDrawActive_ = false;
    te::AudioTrack* selectedTrack_ = nullptr;

    // Clipboard for copy/paste
    struct ClipboardEntry {
        juce::ValueTree state;
        int trackOffset = 0;
    };
    std::vector<ClipboardEntry> clipboardEntries_;
    int clipboardSourceTrackIndex_ = 0;

    // Time selection across tracks: the Range tool, or a drag on empty space.
    struct TimeSelection {
        bool active = false;
        double startBeat = 0.0;
        double endBeat = 0.0;
        std::vector<te::EditItemID> tracks;   // top to bottom
    };
    TimeSelection selection_;
    QGraphicsRectItem* selectionItem_ = nullptr;
    bool rangeFromBackground_ = false;   // a Smart-tool drag on empty space
    double selectionAnchorBeat_ = 0.0;
    int selectionAnchorTrack_ = 0;
    // A copied range: whole clips, trimmed to the range when pasted.
    struct RangeClipboard {
        bool valid = false;
        double startSecs = 0.0;
        double lengthSecs = 0.0;
        std::vector<std::pair<int, juce::ValueTree>> clips;   // (track offset, clip state)
        int firstTrack = 0;   // display index the range started on
    };
    RangeClipboard rangeClipboard_;

    void beginTimeSelection(QPointF scenePos);
    void updateTimeSelection(QPointF scenePos);
    void clearTimeSelection();
    void updateSelectionOverlay();
    juce::Array<te::AudioTrack*> selectionTracks() const;
    tracktion::TimeRange selectionTimeRange() const;
    void deleteTimeSelection(bool ripple);
    void handleToolClick(QPointF scenePos, Qt::KeyboardModifiers mods);
    ClipItem* clipItemAt(QPointF scenePos) const;
    std::vector<te::Clip*> withLinkedClips(const std::vector<te::Clip*>& clips) const;

    // Automation lane state
    std::vector<TrackLayoutInfo> layout_;
    std::vector<AutomationLaneItem*> automationLaneItems_;
    std::vector<AutomationLaneHeader*> automationLaneHeaders_;
    std::vector<QGraphicsRectItem*> laneResizeHandles_;
    std::vector<TakeLaneItem*> takeLaneItems_;
    std::vector<TakeLanesHeader*> takeLaneHeaders_;

    void rebuildLayout();
    void updateLoopOverlay();
    void setLoopInAtPlayhead();
    void setLoopOutAtPlayhead();
    void applyLoopRegionToTransport(double inBeat, double outBeat);
    void syncLoopStateFromTransport();
    void toggleAutomation(te::AudioTrack* track, bool visible);
    void toggleCollapse(te::AudioTrack* track, bool collapsed);
    void onAutomationParamChanged(int trackIndex, te::AutomatableParameter* param);
    int trackIndexAtSceneY(double sceneY) const;
    double trackYOffset(int trackIndex) const;
    void cleanupAutomationItems();
    void rebuildAutomationLanes(double sceneWidth);
    void toggleTakes(te::AudioTrack* track, bool visible);
    void cleanupTakeLaneItems();
    void rebuildTakeLanes(double sceneWidth);

    // Track transport state for automation refresh
    bool wasPlaying_ = false;
    std::map<te::EditItemID, RecordingPreviewItem*> recordingPreviews_;
    std::map<te::EditItemID, LooperPrintPreviewItem*> looperPreviews_;

    // Folder divider positions in the scene
    struct FolderDividerInfo {
        int folderId = 0;
        double yOffset = 0.0;
        static constexpr double kHeight = 28.0;
    };
    std::vector<FolderDividerInfo> folderDividers_;

    // Ordered list of all headers (folders + tracks) for drag reorder
    struct OrderedHeader {
        bool isFolder = false;
        int folderId = 0;
        te::AudioTrack* track = nullptr;
        QWidget* widget = nullptr;
    };
    std::vector<OrderedHeader> orderedHeaders_;

    juce::Array<te::AudioTrack*> getVisibleTracks() const;

    bool insideTracksChanged_ = false;

    // Live track/folder reorder state
    int reorderDragSourceIndex_ = -1;
    int reorderCurrentIndex_ = -1;
    int reorderGroupSize_ = 1;
    QLabel* reorderGhost_ = nullptr;
    int reorderLastGlobalY_ = 0;
    bool reorderDroppedOnFolder_ = false;
    int reorderDropFolderId_ = 0;

    void onTrackDragStarted(TrackHeaderWidget* header);
    void onTrackDragMoved(TrackHeaderWidget* header, int globalY);
    void onTrackDragFinished(TrackHeaderWidget* header);
    void onFolderDragStarted(FolderHeaderWidget* header);
    void onFolderDragMoved(FolderHeaderWidget* header, int globalY);
    void onFolderDragFinished(FolderHeaderWidget* header);
    void commitReorder();

    // Lane resize
    bool laneResizing_ = false;
    int laneResizeTrackIndex_ = -1;
    double laneResizeStartHeight_ = 0.0;
    double laneResizeStartY_ = 0.0;

    static constexpr int HEADER_WIDTH = metrics::kTrackHeaderWidth;
    static constexpr double kCollapsedTrackHeight = 30.0;
    static constexpr double kMinLaneHeight = 50.0;
    static constexpr double kMaxLaneHeight = 120.0;
    static constexpr double kDefaultLaneHeight = 80.0;
    static constexpr double kResizeHandleHeight = 4.0;
};

} // namespace OpenDaw
