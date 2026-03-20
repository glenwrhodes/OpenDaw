#include "CcLane.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace freedaw {

CcLane::CcLane(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("CC Lane");
    setFixedHeight(80);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

void CcLane::setClip(te::MidiClip* clip)
{
    clip_ = clip;
    rebuildPoints();
    update();
}

void CcLane::setCcNumber(int ccNumber)
{
    ccNumber_ = ccNumber;
    rebuildPoints();
    update();
}

void CcLane::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat_ = ppb;
    update();
}

void CcLane::setScrollOffset(int x)
{
    scrollOffset_ = x;
    update();
}

void CcLane::setSnapFunction(std::function<double(double)> fn)
{
    snapFn_ = std::move(fn);
}

void CcLane::refresh()
{
    rebuildPoints();
    update();
}

// ── Coordinate helpers ──

double CcLane::beatToLocalX(double beat) const
{
    return EnvelopeUtils::beatToX(beat, pixelsPerBeat_) - scrollOffset_;
}

double CcLane::localXToBeat(double x) const
{
    return EnvelopeUtils::xToBeat(x + scrollOffset_, pixelsPerBeat_);
}

double CcLane::valueToLocalY(int value) const
{
    return EnvelopeUtils::valueToY(
        static_cast<float>(value), 0.0f, 127.0f, static_cast<double>(height()));
}

int CcLane::localYToValue(double y) const
{
    float v = EnvelopeUtils::yToValue(y, 0.0f, 127.0f, static_cast<double>(height()));
    return std::clamp(static_cast<int>(std::round(v)), 0, 127);
}

// ── Point cache ──

void CcLane::rebuildPoints()
{
    points_.clear();
    if (!clip_) return;

    auto& seq = clip_->getSequence();
    for (auto* evt : seq.getControllerEvents()) {
        if (evt->getType() != ccNumber_) continue;
        CcPoint pt;
        pt.event = evt;
        pt.beat = evt->getBeatPosition().inBeats();
        pt.value = evt->getControllerValue();
        points_.append(pt);
    }

    std::sort(points_.begin(), points_.end(),
              [](const CcPoint& a, const CcPoint& b) { return a.beat < b.beat; });
}

int CcLane::hitTestPointAt(QPointF pos, double tolerance) const
{
    for (int i = 0; i < points_.size(); ++i) {
        double px = beatToLocalX(points_[i].beat);
        double py = valueToLocalY(points_[i].value);
        double dx = pos.x() - px;
        double dy = pos.y() - py;
        if (dx * dx + dy * dy <= tolerance * tolerance)
            return i;
    }
    return -1;
}

void CcLane::clearSelection()
{
    for (auto& pt : points_)
        pt.selected = false;
}

int CcLane::selectedCount() const
{
    int n = 0;
    for (const auto& pt : points_)
        if (pt.selected) ++n;
    return n;
}

QVector<EnvelopePoint> CcLane::buildEnvelopePoints() const
{
    QVector<EnvelopePoint> eps;
    eps.reserve(points_.size());
    for (const auto& pt : points_) {
        EnvelopePoint ep;
        ep.beat = pt.beat;
        ep.value = static_cast<float>(pt.value);
        ep.curve = 1.0f; // step mode
        eps.append(ep);
    }
    return eps;
}

QString CcLane::ccValueLabel(int value) const
{
    return QStringLiteral("CC%1: %2").arg(ccNumber_).arg(value);
}

void CcLane::deleteSelectedPoints()
{
    if (!clip_) return;
    auto* um = &clip_->edit.getUndoManager();
    auto& seq = clip_->getSequence();

    QVector<te::MidiControllerEvent*> toRemove;
    for (const auto& pt : points_)
        if (pt.selected && pt.event)
            toRemove.append(pt.event);

    for (auto* evt : toRemove)
        seq.removeControllerEvent(*evt, um);

    rebuildPoints();
    update();
    emit ccDataChanged();
}

// ── Paint ──

