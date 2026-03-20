#pragma once

#include "ui/timeline/EnvelopeUtils.h"
#include <QWidget>
#include <QVector>
#include <functional>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace freedaw {

class CcLane : public QWidget {
    Q_OBJECT

public:
    enum class DrawTool { Freehand, Line };

    explicit CcLane(QWidget* parent = nullptr);

    void setClip(te::MidiClip* clip);
    void setCcNumber(int ccNumber);
    int ccNumber() const { return ccNumber_; }
    void setDrawTool(DrawTool tool) { drawTool_ = tool; }
    DrawTool drawTool() const { return drawTool_; }
    void setPixelsPerBeat(double ppb);
    void setScrollOffset(int x);
    void setSnapFunction(std::function<double(double)> fn);
    void refresh();

signals:
    void ccDataChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct CcPoint {
        te::MidiControllerEvent* event = nullptr;
        double beat = 0.0;
        int value = 0;
        bool selected = false;
        bool hovered = false;
    };

    void rebuildPoints();
    int hitTestPointAt(QPointF pos, double tolerance = 7.0) const;
    double beatToLocalX(double beat) const;
    double localXToBeat(double x) const;
    double valueToLocalY(int value) const;
    int localYToValue(double y) const;
    void clearSelection();
    int selectedCount() const;
    void deleteSelectedPoints();
    QVector<EnvelopePoint> buildEnvelopePoints() const;
    QString ccValueLabel(int value) const;

    te::MidiClip* clip_ = nullptr;
    int ccNumber_ = 1;
    DrawTool drawTool_ = DrawTool::Freehand;
    double pixelsPerBeat_ = 60.0;
    int scrollOffset_ = 0;
    std::function<double(double)> snapFn_;

    QVector<CcPoint> points_;

    enum class DragMode { None, DrawFreehand, DrawLine, DragPoint, DragSelected, RubberBand };
    DragMode dragMode_ = DragMode::None;

    int dragAnchorIdx_ = -1;
    QPointF dragStartPos_;
    QPointF lineEndPos_;
    double dragStartBeat_ = 0.0;
    int dragStartValue_ = 0;
    bool shiftHeld_ = false;

    QVector<QPair<double, int>> dragStartPositions_;

    QVector<QPointF> freehandSamples_;

    QRectF rubberBandRect_;

    static constexpr double kPointSize = 8.0;
    static constexpr double kPointHoverSize = 10.0;
};

} // namespace freedaw
