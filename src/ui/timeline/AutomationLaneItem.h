#pragma once

#include "EnvelopeUtils.h"
#include <QGraphicsItem>
#include <QVector>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace freedaw {

class GridSnapper;
class AutomationPointItem;

class AutomationLaneItem : public QGraphicsItem {
public:
    AutomationLaneItem(te::AutomatableParameter* param, te::Edit* edit,
                       double pixelsPerBeat, double laneHeight,
                       GridSnapper* snapper, QGraphicsItem* parent = nullptr);
    ~AutomationLaneItem() override;

    te::AutomatableParameter* param() const { return param_; }
    te::Edit* edit() const { return edit_; }
    GridSnapper* snapper() const { return snapper_; }
    double pixelsPerBeat() const { return pixelsPerBeat_; }
    double laneHeight() const { return laneHeight_; }

    void setParam(te::AutomatableParameter* param);
    void setPixelsPerBeat(double ppb);
    void setLaneHeight(double h);
    void setSceneWidth(double w);
    void setPlayheadBeat(double beat);
    void rebuildFromCurve();
    void updateCurvePathOnly();

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    enum { Type = QGraphicsItem::UserType + 11 };
    int type() const override { return Type; }

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    void clearPointItems();
    void rebuildPointItems();
    QVector<EnvelopePoint> getPointsFromCurve() const;

    te::AutomatableParameter* param_ = nullptr;
    te::Edit* edit_ = nullptr;
    GridSnapper* snapper_ = nullptr;

    double pixelsPerBeat_ = 40.0;
    double laneHeight_ = 80.0;
    double sceneWidth_ = 2000.0;

    QVector<AutomationPointItem*> pointItems_;
    QPainterPath curvePath_;
    QVector<EnvelopePoint> cachedPoints_;
    double playheadBeat_ = -1.0;

    // Freehand draw state
    bool freehandDrawing_ = false;
    QVector<QPointF> freehandPath_;

    // Curve segment drag state
    bool curveDragging_ = false;
    int curveDragSegmentIndex_ = -1;
    float curveDragStartValue_ = 0.0f;
    QPointF curveDragStartPos_;
};

} // namespace freedaw