void CcLane::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int h = height();
    const int w = width();

    // 1. Background
    p.fillRect(rect(), theme.pianoRollBackground);

    // 2. Reference lines at 25%, 50%, 75%
    p.setPen(QPen(theme.pianoRollGrid, 0.5, Qt::DotLine));
    for (int refVal : {32, 64, 96}) {
        double y = valueToLocalY(refVal);
        p.drawLine(QPointF(0, y), QPointF(w, y));
    }

    // 3. Value labels at left edge
    QFont labelFont = font();
    labelFont.setPixelSize(8);
    p.setFont(labelFont);
    p.setPen(theme.textDim);
    p.drawText(QRectF(2, 1, 30, 12), Qt::AlignLeft | Qt::AlignTop, "127");
    p.drawText(QRectF(2, h / 2.0 - 6, 30, 12), Qt::AlignLeft | Qt::AlignVCenter, "64");
    p.drawText(QRectF(2, h - 13, 30, 12), Qt::AlignLeft | Qt::AlignBottom, "0");

    if (clip_ && !points_.isEmpty()) {
        // 4. Step-curve fill + stroke
        auto eps = buildEnvelopePoints();
        auto curvePath = EnvelopeUtils::buildEnvelopePath(
            eps, pixelsPerBeat_, 0.0f, 127.0f,
            static_cast<double>(h), static_cast<double>(scrollOffset_), true);

        if (!curvePath.isEmpty()) {
            QPainterPath fillPath = curvePath;
            QPointF lastPt = curvePath.currentPosition();
            QPointF firstPt = curvePath.elementAt(0);
            fillPath.lineTo(lastPt.x(), h);
            fillPath.lineTo(firstPt.x(), h);
            fillPath.closeSubpath();

            QColor fillColor = theme.accent;
            fillColor.setAlpha(30);
            p.fillPath(fillPath, fillColor);

            p.setPen(QPen(theme.accent, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawPath(curvePath);
        }

        // 5. Diamond point handles
        for (int i = 0; i < points_.size(); ++i) {
            const auto& pt = points_[i];
            double px = beatToLocalX(pt.beat);
            double py = valueToLocalY(pt.value);

            if (px < -20 || px > w + 20) continue;

            double sz = pt.hovered ? kPointHoverSize : kPointSize;
            double half = sz / 2.0;

            QPolygonF diamond;
            diamond << QPointF(px, py - half)
                    << QPointF(px + half, py)
                    << QPointF(px, py + half)
                    << QPointF(px - half, py);

            QColor fill = pt.selected ? theme.pianoRollNoteSelected
                        : (pt.hovered ? theme.accentLight : theme.accent);
            QColor border = pt.hovered ? theme.text : theme.accentLight;

            p.setPen(QPen(border, 1.0));
            p.setBrush(fill);
            p.drawPolygon(diamond);
        }
    }

    // 6. Rubber-band selection rectangle
    if (dragMode_ == DragMode::RubberBand && !rubberBandRect_.isNull()) {
        QColor rbFill = theme.accent;
        rbFill.setAlpha(40);
        p.fillRect(rubberBandRect_, rbFill);
        p.setPen(QPen(theme.accent, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rubberBandRect_);
    }

    // 7. Freehand draw preview
    if (dragMode_ == DragMode::DrawFreehand && freehandSamples_.size() > 1) {
        QPen drawPen(theme.accentLight, 1.5, Qt::DashLine);
        p.setPen(drawPen);
        for (int i = 1; i < freehandSamples_.size(); ++i) {
            p.drawLine(freehandSamples_[i - 1], freehandSamples_[i]);
        }
    }

    // 7b. Straight-line draw preview
    if (dragMode_ == DragMode::DrawLine) {
        p.setPen(QPen(theme.accentLight, 1.5, Qt::SolidLine));
        p.drawLine(dragStartPos_, lineEndPos_);
    }

    // 8. Top separator
    p.setPen(QPen(theme.pianoRollGrid, 0.5));
    p.drawLine(0, 0, w, 0);
}

// ── Mouse interaction ──

void CcLane::mousePressEvent(QMouseEvent* event)
{
    if (!clip_ || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    setFocus();
    QPointF pos = event->position();
    shiftHeld_ = (event->modifiers() & Qt::ShiftModifier);
    bool ctrlHeld = (event->modifiers() & Qt::ControlModifier);

    int hitIdx = hitTestPointAt(pos);

    if (hitIdx >= 0) {
        // Clicked on a point
        if (shiftHeld_) {
            points_[hitIdx].selected = !points_[hitIdx].selected;
            update();
            return;
        }

        if (!points_[hitIdx].selected) {
            if (!ctrlHeld)
                clearSelection();
            points_[hitIdx].selected = true;
        }

        // Start drag
        if (selectedCount() > 1) {
            dragMode_ = DragMode::DragSelected;
        } else {
            dragMode_ = DragMode::DragPoint;
        }

        dragAnchorIdx_ = hitIdx;
        dragStartPos_ = pos;
        dragStartBeat_ = points_[hitIdx].beat;
        dragStartValue_ = points_[hitIdx].value;

        dragStartPositions_.clear();
        for (const auto& pt : points_) {
            if (pt.selected)
                dragStartPositions_.append({pt.beat, pt.value});
        }

        update();
        return;
    }

    // Clicked on empty space
    if (ctrlHeld) {
        dragMode_ = DragMode::RubberBand;
        dragStartPos_ = pos;
        rubberBandRect_ = QRectF();
        update();
        return;
    }

    if (!shiftHeld_)
        clearSelection();

    bool altHeld = (event->modifiers() & Qt::AltModifier);
    bool useLine = (drawTool_ == DrawTool::Line) || altHeld;
    if (useLine) {
        dragMode_ = DragMode::DrawLine;
        dragStartPos_ = pos;
        lineEndPos_ = pos;
    } else {
        dragMode_ = DragMode::DrawFreehand;
        dragStartPos_ = pos;
        freehandSamples_.clear();
        freehandSamples_.append(pos);
    }
    update();
}

void CcLane::mouseMoveEvent(QMouseEvent* event)
{
    QPointF pos = event->position();

    if (dragMode_ == DragMode::None) {
        // Hover highlight
        int hitIdx = hitTestPointAt(pos);
        bool changed = false;
        for (int i = 0; i < points_.size(); ++i) {
            bool shouldHover = (i == hitIdx);
            if (points_[i].hovered != shouldHover) {
                points_[i].hovered = shouldHover;
                changed = true;
            }
        }
        if (hitIdx >= 0)
            setCursor(Qt::SizeAllCursor);
        else
            setCursor(Qt::CrossCursor);
        if (changed)
            update();
        return;
    }

    if (!(event->buttons() & Qt::LeftButton)) return;

    if (dragMode_ == DragMode::DrawFreehand) {
        freehandSamples_.append(pos);
        update();
        return;
    }

    if (dragMode_ == DragMode::DrawLine) {
        lineEndPos_ = pos;
        update();
        return;
    }

    if (dragMode_ == DragMode::RubberBand) {
        rubberBandRect_ = QRectF(dragStartPos_, pos).normalized();

        for (auto& pt : points_) {
            double px = beatToLocalX(pt.beat);
            double py = valueToLocalY(pt.value);
            pt.selected = rubberBandRect_.contains(px, py);
        }
        update();
        return;
    }

    if (dragMode_ == DragMode::DragPoint || dragMode_ == DragMode::DragSelected) {
        if (dragAnchorIdx_ < 0 || dragAnchorIdx_ >= points_.size()) return;

        QPointF delta = pos - dragStartPos_;
        bool shiftNow = (event->modifiers() & Qt::ShiftModifier);

        double beatDelta = 0.0;
        int valueDelta = 0;

        if (!shiftNow || std::abs(delta.x()) > std::abs(delta.y())) {
            double newBeat = localXToBeat(pos.x());
            if (snapFn_) newBeat = snapFn_(newBeat);
            beatDelta = newBeat - dragStartBeat_;
        }

        if (!shiftNow || std::abs(delta.y()) >= std::abs(delta.x())) {
            int newVal = localYToValue(pos.y());
            valueDelta = newVal - dragStartValue_;
        }

        if (shiftNow) {
            if (std::abs(delta.x()) > std::abs(delta.y()))
                valueDelta = 0;
            else
                beatDelta = 0.0;
        }

        auto* um = &clip_->edit.getUndoManager();

        if (dragMode_ == DragMode::DragPoint) {
            auto& pt = points_[dragAnchorIdx_];
            double newBeat = std::max(0.0, dragStartBeat_ + beatDelta);
            int newVal = std::clamp(dragStartValue_ + valueDelta, 0, 127);

            pt.event->setBeatPosition(
                tracktion::BeatPosition::fromBeats(newBeat), um);
            pt.event->setControllerValue(newVal, um);
            pt.beat = newBeat;
            pt.value = newVal;
        } else {
            int selIdx = 0;
            for (auto& pt : points_) {
                if (!pt.selected) continue;
                if (selIdx >= dragStartPositions_.size()) break;

                double origBeat = dragStartPositions_[selIdx].first;
                int origVal = dragStartPositions_[selIdx].second;

                double newBeat = std::max(0.0, origBeat + beatDelta);
                int newVal = std::clamp(origVal + valueDelta, 0, 127);

                pt.event->setBeatPosition(
                    tracktion::BeatPosition::fromBeats(newBeat), um);
                pt.event->setControllerValue(newVal, um);
                pt.beat = newBeat;
                pt.value = newVal;
                ++selIdx;
            }
        }

        // Tooltip
        int anchorVal = points_[dragAnchorIdx_].value;
        QPoint screenPos = mapToGlobal(pos.toPoint());
        QToolTip::showText(screenPos, ccValueLabel(anchorVal));

        update();
        return;
    }
}

void CcLane::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (dragMode_ == DragMode::DrawFreehand && clip_ && freehandSamples_.size() >= 2) {
        auto* um = &clip_->edit.getUndoManager();
        auto& seq = clip_->getSequence();

        double minBeat = localXToBeat(freehandSamples_.first().x());
        double maxBeat = localXToBeat(freehandSamples_.last().x());
        if (minBeat > maxBeat) std::swap(minBeat, maxBeat);

        seq.removeControllersBetween(
            ccNumber_,
            tracktion::BeatPosition::fromBeats(minBeat),
            tracktion::BeatPosition::fromBeats(maxBeat),
            um);

        // Sample at ~1/32 beat intervals, minimum 2px apart
        double intervalBeats = 0.125;
        double intervalPx = intervalBeats * pixelsPerBeat_;
        if (intervalPx < 2.0 && pixelsPerBeat_ > 0)
            intervalBeats = 2.0 / pixelsPerBeat_;

        double lastBeat = -999.0;
        for (const auto& sample : freehandSamples_) {
            double beat = localXToBeat(sample.x());
            if (beat < 0) beat = 0;
            if (std::abs(beat - lastBeat) < intervalBeats * 0.5) continue;

            int val = localYToValue(sample.y());
            seq.addControllerEvent(
                tracktion::BeatPosition::fromBeats(beat),
                ccNumber_, val, um);
            lastBeat = beat;
        }

        rebuildPoints();
        update();
        emit ccDataChanged();
    }

    if (dragMode_ == DragMode::DrawLine && clip_) {
        auto* um = &clip_->edit.getUndoManager();
        auto& seq = clip_->getSequence();

        double startBeat = localXToBeat(dragStartPos_.x());
        double endBeat = localXToBeat(lineEndPos_.x());
        int startVal = localYToValue(dragStartPos_.y());
        int endVal = localYToValue(lineEndPos_.y());

        if (startBeat > endBeat) {
            std::swap(startBeat, endBeat);
            std::swap(startVal, endVal);
        }
        if (startBeat < 0) startBeat = 0;

        seq.removeControllersBetween(
            ccNumber_,
            tracktion::BeatPosition::fromBeats(startBeat),
            tracktion::BeatPosition::fromBeats(endBeat),
            um);

        double intervalBeats = 0.125;
        double intervalPx = intervalBeats * pixelsPerBeat_;
        if (intervalPx < 2.0 && pixelsPerBeat_ > 0)
            intervalBeats = 2.0 / pixelsPerBeat_;

        double range = endBeat - startBeat;
        if (range < 0.001) {
            seq.addControllerEvent(
                tracktion::BeatPosition::fromBeats(startBeat),
                ccNumber_, startVal, um);
        } else {
            for (double b = startBeat; b <= endBeat; b += intervalBeats) {
                double t = (b - startBeat) / range;
                int val = static_cast<int>(std::round(startVal + t * (endVal - startVal)));
                val = std::clamp(val, 0, 127);
                seq.addControllerEvent(
                    tracktion::BeatPosition::fromBeats(b),
                    ccNumber_, val, um);
            }
            double lastInserted = startBeat + std::floor((endBeat - startBeat) / intervalBeats) * intervalBeats;
            if (endBeat - lastInserted > intervalBeats * 0.1) {
                seq.addControllerEvent(
                    tracktion::BeatPosition::fromBeats(endBeat),
                    ccNumber_, endVal, um);
            }
        }

        rebuildPoints();
        update();
        emit ccDataChanged();
    }

    if (dragMode_ == DragMode::DragPoint || dragMode_ == DragMode::DragSelected) {
        QToolTip::hideText();
        rebuildPoints();
        update();
        emit ccDataChanged();
    }

    if (dragMode_ == DragMode::RubberBand) {
        rubberBandRect_ = QRectF();
        update();
    }

    dragMode_ = DragMode::None;
    dragAnchorIdx_ = -1;
    freehandSamples_.clear();
    dragStartPositions_.clear();
}

void CcLane::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!clip_ || event->button() != Qt::LeftButton) return;

    QPointF pos = event->position();
    int hitIdx = hitTestPointAt(pos);

    if (hitIdx >= 0) return; // double-click on existing point -- no-op

    double beat = localXToBeat(pos.x());
    if (beat < 0) beat = 0;
    if (snapFn_) beat = snapFn_(beat);

    int value = localYToValue(pos.y());

    auto* um = &clip_->edit.getUndoManager();
    clip_->getSequence().addControllerEvent(
        tracktion::BeatPosition::fromBeats(beat),
        ccNumber_, value, um);

    rebuildPoints();
    update();
    emit ccDataChanged();
}

