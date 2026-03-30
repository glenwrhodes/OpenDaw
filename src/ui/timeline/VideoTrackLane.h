#pragma once

#include "engine/EditManager.h"
#include "video/VideoThumbnailCache.h"
#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QTimer>
#include <memory>

namespace OpenDaw {

class VideoTrackLane : public QWidget {
    Q_OBJECT

public:
    explicit VideoTrackLane(EditManager* editMgr, QWidget* parent = nullptr);
    ~VideoTrackLane() override;

    void setPixelsPerBeat(double ppb);
    void setScrollX(int scrollX);
    void setPlayheadBeat(double beat);
    void refresh();
    void forceRebuildFilmstrip();

    bool loadVideo(const QString& filePath);
    void closeVideo();
    bool hasVideo() const;
    QString videoFilePath() const;

    VideoThumbnailCache* thumbnailCache() { return cache_.get(); }

signals:
    void videoLoaded(const QString& filePath);
    void videoClosed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    int beatToX(double beat) const;
    double beatToSeconds(double beat) const;
    double secondsToBeats(double sec) const;
    void rebuildFilmstrip();

    EditManager* editMgr_;
    std::unique_ptr<VideoThumbnailCache> cache_;
    double pixelsPerBeat_ = 40.0;
    int scrollX_ = 0;
    double playheadBeat_ = 0.0;

    double lastRenderPpb_ = 0.0;
    int lastRenderHeight_ = 0;
    QTimer rebuildTimer_;

    static constexpr int kDefaultHeight = 100;
    static constexpr int kMinHeight = 50;
    static constexpr int kMaxHeight = 250;
    static constexpr int kResizeGrabHeight = 4;

    int laneHeight_ = kDefaultHeight;

    enum class DragMode { None, ResizeHeight };
    DragMode dragMode_ = DragMode::None;
    int resizeStartY_ = 0;
    int resizeStartHeight_ = 0;
};

} // namespace OpenDaw
