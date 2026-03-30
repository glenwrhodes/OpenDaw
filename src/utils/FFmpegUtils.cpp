#include "FFmpegUtils.h"
#include <QDebug>
#include <cmath>
#include <algorithm>

namespace OpenDaw {

// ── VideoDecoder ────────────────────────────────────────────────────────────

VideoDecoder::VideoDecoder()
{
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
}

VideoDecoder::~VideoDecoder()
{
    close();
    av_frame_free(&frame_);
    av_packet_free(&packet_);
}

bool VideoDecoder::open(const QString& filePath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    close();

    std::string path = filePath.toStdString();
    if (avformat_open_input(&fmtCtx_, path.c_str(), nullptr, nullptr) < 0) {
        qWarning() << "[VideoDecoder] failed to open" << filePath;
        return false;
    }

    if (avformat_find_stream_info(fmtCtx_, nullptr) < 0) {
        qWarning() << "[VideoDecoder] failed to find stream info";
        close();
        return false;
    }

    videoStreamIndex_ = -1;
    for (unsigned i = 0; i < fmtCtx_->nb_streams; ++i) {
        if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex_ = static_cast<int>(i);
            break;
        }
    }
    if (videoStreamIndex_ < 0) {
        qWarning() << "[VideoDecoder] no video stream found";
        close();
        return false;
    }

    auto* codecPar = fmtCtx_->streams[videoStreamIndex_]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        qWarning() << "[VideoDecoder] unsupported codec";
        close();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx_, codecPar);
    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        qWarning() << "[VideoDecoder] failed to open codec";
        close();
        return false;
    }

    info_.width = codecCtx_->width;
    info_.height = codecCtx_->height;
    info_.valid = true;

    auto* stream = fmtCtx_->streams[videoStreamIndex_];
    if (stream->duration > 0 && stream->time_base.den > 0) {
        info_.durationSeconds = static_cast<double>(stream->duration)
            * av_q2d(stream->time_base);
    } else if (fmtCtx_->duration > 0) {
        info_.durationSeconds = static_cast<double>(fmtCtx_->duration) / AV_TIME_BASE;
    }

    if (stream->avg_frame_rate.den > 0)
        info_.frameRate = av_q2d(stream->avg_frame_rate);
    else if (stream->r_frame_rate.den > 0)
        info_.frameRate = av_q2d(stream->r_frame_rate);
    else
        info_.frameRate = 24.0;

    return true;
}

void VideoDecoder::close()
{
    if (swsCtx_) { sws_freeContext(swsCtx_); swsCtx_ = nullptr; }
    if (codecCtx_) { avcodec_free_context(&codecCtx_); }
    if (fmtCtx_) { avformat_close_input(&fmtCtx_); }
    videoStreamIndex_ = -1;
    info_ = {};
    lastTargetSize_ = {};
}

bool VideoDecoder::isOpen() const { return fmtCtx_ != nullptr && info_.valid; }
VideoInfo VideoDecoder::info() const { return info_; }