void CcLane::contextMenuEvent(QContextMenuEvent* event)
{
    if (!clip_) return;

    QPointF pos = event->pos();
    int hitIdx = hitTestPointAt(pos);

    QMenu menu;
    menu.setAccessibleName("CC Lane Context Menu");

    if (hitIdx >= 0) {
        int selCount = selectedCount();

        if (selCount > 1 && points_[hitIdx].selected) {
            menu.addAction(
                QStringLiteral("Delete %1 Selected Points").arg(selCount),
                [this]() { deleteSelectedPoints(); });
        } else {
            menu.addAction("Delete Point", [this, hitIdx]() {
                if (hitIdx < points_.size() && points_[hitIdx].event) {
                    auto* um = &clip_->edit.getUndoManager();
                    clip_->getSequence().removeControllerEvent(
                        *points_[hitIdx].event, um);
                    rebuildPoints();
                    update();
                    emit ccDataChanged();
                }
            });
        }
    } else {
        int selCount = selectedCount();
        if (selCount > 0) {
            menu.addAction(
                QStringLiteral("Delete %1 Selected Points").arg(selCount),
                [this]() { deleteSelectedPoints(); });
        }
        menu.addAction("Add Point Here", [this, pos]() {
            double beat = localXToBeat(pos.x());
            if (beat < 0) beat = 0;
            if (snapFn_) beat = snapFn_(beat);
            int value = localYToValue(pos.y());

            auto* um = &clip_->edit.getUndoManager();
            clip_->getSequence().addControllerEvent(
                tracktion::BeatPosition::fromBeats(beat),
                ccNumber_, value, um);
            rebuildPoints();
            update();
            emit ccDataChanged();
        });
    }

    menu.exec(event->globalPos());
}

void CcLane::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (selectedCount() > 0) {
            deleteSelectedPoints();
            return;
        }
    }

    if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) {
        for (auto& pt : points_)
            pt.selected = true;
        update();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        clearSelection();
        update();
        return;
    }

    QWidget::keyPressEvent(event);
}

} // namespace freedaw
