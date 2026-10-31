#include "RoutingNode.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <cmath>

namespace OpenDaw {

bool RoutingNode::groupMoveGuard_ = false;

// ── JackItem ─────────────────────────────────────────────────────────────────

JackItem::JackItem(bool isInput, int index, RoutingNode* parentNode)
    : QGraphicsEllipseItem(parentNode)
    , isInput_(isInput), index_(index), parentNode_(parentNode)
{
    const qreal d = RoutingNode::JACK_OUTER_DIAMETER;
    setRect(-d / 2, -d / 2, d, d);
    setAcceptHoverEvents(true);
    setCursor(QCursor(Qt::CrossCursor));
    setZValue(2);
}

QPointF JackItem::sceneCenterPos() const
{
    return mapToScene(0, 0);
}

void JackItem::setConnected(bool c, const QColor& cableColor)
{
    connected_ = c;
    cableColor_ = cableColor;
    update();
}

void JackItem::setHighlighted(bool h)
{
    if (highlighted_ == h) return;
    highlighted_ = h;
    update();
}

void JackItem::setDimmed(bool d)
{
    if (dimmed_ == d) return;
    dimmed_ = d;
    update();
}

void JackItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);
    if (dimmed_)
        painter->setOpacity(0.3);
    const qreal outer = RoutingNode::JACK_OUTER_DIAMETER;
    const qreal inner = RoutingNode::JACK_INNER_DIAMETER;

    QColor ringColor = parentNode_->categoryColor();
    if (highlighted_)
        ringColor = ringColor.lighter(160);

    QColor fillColor = ringColor.darker(130);

    QPen ringPen(ringColor, 2.0);
    painter->setPen(ringPen);
    painter->setBrush(QBrush(fillColor));
    painter->drawEllipse(QRectF(-outer / 2, -outer / 2, outer, outer));

    QColor holeColor(15, 15, 15);
    if (connected_)
        holeColor = cableColor_.darker(200);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(holeColor));
    painter->drawEllipse(QRectF(-inner / 2, -inner / 2, inner, inner));

    if (highlighted_) {
        QPen glowPen(ringColor.lighter(180), 2.0);
        glowPen.setCosmetic(true);
        painter->setPen(glowPen);
        painter->setBrush(Qt::NoBrush);
        const qreal g = outer / 2 + 4;
        painter->drawEllipse(QRectF(-g, -g, g * 2, g * 2));
    }
}

// ── RoutingNode ──────────────────────────────────────────────────────────────

RoutingNode::RoutingNode(NodeType type, const QString& name, const QColor& color,
                         const QColor& badgeColor, QGraphicsItem* parent)
    : QGraphicsObject(parent), type_(type), name_(name), color_(color),
      badgeColor_(badgeColor)
{
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setZValue(1);
    setCursor(Qt::OpenHandCursor);
}

QVariant RoutingNode::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        QPointF newPos = value.toPointF();

        if (!groupMoveGuard_ && isSelected()) {
            groupMoveGuard_ = true;
            QPointF delta = newPos - pos();
            for (auto* item : scene()->selectedItems()) {
                if (item != this) {
                    if (auto* node = dynamic_cast<RoutingNode*>(item)) {
                        node->blockSignals(true);
                        node->moveBy(delta.x(), delta.y());
                        node->blockSignals(false);
                    }
                }
            }
            groupMoveGuard_ = false;
        }
        return newPos;
    }
    if (change == ItemPositionHasChanged)
        emit nodeMoved();
    if (change == ItemSelectedHasChanged)
        update();
    return QGraphicsObject::itemChange(change, value);
}

QString RoutingNode::badgeText() const
{
    if (!badgeOverride_.isEmpty())
        return badgeOverride_;
    switch (type_) {
        case NodeType::InputSource:  return "IN";
        case NodeType::Track:        return "TRK";
        case NodeType::Bus:          return "BUS";
        case NodeType::Master:       return "MST";
        case NodeType::OutputDevice: return "OUT";
    }
    return {};
}

QRectF RoutingNode::badgeRect() const
{
    QFont badgeFont;
    badgeFont.setPixelSize(8);
    badgeFont.setBold(true);
    const qreal bw = QFontMetrics(badgeFont).horizontalAdvance(badgeText()) + 8;
    return QRectF(NODE_WIDTH - bw - 6, 5, bw, 16);
}