void VideoDecoder::resetSwsContext(int srcW, int srcH, QSize targetSize)
{
    if (swsCtx_ && lastTargetSize_ == targetSize) return;
    if (swsCtx_) sws_freeContext(swsCtx_);

    swsCtx_ = sws_getContext(srcW, srcH, codecCtx_->pix_fmt,
                             targetSize.width(), targetSize.height(),
                             AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    lastTargetSize_ = targetSize;
}

QImage VideoDecoder::frameToImage(QSize targetSize)
{
    if (!frame_ || frame_->width <= 0 || frame_->height <= 0)
        return {};

    resetSwsContext(frame_->width, frame_->height, targetSize);
    if (!swsCtx_) return {};

    QImage image(targetSize.width(), targetSize.height(), QImage::Format_RGB888);
    uint8_t* dstData[4] = { image.bits(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };

    sws_scale(swsCtx_, frame_->data, frame_->linesize, 0,
              frame_->height, dstData, dstLinesize);

    return image;
}

QImage VideoDecoder::extractFrame(double timeSeconds, QSize targetSize)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isOpen()) return {};

    timeSeconds = std::clamp(timeSeconds, 0.0, info_.durationSeconds);

    auto* stream = fmtCtx_->streams[videoStreamIndex_];
    double timeBase = av_q2d(stream->time_base);
    int64_t timestamp = static_cast<int64_t>(timeSeconds / timeBase);

    if (av_seek_frame(fmtCtx_, videoStreamIndex_, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        if (av_seek_frame(fmtCtx_, videoStreamIndex_, 0, AVSEEK_FLAG_BACKWARD) < 0)
            return {};
    }
    avcodec_flush_buffers(codecCtx_);

    bool gotFrame = false;
    int maxPackets = 300;

    while (--maxPackets > 0) {
        int ret = av_read_frame(fmtCtx_, packet_);
        if (ret < 0) break;

        if (packet_->stream_index != videoStreamIndex_) {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);
        if (ret < 0) continue;

        while (true) {
            ret = avcodec_receive_frame(codecCtx_, frame_);
            if (ret == AVERROR(EAGAIN) || ret < 0) break;

            double framePts = (frame_->pts != AV_NOPTS_VALUE)
                ? frame_->pts * timeBase : -1.0;

            if (framePts >= timeSeconds - 1.0 / info_.frameRate) {
                gotFrame = true;
                break;
            }
        }
        if (gotFrame) break;
    }

    if (!gotFrame) return {};

    if (targetSize.isEmpty())
        targetSize = QSize(info_.width, info_.height);

    return frameToImage(targetSize);
}

std::vector<FilmstripFrame> VideoDecoder::decodeFilmstrip(
    const std::vector<double>& times,
    QSize targetSize,
    std::atomic<uint64_t>& renderId,
    uint64_t myId,
    std::function<void(int done, int total)> progress)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FilmstripFrame> result;
    if (!isOpen() || times.empty()) return result;
    result.reserve(times.size());

    auto* stream = fmtCtx_->streams[videoStreamIndex_];
    double timeBase = av_q2d(stream->time_base);

    av_seek_frame(fmtCtx_, videoStreamIndex_, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx_);

    size_t nextIdx = 0;
    int total = static_cast<int>(times.size());
    QImage lastGoodImage;

    while (nextIdx < times.size()) {
        if (renderId.load() != myId) return result;

        int ret = av_read_frame(fmtCtx_, packet_);
        if (ret < 0) break;

        if (packet_->stream_index != videoStreamIndex_) {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);
        if (ret < 0) continue;

        while (nextIdx < times.size()) {
            ret = avcodec_receive_frame(codecCtx_, frame_);
            if (ret == AVERROR(EAGAIN) || ret < 0) break;

            double framePts = (frame_->pts != AV_NOPTS_VALUE)
                ? frame_->pts * timeBase : -1.0;

            while (nextIdx < times.size() && framePts >= times[nextIdx] - 0.5 / info_.frameRate) {
                QImage img = frameToImage(targetSize);
                if (!img.isNull()) lastGoodImage = img;

                result.push_back({ times[nextIdx], lastGoodImage });
                nextIdx++;

                if (progress)
                    progress(static_cast<int>(nextIdx), total);
            }
        }
    }

    while (nextIdx < times.size()) {
        if (!lastGoodImage.isNull())
            result.push_back({ times[nextIdx], lastGoodImage });
        nextIdx++;
    }

    return result;
}

// ── MP3 Transcode ───────────────────────────────────────────────────────────

