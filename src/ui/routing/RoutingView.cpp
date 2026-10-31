#include "RoutingView.h"
#include "RoutingMatrixView.h"
#include "ui/mixer/SendsPanel.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QButtonGroup>
#include <QSettings>
#include <QMenu>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace OpenDaw {

namespace {
const QColor kInputColor(76, 175, 80);
const QColor kTrackBadge(66, 133, 244);
const QColor kBusBadge(255, 152, 0);
const QColor kMasterColor(211, 47, 47);
const QColor kOutputColor(158, 158, 158);
const QColor kSidechainColor(0, 188, 212);

constexpr int kGainRole = 0;          // QGraphicsItem::data key: a send cable's level in dB
constexpr qreal kPickRadius = 24.0;   // how close to a cable's plug a press unplugs it

QString formatDb(float db)
{
    if (db <= -59.9f) return QStringLiteral("-inf");
    return (db > 0.05f ? "+" : "") + QString::number(db, 'f', 1) + " dB";
}

float dbToMeter(float db)
{
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

QString trackName(te::AudioTrack* t)
{
    return t ? QString::fromStdString(t->getName().toStdString()) : QString();
}
}

// A small label that follows the cursor while dragging a cable, saying what
// letting go will do. Drawn at screen size whatever the zoom.
class RoutingHintItem : public QGraphicsItem {
public:
    RoutingHintItem()
    {
        setFlag(ItemIgnoresTransformations, true);
        setZValue(1000);
    }

    void setHint(const QString& text, bool valid)
    {
        prepareGeometryChange();
        text_ = text;
        valid_ = valid;
        update();
    }

    QRectF boundingRect() const override
    {
        QFontMetrics fm(font());
        return QRectF(14, 14, fm.horizontalAdvance(text_) + 16, fm.height() + 8);
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        auto& theme = ThemeManager::instance().current();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = boundingRect();
        const QColor edge = valid_ ? theme.accent : QColor(220, 140, 30);
        p->setPen(QPen(edge, 1.2));
        p->setBrush(theme.surface);
        p->drawRoundedRect(r, 4, 4);
        p->setFont(font());
        p->setPen(theme.text);
        p->drawText(r, Qt::AlignCenter, text_);
    }

private:
    static QFont font()
    {
        QFont f;
        f.setPixelSize(11);
        return f;
    }

    QString text_;
    bool valid_ = true;
};

RoutingView::RoutingView(EditManager* editMgr, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("Routing View");
    auto& theme = ThemeManager::instance().current();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    toolbar_ = new QToolBar(this);
    toolbar_->setAccessibleName("Routing Toolbar");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(16, 16));

    graphBtn_ = new QPushButton("Graph", toolbar_);
    graphBtn_->setAccessibleName("Graph View");
    graphBtn_->setToolTip("Patch with cables between nodes");
    matrixBtn_ = new QPushButton("Matrix", toolbar_);
    matrixBtn_->setAccessibleName("Matrix View");
    matrixBtn_->setToolTip("Patch from a grid: one row per track, a column per destination");
    auto* modeGroup = new QButtonGroup(this);
    for (auto* b : { graphBtn_, matrixBtn_ }) {
        b->setCheckable(true);
        b->setStyleSheet(QString("QPushButton:checked { background: %1; color: %2; border-color: %1; }")
                             .arg(theme.accent.name(),
                                  ThemeManager::readableTextOn(theme.accent).name()));
        modeGroup->addButton(b);
        toolbar_->addWidget(b);
    }
    connect(matrixBtn_, &QPushButton::toggled, this, &RoutingView::setMatrixMode);
    toolbar_->addSeparator();

    auto* addBusBtn = new QPushButton("+ Bus", toolbar_);
    addBusBtn->setAccessibleName("Add Bus Track");
    addBusBtn->setToolTip("Add a bus: route tracks into it to process or level them together");
    connect(addBusBtn, &QPushButton::clicked, this, [this]() {
        editMgr_->addBusTrack();
    });
    toolbar_->addWidget(addBusBtn);

    auto* fxBusBtn = new QPushButton("+ Effects Bus", toolbar_);
    fxBusBtn->setAccessibleName("Add Effects Bus");
    fxBusBtn->setToolTip("Add a bus with one effect on it, fully wet, ready to take sends");
    auto* fxMenu = new QMenu(fxBusBtn);
    for (const QString fx : { QStringLiteral("Reverb"), QStringLiteral("Delay"),
                              QStringLiteral("Chorus"), QStringLiteral("Phaser") })
        fxMenu->addAction(fx + " Bus", this, [this, fx]() { editMgr_->addFxBus(fx); });
    fxBusBtn->setMenu(fxMenu);
    toolbar_->addWidget(fxBusBtn);
    toolbar_->addSeparator();

    autoLayoutBtn_ = new QPushButton("Auto Layout", toolbar_);
    autoLayoutBtn_->setAccessibleName("Auto Layout");
    connect(autoLayoutBtn_, &QPushButton::clicked, this, [this]() {
        layoutNodes();
        for (auto* c : cables_) c->updatePath();
        updateSceneRect();
    });
    toolbar_->addWidget(autoLayoutBtn_);

    zoomFitBtn_ = new QPushButton("Zoom to Fit", toolbar_);
    zoomFitBtn_->setAccessibleName("Zoom to Fit");
    connect(zoomFitBtn_, &QPushButton::clicked, this, &RoutingView::zoomToFit);
    toolbar_->addWidget(zoomFitBtn_);
    toolbar_->addSeparator();

    helpLabel_ = new QLabel(toolbar_);
    helpLabel_->setStyleSheet(QString("color: %1; padding-left: 8px;").arg(theme.textDim.name()));
    toolbar_->addWidget(helpLabel_);

    layout->addWidget(toolbar_);

    scene_ = new QGraphicsScene(this);
    scene_->setItemIndexMethod(QGraphicsScene::NoIndex);
    scene_->setBackgroundBrush(QBrush(theme.background.darker(110)));

    view_ = new QGraphicsView(scene_, this);
    view_->setAccessibleName("Routing Canvas");
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setDragMode(QGraphicsView::RubberBandDrag);
    view_->setRubberBandSelectionMode(Qt::IntersectsItemBoundingRect);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    view_->setFrameStyle(0);
    view_->setFocusPolicy(Qt::StrongFocus);
    view_->viewport()->installEventFilter(this);
    view_->installEventFilter(this);

    matrix_ = new RoutingMatrixView(editMgr_, this);
    connect(matrix_, &RoutingMatrixView::trackSelected, this, &RoutingView::trackSelected);
    auto* matrixScroll = new QScrollArea(this);
    matrixScroll->setAccessibleName("Routing Matrix Scroll Area");
    matrixScroll->setFrameStyle(0);
    matrixScroll->setWidget(matrix_);
    matrixScroll->setWidgetResizable(false);
    matrixScroll->setStyleSheet(QString("QScrollArea { background: %1; }")
                                    .arg(theme.background.darker(110).name()));

    stack_ = new QStackedWidget(this);
    stack_->addWidget(view_);
    stack_->addWidget(matrixScroll);
    layout->addWidget(stack_, 1);

    rebuildTimer_.setSingleShot(true);
    rebuildTimer_.setInterval(50);
    connect(&rebuildTimer_, &QTimer::timeout, this, &RoutingView::rebuild);

    sceneRectTimer_.setSingleShot(true);
    sceneRectTimer_.setInterval(0);
    connect(&sceneRectTimer_, &QTimer::timeout, this, &RoutingView::updateSceneRect);

    physicsTimer_.setInterval(16);
    connect(&physicsTimer_, &QTimer::timeout, this, &RoutingView::tickCablePhysics);

    meterTimer_.setInterval(33);
    connect(&meterTimer_, &QTimer::timeout, this, &RoutingView::tickMeters);

    connect(scene_, &QGraphicsScene::selectionChanged,
            this, &RoutingView::onSceneSelectionChanged);

    const bool matrixMode = QSettings().value("routing/matrixMode", false).toBool();
    (matrixMode ? matrixBtn_ : graphBtn_)->setChecked(true);
    setMatrixMode(matrixMode);