QRectF RoutingNode::soloRect() const
{
    const QRectF badge = badgeRect();
    return QRectF(badge.left() - 4 - 16, 5, 16, 16);
}

QRectF RoutingNode::muteRect() const
{
    const QRectF solo = soloRect();
    return QRectF(solo.left() - 3 - 16, 5, 16, 16);
}

bool RoutingNode::hasMeter() const
{
    return type_ == NodeType::Track || type_ == NodeType::Bus || type_ == NodeType::Master;
}

void RoutingNode::setLevel(float normalized)
{
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    if (std::abs(normalized - level_) < 0.004f) return;
    level_ = normalized;
    update(QRectF(0, HEADER_HEIGHT, NODE_WIDTH, METER_AREA_HEIGHT));
}

void RoutingNode::setMuteSolo(bool muted, bool soloed)
{
    muted_ = muted;
    soloed_ = soloed;
    update();
}

void RoutingNode::setSilenced(bool s)
{
    silenced_ = s;
    update();
}

void RoutingNode::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && hasMuteSolo()) {
        if (muteRect().contains(event->pos())) {
            emit muteClicked();
            event->accept();
            return;
        }
        if (soloRect().contains(event->pos())) {
            emit soloClicked();
            event->accept();
            return;
        }
    }
    QGraphicsObject::mousePressEvent(event);
}

void RoutingNode::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    const bool onName = event->pos().y() <= HEADER_HEIGHT
                        && event->pos().x() < (hasMuteSolo() ? muteRect() : badgeRect()).left();
    if (type_ == NodeType::OutputDevice || type_ == NodeType::Master || !onName) {
        QGraphicsObject::mouseDoubleClickEvent(event);
        return;
    }
    startRename();
    event->accept();
}

void RoutingNode::startRename()
{
    if (type_ == NodeType::OutputDevice || type_ == NodeType::Master)
        return;

    auto& theme = ThemeManager::instance().current();
    auto* edit = new QLineEdit();
    edit->setText(name_);
    edit->setAccessibleName("Rename Node");
    edit->selectAll();
    edit->setFixedWidth(static_cast<int>(NODE_WIDTH - 4));
    edit->setFixedHeight(static_cast<int>(HEADER_HEIGHT - 2));
    edit->setStyleSheet(
        QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                "font-size: 11px; font-weight: bold; padding: 0 4px; }")
            .arg(theme.background.name(), theme.text.name(), theme.accent.name()));

    auto* proxy = new QGraphicsProxyWidget(this);
    proxy->setWidget(edit);
    proxy->setPos(2, 1);
    proxy->setZValue(10);
    edit->setFocus();

    connect(edit, &QLineEdit::editingFinished, this, [this, proxy, edit]() {
        QString newName = edit->text().trimmed();
        if (!newName.isEmpty() && newName != name_) {
            name_ = newName;
            emit renameRequested(newName);
            update();
        }
        proxy->deleteLater();
    });
}

qreal RoutingNode::contentTop() const
{
    return HEADER_HEIGHT + (hasMeter() ? METER_AREA_HEIGHT : 0.0);
}

qreal RoutingNode::bodyHeight() const
{
    qreal h = contentTop();
    if (type_ != NodeType::InputSource && type_ != NodeType::OutputDevice)
        h += std::max(1, static_cast<int>(effectNames_.size())) * EFFECT_ROW_HEIGHT;
    int maxJacks = std::max(static_cast<int>(inputJacks_.size()),
                            static_cast<int>(outputJacks_.size()));
    qreal jackH = JACK_AREA_HEIGHT;
    if (maxJacks > 1)
        jackH = JACK_AREA_HEIGHT + (maxJacks - 1) * (JACK_OUTER_DIAMETER + 4);
    h += jackH;
    return h;
}

QRectF RoutingNode::boundingRect() const
{
    constexpr qreal margin = 10.0;
    return QRectF(-margin, -margin, NODE_WIDTH + margin * 2, bodyHeight() + margin * 2);
}

