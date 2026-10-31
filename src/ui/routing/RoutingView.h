#pragma once

#include "RoutingNode.h"
#include "CableItem.h"
#include "engine/EditManager.h"
#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QToolBar>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QMap>
#include <functional>
#include <memory>
#include <vector>

namespace OpenDaw {

class RoutingHintItem;
class RoutingMatrixView;

class RoutingView : public QWidget {
    Q_OBJECT

public:
    explicit RoutingView(EditManager* editMgr, QWidget* parent = nullptr);
    ~RoutingView() override;

signals:
    void trackSelected(te::AudioTrack* track);
    void masterSelected();
    void audioSettingsRequested();

public slots:
    void rebuild();
    void scheduleRebuild();
    void flushNodePositions();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // What dropping a cable from one jack onto another would do, worked out
    // while dragging so the target can be highlighted and described.
    struct Connection {
        bool valid = false;
        QString description;
        std::function<void()> apply;
    };

    struct MeterTap {
        RoutingNode* node = nullptr;
        te::Plugin::Ptr plugin;   // keeps the meter alive until the client is removed
        std::unique_ptr<te::LevelMeasurer::Client> client;
        float level = 0.0f;
        bool isMaster = false;
    };

    void onSceneSelectionChanged();
    void buildNodes();
    void buildCables();
    void layoutNodes();
    void clearAll();
    void updateAllCablePaths();
    void connectNodeSignals(RoutingNode* node);
    void saveNodePositions();
    void restoreNodePositions();
    QString nodeKey(RoutingNode* node) const;
    std::vector<RoutingNode*> allNodes() const;
    RoutingNode* nodeForTrack(te::AudioTrack* track) const;
    RoutingNode* nodeAt(const QPointF& scenePos) const;

    QStringList getTrackEffectNames(te::AudioTrack* track) const;
    QStringList getMasterEffectNames() const;
    bool trackHasSidechainPlugin(te::AudioTrack* track) const;
    te::Plugin* getFirstSidechainPlugin(te::AudioTrack* track) const;

    JackItem* findJackAt(const QPointF& scenePos, bool wantInput) const;
    CableItem* cableAt(const QPointF& scenePos) const;
    CableItem* cableEndAt(const QPointF& scenePos) const;
    Connection planConnection(JackItem* from, JackItem* to) const;
    void disconnectCable(CableItem* cable);
    void updateSendLabels();
    void addCable(JackItem* from, JackItem* to, const QColor& color, CableKind kind);

    bool eventFilter(QObject* watched, QEvent* event) override;

    void startCableDrag(JackItem* fromJack, const QPointF& scenePos);
    void pickUpCable(CableItem* cable, const QPointF& scenePos);
    void updateCableDrag(const QPointF& scenePos);
    void finishCableDrag(const QPointF& scenePos, const QPoint& globalPos);
    void cancelCableDrag();
    void showDropMenu(JackItem* from, const QPointF& scenePos, const QPoint& globalPos);
    void placeNewNodeAt(te::AudioTrack* track, const QPointF& scenePos);
    void showHint(const QString& text, bool valid, const QPointF& scenePos);
    void hideHint();
    void markCable(CableItem* cable);
    void nudgeSendLevel(CableItem* cable, float deltaDb);

    void onCableRightClicked(CableItem* cable, const QPoint& screenPos);
    void onNodeRightClicked(RoutingNode* node, const QPoint& screenPos);
    void onEmptySpaceRightClicked(const QPoint& screenPos);
    void selectAllNodes();
    void deselectAllNodes();
    void deleteSelection();
    void zoomBy(double factor, const QPoint& viewAnchor);
    void updateSceneRect();
    void zoomToFit();
    void startCablePhysics();
    void tickCablePhysics();

    void setMatrixMode(bool matrix);
    void createMeterTaps();
    void removeMeterTaps();
    void tickMeters();

    EditManager* editMgr_;
    QGraphicsScene* scene_;
    QGraphicsView* view_;
    QToolBar* toolbar_;
    QStackedWidget* stack_ = nullptr;
    RoutingMatrixView* matrix_ = nullptr;
    QPushButton* graphBtn_ = nullptr;
    QPushButton* matrixBtn_ = nullptr;
    QPushButton* autoLayoutBtn_ = nullptr;
    QPushButton* zoomFitBtn_ = nullptr;
    QLabel* helpLabel_ = nullptr;

    std::vector<RoutingNode*> inputNodes_;
    std::vector<RoutingNode*> trackNodes_;
    std::vector<RoutingNode*> busNodes_;
    RoutingNode* masterNode_ = nullptr;
    RoutingNode* mainOutputNode_ = nullptr;
    std::vector<RoutingNode*> outputNodes_;
    std::vector<CableItem*> cables_;

    CableItem* dragCable_ = nullptr;
    JackItem* dragSourceJack_ = nullptr;
    CableItem* pickedCable_ = nullptr;     // an existing cable being re-plugged
    JackItem* hoverTarget_ = nullptr;
    CableItem* markedCable_ = nullptr;
    RoutingHintItem* hint_ = nullptr;

    bool panning_ = false;
    QPoint panStartPos_;
    int panStartHScroll_ = 0;
    int panStartVScroll_ = 0;

    bool rebuildPending_ = false;
    QTimer rebuildTimer_;
    QTimer sceneRectTimer_;
    QTimer physicsTimer_;
    QTimer meterTimer_;
    QElapsedTimer lastSendWheel_;
    QMap<QString, QPointF> savedPositions_;
    std::vector<MeterTap> meterTaps_;

    static constexpr qreal COL_INPUTS = 30.0;
    static constexpr qreal COL_TRACKS = 260.0;
    static constexpr qreal COL_BUSES  = 500.0;
    static constexpr qreal COL_MASTER = 740.0;
    static constexpr qreal COL_OUTPUTS = 980.0;
    static constexpr qreal ROW_START = 20.0;
    static constexpr qreal ROW_SPACING = 20.0;
};

} // namespace OpenDaw