bool transcodeToMp3(const QString& inputWavPath,
                    const QString& outputMp3Path,
                    int bitrate,
                    std::function<void(float)> progressCallback)
{
    std::string inPath = inputWavPath.toStdString();
    std::string outPath = outputMp3Path.toStdString();

    AVFormatContext* inFmtCtx = nullptr;
    if (avformat_open_input(&inFmtCtx, inPath.c_str(), nullptr, nullptr) < 0) {
        qWarning() << "[transcodeToMp3] cannot open input:" << inputWavPath;
        return false;
    }
    if (avformat_find_stream_info(inFmtCtx, nullptr) < 0) {
        avformat_close_input(&inFmtCtx);
        return false;
    }

    int audioIdx = -1;
    for (unsigned i = 0; i < inFmtCtx->nb_streams; ++i) {
        if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIdx = static_cast<int>(i);
            break;
        }
    }
    if (audioIdx < 0) {
        avformat_close_input(&inFmtCtx);
        return false;
    }

    auto* inCodecPar = inFmtCtx->streams[audioIdx]->codecpar;
    const AVCodec* inCodec = avcodec_find_decoder(inCodecPar->codec_id);
    AVCodecContext* inCodecCtx = avcodec_alloc_context3(inCodec);
    avcodec_parameters_to_context(inCodecCtx, inCodecPar);
    avcodec_open2(inCodecCtx, inCodec, nullptr);

    const AVCodec* mp3Codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!mp3Codec) {
        qWarning() << "[transcodeToMp3] MP3 encoder not available";
        avcodec_free_context(&inCodecCtx);
        avformat_close_input(&inFmtCtx);
        return false;
    }

    AVFormatContext* outFmtCtx = nullptr;
    avformat_alloc_output_context2(&outFmtCtx, nullptr, "mp3", outPath.c_str());
    if (!outFmtCtx) {
        avcodec_free_context(&inCodecCtx);
        avformat_close_input(&inFmtCtx);
        return false;
    }

    AVStream* outStream = avformat_new_stream(outFmtCtx, mp3Codec);
    AVCodecContext* outCodecCtx = avcodec_alloc_context3(mp3Codec);
    outCodecCtx->bit_rate = bitrate * 1000;
    outCodecCtx->sample_rate = inCodecCtx->sample_rate;
    outCodecCtx->sample_fmt = AV_SAMPLE_FMT_S16P;

    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    av_channel_layout_copy(&outCodecCtx->ch_layout,
                           inCodecCtx->ch_layout.nb_channels >= 2 ? &stereo : &mono);

    if (outFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        outCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(outCodecCtx, mp3Codec, nullptr) < 0) {
        qWarning() << "[transcodeToMp3] cannot open MP3 encoder";
        avcodec_free_context(&outCodecCtx);
        avformat_free_context(outFmtCtx);
        avcodec_free_context(&inCodecCtx);
        avformat_close_input(&inFmtCtx);
        return false;
    }
    avcodec_parameters_from_context(outStream->codecpar, outCodecCtx);
    outStream->time_base = { 1, outCodecCtx->sample_rate };

    SwrContext* swrCtx = nullptr;
    if (inCodecCtx->sample_fmt != outCodecCtx->sample_fmt ||
        inCodecCtx->sample_rate != outCodecCtx->sample_rate ||
        av_channel_layout_compare(&inCodecCtx->ch_layout, &outCodecCtx->ch_layout) != 0)
    {
        swr_alloc_set_opts2(&swrCtx,
                            &outCodecCtx->ch_layout, outCodecCtx->sample_fmt, outCodecCtx->sample_rate,
                            &inCodecCtx->ch_layout, inCodecCtx->sample_fmt, inCodecCtx->sample_rate,
                            0, nullptr);
        swr_init(swrCtx);
    }

    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE))
        avio_open(&outFmtCtx->pb, outPath.c_str(), AVIO_FLAG_WRITE);

    if (avformat_write_header(outFmtCtx, nullptr) < 0) {
        avcodec_free_context(&outCodecCtx);
        if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outFmtCtx->pb);
        avformat_free_context(outFmtCtx);
        avcodec_free_context(&inCodecCtx);
        avformat_close_input(&inFmtCtx);
        return false;
    }

    double totalDuration = (inFmtCtx->duration > 0)
        ? static_cast<double>(inFmtCtx->duration) / AV_TIME_BASE : 0.0;

    AVPacket* inPkt = av_packet_alloc();
    AVPacket* outPkt = av_packet_alloc();
    AVFrame* inFrame = av_frame_alloc();
    AVFrame* resampledFrame = av_frame_alloc();

    resampledFrame->format = outCodecCtx->sample_fmt;
    av_channel_layout_copy(&resampledFrame->ch_layout, &outCodecCtx->ch_layout);
    resampledFrame->sample_rate = outCodecCtx->sample_rate;
    resampledFrame->nb_samples = outCodecCtx->frame_size > 0 ? outCodecCtx->frame_size : 1152;
    av_frame_get_buffer(resampledFrame, 0);

    bool success = true;
    int64_t outPts = 0;

    while (av_read_frame(inFmtCtx, inPkt) >= 0) {
        if (inPkt->stream_index != audioIdx) {
            av_packet_unref(inPkt);
            continue;
        }

        if (avcodec_send_packet(inCodecCtx, inPkt) < 0) {
            av_packet_unref(inPkt);
            continue;
        }
        av_packet_unref(inPkt);

        while (avcodec_receive_frame(inCodecCtx, inFrame) >= 0) {
            AVFrame* frameToEncode = inFrame;

            if (swrCtx) {
                av_frame_make_writable(resampledFrame);
                resampledFrame->nb_samples = swr_convert(swrCtx,
                    resampledFrame->data, resampledFrame->nb_samples,
                    (const uint8_t**)inFrame->data, inFrame->nb_samples);
                resampledFrame->pts = outPts;
                outPts += resampledFrame->nb_samples;
                frameToEncode = resampledFrame;
            }

            if (avcodec_send_frame(outCodecCtx, frameToEncode) < 0) continue;
            while (avcodec_receive_packet(outCodecCtx, outPkt) >= 0) {
                av_packet_rescale_ts(outPkt, outCodecCtx->time_base, outStream->time_base);
                outPkt->stream_index = outStream->index;
                av_interleaved_write_frame(outFmtCtx, outPkt);
            }

            if (progressCallback && totalDuration > 0.0) {
                double pos = static_cast<double>(inFrame->pts)
                    * av_q2d(inFmtCtx->streams[audioIdx]->time_base);
                progressCallback(static_cast<float>(std::clamp(pos / totalDuration, 0.0, 1.0)));
            }
        }
    }

    avcodec_send_frame(outCodecCtx, nullptr);
    while (avcodec_receive_packet(outCodecCtx, outPkt) >= 0) {
        av_packet_rescale_ts(outPkt, outCodecCtx->time_base, outStream->time_base);
        outPkt->stream_index = outStream->index;
        av_interleaved_write_frame(outFmtCtx, outPkt);
    }

    av_write_trailer(outFmtCtx);

    av_frame_free(&resampledFrame);
    av_frame_free(&inFrame);
    av_packet_free(&outPkt);
    av_packet_free(&inPkt);
    if (swrCtx) swr_free(&swrCtx);
    avcodec_free_context(&outCodecCtx);
    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outFmtCtx->pb);
    avformat_free_context(outFmtCtx);
    avcodec_free_context(&inCodecCtx);
    avformat_close_input(&inFmtCtx);

    return success;
}

} // namespace OpenDaw