    connect(editMgr_, &EditManager::tracksChanged, this, &RoutingView::scheduleRebuild);
    connect(editMgr_, &EditManager::routingChanged, this, &RoutingView::scheduleRebuild);
    connect(editMgr_, &EditManager::editChanged, this, &RoutingView::scheduleRebuild);
    connect(editMgr_, &EditManager::sendChanged, this, &RoutingView::updateSendLabels);
    connect(editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
        clearAll();
        // Positions are per project; the next one loads its own.
        savedPositions_.clear();
    });

    rebuild();
}

RoutingView::~RoutingView()
{
    rebuildTimer_.stop();
    meterTimer_.stop();
    removeMeterTaps();
    disconnect(scene_, nullptr, this, nullptr);
    disconnect(editMgr_, nullptr, this, nullptr);
}

void RoutingView::setMatrixMode(bool matrix)
{
    stack_->setCurrentIndex(matrix ? 1 : 0);
    autoLayoutBtn_->setVisible(!matrix);
    zoomFitBtn_->setVisible(!matrix);
    helpLabel_->setText(matrix
        ? "Click a dot to choose a track's output &nbsp;&middot;&nbsp; click under SEND TO to add a send, "
          "then drag or scroll it to set the level &nbsp;&middot;&nbsp; right-click a cell for more"
        : "Drag <b>OUT</b> to route &nbsp;&middot;&nbsp; drag <b>SEND</b> for a parallel effect "
          "&nbsp;&middot;&nbsp; drag a plugged end away to unplug &nbsp;&middot;&nbsp; "
          "scroll a send to set its level &nbsp;&middot;&nbsp; right-click for more");
    QSettings().setValue("routing/matrixMode", matrix);
    // The graph meters only run while the graph is on screen.
    if (matrix) {
        meterTimer_.stop();
    } else {
        if (rebuildPending_) rebuild();
        if (isVisible() && !meterTaps_.empty()) meterTimer_.start();
    }
}

void RoutingView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (rebuildPending_)
        rebuild();
    if (!meterTaps_.empty() && stack_->currentIndex() == 0)
        meterTimer_.start();
}

void RoutingView::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    meterTimer_.stop();
}

void RoutingView::clearAll()
{
    saveNodePositions();
    removeMeterTaps();
    dragCable_ = nullptr;
    dragSourceJack_ = nullptr;
    pickedCable_ = nullptr;
    hoverTarget_ = nullptr;
    markedCable_ = nullptr;
    hint_ = nullptr;
    cables_.clear();
    inputNodes_.clear();
    trackNodes_.clear();
    busNodes_.clear();
    masterNode_ = nullptr;
    mainOutputNode_ = nullptr;
    outputNodes_.clear();

    disconnect(scene_, &QGraphicsScene::selectionChanged,
               this, &RoutingView::onSceneSelectionChanged);
    scene_->clear();
    connect(scene_, &QGraphicsScene::selectionChanged,
            this, &RoutingView::onSceneSelectionChanged);
}

void RoutingView::flushNodePositions()
{
    saveNodePositions();
}

void RoutingView::scheduleRebuild()
{
    if (!rebuildTimer_.isActive())
        rebuildTimer_.start();
}

void RoutingView::rebuild()
{
    rebuildTimer_.stop();

    // Hidden behind another tab, or the matrix is showing: catch up when the
    // graph is shown instead of on every edit.
    if (!isVisible() || stack_->currentIndex() != 0) {
        rebuildPending_ = true;
        return;
    }

    if (dragCable_ || panning_ || scene_->mouseGrabberItem()) {
        rebuildTimer_.start(100);
        return;
    }

    rebuildPending_ = false;
    clearAll();
    if (!editMgr_ || !editMgr_->edit()) return;
    buildNodes();
    layoutNodes();
    restoreNodePositions();
    buildCables();
    createMeterTaps();
    updateSceneRect();
}

std::vector<RoutingNode*> RoutingView::allNodes() const
{
    std::vector<RoutingNode*> nodes;
    for (auto* list : { &inputNodes_, &trackNodes_, &busNodes_, &outputNodes_ })
        nodes.insert(nodes.end(), list->begin(), list->end());
    if (masterNode_)
        nodes.push_back(masterNode_);
    return nodes;
}

RoutingNode* RoutingView::nodeForTrack(te::AudioTrack* track) const
{
    if (!track) return nullptr;
    for (auto* list : { &trackNodes_, &busNodes_ })
        for (auto* n : *list)
            if (n->track() == track) return n;
    return nullptr;
}

RoutingNode* RoutingView::nodeAt(const QPointF& scenePos) const
{
    for (auto* item : scene_->items(scenePos)) {
        if (auto* node = dynamic_cast<RoutingNode*>(item))
            return node;
        if (auto* jack = dynamic_cast<JackItem*>(item))
            return jack->parentNode();
        if (item->parentItem())
            if (auto* node = dynamic_cast<RoutingNode*>(item->parentItem()))
                return node;
    }
    return nullptr;
}

void RoutingView::updateAllCablePaths()
{
    for (auto* c : cables_)
        c->updatePath();
    startCablePhysics();
}

void RoutingView::startCablePhysics()
{
    if (!physicsTimer_.isActive())
        physicsTimer_.start();
}

void RoutingView::tickCablePhysics()
{
    constexpr qreal dt = 0.016;
    bool anyMoving = false;

    for (auto* c : cables_) {
        if (c->stepPhysics(dt))
            anyMoving = true;
    }
    if (dragCable_ && dragCable_->stepPhysics(dt))
        anyMoving = true;

    if (!anyMoving)
        physicsTimer_.stop();
}

void RoutingView::connectNodeSignals(RoutingNode* node)
{
    connect(node, &RoutingNode::nodeMoved, this, &RoutingView::updateAllCablePaths);
    connect(node, &RoutingNode::nodeMoved, this, [this]() {
        if (!sceneRectTimer_.isActive())
            sceneRectTimer_.start();
    });
    connect(node, &RoutingNode::renameRequested, this, [this, node](const QString& newName) {
        switch (node->nodeType()) {
            case NodeType::Track:
            case NodeType::Bus:
                if (node->track()) {
                    node->track()->setName(juce::String(newName.toStdString()));
                    emit editMgr_->tracksChanged();
                }
                break;
            case NodeType::InputSource:
                editMgr_->setInputDisplayName(node->deviceName(), newName);
                break;
            case NodeType::Master:
            case NodeType::OutputDevice:
                break;
        }
    });
    // Mixer strips and track headers re-read mute and solo on editChanged.
    connect(node, &RoutingNode::muteClicked, this, [this, node]() {
        if (auto* t = node->track()) {
            t->setMute(!t->isMuted(false));
            emit editMgr_->editChanged();
        }
    });
    connect(node, &RoutingNode::soloClicked, this, [this, node]() {
        if (auto* t = node->track()) {
            t->setSolo(!t->isSolo(false));
            emit editMgr_->editChanged();
        }
    });
}

QString RoutingView::nodeKey(RoutingNode* node) const
{
    switch (node->nodeType()) {
        case NodeType::InputSource:
            return "input:" + QString::fromStdString(node->deviceName().toStdString());
        case NodeType::Track:
        case NodeType::Bus:
            if (node->track())
                return "track:" + QString::number(node->track()->itemID.getRawID());
            break;
        case NodeType::Master:
            return "master";
        case NodeType::OutputDevice:
            return "output:" + QString::fromStdString(node->deviceName().toStdString());
    }
    return {};
}

void RoutingView::saveNodePositions()
{
    // Nothing built yet (never shown): keep the project's stored layout as it is.
    if (!masterNode_) return;

    for (auto* node : allNodes()) {
        QString key = nodeKey(node);
        if (!key.isEmpty())
            savedPositions_[key] = node->pos();
    }
    editMgr_->saveRoutingLayout(savedPositions_);
}

void RoutingView::restoreNodePositions()
{
    if (savedPositions_.isEmpty())
        savedPositions_ = editMgr_->loadRoutingLayout();

    if (savedPositions_.isEmpty()) return;

    for (auto* node : allNodes()) {
        auto it = savedPositions_.find(nodeKey(node));
        if (it != savedPositions_.end())
            node->setPos(it.value());
    }
}

