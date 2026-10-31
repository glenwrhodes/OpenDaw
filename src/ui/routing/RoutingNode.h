#pragma once

#include "engine/EditManager.h"
#include <QGraphicsObject>
#include <QGraphicsEllipseItem>
#include <QGraphicsProxyWidget>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QString>
#include <vector>

namespace OpenDaw {

enum class NodeType { InputSource, Track, Bus, Master, OutputDevice };

class RoutingNode;

class JackItem : public QGraphicsEllipseItem {
public:
    JackItem(bool isInput, int index, RoutingNode* parentNode);

    bool isInput() const { return isInput_; }
    int jackIndex() const { return index_; }
    RoutingNode* parentNode() const { return parentNode_; }
    QPointF sceneCenterPos() const;

    void setConnected(bool c, const QColor& cableColor = Qt::black);
    void setHighlighted(bool h);
    // Faded while a cable is being dragged and this jack can't take it.
    void setDimmed(bool d);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bool isInput_;
    int index_;
    RoutingNode* parentNode_;
    bool connected_ = false;
    bool highlighted_ = false;
    bool dimmed_ = false;
    QColor cableColor_ = Qt::black;
};

class RoutingNode : public QGraphicsObject {
    Q_OBJECT

public:
    // `color` is the node's own colour (the track colour for tracks and buses);
    // `badgeColor` marks its kind.
    RoutingNode(NodeType type, const QString& name, const QColor& color,
                const QColor& badgeColor, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    NodeType nodeType() const { return type_; }
    QString name() const { return name_; }
    QColor categoryColor() const { return color_; }

    void setEffectNames(const QStringList& names);
    void setInputJackCount(int count);
    void setOutputJackCount(int count);
    void setInputJackLabels(const QStringList& labels);
    void setOutputJackLabels(const QStringList& labels);

    JackItem* inputJack(int index) const;
    JackItem* outputJack(int index) const;
    int inputJackCount() const { return static_cast<int>(inputJacks_.size()); }
    int outputJackCount() const { return static_cast<int>(outputJacks_.size()); }

    te::AudioTrack* track() const { return track_; }
    void setTrack(te::AudioTrack* t) { track_ = t; }

    juce::String deviceName() const { return deviceName_; }
    void setDeviceName(const juce::String& d) { deviceName_ = d; }

    bool isWarning() const { return warning_; }
    void setWarning(bool w) { warning_ = w; update(); }

    // Live level, 0..1, shown as a bar under the header.
    bool hasMeter() const;
    void setLevel(float normalized);
    float level() const { return level_; }

    bool hasMuteSolo() const { return type_ == NodeType::Track || type_ == NodeType::Bus; }
    void setMuteSolo(bool muted, bool soloed);
    // Muted, or silent because another track is soloed: drawn greyed out.
    void setSilenced(bool s);
    bool isSilenced() const { return silenced_; }

    void startRename();
    // Replaces the kind badge, e.g. MAIN on the output Master plays through.
    void setBadgeText(const QString& text) { badgeOverride_ = text; update(); }

    static constexpr qreal NODE_WIDTH = 168.0;
    static constexpr qreal HEADER_HEIGHT = 26.0;
    static constexpr qreal METER_AREA_HEIGHT = 9.0;
    static constexpr qreal EFFECT_ROW_HEIGHT = 18.0;
    static constexpr qreal JACK_AREA_HEIGHT = 28.0;
    static constexpr qreal JACK_OUTER_DIAMETER = 16.0;
    static constexpr qreal JACK_INNER_DIAMETER = 8.0;

signals:
    void nodeMoved();
    void renameRequested(const QString& newName);
    void muteClicked();
    void soloClicked();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void rebuildJacks();
    qreal bodyHeight() const;
    qreal contentTop() const;
    QRectF badgeRect() const;
    QRectF soloRect() const;
    QRectF muteRect() const;
    QString badgeText() const;

    NodeType type_;
    QString name_;
    QColor color_;
    QColor badgeColor_;
    QStringList effectNames_;
    std::vector<JackItem*> inputJacks_;
    std::vector<JackItem*> outputJacks_;
    QStringList inputJackLabels_;
    QStringList outputJackLabels_;
    te::AudioTrack* track_ = nullptr;
    juce::String deviceName_;
    static bool groupMoveGuard_;
    bool warning_ = false;
    float level_ = 0.0f;
    bool muted_ = false;
    bool soloed_ = false;
    bool silenced_ = false;
    QString badgeOverride_;
};

} // namespace OpenDaw
