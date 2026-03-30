#pragma once

#include "utils/FFmpegUtils.h"
#include <QObject>
#include <QPixmap>
#include <QSize>
#include <memory>
#include <atomic>
#include <vector>

namespace OpenDaw {

class VideoThumbnailCache : public QObject {
    Q_OBJECT

public:
    explicit VideoThumbnailCache(QObject* parent = nullptr);
    ~VideoThumbnailCache() override;

    bool openVideo(const QString& filePath);
    void closeVideo();
    bool isOpen() const;
    VideoInfo videoInfo() const;
    QString filePath() const { return filePath_; }

    void renderFilmstrip(const std::vector<double>& frameTimes,
                         QSize frameSize, double ppb);
    void cancelRender();

    QPixmap filmstrip() const { return filmstrip_; }
    int filmstripThumbWidth() const { return thumbWidth_; }
    double filmstripPpb() const { return renderedPpb_; }
    bool isRendering() const { return rendering_.load(); }

signals:
    void filmstripReady();
    void renderProgress(int done, int total);
    void videoOpened(const QString& filePath);
    void videoClosed();

private:
    std::unique_ptr<VideoDecoder> decoder_;
    QString filePath_;
    QPixmap filmstrip_;
    int thumbWidth_ = 0;
    double renderedPpb_ = 0.0;
    std::atomic<bool> rendering_{false};
    std::atomic<uint64_t> renderId_{0};
};

} // namespace OpenDaw
