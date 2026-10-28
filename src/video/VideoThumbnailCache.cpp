#include "VideoThumbnailCache.h"
#include <QtConcurrent/QtConcurrent>
#include <QPainter>
#include <QDebug>

namespace OpenDaw {

VideoThumbnailCache::VideoThumbnailCache(QObject* parent)
    : QObject(parent)
    , decoder_(std::make_unique<VideoDecoder>())
{
}

VideoThumbnailCache::~VideoThumbnailCache()
{
    stopRender();
}

void VideoThumbnailCache::stopRender()
{
    // decodeFilmstrip checks renderId_ between frames, so this returns quickly.
    renderId_.fetch_add(1);
    renderFuture_.waitForFinished();
}

bool VideoThumbnailCache::openVideo(const QString& filePath)
{
    stopRender();
    decoder_->close();
    filmstrip_ = {};
    filePath_.clear();

    if (!decoder_->open(filePath)) return false;
    filePath_ = filePath;
    emit videoOpened(filePath);
    return true;
}

void VideoThumbnailCache::closeVideo()
{
    stopRender();
    decoder_->close();
    filmstrip_ = {};
    filePath_.clear();
    emit videoClosed();
}

bool VideoThumbnailCache::isOpen() const { return decoder_->isOpen(); }
VideoInfo VideoThumbnailCache::videoInfo() const { return decoder_->info(); }

void VideoThumbnailCache::cancelRender()
{
    renderId_.fetch_add(1);
}

void VideoThumbnailCache::renderFilmstrip(const std::vector<double>& frameTimes,
                                           QSize frameSize, double ppb)
{
    if (frameTimes.empty() || !decoder_->isOpen()) return;

    stopRender();
    uint64_t myId = renderId_.fetch_add(1) + 1;
    rendering_.store(true);

    auto* self = this;
    auto times = frameTimes;
    QSize sz = frameSize;
    double myPpb = ppb;

    renderFuture_ = QtConcurrent::run([self, times, sz, myId, myPpb]() {
        auto& rid = self->renderId_;

        auto frames = self->decoder_->decodeFilmstrip(
            times, sz, rid, myId,
            [self, myId](int done, int total) {
                if (self->renderId_.load() != myId) return;
                QMetaObject::invokeMethod(self, [self, done, total]() {
                    emit self->renderProgress(done, total);
                }, Qt::QueuedConnection);
            });

        if (rid.load() != myId) {
            self->rendering_.store(false);
            return;
        }

        int totalWidth = 0;
        for (const auto& f : frames)
            totalWidth += f.image.width();

        if (totalWidth <= 0) {
            self->rendering_.store(false);
            return;
        }

        QImage strip(totalWidth, sz.height(), QImage::Format_RGB888);
        strip.fill(Qt::black);

        int xPos = 0;
        {
            QPainter painter(&strip);
            for (const auto& f : frames) {
                painter.drawImage(xPos, 0, f.image);
                xPos += f.image.width();
            }
        }

        self->rendering_.store(false);

        // QPixmap may only be created on the GUI thread.
        int tw = sz.width();
        QMetaObject::invokeMethod(self, [self, strip = std::move(strip), tw, myPpb, myId]() {
            if (self->renderId_.load() != myId) return;
            self->filmstrip_ = QPixmap::fromImage(strip);
            self->thumbWidth_ = tw;
            self->renderedPpb_ = myPpb;
            emit self->filmstripReady();
        }, Qt::QueuedConnection);
    });
}

} // namespace OpenDaw
