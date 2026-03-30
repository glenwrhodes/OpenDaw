#pragma once

#include "engine/EditManager.h"
#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QCheckBox>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

namespace OpenDaw {

class VideoPlayerWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoPlayerWidget(EditManager* editMgr, QWidget* parent = nullptr);
    ~VideoPlayerWidget() override;

    void setVideoFile(const QString& path);
    void closeVideo();
    bool hasVideo() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void pollTransport();
    void seekTo(double timeSec);
    bool decodeNextFrame();
    QImage convertFrame();
    QString formatTimecode(double seconds, double fps) const;

    EditManager* editMgr_;
    QTimer pollTimer_;
    QCheckBox* timecodeCheck_ = nullptr;

    AVFormatContext* fmtCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    int videoStreamIdx_ = -1;

    double fps_ = 24.0;
    double duration_ = 0.0;
    int videoW_ = 0;
    int videoH_ = 0;

    QImage currentFrame_;
    double lastDisplayTime_ = -1.0;
    double lastDecodedPts_ = -1.0;
    double videoOffset_ = 0.0;
    bool showTimecode_ = true;
    bool wasPlaying_ = false;

    static constexpr double kSeekThreshold = 0.5;
};

} // namespace OpenDaw
