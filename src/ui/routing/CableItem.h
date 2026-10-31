#pragma once

#include "RoutingNode.h"
#include <QGraphicsPathItem>
#include <QColor>

namespace OpenDaw {

// Main: a track's output. Send: an aux send (dashed, with its level on a label).
// Sidechain: a compressor's key input (dotted). Device: master to the sound card.
enum class CableKind { Main, Send, Sidechain, Device };

class CableItem : public QGraphicsPathItem {
public:
    CableItem(JackItem* sourceJack, JackItem* destJack,
              const QColor& color, CableKind kind = CableKind::Main,
              QGraphicsItem* parent = nullptr);

    JackItem* sourceJack() const { return sourceJack_; }
    JackItem* destJack() const { return destJack_; }
    CableKind kind() const { return kind_; }

    void updatePath();
    void setColor(const QColor& c);
    void setDangling(bool d);
    void setDanglingEnd(const QPointF& scenePos);

    // Text drawn on a pill at the cable's midpoint (a send's level).
    void setLabel(const QString& text);
    bool labelContains(const QPointF& scenePos) const;

    // Signal level 0..1; above silence a bright pulse flows along the cable.
    void setSignal(float level);
    float signal() const { return signal_; }
    void advanceFlow(qreal dt);

    void setMarked(bool marked);
    bool isMarked() const { return marked_; }
    void setMuted(bool muted);

    bool stepPhysics(qreal dt);
    void resetPhysics();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

    static QPainterPath buildCablePath(QPointF p0, QPointF p3);
    static QPainterPath buildCablePath(QPointF p0, QPointF c1, QPointF c2, QPointF p3);
    static void computeTargetControlPoints(QPointF p0, QPointF p3,
                                           QPointF& c1Out, QPointF& c2Out);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    QRectF labelRect() const;
    qreal thickness() const;

    JackItem* sourceJack_;
    JackItem* destJack_;
    QColor color_;
    CableKind kind_;
    bool hovered_ = false;
    bool dangling_ = false;
    bool marked_ = false;
    bool muted_ = false;
    QPointF danglingEnd_;
    QString label_;
    float signal_ = 0.0f;
    qreal flowPhase_ = 0.0;

    QPointF c1Current_;
    QPointF c2Current_;
    QPointF c1Velocity_;
    QPointF c2Velocity_;
    bool physicsInitialized_ = false;
};

} // namespace OpenDaw
