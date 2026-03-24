#pragma once

#include "engine/EditManager.h"
#include "GridSnapper.h"
#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace OpenDaw {

class MarkerTempoLane : public QWidget {
    Q_OBJECT

public:
    explicit MarkerTempoLane(EditManager* editMgr, GridSnapper* snapper,
                              QWidget* parent = nullptr);

    void setPixelsPerBeat(double ppb);
    void setScrollX(int scrollX);
    void refresh();
    void setPlayheadBeat(double beat);

signals:
    void markerAdded(const QString& name, double beat);
    void tempoChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    double xToBeat(int x) const;
    int beatToX(double beat) const;

    EditManager* editMgr_;
    GridSnapper* snapper_;
    double pixelsPerBeat_ = 40.0;
    int scrollX_ = 0;

    enum class DragMode { None, DragMarker, DragTempo, ResizeHeight };
    DragMode dragMode_ = DragMode::None;
    te::MarkerClip* draggedMarker_ = nullptr;
    int draggedTempoIndex_ = -1;
    double dragStartBeat_ = 0.0;

    static constexpr int kDefaultHeight = 120;
    static constexpr int kMinHeight = 60;
    static constexpr int kMaxHeight = 300;
    static constexpr int kMarkerZoneHeight = 28;
    static constexpr int kResizeGrabHeight = 4;
    static constexpr int kNodeRadius = 5;
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 300.0;

    double playheadBeat_ = 0.0;
    int laneHeight_ = kDefaultHeight;
    int resizeStartY_ = 0;
    int resizeStartHeight_ = 0;

    int tempoZoneHeight() const { return laneHeight_ - kMarkerZoneHeight; }
};

} // namespace OpenDaw
