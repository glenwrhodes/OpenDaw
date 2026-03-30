#include "VideoPlayerWidget.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <cmath>
#include <algorithm>
#include <thread>

extern "C" {
#include <libavutil/imgutils.h>
}

namespace OpenDaw {

VideoPlayerWidget::VideoPlayerWidget(EditManager* editMgr, QWidget* parent)
    : QWidget(parent)
    , editMgr_(editMgr)
{
    setAccessibleName("Video Player");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(28);
    toolbar->setAutoFillBackground(true);
    auto& theme = ThemeManager::instance().current();
    QPalette tbPal;
    tbPal.setColor(QPalette::Window, theme.surface);
    toolbar->setPalette(tbPal);

    auto* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(8, 2, 8, 2);
    tbLayout->setSpacing(6);

    timecodeCheck_ = new QCheckBox("Show Timecode", toolbar);
    timecodeCheck_->setAccessibleName("Toggle Timecode Overlay");
    timecodeCheck_->setChecked(true);
    timecodeCheck_->setStyleSheet(
        QString("QCheckBox { color: %1; font-size: 11px; }")
            .arg(theme.text.name()));
    connect(timecodeCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        showTimecode_ = checked;
        update();
    });
    tbLayout->addWidget(timecodeCheck_);
    tbLayout->addStretch();

    layout->addWidget(toolbar);
    layout->addStretch();

    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();

    pollTimer_.setInterval(16);
    connect(&pollTimer_, &QTimer::timeout, this, &VideoPlayerWidget::pollTransport);
    pollTimer_.start();
}

VideoPlayerWidget::~VideoPlayerWidget()
{
    closeVideo();
    av_frame_free(&frame_);
    av_packet_free(&packet_);
}

void VideoPlayerWidget::setVideoFile(const QString& path)
{
    closeVideo();

    if (path.isEmpty()) {
        update();
        return;
    }

    std::string p = path.toStdString();
    if (avformat_open_input(&fmtCtx_, p.c_str(), nullptr, nullptr) < 0)
        return;

    if (avformat_find_stream_info(fmtCtx_, nullptr) < 0) {
        avformat_close_input(&fmtCtx_);
        return;
    }

    videoStreamIdx_ = -1;
    for (unsigned i = 0; i < fmtCtx_->nb_streams; ++i) {
        if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx_ = static_cast<int>(i);
            break;
        }
    }
    if (videoStreamIdx_ < 0) {
        avformat_close_input(&fmtCtx_);
        return;
    }

    auto* codecPar = fmtCtx_->streams[videoStreamIdx_]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx_);
        return;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx_, codecPar);
    codecCtx_->thread_count = std::min(4, static_cast<int>(std::thread::hardware_concurrency()));
    codecCtx_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx_);
        avformat_close_input(&fmtCtx_);
        return;
    }

    videoW_ = codecCtx_->width;
    videoH_ = codecCtx_->height;

    auto* stream = fmtCtx_->streams[videoStreamIdx_];
    if (stream->avg_frame_rate.den > 0)
        fps_ = av_q2d(stream->avg_frame_rate);
    else
        fps_ = 24.0;

    if (stream->duration > 0 && stream->time_base.den > 0)
        duration_ = stream->duration * av_q2d(stream->time_base);
    else if (fmtCtx_->duration > 0)
        duration_ = static_cast<double>(fmtCtx_->duration) / AV_TIME_BASE;

    lastDecodedPts_ = -1.0;
    lastDisplayTime_ = -1.0;
    wasPlaying_ = false;

    update();
}

void VideoPlayerWidget::closeVideo()
{
    if (swsCtx_) { sws_freeContext(swsCtx_); swsCtx_ = nullptr; }
    if (codecCtx_) { avcodec_free_context(&codecCtx_); }
    if (fmtCtx_) { avformat_close_input(&fmtCtx_); }
    videoStreamIdx_ = -1;
    currentFrame_ = {};
    lastDisplayTime_ = -1.0;
    lastDecodedPts_ = -1.0;
    videoW_ = videoH_ = 0;
    duration_ = 0.0;
    update();
}

bool VideoPlayerWidget::hasVideo() const
{
    return fmtCtx_ != nullptr && videoStreamIdx_ >= 0;
}

void VideoPlayerWidget::seekTo(double timeSec)
{
    if (!fmtCtx_) return;

    auto* stream = fmtCtx_->streams[videoStreamIdx_];
    int64_t ts = static_cast<int64_t>(timeSec / av_q2d(stream->time_base));

    av_seek_frame(fmtCtx_, videoStreamIdx_, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx_);
    lastDecodedPts_ = -1.0;

    int maxPackets = 200;
    while (--maxPackets > 0) {
        if (!decodeNextFrame()) break;
        if (lastDecodedPts_ >= timeSec - 1.0 / fps_)
            break;
    }
}