void RoutingView::buildNodes()
{
    bool anySolo = false;
    for (auto* t : editMgr_->getAudioTracks())
        if (t->isSolo(false)) anySolo = true;

    for (const auto& src : editMgr_->getAvailableInputSources()) {
        auto* node = new RoutingNode(NodeType::InputSource, src.displayName,
                                     kInputColor, kInputColor);
        node->setDeviceName(src.deviceName);
        node->setInputJackCount(0);
        node->setOutputJackCount(1);
        node->setToolTip("Audio input. Drag its OUT onto a track's IN to record from it.\n"
                         "Double-click the name to rename it.");
        node->outputJack(0)->setToolTip("Drag onto a track's IN");
        scene_->addItem(node);
        connectNodeSignals(node);
        inputNodes_.push_back(node);
    }

    auto makeTrackNode = [&](te::AudioTrack* track, NodeType type) {
        const bool isBus = type == NodeType::Bus;
        auto* node = new RoutingNode(type, trackName(track), editMgr_->getTrackColour(track),
                                     isBus ? kBusBadge : kTrackBadge);
        node->setTrack(track);
        node->setEffectNames(getTrackEffectNames(track));

        const bool hasSC = trackHasSidechainPlugin(track);
        node->setInputJackCount(hasSC ? 2 : 1);
        node->setOutputJackCount(2);
        node->setInputJackLabels(hasSC ? QStringList{ "IN", "SC" } : QStringList{ "IN" });
        node->setOutputJackLabels({ "OUT", "SEND" });

        node->inputJack(0)->setToolTip(isBus ? "Bus input: tracks routed or sent here arrive here"
                                             : "Track input: drop an audio input here to record it");
        if (hasSC)
            node->inputJack(1)->setToolTip("Sidechain key: drop another track's OUT here to drive "
                                           "this track's compressor from it");
        node->outputJack(0)->setToolTip("Output: drag to a bus, Master or a direct hardware output");
        node->outputJack(1)->setToolTip("Send: drag to a bus to add a parallel send "
                                        "(the output keeps going where it goes)");
        node->setToolTip(isBus ? "Bus: double-click the name to rename it, right-click for routing and sends"
                               : "Track: double-click the name to rename it, right-click for routing and sends");

        const bool muted = track->isMuted(false);
        const bool soloed = track->isSolo(false);
        node->setMuteSolo(muted, soloed);
        node->setSilenced(muted || (anySolo && !soloed && !isBus));
        node->setWarning(editMgr_->isTrackOutputDisconnected(track));

        scene_->addItem(node);
        connectNodeSignals(node);
        return node;
    };

    for (auto* track : editMgr_->getNonBusAudioTracks())
        trackNodes_.push_back(makeTrackNode(track, NodeType::Track));
    for (auto* bus : editMgr_->getBusTracks())
        busNodes_.push_back(makeTrackNode(bus, NodeType::Bus));

    masterNode_ = new RoutingNode(NodeType::Master, "Master", kMasterColor, kMasterColor);
    masterNode_->setEffectNames(getMasterEffectNames());
    masterNode_->setInputJackCount(1);
    masterNode_->setOutputJackCount(1);
    masterNode_->setToolTip("Master: everything routed here is mixed and played through the main output");
    masterNode_->inputJack(0)->setToolTip("Master input");
    scene_->addItem(masterNode_);
    connectNodeSignals(masterNode_);

    for (const auto& dev : editMgr_->getAudioOutputDevices()) {
        auto* node = new RoutingNode(NodeType::OutputDevice, dev.name,
                                     kOutputColor, dev.isMain ? kMasterColor : kOutputColor);
        node->setDeviceName(dev.id);
        if (dev.isMain) {
            node->setBadgeText("MAIN");
            mainOutputNode_ = node;
        }
        node->setInputJackCount(1);
        node->setOutputJackCount(0);
        node->setToolTip(dev.isMain
            ? "Main output: Master plays here. Double-click for Audio Settings."
            : "Hardware output: drop a track's OUT here to play it directly, skipping Master "
              "(for a cue mix or outboard gear). Double-click for Audio Settings.");
        scene_->addItem(node);
        connectNodeSignals(node);
        outputNodes_.push_back(node);
    }
}

void RoutingView::layoutNodes()
{
    auto placeColumn = [](std::vector<RoutingNode*>& nodes, qreal x, qreal startY, qreal spacing) {
        qreal y = startY;
        for (auto* node : nodes) {
            node->setPos(x, y);
            y += node->boundingRect().height() + spacing;
        }
    };

    placeColumn(inputNodes_, COL_INPUTS, ROW_START, ROW_SPACING);
    placeColumn(trackNodes_, COL_TRACKS, ROW_START, ROW_SPACING);
    placeColumn(busNodes_, COL_BUSES, ROW_START, ROW_SPACING);

    if (masterNode_) {
        qreal centerY = ROW_START;
        if (!trackNodes_.empty()) {
            qreal totalH = 0;
            for (auto* n : trackNodes_)
                totalH += n->boundingRect().height() + ROW_SPACING;
            centerY = ROW_START + (totalH - ROW_SPACING) / 2
                       - masterNode_->boundingRect().height() / 2;
            if (centerY < ROW_START) centerY = ROW_START;
        }
        masterNode_->setPos(COL_MASTER, centerY);
    }

    placeColumn(outputNodes_, COL_OUTPUTS, ROW_START, ROW_SPACING);
}

void RoutingView::addCable(JackItem* from, JackItem* to, const QColor& color, CableKind kind)
{
    if (!from || !to) return;
    auto* cable = new CableItem(from, to, color, kind);
    scene_->addItem(cable);
    cables_.push_back(cable);
    from->setConnected(true, color);
    to->setConnected(true, color);
}

void RoutingView::buildCables()
{
    // Input source -> track
    for (auto* list : { &trackNodes_, &busNodes_ }) {
        for (auto* tNode : *list) {
            QString inputName = editMgr_->getTrackInputName(tNode->track());
            if (inputName.isEmpty()) continue;
            for (auto* iNode : inputNodes_) {
                if (QString::fromStdString(iNode->deviceName().toStdString()) == inputName) {
                    addCable(iNode->outputJack(0), tNode->inputJack(0), kInputColor, CableKind::Main);
                    break;
                }
            }
        }
    }

    // Track/bus outputs: into a bus, to Master, or straight to a hardware output
    for (auto* list : { &trackNodes_, &busNodes_ }) {
        for (auto* node : *list) {
            auto* track = node->track();
            const QColor colour = editMgr_->getTrackColour(track);
            if (editMgr_->isTrackOutputDisconnected(track)) continue;
            if (auto* dest = editMgr_->getTrackOutputDestination(track)) {
                if (auto* destNode = nodeForTrack(dest))
                    addCable(node->outputJack(0), destNode->inputJack(0), colour, CableKind::Main);
                continue;
            }
            const auto direct = editMgr_->getTrackDirectOutputID(track);
            if (direct.isNotEmpty()) {
                for (auto* out : outputNodes_)
                    if (out->deviceName() == direct)
                        addCable(node->outputJack(0), out->inputJack(0), colour, CableKind::Main);
                continue;
            }
            if (masterNode_)
                addCable(node->outputJack(0), masterNode_->inputJack(0), colour, CableKind::Main);
        }
    }

    // Aux sends
    for (auto* list : { &trackNodes_, &busNodes_ }) {
        for (auto* node : *list) {
            const QColor colour = editMgr_->getTrackColour(node->track());
            for (const auto& send : editMgr_->getTrackSends(node->track())) {
                auto* busNode = nodeForTrack(send.bus);
                if (!busNode) continue;
                addCable(node->outputJack(1), busNode->inputJack(0), colour, CableKind::Send);
            }
        }
    }

    // Sidechain keys (a track/bus output -> another node's SC jack)
    for (auto* list : { &trackNodes_, &busNodes_ }) {
        for (auto* node : *list) {
            if (node->inputJackCount() < 2) continue;
            for (auto* p : node->track()->pluginList.getPlugins()) {
                if (!p->canSidechain()) continue;
                auto scSourceID = p->getSidechainSourceID();
                if (!scSourceID.isValid()) continue;
                for (auto* srcList : { &trackNodes_, &busNodes_ })
                    for (auto* src : *srcList)
                        if (src->track()->itemID == scSourceID)
                            addCable(src->outputJack(0), node->inputJack(1),
                                     kSidechainColor, CableKind::Sidechain);
            }
        }
    }

    if (masterNode_ && mainOutputNode_)
        addCable(masterNode_->outputJack(0), mainOutputNode_->inputJack(0),
                 kMasterColor.lighter(130), CableKind::Device);

    // Muted sources draw faint cables.
    for (auto* c : cables_)
        c->setMuted(c->sourceJack()->parentNode()->isSilenced());

    updateSendLabels();

    for (auto* c : cables_) {
        auto* src = c->sourceJack()->parentNode();
        auto* dst = c->destJack()->parentNode();
        switch (c->kind()) {
            case CableKind::Main:
                c->setToolTip(src->name() + " → " + dst->name()
                              + "\nDrag the plugged end to move it, or away to unplug.");
                break;
            case CableKind::Sidechain:
                c->setToolTip(src->name() + " keys " + dst->name() + "'s sidechain");
                break;
            case CableKind::Device:
                c->setToolTip("Master plays through " + dst->name());
                break;
            case CableKind::Send:
                break;   // set with the label
        }
    }
}