void RoutingNode::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);
    auto& theme = ThemeManager::instance().current();

    const QRectF bodyRect(0, 0, NODE_WIDTH, bodyHeight());
    const QColor headerColor = silenced_ ? theme.surfaceLight : color_;

    // Selection glow (drawn behind the body, within boundingRect margin)
    if (isSelected()) {
        QColor glowColor = theme.accent;
        glowColor.setAlpha(90);
        QPainterPath glowPath;
        glowPath.addRoundedRect(bodyRect.adjusted(-4, -4, 4, 4), 8, 8);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(glowColor));
        painter->drawPath(glowPath);
    }

    // Body background
    QPainterPath bodyPath;
    bodyPath.addRoundedRect(bodyRect, 6, 6);
    QColor borderColor = warning_ ? QColor(220, 140, 30)
                       : isSelected() ? theme.accent : theme.border;
    painter->setPen(QPen(borderColor, (isSelected() || warning_) ? 2.0 : 1.0));
    painter->setBrush(QBrush(theme.surface));
    painter->drawPath(bodyPath);

    // Header bar (rounded top corners, flat bottom via clip)
    painter->save();
    painter->setClipRect(QRectF(0, 0, NODE_WIDTH, HEADER_HEIGHT));
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(headerColor));
    painter->drawRoundedRect(QRectF(1, 1, NODE_WIDTH - 2, HEADER_HEIGHT + 6), 5, 5);
    painter->restore();

    const QColor headerText = silenced_ ? theme.textDim
                                        : ThemeManager::readableTextOn(headerColor);
    const qreal nameRight = (hasMuteSolo() ? muteRect() : badgeRect()).left() - 4;

    QFont headerFont;
    headerFont.setPixelSize(11);
    headerFont.setBold(true);
    painter->setFont(headerFont);
    painter->setPen(headerText);
    QFontMetrics hfm(headerFont);
    const QRectF nameRect(8, 0, nameRight - 8, HEADER_HEIGHT);
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                      hfm.elidedText(name_, Qt::ElideRight, static_cast<int>(nameRect.width())));

    QFont smallFont;
    smallFont.setPixelSize(8);
    smallFont.setBold(true);
    painter->setFont(smallFont);

    if (hasMuteSolo()) {
        auto drawToggle = [&](const QRectF& r, const QString& text, bool on, const QColor& onColor) {
            painter->setPen(QPen(on ? onColor.darker(130) : headerText, 1.0));
            painter->setBrush(on ? QBrush(onColor) : Qt::NoBrush);
            painter->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            painter->setPen(on ? QColor(17, 17, 17) : headerText);
            painter->drawText(r, Qt::AlignCenter, text);
        };
        drawToggle(muteRect(), "M", muted_, theme.muteButton);
        drawToggle(soloRect(), "S", soloed_, theme.soloButton);
    }

    const QRectF badge = badgeRect();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(badgeColor_));
    painter->drawRoundedRect(badge, 3, 3);
    painter->setPen(ThemeManager::readableTextOn(badgeColor_));
    painter->drawText(badge, Qt::AlignCenter, badgeText());

    // Level meter
    if (hasMeter()) {
        const QRectF well(8, HEADER_HEIGHT + 2, NODE_WIDTH - 16, METER_AREA_HEIGHT - 4);
        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.controlTrack);
        painter->drawRoundedRect(well, 2, 2);
        if (level_ > 0.0f) {
            QLinearGradient grad(well.topLeft(), well.topRight());
            grad.setColorAt(0.0, theme.meterGreen);
            grad.setColorAt(0.75, theme.meterGreen);
            grad.setColorAt(0.88, theme.meterYellow);
            grad.setColorAt(1.0, theme.meterRed);
            painter->setBrush(grad);
            painter->drawRoundedRect(QRectF(well.left(), well.top(),
                                            well.width() * level_, well.height()), 2, 2);
        }
    }

    // Effects list
    QFont fxFont;
    fxFont.setPixelSize(9);
    painter->setFont(fxFont);
    QFontMetrics ffm(fxFont);
    qreal y = contentTop();
    if (type_ != NodeType::InputSource && type_ != NodeType::OutputDevice) {
        if (effectNames_.isEmpty()) {
            painter->setPen(theme.textDim);
            painter->drawText(QRectF(10, y, NODE_WIDTH - 20, EFFECT_ROW_HEIGHT),
                              Qt::AlignVCenter | Qt::AlignLeft, "(no effects)");
        } else {
            for (const auto& fx : effectNames_) {
                QRectF fxRect(10, y, NODE_WIDTH - 20, EFFECT_ROW_HEIGHT);

                QRectF dotRect(12, y + (EFFECT_ROW_HEIGHT - 6) / 2, 6, 6);
                painter->setBrush(QBrush(color_));
                painter->setPen(Qt::NoPen);
                painter->drawEllipse(dotRect);

                painter->setPen(theme.textDim);
                const QRectF textRect = fxRect.adjusted(14, 0, 0, 0);
                painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                                  ffm.elidedText(fx, Qt::ElideRight, static_cast<int>(textRect.width())));
                y += EFFECT_ROW_HEIGHT;
            }
        }
    }

    // Separator line before jack area
    qreal sepY = bodyHeight() - (bodyHeight() - y);
    if (type_ != NodeType::InputSource && type_ != NodeType::OutputDevice) {
        painter->setPen(QPen(theme.border, 0.5));
        painter->drawLine(QPointF(4, sepY), QPointF(NODE_WIDTH - 4, sepY));
    }

    // Per-jack labels
    QFont jackFont;
    jackFont.setPixelSize(8);
    jackFont.setBold(true);
    painter->setFont(jackFont);
    painter->setPen(theme.textDim);
    for (int i = 0; i < static_cast<int>(inputJacks_.size()); ++i) {
        QPointF jp = inputJacks_[i]->pos();
        QString label = (i < inputJackLabels_.size()) ? inputJackLabels_[i] : "IN";
        painter->drawText(QRectF(jp.x() + JACK_OUTER_DIAMETER / 2 + 3, jp.y() - 8, 34, 16),
                          Qt::AlignVCenter | Qt::AlignLeft, label);
    }
    for (int i = 0; i < static_cast<int>(outputJacks_.size()); ++i) {
        QPointF jp = outputJacks_[i]->pos();
        QString label = (i < outputJackLabels_.size()) ? outputJackLabels_[i] : "OUT";
        painter->drawText(QRectF(jp.x() - JACK_OUTER_DIAMETER / 2 - 37, jp.y() - 8, 34, 16),
                          Qt::AlignVCenter | Qt::AlignRight, label);
    }
}

