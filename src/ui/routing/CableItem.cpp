#include "CableItem.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QGraphicsSceneHoverEvent>
#include <QFontMetrics>
#include <cmath>

namespace OpenDaw {

namespace {
constexpr qreal CABLE_THICKNESS = 3.5;
constexpr qreal SEND_THICKNESS = 2.6;
constexpr qreal SIDECHAIN_THICKNESS = 2.2;
constexpr qreal HOVER_EXTRA = 2.0;
constexpr qreal HIT_TOLERANCE = 12.0;
constexpr qreal DROOP_BASE = 25.0;
constexpr qreal DROOP_FACTOR = 0.25;
constexpr qreal SHADOW_OFFSET = 1.5;
constexpr int   SHADOW_ALPHA = 45;
constexpr float SIGNAL_FLOOR = 0.03f;

constexpr qreal SPRING_STIFFNESS = 35.0;
constexpr qreal SPRING_DAMPING   = 8.0;
constexpr qreal SETTLE_THRESHOLD = 0.5;

QFont labelFont()
{
    QFont f;
    f.setPixelSize(9);
    f.setBold(true);
    return f;
}
}

CableItem::CableItem(JackItem* sourceJack, JackItem* destJack,
                     const QColor& color, CableKind kind, QGraphicsItem* parent)
    : QGraphicsPathItem(parent)
    , sourceJack_(sourceJack), destJack_(destJack), color_(color), kind_(kind)
{
    setAcceptHoverEvents(true);
    setZValue(kind == CableKind::Send ? 101 : 100);
    updatePath();
}

qreal CableItem::thickness() const
{
    qreal t = kind_ == CableKind::Send ? SEND_THICKNESS
            : kind_ == CableKind::Sidechain ? SIDECHAIN_THICKNESS
            : CABLE_THICKNESS;
    return hovered_ ? t + HOVER_EXTRA : t;
}

void CableItem::computeTargetControlPoints(QPointF p0, QPointF p3,
                                           QPointF& c1Out, QPointF& c2Out)
{
    const qreal dx = p3.x() - p0.x();
    const qreal dy = p3.y() - p0.y();
    const qreal dist = std::sqrt(dx * dx + dy * dy);
    const qreal droop = DROOP_BASE + DROOP_FACTOR * dist;
    const qreal hExtent = std::abs(dx) * 0.25;

    c1Out = QPointF(p0.x() + hExtent, p0.y() + droop);
    c2Out = QPointF(p3.x() - hExtent, p3.y() + droop);
}

QPainterPath CableItem::buildCablePath(QPointF p0, QPointF p3)
{
    QPointF c1, c2;
    computeTargetControlPoints(p0, p3, c1, c2);
    return buildCablePath(p0, c1, c2, p3);
}

QPainterPath CableItem::buildCablePath(QPointF p0, QPointF c1, QPointF c2, QPointF p3)
{
    QPainterPath path;
    path.moveTo(p0);
    path.cubicTo(c1, c2, p3);
    return path;
}

void CableItem::updatePath()
{
    QPointF p0 = sourceJack_ ? sourceJack_->sceneCenterPos() : QPointF(0, 0);
    QPointF p3 = dangling_ ? danglingEnd_
                            : (destJack_ ? destJack_->sceneCenterPos() : QPointF(0, 0));

    QPointF t1, t2;
    computeTargetControlPoints(p0, p3, t1, t2);

    if (!physicsInitialized_) {
        c1Current_ = t1;
        c2Current_ = t2;
        c1Velocity_ = QPointF(0, 0);
        c2Velocity_ = QPointF(0, 0);
        physicsInitialized_ = true;
    }

    prepareGeometryChange();
    setPath(buildCablePath(p0, c1Current_, c2Current_, p3));
}

bool CableItem::stepPhysics(qreal dt)
{
    QPointF p0 = sourceJack_ ? sourceJack_->sceneCenterPos() : QPointF(0, 0);
    QPointF p3 = dangling_ ? danglingEnd_
                            : (destJack_ ? destJack_->sceneCenterPos() : QPointF(0, 0));

    QPointF t1, t2;
    computeTargetControlPoints(p0, p3, t1, t2);

    if (!physicsInitialized_) {
        c1Current_ = t1;
        c2Current_ = t2;
        c1Velocity_ = QPointF(0, 0);
        c2Velocity_ = QPointF(0, 0);
        physicsInitialized_ = true;
        prepareGeometryChange();
        setPath(buildCablePath(p0, c1Current_, c2Current_, p3));
        return false;
    }

    auto springStep = [&](QPointF& pos, QPointF& vel, const QPointF& target) {
        QPointF force = SPRING_STIFFNESS * (target - pos) - SPRING_DAMPING * vel;
        vel += force * dt;
        pos += vel * dt;
    };

    springStep(c1Current_, c1Velocity_, t1);
    springStep(c2Current_, c2Velocity_, t2);

    prepareGeometryChange();
    setPath(buildCablePath(p0, c1Current_, c2Current_, p3));

    qreal err = QPointF(c1Current_ - t1).manhattanLength()
              + QPointF(c2Current_ - t2).manhattanLength();
    qreal spd = c1Velocity_.manhattanLength() + c2Velocity_.manhattanLength();
    return (err > SETTLE_THRESHOLD || spd > SETTLE_THRESHOLD);
}

void CableItem::resetPhysics()
{
    physicsInitialized_ = false;
}

void CableItem::setColor(const QColor& c)
{
    color_ = c;
    update();
}

void CableItem::setDangling(bool d)
{
    dangling_ = d;
}

void CableItem::setDanglingEnd(const QPointF& scenePos)
{
    danglingEnd_ = scenePos;
    updatePath();
}

void CableItem::setLabel(const QString& text)
{
    if (label_ == text) return;
    prepareGeometryChange();
    label_ = text;
    update();
}

QRectF CableItem::labelRect() const
{
    if (label_.isEmpty() || path().isEmpty()) return {};
    QFontMetrics fm(labelFont());
    const qreal w = fm.horizontalAdvance(label_) + 12;
    const qreal h = 15;
    const QPointF c = path().pointAtPercent(0.5);
    return QRectF(c.x() - w / 2, c.y() - h / 2, w, h);
}

bool CableItem::labelContains(const QPointF& scenePos) const
{
    return labelRect().adjusted(-3, -3, 3, 3).contains(mapFromScene(scenePos));
}

void CableItem::setSignal(float level)
{
    level = std::clamp(level, 0.0f, 1.0f);
    const bool wasFlowing = signal_ > SIGNAL_FLOOR;
    signal_ = level;
    if (wasFlowing && signal_ <= SIGNAL_FLOOR)
        update();
}

void CableItem::advanceFlow(qreal dt)
{
    if (signal_ <= SIGNAL_FLOOR || muted_) return;
    flowPhase_ += dt * (6.0 + 22.0 * signal_);
    if (flowPhase_ > 1000.0) flowPhase_ -= 1000.0;
    update();
}

void CableItem::setMarked(bool marked)
{
    if (marked_ == marked) return;
    marked_ = marked;
    update();
}

void CableItem::setMuted(bool muted)
{
    if (muted_ == muted) return;
    muted_ = muted;
    update();
}

QRectF CableItem::boundingRect() const
{
    const qreal pad = thickness() + 8;
    QRectF r = QGraphicsPathItem::boundingRect().adjusted(-pad, -pad, pad, pad);
    if (!label_.isEmpty())
        r = r.united(labelRect().adjusted(-2, -2, 2, 2));
    return r;
}

QPainterPath CableItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(HIT_TOLERANCE);
    stroker.setCapStyle(Qt::RoundCap);
    QPainterPath s = stroker.createStroke(path());
    if (!label_.isEmpty())
        s.addRect(labelRect());
    return s;
}

void CableItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);
    auto& theme = ThemeManager::instance().current();
    const qreal t = thickness();

    if (marked_) {
        QColor glow = theme.accent;
        glow.setAlpha(150);
        painter->setPen(QPen(glow, t + 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path());
    }

    QPainterPath shadowPath = path();
    shadowPath.translate(SHADOW_OFFSET, SHADOW_OFFSET);
    QColor shadowColor(0, 0, 0, SHADOW_ALPHA);
    painter->setPen(QPen(shadowColor, t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(shadowPath);

    QColor drawColor = color_;
    if (dangling_ || muted_)
        drawColor.setAlpha(muted_ ? 90 : 170);
    if (hovered_)
        drawColor = drawColor.lighter(125);

    QPen cablePen(drawColor, t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (kind_ == CableKind::Send)
        cablePen.setDashPattern({ 3.0, 2.0 });
    else if (kind_ == CableKind::Sidechain)
        cablePen.setDashPattern({ 1.0, 2.0 });
    painter->setPen(cablePen);
    painter->drawPath(path());

    // Signal flowing from source to destination.
    if (signal_ > SIGNAL_FLOOR && !muted_ && !dangling_) {
        QColor pulse = color_.lighter(175);
        pulse.setAlpha(static_cast<int>(70 + 185 * signal_));
        QPen flowPen(pulse, std::max(1.5, t * 0.6), Qt::CustomDashLine, Qt::RoundCap);
        flowPen.setDashPattern({ 1.0, 5.0 });
        flowPen.setDashOffset(-flowPhase_);
        painter->setPen(flowPen);
        painter->drawPath(path());
    }

    if (!label_.isEmpty()) {
        const QRectF r = labelRect();
        painter->setPen(QPen(color_, 1.2));
        painter->setBrush(theme.surface);
        painter->drawRoundedRect(r, 7, 7);
        painter->setFont(labelFont());
        painter->setPen(theme.text);
        painter->drawText(r, Qt::AlignCenter, label_);
    }
}

void CableItem::hoverEnterEvent(QGraphicsSceneHoverEvent*)
{
    prepareGeometryChange();
    hovered_ = true;
    update();
}

void CableItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    prepareGeometryChange();
    hovered_ = false;
    update();
}

} // namespace OpenDaw