void RoutingView::updateSendLabels()
{
    for (auto* c : cables_) {
        if (c->kind() != CableKind::Send) continue;
        auto* srcTrack = c->sourceJack()->parentNode()->track();
        auto* busTrack = c->destJack()->parentNode()->track();
        for (const auto& send : editMgr_->getTrackSends(srcTrack)) {
            if (send.bus != busTrack) continue;
            QString label = send.muted ? QStringLiteral("muted") : formatDb(send.gainDb);
            if (send.preFader) label += " PRE";
            if (send.phaseInverted) label += QString::fromUtf8(" Ø");
            c->setLabel(label);
            c->setData(kGainRole, send.muted ? -100.0f : send.gainDb);
            c->setToolTip(QString("%1 sends to %2 at %3, %4-fader.\n"
                                  "Scroll to change the level, double-click for all sends,\n"
                                  "right-click for pre/post, mute and phase.")
                              .arg(trackName(srcTrack), trackName(busTrack),
                                   formatDb(send.gainDb), send.preFader ? "pre" : "post"));
            break;
        }
    }
}

QStringList RoutingView::getTrackEffectNames(te::AudioTrack* track) const
{
    QStringList names;
    if (!track) return names;
    for (auto* p : track->pluginList.getPlugins()) {
        if (EditManager::isInternalPlugin(p)) continue;
        names.append(QString::fromStdString(p->getName().toStdString()));
    }
    return names;
}

QStringList RoutingView::getMasterEffectNames() const
{
    QStringList names;
    if (!editMgr_ || !editMgr_->edit()) return names;
    for (auto* p : editMgr_->edit()->getMasterPluginList().getPlugins()) {
        if (EditManager::isInternalPlugin(p)) continue;
        names.append(QString::fromStdString(p->getName().toStdString()));
    }
    return names;
}

bool RoutingView::trackHasSidechainPlugin(te::AudioTrack* track) const
{
    return getFirstSidechainPlugin(track) != nullptr;
}

te::Plugin* RoutingView::getFirstSidechainPlugin(te::AudioTrack* track) const
{
    if (!track) return nullptr;
    for (auto* p : track->pluginList.getPlugins())
        if (p->canSidechain()) return p;
    return nullptr;
}

// ── Meters and signal flow ───────────────────────────────────────────────────

void RoutingView::createMeterTaps()
{
    removeMeterTaps();
    auto addTap = [this](RoutingNode* node, te::LevelMeterPlugin* meter, bool isMaster) {
        if (!node || !meter) return;
        MeterTap tap;
        tap.node = node;
        tap.plugin = meter;
        tap.client = std::make_unique<te::LevelMeasurer::Client>();
        tap.isMaster = isMaster;
        meter->measurer.addClient(*tap.client);
        meterTaps_.push_back(std::move(tap));
    };
    for (auto* list : { &trackNodes_, &busNodes_ })
        for (auto* node : *list)
            addTap(node, node->track()->getLevelMeterPlugin(), false);
    if (masterNode_ && editMgr_->edit()) {
        auto meters = editMgr_->edit()->getMasterPluginList().getPluginsOfType<te::LevelMeterPlugin>();
        if (!meters.isEmpty())
            addTap(masterNode_, meters.getLast(), true);
    }
    if (isVisible() && stack_->currentIndex() == 0 && !meterTaps_.empty())
        meterTimer_.start();
}

void RoutingView::removeMeterTaps()
{
    for (auto& tap : meterTaps_)
        if (auto* meter = dynamic_cast<te::LevelMeterPlugin*>(tap.plugin.get()))
            meter->measurer.removeClient(*tap.client);
    meterTaps_.clear();
    meterTimer_.stop();
}

void RoutingView::tickMeters()
{
    if (!editMgr_->edit()) return;
    float masterDb = 0.0f;
    if (auto masterVol = editMgr_->edit()->getMasterVolumePlugin())
        masterDb = masterVol->getVolumeDb();

    for (auto& tap : meterTaps_) {
        const auto l = tap.client->getAndClearAudioLevel(0);
        const auto r = tap.client->getAndClearAudioLevel(1);
        float db = std::max(l.dB, r.dB);
        if (tap.isMaster) db += masterDb;
        tap.level = std::max(dbToMeter(db), tap.level * 0.82f);
        if (tap.level < 0.01f) tap.level = 0.0f;
        tap.node->setLevel(tap.level);
    }

    for (auto* c : cables_) {
        auto* src = c->sourceJack()->parentNode();
        float level = src->level();
        if (c->kind() == CableKind::Send) {
            const float gainDb = c->data(kGainRole).toFloat();
            level = gainDb <= -59.0f ? 0.0f : level * std::clamp(std::pow(10.0f, gainDb / 40.0f), 0.0f, 1.2f);
        }
        if (src->nodeType() == NodeType::InputSource) level = 0.0f;
        c->setSignal(level);
        c->advanceFlow(0.033);
    }
}

// ── Hit testing ──────────────────────────────────────────────────────────────

JackItem* RoutingView::findJackAt(const QPointF& scenePos, bool wantInput) const
{
    constexpr qreal SNAP_RADIUS = 20.0;
    JackItem* closest = nullptr;
    qreal closestDist = SNAP_RADIUS;

    for (auto* node : allNodes()) {
        int count = wantInput ? node->inputJackCount() : node->outputJackCount();
        for (int i = 0; i < count; ++i) {
            auto* jack = wantInput ? node->inputJack(i) : node->outputJack(i);
            if (!jack) continue;
            const QPointF d = jack->sceneCenterPos() - scenePos;
            const qreal dist = std::hypot(d.x(), d.y());
            if (dist < closestDist) {
                closestDist = dist;
                closest = jack;
            }
        }
    }
    return closest;
}

CableItem* RoutingView::cableAt(const QPointF& scenePos) const
{
    for (auto* c : cables_)
        if (c->isVisible() && c->labelContains(scenePos))
            return c;
    CableItem* best = nullptr;
    for (auto* c : cables_)
        if (c->isVisible() && c->shape().contains(c->mapFromScene(scenePos))
            && (!best || c->zValue() > best->zValue()))
            best = c;
    return best;
}

