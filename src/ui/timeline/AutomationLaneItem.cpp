#include "AutomationLaneItem.h"
#include "AutomationPointItem.h"
#include "GridSnapper.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QCursor>
#include <cmath>

namespace freedaw {

AutomationLaneItem::AutomationLaneItem(te::AutomatableParameter* param, te::Edit* edit,
                                       double pixelsPerBeat, double laneHeight,
                                       GridSnapper* snapper, QGraphicsItem* parent)
    : QGraphicsItem(parent), param_(param), edit_(edit),
      snapper_(snapper), pixelsPerBeat_(pixelsPerBeat), laneHeight_(laneHeight)
{
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemClipsToShape, false);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    rebuildFromCurve();
}

AutomationLaneItem::~AutomationLaneItem()
{
    clearPointItems();
}

void AutomationLaneItem::setParam(te::AutomatableParameter* param)
{
    param_ = param;
    rebuildFromCurve();
}

void AutomationLaneItem::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat_ = ppb;
    rebuildFromCurve();
}

void AutomationLaneItem::setLaneHeight(double h)
{
    prepareGeometryChange();
    laneHeight_ = h;
    rebuildFromCurve();
}

void AutomationLaneItem::setSceneWidth(double w)
{
    prepareGeometryChange();
    sceneWidth_ = w;
    rebuildFromCurve();
}

void AutomationLaneItem::setPlayheadBeat(double beat)
{
    if (std::abs(playheadBeat_ - beat) > 0.001) {
        playheadBeat_ = beat;
        update();
    }
}

QRectF AutomationLaneItem::boundingRect() const
{
    return QRectF(0, 0, sceneWidth_, laneHeight_);
}

QVector<EnvelopePoint> AutomationLaneItem::getPointsFromCurve() const
{
    QVector<EnvelopePoint> pts;
    if (!param_ || !edit_) return pts;

    auto& curve = param_->getCurve();
    auto& ts = edit_->tempoSequence;
    int n = curve.getNumPoints();
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto pt = curve.getPoint(i);
        double beat = te::toBeats(pt.time, ts).inBeats();
        pts.append({beat, pt.value, pt.curve});
    }
    return pts;
}

void AutomationLaneItem::clearPointItems()
{
    for (auto* item : pointItems_) {
        item->setParentItem(nullptr);
        if (item->scene())
            item->scene()->removeItem(item);
        delete item;
    }
    pointItems_.clear();
}

void AutomationLaneItem::rebuildPointItems()
{
    clearPointItems();
    if (!param_ || !edit_) return;

    auto& curve = param_->getCurve();
    float minVal = param_->getValueRange().getStart();
    float maxVal = param_->getValueRange().getEnd();

    for (int i = 0; i < curve.getNumPoints(); ++i) {
        auto* ptItem = new AutomationPointItem(i, param_, this, this);
        ptItem->updatePosition(pixelsPerBeat_, minVal, maxVal, laneHeight_);
        pointItems_.append(ptItem);
    }
}

void AutomationLaneItem::rebuildFromCurve()
{
    cachedPoints_ = getPointsFromCurve();

    float minVal = 0.0f, maxVal = 1.0f;
    if (param_) {
        minVal = param_->getValueRange().getStart();
        maxVal = param_->getValueRange().getEnd();
    }

    bool discrete = param_ && param_->isDiscrete();
    curvePath_ = EnvelopeUtils::buildEnvelopePath(
        cachedPoints_, pixelsPerBeat_, minVal, maxVal, laneHeight_, 0.0, discrete);

    rebuildPointItems();
    update();
}

void AutomationLaneItem::updateCurvePathOnly()
{
    cachedPoints_ = getPointsFromCurve();

    float minVal = 0.0f, maxVal = 1.0f;
    if (param_) {
        minVal = param_->getValueRange().getStart();
        maxVal = param_->getValueRange().getEnd();
    }

    bool discrete = param_ && param_->isDiscrete();
    curvePath_ = EnvelopeUtils::buildEnvelopePath(
        cachedPoints_, pixelsPerBeat_, minVal, maxVal, laneHeight_, 0.0, discrete);

    update();
}

void AutomationLaneItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    auto& theme = ThemeManager::instance().current();
    QRectF r = boundingRect();

    // Background
    QColor bg = theme.surface;
    bg.setAlpha(200);
    painter->fillRect(r, bg);

    // Reference lines at 25%, 50%, 75%
    QPen refPen(theme.gridLine, 0.5, Qt::DotLine);
    painter->setPen(refPen);
    for (double frac : {0.25, 0.5, 0.75}) {
        double y = r.height() * frac;
        painter->drawLine(QPointF(0, y), QPointF(r.width(), y));
    }

    // Value labels on left edge
    if (param_) {
        QFont labelFont;
        labelFont.setPixelSize(9);
        painter->setFont(labelFont);
        painter->setPen(theme.textDim);

        auto paramName = param_->getParameterName();
        bool isVolume = (paramName == "Volume" || param_->paramID == juce::String("volume"));
        bool isPan = (paramName == "Pan" || param_->paramID == juce::String("pan"));

        QString topLabel, midLabel, bottomLabel;
        if (isVolume) {
            topLabel = "+6 dB";
            midLabel = "-12 dB";
            bottomLabel = QString::fromUtf8("-\xe2\x88\x9e");
        } else if (isPan) {
            topLabel = "R";
            midLabel = "C";
            bottomLabel = "L";
        } else {
            topLabel = "100%";
            midLabel = "50%";
            bottomLabel = "0%";
        }

        painter->drawText(QPointF(3, 10), topLabel);
        painter->drawText(QPointF(3, r.height() * 0.5 + 3), midLabel);
        painter->drawText(QPointF(3, r.height() - 3), bottomLabel);
    }

    painter->setRenderHint(QPainter::Antialiasing, true);

    // Draw existing curve if we have points
    if (param_ && !cachedPoints_.isEmpty()) {
        float minVal = param_->getValueRange().getStart();
        float maxVal = param_->getValueRange().getEnd();

        if (!curvePath_.isEmpty()) {
            QPainterPath fillPath = curvePath_;
            double lastX = EnvelopeUtils::beatToX(cachedPoints_.last().beat, pixelsPerBeat_);
            double firstX = EnvelopeUtils::beatToX(cachedPoints_.first().beat, pixelsPerBeat_);
            fillPath.lineTo(lastX, laneHeight_);
            fillPath.lineTo(firstX, laneHeight_);
            fillPath.closeSubpath();

            QColor fillColor = theme.accent;
            fillColor.setAlpha(38);
            painter->fillPath(fillPath, fillColor);

            // Drop shadow
            constexpr double kShadowOffset = 1.5;
            constexpr int kShadowAlpha = 50;
            QPainterPath shadowPath = curvePath_;
            shadowPath.translate(kShadowOffset, kShadowOffset);
            painter->setPen(QPen(QColor(0, 0, 0, kShadowAlpha), 2.8,
                                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(shadowPath);

            // Main curve line
            QPen curvePen(theme.accentLight, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(curvePen);
            painter->drawPath(curvePath_);
        }

        // Extend line to edges if points don't cover full range
        QPen extPen(theme.accent, 1.2, Qt::DashLine, Qt::RoundCap);
        painter->setPen(extPen);

        double firstY = EnvelopeUtils::valueToY(cachedPoints_.first().value, minVal, maxVal, laneHeight_);
        double firstX = EnvelopeUtils::beatToX(cachedPoints_.first().beat, pixelsPerBeat_);
        if (firstX > 0)
            painter->drawLine(QPointF(0, firstY), QPointF(firstX, firstY));

        double lastY = EnvelopeUtils::valueToY(cachedPoints_.last().value, minVal, maxVal, laneHeight_);
        double lastX = EnvelopeUtils::beatToX(cachedPoints_.last().beat, pixelsPerBeat_);
        if (lastX < r.width())
            painter->drawLine(QPointF(lastX, lastY), QPointF(r.width(), lastY));
    }

    // Freehand preview (always drawn, even on empty lane)
    if (freehandDrawing_ && freehandPath_.size() > 1) {
        QPen drawPen(theme.accentLight, 2.0);
        painter->setPen(drawPen);
        for (int i = 1; i < freehandPath_.size(); ++i)
            painter->drawLine(freehandPath_[i - 1], freehandPath_[i]);
    }

    // Playback position indicator dot
    if (param_ && playheadBeat_ >= 0.0) {
        float minVal = param_->getValueRange().getStart();
        float maxVal = param_->getValueRange().getEnd();
        auto& ts = edit_->tempoSequence;
        auto beatPos = tracktion::BeatPosition::fromBeats(playheadBeat_);
        auto timePos = ts.toTime(beatPos);
        auto posForLookup = te::EditPosition(timePos);
        float val = param_->getCurve().getValueAt(posForLookup, param_->getCurrentBaseValue());

        double dotX = EnvelopeUtils::beatToX(playheadBeat_, pixelsPerBeat_);
        double dotY = EnvelopeUtils::valueToY(val, minVal, maxVal, laneHeight_);

        if (dotX >= 0 && dotX <= r.width()) {
            constexpr double kDotRadius = 4.5;
            painter->setPen(QPen(theme.text, 1.5));
            painter->setBrush(theme.accentLight);
            painter->drawEllipse(QPointF(dotX, dotY), kDotRadius, kDotRadius);
        }
    }
}

void AutomationLaneItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (!param_ || !edit_ || event->button() != Qt::LeftButton) return;

    // Cancel any freehand draw that the first press started
    freehandDrawing_ = false;
    freehandPath_.clear();

    QPointF local = event->pos();
    float minVal = param_->getValueRange().getStart();
    float maxVal = param_->getValueRange().getEnd();

    double beat = EnvelopeUtils::xToBeat(local.x(), pixelsPerBeat_);
    if (beat < 0) beat = 0;
    if (snapper_) beat = snapper_->snapBeat(beat);

    auto& curve = param_->getCurve();
    auto* um = &edit_->getUndoManager();
    auto& ts = edit_->tempoSequence;
    auto beatPos = tracktion::BeatPosition::fromBeats(beat);
    auto timePos = ts.toTime(beatPos);

    // Use both representations: TimePosition for accurate curve lookup,
    // BeatPosition for adding the point
    auto posForLookup = te::EditPosition(timePos);
    auto posForInsert = te::EditPosition(beatPos);

    // If on the curve line, use the interpolated value to preserve shape
    float value;
    if (EnvelopeUtils::hitTestCurve(curvePath_, local, 6.0)) {
        value = curve.getValueAt(posForLookup, param_->getCurrentBaseValue());
    } else {
        value = EnvelopeUtils::yToValue(local.y(), minVal, maxVal, laneHeight_);
    }
    value = std::clamp(value, minVal, maxVal);

    if (param_->isDiscrete())
        value = param_->snapToState(value);

    float curveVal = param_->isDiscrete() ? 1.0f : 0.0f;
    curve.addPoint(posForInsert, value, curveVal, um);
    rebuildFromCurve();
    event->accept();
}

void AutomationLaneItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (!param_ || !edit_ || event->button() != Qt::LeftButton) {
        QGraphicsItem::mousePressEvent(event);
        return;
    }

    QPointF local = event->pos();

    // Check if we hit the curve line for segment bending
    if (!param_->isDiscrete() && (event->modifiers() & Qt::ControlModifier)) {
        int segIdx = -1;

        for (int i = 0; i < cachedPoints_.size() - 1; ++i) {
            double x0 = EnvelopeUtils::beatToX(cachedPoints_[i].beat, pixelsPerBeat_);
            double x1 = EnvelopeUtils::beatToX(cachedPoints_[i + 1].beat, pixelsPerBeat_);
            if (local.x() >= x0 && local.x() <= x1) {
                segIdx = i;
                break;
            }
        }

        if (segIdx >= 0) {
            curveDragging_ = true;
            curveDragSegmentIndex_ = segIdx;
            curveDragStartValue_ = cachedPoints_[segIdx].curve;
            curveDragStartPos_ = event->scenePos();
            setCursor(Qt::SizeVerCursor);
            event->accept();
            return;
        }
    }

    // Start freehand draw (on curve or empty space)
    freehandDrawing_ = true;
    freehandPath_.clear();
    freehandPath_.append(local);
    event->accept();
}

void AutomationLaneItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (curveDragging_ && param_ && edit_) {
        double deltaY = event->scenePos().y() - curveDragStartPos_.y();
        float newCurve = curveDragStartValue_ - float(deltaY / 100.0);
        newCurve = std::clamp(newCurve, -1.0f, 1.0f);

        auto& curve = param_->getCurve();
        auto* um = &edit_->getUndoManager();
        curve.setCurveValue(curveDragSegmentIndex_, newCurve, um);
        rebuildFromCurve();
        event->accept();
        return;
    }

    if (freehandDrawing_) {
        freehandPath_.append(event->pos());
        update();
        event->accept();
        return;
    }
}

void AutomationLaneItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (curveDragging_) {
        curveDragging_ = false;
        curveDragSegmentIndex_ = -1;
        unsetCursor();
        event->accept();
        return;
    }

    if (freehandDrawing_ && param_ && edit_) {
        freehandDrawing_ = false;

        if (freehandPath_.size() >= 2) {
            auto& curve = param_->getCurve();
            auto* um = &edit_->getUndoManager();
            float minVal = param_->getValueRange().getStart();
            float maxVal = param_->getValueRange().getEnd();
            bool discrete = param_->isDiscrete();

            double startBeat = EnvelopeUtils::xToBeat(freehandPath_.first().x(), pixelsPerBeat_);
            double endBeat = EnvelopeUtils::xToBeat(freehandPath_.last().x(), pixelsPerBeat_);
            if (startBeat > endBeat) std::swap(startBeat, endBeat);

            auto beatStart = tracktion::BeatPosition::fromBeats(startBeat);
            auto beatEnd = tracktion::BeatPosition::fromBeats(endBeat);
            te::EditTimeRange range(beatStart, beatEnd);

            curve.removePoints(range, um);

            double gridInterval = snapper_ ? snapper_->gridIntervalBeats() : 0.25;
            double drawInterval = gridInterval * 0.25;
            if (drawInterval < 0.0625) drawInterval = 0.0625;

            for (auto& pt : freehandPath_) {
                double beat = EnvelopeUtils::xToBeat(pt.x(), pixelsPerBeat_);
                if (beat < 0) beat = 0;

                float value = EnvelopeUtils::yToValue(pt.y(), minVal, maxVal, laneHeight_);
                value = std::clamp(value, minVal, maxVal);
                if (discrete) value = param_->snapToState(value);

                float curveVal = discrete ? 1.0f : 0.0f;

                auto pos = te::EditPosition(tracktion::BeatPosition::fromBeats(beat));
                curve.addPoint(pos, value, curveVal, um);
            }

            auto simplifyDuration = tracktion::BeatDuration::fromBeats(drawInterval);
            te::EditDuration ed(simplifyDuration);
            curve.simplify(range, ed, 0.01f, um);
        }

        freehandPath_.clear();
        rebuildFromCurve();
        event->accept();
        return;
    }

    QGraphicsItem::mouseReleaseEvent(event);
}

void AutomationLaneItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    if (!param_) {
        setCursor(Qt::CrossCursor);
        return;
    }

    QPointF local = event->pos();

    if (!param_->isDiscrete() && EnvelopeUtils::hitTestCurve(curvePath_, local, 5.0)) {
        if (event->modifiers() & Qt::ControlModifier)
            setCursor(Qt::SizeVerCursor);
        else
            setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

} // namespace freedaw
