#pragma once

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>
#include <functional>

namespace te = tracktion::engine;

namespace freedaw {

class GridSnapper;
class EditManager;

struct MidiNotePreview {
    int pitch;
    double startBeat;
    double lengthBeats;
    int velocity;
};

class ClipItem : public QGraphicsRectItem {
public:
    ClipItem(te::Clip* clip, int trackIndex, double pixelsPerBeat,
             double trackHeight, QGraphicsItem* parent = nullptr);

    te::Clip* clip() const { return clip_; }
    int trackIndex() const { return trackIndex_; }
    void setTrackIndex(int idx) { trackIndex_ = idx; }
    bool isMidiClip() const { return isMidiClip_; }

    void setDragContext(GridSnapper* snapper, EditManager* editMgr,
                        double* pixelsPerBeatPtr, double* trackHeightPtr,
                        int trackCount,
                        std::function<void()> requestRefresh);
    void setLinkedChannelCount(int count) { linkedChannelCount_ = count; }
    int linkedChannelCount() const { return linkedChannelCount_; }
    void updateGeometry(double pixelsPerBeat, double trackHeight, double scrollY);
    void loadWaveform(int numPoints);
    void loadMidiPreview();
    void loadMidiPreviewFromClips(const std::vector<te::MidiClip*>& clips);

    enum { Type = QGraphicsItem::UserType + 1 };
    int type() const override { return Type; }

protected:
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    bool isNearRightEdge(const QPointF& localPos) const;
    double computeSnappedEndBeatFromSceneX(double sceneX) const;
    void paintMidiNotes(QPainter* painter, const QRectF& r);

    te::Clip* clip_ = nullptr;
    int trackIndex_ = 0;
    bool isMidiClip_ = false;
    int linkedChannelCount_ = 1;

    std::vector<float> waveMin_;
    std::vector<float> waveMax_;

    std::vector<MidiNotePreview> midiNotes_;
    int midiLowestNote_ = 127;
    int midiHighestNote_ = 0;

    bool dragging_ = false;
    bool resizingRight_ = false;
    bool duplicateDragging_ = false;
    double resizePreviewSegmentWidthPx_ = 0.0;
    QGraphicsRectItem* duplicateGhostItem_ = nullptr;
    QGraphicsPathItem* duplicateGhostWaveItem_ = nullptr;
    QPointF dragStartScene_;
    double dragStartBeat_ = 0;
    int    dragStartTrack_ = 0;

    GridSnapper* snapper_ = nullptr;
    EditManager* editMgr_ = nullptr;
    double* pixelsPerBeatPtr_ = nullptr;
    double* trackHeightPtr_ = nullptr;
    int     trackCount_ = 1;
    std::function<void()> requestRefresh_;
};

} // namespace freedaw
