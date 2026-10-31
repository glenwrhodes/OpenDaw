#pragma once

#include "engine/EditManager.h"
#include <QWidget>
#include <QTimer>

namespace OpenDaw {

// Routing as a grid, for patching many tracks quickly: one row per track or
// bus; "Output to" columns (one choice per row) and "Send to" columns (any
// number, each with its own level).
class RoutingMatrixView : public QWidget {
    Q_OBJECT

public:
    explicit RoutingMatrixView(EditManager* editMgr, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void trackSelected(te::AudioTrack* track);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    struct Row {
        te::AudioTrack* track = nullptr;
        QString name;
        QColor colour;
        bool isBus = false;
        QString outputKey;
        QStringList validOutputs;
    };
    struct Column {
        bool isSend = false;
        QString key;                  // output key, or the bus's "track:<id>" for sends
        te::AudioTrack* bus = nullptr;
        QString label;
    };
    struct Cell {
        int row = -1;
        int col = -1;
        bool valid() const { return row >= 0 && col >= 0; }
    };

    void reload();
    void scheduleReload();
    Cell cellAt(const QPoint& pos) const;
    QRect cellRect(int row, int col) const;
    bool cellEnabled(int row, int col) const;
    const EditManager::SendInfo* sendFor(int row, int col) const;
    QString cellDescription(int row, int col) const;
    void showCellMenu(int row, int col, const QPoint& globalPos);

    EditManager* editMgr_;
    std::vector<Row> rows_;
    std::vector<Column> columns_;
    int firstSendColumn_ = 0;
    std::vector<std::vector<EditManager::SendInfo>> sends_;   // per row
    Cell hover_;
    Cell dragging_;
    int dragStartY_ = 0;
    float dragStartDb_ = 0.0f;
    bool reloadPending_ = false;
    QTimer reloadTimer_;

    static constexpr int kRowHeaderWidth = 170;
    static constexpr int kGroupHeaderHeight = 20;
    static constexpr int kColumnHeaderHeight = 54;
    static constexpr int kRowHeight = 26;
    static constexpr int kColumnWidth = 64;
    static constexpr int kGroupGap = 14;
};

} // namespace OpenDaw