CableItem* RoutingView::cableEndAt(const QPointF& scenePos) const
{
    // Several cables can share one input jack; take the one whose last stretch
    // passes nearest the press.
    CableItem* best = nullptr;
    qreal bestDist = 1e9;
    for (auto* c : cables_) {
        if (!c->isVisible() || c->kind() == CableKind::Device || !c->destJack()) continue;
        const QPointF end = c->destJack()->sceneCenterPos();
        const qreal toEnd = std::hypot(end.x() - scenePos.x(), end.y() - scenePos.y());
        if (toEnd > kPickRadius) continue;
        // On the jack itself, or on the cable's last stretch; not the node body beside it.
        const bool onJack = toEnd <= RoutingNode::JACK_OUTER_DIAMETER / 2 + 2;
        if (!onJack && !c->shape().contains(c->mapFromScene(scenePos))) continue;
        const QPointF near = c->path().pointAtPercent(0.93);
        const qreal d = std::hypot(near.x() - scenePos.x(), near.y() - scenePos.y());
        if (d < bestDist) {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

// ── What a connection means ──────────────────────────────────────────────────

RoutingView::Connection RoutingView::planConnection(JackItem* from, JackItem* to) const
{
    Connection c;
    if (!from || !to || from->isInput() || !to->isInput()) return c;
    auto* src = from->parentNode();
    auto* dst = to->parentNode();
    auto* srcTrack = src->track();
    auto* dstTrack = dst->track();
    const QString srcName = src->name();
    const QString dstName = dst->name();

    if (src == dst) {
        c.description = "A track can't feed itself";
        return c;
    }

    if (src->nodeType() == NodeType::InputSource) {
        if (!dstTrack || to->jackIndex() != 0) {
            c.description = "Drop an input on a track's IN";
            return c;
        }
        if (editMgr_->isMidiTrack(dstTrack)) {
            c.description = dstName + " is a MIDI track: pick its MIDI input in the track header";
            return c;
        }
        if (editMgr_->getTrackInputName(dstTrack)
            == QString::fromStdString(src->deviceName().toStdString())) {
            c.description = dstName + " already records from " + srcName;
            return c;
        }
        c.valid = true;
        c.description = "Record " + srcName + " on " + dstName;
        const auto device = src->deviceName();
        c.apply = [this, dstTrack, device]() { editMgr_->assignInputToTrack(*dstTrack, device); };
        return c;
    }

    if (src->nodeType() == NodeType::Master) {
        c.description = "Master plays through the main output; change it in Audio Settings";
        return c;
    }
    if (!srcTrack) return c;

    // Sidechain key input
    if (dstTrack && to->jackIndex() == 1) {
        auto* plugin = getFirstSidechainPlugin(dstTrack);
        if (!plugin) return c;
        if (plugin->getSidechainSourceID() == srcTrack->itemID) {
            c.description = srcName + " already keys " + dstName;
            return c;
        }
        c.valid = true;
        c.description = "Key " + dstName + "'s "
                        + QString::fromStdString(plugin->getName().toStdString())
                        + " from " + srcName;
        c.apply = [this, plugin, srcTrack]() {
            plugin->setSidechainSourceID(srcTrack->itemID);
            plugin->guessSidechainRouting();
            emit editMgr_->editChanged();
        };
        return c;
    }

    const bool isSend = from->jackIndex() == 1;
    if (isSend) {
        if (dst->nodeType() != NodeType::Bus || !dstTrack) {
            c.description = "Sends go into a bus. Drop on empty space to make one.";
            return c;
        }
        for (const auto& s : editMgr_->getTrackSends(srcTrack)) {
            if (s.bus == dstTrack) {
                c.description = srcName + " already sends to " + dstName;
                return c;
            }
        }
        if (!editMgr_->canSendTo(srcTrack, dstTrack)) {
            c.description = "That would feed " + dstName + " back into itself";
            return c;
        }
        c.valid = true;
        c.description = "Send " + srcName + " to " + dstName + " (its output stays put)";
        c.apply = [this, srcTrack, dstTrack]() { editMgr_->addSend(*srcTrack, *dstTrack); };
        return c;
    }

    // Main output
    const QString current = editMgr_->currentOutputKey(srcTrack);
    if (dst->nodeType() == NodeType::Master) {
        if (current == "master") {
            c.description = srcName + " already goes to Master";
            return c;
        }
        c.valid = true;
        c.description = "Route " + srcName + " to Master";
        c.apply = [this, srcTrack]() { editMgr_->setTrackOutputToMaster(*srcTrack); };
        return c;
    }
    if (dst->nodeType() == NodeType::Bus && dstTrack) {
        if (current == "track:" + QString::number(dstTrack->itemID.getRawID())) {
            c.description = srcName + " already goes into " + dstName;
            return c;
        }
        if (editMgr_->wouldCreateCycle(srcTrack, dstTrack)) {
            c.description = "That would feed " + dstName + " back into itself";
            return c;
        }
        c.valid = true;
        c.description = "Route " + srcName + " into " + dstName;
        c.apply = [this, srcTrack, dstTrack]() { editMgr_->setTrackOutputToTrack(*srcTrack, *dstTrack); };
        return c;
    }
    if (dst->nodeType() == NodeType::OutputDevice) {
        if (dst == mainOutputNode_) {
            c.description = "Master plays here; route " + srcName + " to Master instead";
            return c;
        }
        if (current == "device:" + QString::fromStdString(dst->deviceName().toStdString())) {
            c.description = srcName + " already plays out of " + dst->name();
            return c;
        }
        c.valid = true;
        c.description = "Play " + srcName + " straight out of " + dst->name() + ", skipping Master";
        const auto deviceID = dst->deviceName();
        c.apply = [this, srcTrack, deviceID]() { editMgr_->setTrackOutputToDevice(*srcTrack, deviceID); };
        return c;
    }
    c.description = "A track's output goes into a bus or Master, not into another track";
    return c;
}

void RoutingView::disconnectCable(CableItem* cable)
{
    if (!cable) return;
    auto* src = cable->sourceJack()->parentNode();
    auto* dst = cable->destJack()->parentNode();
    switch (cable->kind()) {
        case CableKind::Main:
            if (src->nodeType() == NodeType::InputSource) {
                if (dst->track()) editMgr_->clearTrackInput(*dst->track());
            } else if (src->track()) {
                editMgr_->clearTrackOutput(*src->track());
            }
            break;
        case CableKind::Send:
            if (src->track() && dst->track())
                editMgr_->removeSend(*src->track(), *dst->track());
            break;
        case CableKind::Sidechain:
            if (auto* plugin = getFirstSidechainPlugin(dst->track())) {
                plugin->setSidechainSourceID({});
                emit editMgr_->editChanged();
            }
            break;
        case CableKind::Device:
            break;
    }
}

// ── Mouse and keyboard ───────────────────────────────────────────────────────

bool RoutingView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == view_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            QPointF scenePos = view_->mapToScene(me->pos());

            if (me->button() == Qt::RightButton) {
                if (auto* node = nodeAt(scenePos)) {
                    onNodeRightClicked(node, me->globalPosition().toPoint());
                    return true;
                }
                if (auto* cable = cableAt(scenePos)) {
                    onCableRightClicked(cable, me->globalPosition().toPoint());
                    return true;
                }
                onEmptySpaceRightClicked(me->globalPosition().toPoint());
                return true;
            }

            if (me->button() == Qt::MiddleButton) {
                panning_ = true;
                panStartPos_ = me->pos();
                panStartHScroll_ = view_->horizontalScrollBar()->value();
                panStartVScroll_ = view_->verticalScrollBar()->value();
                view_->setCursor(Qt::ClosedHandCursor);
                return true;
            }

            if (me->button() == Qt::LeftButton) {
                if (auto* jack = findJackAt(scenePos, false)) {
                    markCable(nullptr);
                    startCableDrag(jack, scenePos);
                    return true;
                }
                if (auto* cable = cableEndAt(scenePos)) {
                    markCable(nullptr);
                    pickUpCable(cable, scenePos);
                    return true;
                }
                if (auto* cable = cableAt(scenePos)) {
                    scene_->clearSelection();
                    markCable(cable);
                    view_->setFocus();
                    return true;
                }
                markCable(nullptr);
            }
        }

        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* me = static_cast<QMouseEvent*>(event);
            QPointF scenePos = view_->mapToScene(me->pos());
            if (auto* node = nodeAt(scenePos)) {
                if (node->nodeType() == NodeType::OutputDevice) {
                    emit audioSettingsRequested();
                    return true;
                }
            } else if (auto* cable = cableAt(scenePos)) {
                auto* srcTrack = cable->sourceJack()->parentNode()->track();
                if (cable->kind() == CableKind::Send && srcTrack) {
                    SendsPanel::showFor(editMgr_, srcTrack, me->globalPosition().toPoint(), window());
                    return true;
                }
            }
        }

        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (dragCable_) {
                updateCableDrag(view_->mapToScene(me->pos()));
                return true;
            }
            if (panning_) {
                QPoint delta = me->pos() - panStartPos_;
                view_->horizontalScrollBar()->setValue(panStartHScroll_ - delta.x());
                view_->verticalScrollBar()->setValue(panStartVScroll_ - delta.y());
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && dragCable_) {
                finishCableDrag(view_->mapToScene(me->pos()), me->globalPosition().toPoint());
                return true;
            }
            if (me->button() == Qt::MiddleButton && panning_) {
                panning_ = false;
                view_->setCursor(Qt::ArrowCursor);
                return true;
            }
        }

        if (event->type() == QEvent::Wheel) {
            auto* we = static_cast<QWheelEvent*>(event);
            if (we->modifiers() & Qt::ControlModifier) {
                double factor = (we->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
                zoomBy(factor, we->position().toPoint());
                return true;
            }
            // Scrolling over a send changes its level instead of the view.
            auto* cable = cableAt(view_->mapToScene(we->position().toPoint()));
            if (cable && cable->kind() == CableKind::Send && we->angleDelta().y() != 0) {
                nudgeSendLevel(cable, we->angleDelta().y() > 0 ? 1.0f : -1.0f);
                return true;
            }
        }
    }

    if (watched == view_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);

        if (ke->matches(QKeySequence::SelectAll)) {
            selectAllNodes();
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            if (dragCable_)
                cancelCableDrag();
            else {
                markCable(nullptr);
                deselectAllNodes();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            deleteSelection();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void RoutingView::markCable(CableItem* cable)
{
    if (markedCable_ && markedCable_ != cable)
        markedCable_->setMarked(false);
    markedCable_ = cable;
    if (cable)
        cable->setMarked(true);
}

void RoutingView::nudgeSendLevel(CableItem* cable, float deltaDb)
{
    auto* srcTrack = cable->sourceJack()->parentNode()->track();
    auto* busTrack = cable->destJack()->parentNode()->track();
    if (!srcTrack || !busTrack || !editMgr_->edit()) return;
    for (const auto& send : editMgr_->getTrackSends(srcTrack)) {
        if (send.bus != busTrack) continue;
        // One undo step per burst of scrolling.
        if (!lastSendWheel_.isValid() || lastSendWheel_.elapsed() > 700)
            editMgr_->edit()->getUndoManager().beginNewTransaction("Send Level");
        lastSendWheel_.restart();
        const float from = std::max(send.gainDb, -60.0f);
        editMgr_->setSendGainDb(*srcTrack, *busTrack, std::round(from + deltaDb));
        break;
    }
}

// ── Cable dragging ───────────────────────────────────────────────────────────

void RoutingView::startCableDrag(JackItem* fromJack, const QPointF& scenePos)
{
    dragSourceJack_ = fromJack;
    auto* src = fromJack->parentNode();
    const bool isSend = fromJack->jackIndex() == 1 && src->track();

    dragCable_ = new CableItem(fromJack, nullptr, src->categoryColor(),
                               isSend ? CableKind::Send : CableKind::Main);
    dragCable_->setDangling(true);
    dragCable_->setDanglingEnd(scenePos);
    scene_->addItem(dragCable_);

    // Show at once which inputs can take this cable.
    for (auto* node : allNodes())
        for (int i = 0; i < node->inputJackCount(); ++i) {
            auto* jack = node->inputJack(i);
            const bool isOwnEnd = pickedCable_ && pickedCable_->destJack() == jack;
            jack->setDimmed(!isOwnEnd && !planConnection(fromJack, jack).valid);
        }

    updateCableDrag(scenePos);
}

void RoutingView::pickUpCable(CableItem* cable, const QPointF& scenePos)
{
    pickedCable_ = cable;
    cable->setVisible(false);
    startCableDrag(cable->sourceJack(), scenePos);
}

void RoutingView::updateCableDrag(const QPointF& scenePos)
{
    if (!dragCable_) return;
    dragCable_->setDanglingEnd(scenePos);
    startCablePhysics();

    auto* target = findJackAt(scenePos, true);
    if (hoverTarget_ && hoverTarget_ != target)
        hoverTarget_->setHighlighted(false);
    hoverTarget_ = target;

    if (target && pickedCable_ && target == pickedCable_->destJack()) {
        target->setHighlighted(true);
        showHint("Leave it where it was", true, scenePos);
        return;
    }
    if (target) {
        const auto plan = planConnection(dragSourceJack_, target);
        target->setHighlighted(plan.valid);
        showHint(plan.description.isEmpty() ? QStringLiteral("Can't connect here") : plan.description,
                 plan.valid, scenePos);
        return;
    }

    auto* src = dragSourceJack_->parentNode();
    if (pickedCable_)
        showHint("Let go here to unplug", true, scenePos);
    else if (src->nodeType() == NodeType::InputSource)
        showHint("Drop on a track's IN to record from " + src->name(), true, scenePos);
    else if (dragSourceJack_->jackIndex() == 1)
        showHint("Drop on a bus to send to it, or on empty space for a new effects bus", true, scenePos);
    else if (src->nodeType() == NodeType::Master)
        showHint("Master plays through the main output", false, scenePos);
    else
        showHint("Drop on a bus, Master or an output, or on empty space for a new bus", true, scenePos);
}

void RoutingView::finishCableDrag(const QPointF& scenePos, const QPoint& globalPos)
{
    if (!dragCable_ || !dragSourceJack_) {
        cancelCableDrag();
        return;
    }

    auto* target = findJackAt(scenePos, true);
    auto* from = dragSourceJack_;
    auto* picked = pickedCable_;

    if (picked) {
        if (target == picked->destJack()) {
            cancelCableDrag();
            return;
        }
        if (!target) {
            disconnectCable(picked);
            cancelCableDrag();
            return;
        }
        auto plan = planConnection(from, target);
        if (!plan.valid) {
            cancelCableDrag();
            return;
        }
        auto* srcTrack = from->parentNode()->track();
        auto* oldDst = picked->destJack()->parentNode();
        switch (picked->kind()) {
            case CableKind::Send: {
                // Moving a send keeps its level and settings.
                EditManager::SendInfo old;
                for (const auto& s : editMgr_->getTrackSends(srcTrack))
                    if (s.bus == oldDst->track()) old = s;
                auto* newBus = target->parentNode()->track();
                if (srcTrack && newBus && target->jackIndex() == 0 && old.bus) {
                    editMgr_->removeSend(*srcTrack, *old.bus);
                    editMgr_->addSend(*srcTrack, *newBus, old.gainDb, old.preFader);
                    if (old.muted) editMgr_->setSendMuted(*srcTrack, *newBus, true);
                    if (old.phaseInverted) editMgr_->setSendPhaseInverted(*srcTrack, *newBus, true);
                } else {
                    plan.apply();
                }
                break;
            }
            case CableKind::Sidechain:
                disconnectCable(picked);
                plan.apply();
                break;
            case CableKind::Main:
                if (from->parentNode()->nodeType() == NodeType::InputSource && oldDst->track())
                    editMgr_->clearTrackInput(*oldDst->track());
                plan.apply();   // a new output replaces the old one
                break;
            case CableKind::Device:
                break;
        }
        cancelCableDrag();
        return;
    }

    if (!target) {
        hideHint();
        showDropMenu(from, scenePos, globalPos);
        cancelCableDrag();
        return;
    }

    auto plan = planConnection(from, target);
    cancelCableDrag();
    if (plan.valid && plan.apply)
        plan.apply();
}

void RoutingView::cancelCableDrag()
{
    if (dragCable_) {
        scene_->removeItem(dragCable_);
        delete dragCable_;
        dragCable_ = nullptr;
    }
    if (pickedCable_) {
        pickedCable_->setVisible(true);
        pickedCable_ = nullptr;
    }
    if (hoverTarget_) {
        hoverTarget_->setHighlighted(false);
        hoverTarget_ = nullptr;
    }
    for (auto* node : allNodes())
        for (int i = 0; i < node->inputJackCount(); ++i)
            node->inputJack(i)->setDimmed(false);
    dragSourceJack_ = nullptr;
    hideHint();
}

void RoutingView::showHint(const QString& text, bool valid, const QPointF& scenePos)
{
    if (!hint_) {
        hint_ = new RoutingHintItem();
        scene_->addItem(hint_);
    }
    hint_->setHint(text, valid);
    hint_->setPos(scenePos);
    hint_->setVisible(true);
}

void RoutingView::hideHint()
{
    if (hint_)
        hint_->setVisible(false);
}

void RoutingView::placeNewNodeAt(te::AudioTrack* track, const QPointF& scenePos)
{
    // Put the new node's IN jack under the drop point.
    if (track)
        savedPositions_["track:" + QString::number(track->itemID.getRawID())]
            = scenePos - QPointF(RoutingNode::JACK_OUTER_DIAMETER, 60.0);
}

void RoutingView::showDropMenu(JackItem* from, const QPointF& scenePos, const QPoint& globalPos)
{
    auto* src = from->parentNode();
    auto* srcTrack = src->track();
    QMenu menu;
    menu.setAccessibleName("Cable Drop Menu");

    if (src->nodeType() == NodeType::InputSource) {
        auto* title = menu.addAction("Record " + src->name() + " on");
        title->setEnabled(false);
        for (auto* node : trackNodes_) {
            auto plan = planConnection(from, node->inputJack(0));
            if (plan.valid)
                menu.addAction(node->name(), this, plan.apply);
        }
    } else if (srcTrack && from->jackIndex() == 1) {
        for (const QString fx : { QStringLiteral("Reverb"), QStringLiteral("Delay"),
                                  QStringLiteral("Chorus") }) {
            menu.addAction("Send to a New " + fx + " Bus", this, [this, srcTrack, fx, scenePos]() {
                if (auto* bus = editMgr_->addFxBus(fx)) {
                    placeNewNodeAt(bus, scenePos);
                    editMgr_->addSend(*srcTrack, *bus);
                }
            });
        }
        menu.addAction("Send to a New Empty Bus", this, [this, srcTrack, scenePos]() {
            if (auto* bus = editMgr_->addBusTrack()) {
                placeNewNodeAt(bus, scenePos);
                editMgr_->addSend(*srcTrack, *bus);
            }
        });
        bool first = true;
        for (auto* node : busNodes_) {
            auto plan = planConnection(from, node->inputJack(0));
            if (!plan.valid) continue;
            if (first) { menu.addSeparator(); first = false; }
            menu.addAction("Send to " + node->name(), this, plan.apply);
        }
    } else if (srcTrack) {
        menu.addAction("Route into a New Bus", this, [this, srcTrack, scenePos]() {
            if (auto* bus = editMgr_->addBusTrack()) {
                placeNewNodeAt(bus, scenePos);
                editMgr_->setTrackOutputToTrack(*srcTrack, *bus);
            }
        });
        menu.addSeparator();
        for (auto* node : busNodes_) {
            auto plan = planConnection(from, node->inputJack(0));
            if (plan.valid)
                menu.addAction("Route into " + node->name(), this, plan.apply);
        }
        if (masterNode_) {
            auto plan = planConnection(from, masterNode_->inputJack(0));
            if (plan.valid)
                menu.addAction("Route to Master", this, plan.apply);
        }
        menu.addAction("Unplug Output", this, [this, srcTrack]() {
            editMgr_->clearTrackOutput(*srcTrack);
        });
    }

    if (!menu.isEmpty())
        menu.exec(globalPos);
}

// ── Context menus ────────────────────────────────────────────────────────────

void RoutingView::onCableRightClicked(CableItem* cable, const QPoint& screenPos)
{
    auto* src = cable->sourceJack()->parentNode();
    auto* dst = cable->destJack()->parentNode();
    te::AudioTrack* srcTrack = src->track();
    te::AudioTrack* dstTrack = dst->track();
    markCable(cable);

    QMenu menu;
    menu.setAccessibleName("Cable Context Menu");

    switch (cable->kind()) {
        case CableKind::Send: {
            if (!srcTrack || !dstTrack) break;
            EditManager::SendInfo info;
            for (const auto& s : editMgr_->getTrackSends(srcTrack))
                if (s.bus == dstTrack) info = s;
            auto* title = menu.addAction(src->name() + " → " + dst->name() + ": "
                                         + formatDb(info.gainDb));
            title->setEnabled(false);
            auto* levels = menu.addMenu("Level");
            for (float db : { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f, -30.0f }) {
                auto* a = levels->addAction(formatDb(db), this, [this, srcTrack, dstTrack, db]() {
                    if (editMgr_->edit())
                        editMgr_->edit()->getUndoManager().beginNewTransaction("Send Level");
                    editMgr_->setSendGainDb(*srcTrack, *dstTrack, db);
                });
                a->setCheckable(true);
                a->setChecked(std::abs(info.gainDb - db) < 0.05f);
            }
            auto* pre = menu.addAction("Pre-Fader", this, [this, srcTrack, dstTrack, info]() {
                editMgr_->setSendPreFader(*srcTrack, *dstTrack, !info.preFader);
            });
            pre->setCheckable(true);
            pre->setChecked(info.preFader);
            auto* mute = menu.addAction("Mute Send", this, [this, srcTrack, dstTrack, info]() {
                editMgr_->setSendMuted(*srcTrack, *dstTrack, !info.muted);
            });
            mute->setCheckable(true);
            mute->setChecked(info.muted);
            auto* phase = menu.addAction("Invert Phase", this, [this, srcTrack, dstTrack, info]() {
                editMgr_->setSendPhaseInverted(*srcTrack, *dstTrack, !info.phaseInverted);
            });
            phase->setCheckable(true);
            phase->setChecked(info.phaseInverted);
            menu.addSeparator();
            menu.addAction("All Sends from " + src->name() + "...", this, [this, srcTrack, screenPos]() {
                SendsPanel::showFor(editMgr_, srcTrack, screenPos, window());
            });
            menu.addAction("Remove Send", this, [this, cable]() { disconnectCable(cable); });
            break;
        }
        case CableKind::Sidechain:
            menu.addAction("Remove Sidechain Connection", this, [this, cable]() { disconnectCable(cable); });
            break;
        case CableKind::Device:
            menu.addAction("Audio Settings...", this, [this]() { emit audioSettingsRequested(); });
            break;
        case CableKind::Main:
            if (src->nodeType() == NodeType::InputSource) {
                menu.addAction("Remove Input Connection", this, [this, cable]() { disconnectCable(cable); });
            } else if (srcTrack) {
                if (dst->nodeType() != NodeType::Master)
                    menu.addAction("Route to Master Instead", this, [this, srcTrack]() {
                        editMgr_->setTrackOutputToMaster(*srcTrack);
                    });
                menu.addAction("Unplug (Silences " + src->name() + ")", this,
                               [this, cable]() { disconnectCable(cable); });
            }
            break;
    }

    menu.exec(screenPos);
    markCable(nullptr);
}

void RoutingView::onNodeRightClicked(RoutingNode* node, const QPoint& screenPos)
{
    if (!node->isSelected()) {
        scene_->clearSelection();
        node->setSelected(true);
    }

    QMenu menu;
    menu.setAccessibleName("Routing Node Menu");
    auto* track = node->track();

    if (node->nodeType() == NodeType::Track || node->nodeType() == NodeType::Bus) {
        std::vector<te::AudioTrack*> selected;
        for (auto* item : scene_->selectedItems())
            if (auto* n = dynamic_cast<RoutingNode*>(item))
                if (n->track()) selected.push_back(n->track());

        if (selected.size() > 1) {
            const QString count = QString::number(selected.size());
            menu.addAction("Route " + count + " Selected into a New Bus", this, [this, selected]() {
                if (editMgr_->edit())
                    editMgr_->edit()->getUndoManager().beginNewTransaction("Group into Bus");
                if (auto* bus = editMgr_->addBusTrack())
                    for (auto* t : selected)
                        if (!editMgr_->wouldCreateCycle(t, bus))
                            editMgr_->setTrackOutputToTrack(*t, *bus);
            });
            auto* sendAll = menu.addMenu("Send " + count + " Selected to");
            for (auto* busNode : busNodes_) {
                auto* bus = busNode->track();
                sendAll->addAction(busNode->name(), this, [this, selected, bus]() {
                    for (auto* t : selected)
                        if (editMgr_->canSendTo(t, bus))
                            editMgr_->addSend(*t, *bus);
                });
            }
            sendAll->addAction("A New Reverb Bus", this, [this, selected]() {
                if (auto* bus = editMgr_->addFxBus("Reverb"))
                    for (auto* t : selected)
                        if (editMgr_->canSendTo(t, bus))
                            editMgr_->addSend(*t, *bus);
            });
            menu.addSeparator();
        }

        auto* outMenu = menu.addMenu("Output");
        const QString current = editMgr_->currentOutputKey(track);
        for (const auto& choice : editMgr_->getOutputChoices(track)) {
            auto* a = outMenu->addAction(choice.label, this, [this, track, key = choice.key]() {
                editMgr_->setTrackOutputByKey(*track, key);
            });
            a->setCheckable(true);
            a->setChecked(choice.key == current);
        }
        outMenu->addSeparator();
        outMenu->addAction("A New Bus", this, [this, track]() {
            if (auto* bus = editMgr_->addBusTrack())
                editMgr_->setTrackOutputToTrack(*track, *bus);
        });

        if (node->nodeType() == NodeType::Track && !editMgr_->isMidiTrack(track)) {
            auto* inMenu = menu.addMenu("Input");
            const QString currentInput = editMgr_->getTrackInputName(track);
            for (const auto& src : editMgr_->getAvailableInputSources()) {
                auto* a = inMenu->addAction(src.displayName, this, [this, track, dev = src.deviceName]() {
                    editMgr_->assignInputToTrack(*track, dev);
                });
                a->setCheckable(true);
                a->setChecked(QString::fromStdString(src.deviceName.toStdString()) == currentInput);
            }
            auto* none = inMenu->addAction("None", this, [this, track]() { editMgr_->clearTrackInput(*track); });
            none->setCheckable(true);
            none->setChecked(currentInput.isEmpty());
        }

        menu.addAction("Sends...", this, [this, track, screenPos]() {
            SendsPanel::showFor(editMgr_, track, screenPos, window());
        });
        SendsPanel::fillAddSendMenu(menu.addMenu("Add Send To"), editMgr_, track);
        menu.addSeparator();

        auto* mute = menu.addAction("Mute", this, [this, track]() {
            track->setMute(!track->isMuted(false));
            emit editMgr_->editChanged();
        });
        mute->setCheckable(true);
        mute->setChecked(track->isMuted(false));
        auto* solo = menu.addAction("Solo", this, [this, track]() {
            track->setSolo(!track->isSolo(false));
            emit editMgr_->editChanged();
        });
        solo->setCheckable(true);
        solo->setChecked(track->isSolo(false));
        menu.addAction("Show Effects", this, [this, track]() { emit trackSelected(track); });
        menu.addSeparator();
        menu.addAction("Rename", this, [node]() { node->startRename(); });
        if (node->nodeType() == NodeType::Bus)
            menu.addAction("Delete Bus", this, [this, track]() { editMgr_->removeTrack(track); });
    } else if (node->nodeType() == NodeType::Master) {
        menu.addAction("Show Master Effects", this, [this]() { emit masterSelected(); });
    } else if (node->nodeType() == NodeType::InputSource) {
        auto* rec = menu.addMenu("Record on");
        for (auto* tNode : trackNodes_) {
            auto plan = planConnection(node->outputJack(0), tNode->inputJack(0));
            if (plan.valid)
                rec->addAction(tNode->name(), this, plan.apply);
        }
        menu.addAction("Rename", this, [node]() { node->startRename(); });
    } else if (node->nodeType() == NodeType::OutputDevice) {
        menu.addAction("Audio Settings...", this, [this]() { emit audioSettingsRequested(); });
    }

    menu.exec(screenPos);
}

void RoutingView::onEmptySpaceRightClicked(const QPoint& screenPos)
{
    QMenu menu;
    menu.setAccessibleName("Routing Canvas Menu");

    menu.addAction("Add Bus", this, [this]() { editMgr_->addBusTrack(); });
    auto* fx = menu.addMenu("Add Effects Bus");
    for (const QString name : { QStringLiteral("Reverb"), QStringLiteral("Delay"),
                                QStringLiteral("Chorus"), QStringLiteral("Phaser") })
        fx->addAction(name, this, [this, name]() { editMgr_->addFxBus(name); });

    menu.addSeparator();
    menu.addAction("Select All", this, [this]() { selectAllNodes(); });
    menu.addAction("Deselect All", this, [this]() { deselectAllNodes(); });
    menu.addSeparator();
    menu.addAction("Auto Layout", this, [this]() {
        layoutNodes();
        updateAllCablePaths();
        updateSceneRect();
    });
    menu.addAction("Zoom to Fit", this, [this]() { zoomToFit(); });

    menu.exec(screenPos);
}

void RoutingView::selectAllNodes()
{
    for (auto* node : allNodes())
        node->setSelected(true);
}

void RoutingView::deselectAllNodes()
{
    scene_->clearSelection();
}

void RoutingView::deleteSelection()
{
    if (markedCable_) {
        auto* cable = markedCable_;
        markCable(nullptr);
        disconnectCable(cable);
        return;
    }
    std::vector<te::AudioTrack*> toDelete;
    for (auto* item : scene_->selectedItems()) {
        if (auto* node = dynamic_cast<RoutingNode*>(item)) {
            if (node->nodeType() == NodeType::Bus && node->track())
                toDelete.push_back(node->track());
        }
    }
    for (auto* track : toDelete)
        editMgr_->removeTrack(track);
}

void RoutingView::onSceneSelectionChanged()
{
    te::AudioTrack* found = nullptr;
    int trackBusCount = 0;
    bool masterNodeSelected = false;
    for (auto* item : scene_->selectedItems()) {
        if (auto* node = dynamic_cast<RoutingNode*>(item)) {
            if ((node->nodeType() == NodeType::Track ||
                 node->nodeType() == NodeType::Bus) && node->track()) {
                found = node->track();
                if (++trackBusCount > 1) break;
            }
            if (node->nodeType() == NodeType::Master)
                masterNodeSelected = true;
        }
    }
    if (trackBusCount == 1 && found)
        emit trackSelected(found);
    else if (trackBusCount == 0 && masterNodeSelected)
        emit masterSelected();
}

void RoutingView::zoomBy(double factor, const QPoint& viewAnchor)
{
    QPointF sceneAnchor = view_->mapToScene(viewAnchor);
    view_->scale(factor, factor);
    QPointF newAnchor = view_->mapFromScene(sceneAnchor);
    QPointF delta = QPointF(viewAnchor) - newAnchor;
    view_->horizontalScrollBar()->setValue(
        view_->horizontalScrollBar()->value() - static_cast<int>(delta.x()));
    view_->verticalScrollBar()->setValue(
        view_->verticalScrollBar()->value() - static_cast<int>(delta.y()));
}

void RoutingView::updateSceneRect()
{
    constexpr qreal PADDING = 800.0;

    QRectF itemsRect;
    for (auto* n : allNodes())
        itemsRect = itemsRect.united(n->sceneBoundingRect());

    if (itemsRect.isNull())
        itemsRect = QRectF(0, 0, 1000, 600);

    scene_->setSceneRect(itemsRect.adjusted(-PADDING, -PADDING, PADDING, PADDING));
}

void RoutingView::zoomToFit()
{
    QRectF itemsRect;
    for (auto* n : allNodes())
        itemsRect = itemsRect.united(n->sceneBoundingRect());

    if (itemsRect.isNull()) return;

    constexpr qreal FIT_MARGIN = 40.0;
    itemsRect.adjust(-FIT_MARGIN, -FIT_MARGIN, FIT_MARGIN, FIT_MARGIN);
    view_->fitInView(itemsRect, Qt::KeepAspectRatio);
    updateSceneRect();
}

} // namespace OpenDaw
