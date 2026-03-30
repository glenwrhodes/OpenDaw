#pragma once

#include <QImage>
#include <QString>
#include <string>
#include <mutex>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace OpenDaw {

struct VideoInfo {
    int width = 0;
    int height = 0;
    double durationSeconds = 0.0;
    double frameRate = 0.0;
    bool valid = false;
};

struct FilmstripFrame {
    double timeSec;
    QImage image;
};

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const QString& filePath);
    void close();
    bool isOpen() const;

    VideoInfo info() const;

    QImage extractFrame(double timeSeconds, QSize targetSize);

    std::vector<FilmstripFrame> decodeFilmstrip(
        const std::vector<double>& times,
        QSize targetSize,
        std::atomic<uint64_t>& renderId,
        uint64_t myId,
        std::function<void(int done, int total)> progress = nullptr);

private:
    AVFormatContext* fmtCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    int videoStreamIndex_ = -1;
    VideoInfo info_;
    QSize lastTargetSize_;
    std::mutex mutex_;

    void resetSwsContext(int srcW, int srcH, QSize targetSize);
    QImage frameToImage(QSize targetSize);
};

bool transcodeToMp3(const QString& inputWavPath,
                    const QString& outputMp3Path,
                    int bitrate,
                    std::function<void(float)> progressCallback = nullptr);

} // namespace OpenDaw