bool VideoPlayerWidget::decodeNextFrame()
{
    if (!fmtCtx_ || !codecCtx_) return false;

    while (true) {
        int ret = av_read_frame(fmtCtx_, packet_);
        if (ret < 0) return false;

        if (packet_->stream_index != videoStreamIdx_) {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);
        if (ret < 0) continue;

        ret = avcodec_receive_frame(codecCtx_, frame_);
        if (ret == AVERROR(EAGAIN)) continue;
        if (ret < 0) return false;

        auto* stream = fmtCtx_->streams[videoStreamIdx_];
        if (frame_->pts != AV_NOPTS_VALUE)
            lastDecodedPts_ = frame_->pts * av_q2d(stream->time_base);

        return true;
    }
}

QImage VideoPlayerWidget::convertFrame()
{
    if (!frame_ || frame_->width <= 0 || frame_->height <= 0)
        return {};

    int displayH = height() - 28;
    if (displayH < 10) return {};

    double aspect = static_cast<double>(videoW_) / videoH_;
    int outW = static_cast<int>(displayH * aspect);
    int outH = displayH;
    if (outW > width()) {
        outW = width();
        outH = static_cast<int>(outW / aspect);
    }
    if (outW < 1 || outH < 1) return {};

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    swsCtx_ = sws_getContext(frame_->width, frame_->height,
                             codecCtx_->pix_fmt,
                             outW, outH, AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx_) return {};

    QImage img(outW, outH, QImage::Format_RGB888);
    uint8_t* dstData[4] = { img.bits(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };

    sws_scale(swsCtx_, frame_->data, frame_->linesize, 0,
              frame_->height, dstData, dstLinesize);

    return img;
}

void VideoPlayerWidget::pollTransport()
{
    if (!hasVideo() || !editMgr_ || !editMgr_->edit())
        return;

    if (!isVisible()) return;

    auto& transport = editMgr_->transport();
    bool isPlaying = transport.isPlaying();
    double posSec = transport.getPosition().inSeconds();

    videoOffset_ = editMgr_->edit()->videoOffset.get().inSeconds();
    double targetTime = std::clamp(posSec + videoOffset_, 0.0, duration_);

    bool justStarted = isPlaying && !wasPlaying_;
    bool justStopped = !isPlaying && wasPlaying_;
    wasPlaying_ = isPlaying;

    if (std::abs(targetTime - lastDisplayTime_) < 0.5 / fps_ && !justStarted)
        return;

    bool needSeek = justStarted || justStopped
        || std::abs(targetTime - lastDecodedPts_) > kSeekThreshold
        || targetTime < lastDecodedPts_ - 0.1;

    if (needSeek) {
        seekTo(targetTime);
        QImage img = convertFrame();
        if (!img.isNull()) currentFrame_ = std::move(img);
        lastDisplayTime_ = targetTime;
        update();
        return;
    }

    if (isPlaying) {
        int maxFrames = 5;
        while (--maxFrames > 0 && lastDecodedPts_ < targetTime - 0.5 / fps_) {
            if (!decodeNextFrame()) break;
        }
        QImage img = convertFrame();
        if (!img.isNull()) currentFrame_ = std::move(img);
        lastDisplayTime_ = targetTime;
        update();
    }
}

QString VideoPlayerWidget::formatTimecode(double seconds, double fps) const
{
    if (seconds < 0.0) seconds = 0.0;
    int totalSec = static_cast<int>(seconds);
    int hours = totalSec / 3600;
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;
    int frame = static_cast<int>((seconds - totalSec) * fps);

    return QString("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(frame, 2, 10, QChar('0'));
}

void VideoPlayerWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    auto& theme = ThemeManager::instance().current();

    p.fillRect(rect(), theme.background);

    int toolbarH = 28;
    QRect displayArea(0, toolbarH, width(), height() - toolbarH);

    if (!hasVideo()) {
        p.setPen(theme.textDim);
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(displayArea, Qt::AlignCenter, "No video loaded");
        return;
    }

    if (currentFrame_.isNull()) {
        p.setPen(theme.textDim);
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(displayArea, Qt::AlignCenter, "Waiting for playback...");
        return;
    }

    int drawW = currentFrame_.width();
    int drawH = currentFrame_.height();
    int x = displayArea.x() + (displayArea.width() - drawW) / 2;
    int y = displayArea.y() + (displayArea.height() - drawH) / 2;

    p.drawImage(x, y, currentFrame_);

    if (showTimecode_ && lastDisplayTime_ >= 0.0) {
        QString tc = formatTimecode(lastDisplayTime_, fps_);

        QFont tcFont("Consolas", 10);
        tcFont.setBold(true);
        p.setFont(tcFont);

        QFontMetrics fm(tcFont);
        int textW = fm.horizontalAdvance(tc);
        int textH = fm.height();
        int pad = 5;

        QRect pillRect(x + 8, y + drawH - textH - pad * 2 - 8,
                       textW + pad * 2, textH + pad * 2);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(pillRect, 4, 4);

        p.setPen(Qt::white);
        p.drawText(pillRect, Qt::AlignCenter, tc);
    }
}

void VideoPlayerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    lastDisplayTime_ = -1.0;
}

} // namespace OpenDaw