void RoutingNode::setEffectNames(const QStringList& names)
{
    prepareGeometryChange();
    effectNames_ = names;
    rebuildJacks();
    update();
}

void RoutingNode::setInputJackCount(int count)
{
    prepareGeometryChange();
    for (auto* j : inputJacks_) delete j;
    inputJacks_.clear();
    for (int i = 0; i < count; ++i)
        inputJacks_.push_back(new JackItem(true, i, this));
    rebuildJacks();
}

void RoutingNode::setOutputJackCount(int count)
{
    prepareGeometryChange();
    for (auto* j : outputJacks_) delete j;
    outputJacks_.clear();
    for (int i = 0; i < count; ++i)
        outputJacks_.push_back(new JackItem(false, i, this));
    rebuildJacks();
}

void RoutingNode::setInputJackLabels(const QStringList& labels)
{
    inputJackLabels_ = labels;
    update();
}

void RoutingNode::setOutputJackLabels(const QStringList& labels)
{
    outputJackLabels_ = labels;
    update();
}

JackItem* RoutingNode::inputJack(int index) const
{
    if (index >= 0 && index < static_cast<int>(inputJacks_.size()))
        return inputJacks_[index];
    return nullptr;
}

JackItem* RoutingNode::outputJack(int index) const
{
    if (index >= 0 && index < static_cast<int>(outputJacks_.size()))
        return outputJacks_[index];
    return nullptr;
}

void RoutingNode::rebuildJacks()
{
    qreal h = bodyHeight();
    int maxJacks = std::max(static_cast<int>(inputJacks_.size()),
                            static_cast<int>(outputJacks_.size()));
    qreal jackAreaH = JACK_AREA_HEIGHT;
    if (maxJacks > 1)
        jackAreaH = JACK_AREA_HEIGHT + (maxJacks - 1) * (JACK_OUTER_DIAMETER + 4);
    qreal jackAreaTop = h - jackAreaH;

    int inCount = static_cast<int>(inputJacks_.size());
    for (int i = 0; i < inCount; ++i) {
        qreal spacing = jackAreaH / (inCount + 1);
        qreal yOff = jackAreaTop + spacing * (i + 1);
        inputJacks_[i]->setPos(JACK_OUTER_DIAMETER / 2 + 2, yOff);
    }

    int outCount = static_cast<int>(outputJacks_.size());
    for (int i = 0; i < outCount; ++i) {
        qreal spacing = jackAreaH / (outCount + 1);
        qreal yOff = jackAreaTop + spacing * (i + 1);
        outputJacks_[i]->setPos(NODE_WIDTH - JACK_OUTER_DIAMETER / 2 - 2, yOff);
    }
}

} // namespace OpenDaw
