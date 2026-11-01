#pragma once

#include "engine/EditManager.h"
#include <QGraphicsRectItem>
#include <QWidget>
#include <functional>
#include <vector>

namespace OpenDaw {

class GridSnapper;

// One take of a track, in a lane under the track. Drag across it to comp that
// part onto the track; double-click to use all of it. The parts already in
// the comp are drawn brighter.
class TakeLaneItem : public QGraphicsRectItem {
public:
    static constexpr double kLaneHeight = 40.0;

    TakeLaneItem(EditManager* editMgr, te::AudioTrack* track,
                 const EditManager::TakeInfo& take, double pixelsPerBeat,
                 GridSnapper* snapper);

    void setSceneWidth(double width);

    enum { Type = QGraphicsItem::UserType + 7 };
    int type() const override { return Type; }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    double timeToX(double secs) const;
    double xToTime(double x) const;
    // Scene x snapped to the grid and kept inside the take.
    double snappedX(double x) const;
    // Edits rebuild the lanes, so they run after this item's event returns.
    void runDeferred(std::function<void(EditManager&, te::AudioTrack&)> action) const;

    EditManager* editMgr_;
    te::EditItemID trackId_;
    QColor trackColour_;
    EditManager::TakeInfo take_;
    double pixelsPerBeat_;
    GridSnapper* snapper_;

    std::vector<float> waveMin_, waveMax_;
    double fileSecs_ = 0.0;
    std::vector<std::pair<double, double>> usedRanges_;

    bool selecting_ = false;
    double selStartX_ = 0.0;
    double selEndX_ = 0.0;
};

// Beside the take lanes: each take's name, with buttons to use all of it or
// delete it.
class TakeLanesHeader : public QWidget {
    Q_OBJECT

public:
    TakeLanesHeader(EditManager* editMgr, te::AudioTrack* track, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    int rowAt(int y) const;
    QRect useRect(int row) const;
    QRect deleteRect(int row) const;

    EditManager* editMgr_;
    te::EditItemID trackId_;
    std::vector<EditManager::TakeInfo> takes_;
    std::vector<bool> inComp_;
    int hoverRow_ = -1;
};

} // namespace OpenDaw
