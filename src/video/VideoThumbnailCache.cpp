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
    renderId_.fetch_add(1);
}

bool VideoThumbnailCache::openVideo(const QString& filePath)
{
    renderId_.fetch_add(1);
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
    renderId_.fetch_add(1);
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

    uint64_t myId = renderId_.fetch_add(1) + 1;
    rendering_.store(true);

    auto* self = this;
    auto times = frameTimes;
    QSize sz = frameSize;
    double myPpb = ppb;

    QtConcurrent::run([self, times, sz, myId, myPpb]() {
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
            qDebug() << "[VideoThumbnailCache] render cancelled, id=" << myId;
            self->rendering_.store(false);
            return;
        }

        qDebug() << "[VideoThumbnailCache] render complete: "
                 << frames.size() << "frames, id=" << myId;

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

        QPixmap pix = QPixmap::fromImage(std::move(strip));

        self->rendering_.store(false);

        int tw = sz.width();
        QMetaObject::invokeMethod(self, [self, pix, tw, myPpb]() {
            self->filmstrip_ = pix;
            self->thumbWidth_ = tw;
            self->renderedPpb_ = myPpb;
            emit self->filmstripReady();
        }, Qt::QueuedConnection);
    });
}

} // namespace OpenDaw
