#include "VideoTrackLane.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMenu>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <cmath>
#include <vector>

namespace OpenDaw {

VideoTrackLane::VideoTrackLane(EditManager* editMgr, QWidget* parent)
    : QWidget(parent)
    , editMgr_(editMgr)
    , cache_(std::make_unique<VideoThumbnailCache>(this))
{
    setFixedHeight(kDefaultHeight);
    laneHeight_ = kDefaultHeight;
    setMouseTracking(true);
    setAccessibleName("Video Track Lane");

    connect(cache_.get(), &VideoThumbnailCache::filmstripReady,
            this, [this]() {
                lastRenderPpb_ = pixelsPerBeat_;
                lastRenderHeight_ = height() - kResizeGrabHeight;
                update();
            });

    rebuildTimer_.setSingleShot(true);
    rebuildTimer_.setInterval(150);
    connect(&rebuildTimer_, &QTimer::timeout, this, &VideoTrackLane::rebuildFilmstrip);
}

VideoTrackLane::~VideoTrackLane() = default;

void VideoTrackLane::setPixelsPerBeat(double ppb)
{
    if (std::abs(ppb - pixelsPerBeat_) < 0.01) return;
    pixelsPerBeat_ = ppb;
    rebuildTimer_.start();
    update();
}

void VideoTrackLane::setScrollX(int scrollX)
{
    scrollX_ = scrollX;
    update();
}

void VideoTrackLane::setPlayheadBeat(double beat)
{
    if (std::abs(beat - playheadBeat_) < 0.001) return;

    int oldX = beatToX(playheadBeat_);
    playheadBeat_ = beat;
    int newX = beatToX(playheadBeat_);

    int minX = std::min(oldX, newX) - 2;
    int maxX = std::max(oldX, newX) + 2;
    update(QRect(minX, 0, maxX - minX, height()));
}

void VideoTrackLane::refresh() { update(); }

void VideoTrackLane::forceRebuildFilmstrip()
{
    lastRenderPpb_ = 0.0;
    lastRenderHeight_ = 0;
    rebuildFilmstrip();
}

bool VideoTrackLane::loadVideo(const QString& filePath)
{
    if (!cache_->openVideo(filePath)) return false;

    if (editMgr_ && editMgr_->edit()) {
        juce::File f(filePath.toStdString());
        editMgr_->edit()->setVideoFile(f, "Video import");
    }

    emit videoLoaded(filePath);
    lastRenderPpb_ = 0.0;
    lastRenderHeight_ = 0;
    rebuildFilmstrip();
    return true;
}

void VideoTrackLane::closeVideo()
{
    cache_->closeVideo();

    if (editMgr_ && editMgr_->edit())
        editMgr_->edit()->setVideoFile({}, {});

    lastRenderPpb_ = 0.0;
    lastRenderHeight_ = 0;
    emit videoClosed();
    update();
}

bool VideoTrackLane::hasVideo() const { return cache_->isOpen(); }
QString VideoTrackLane::videoFilePath() const { return cache_->filePath(); }

int VideoTrackLane::beatToX(double beat) const
{
    return static_cast<int>(beat * pixelsPerBeat_ - scrollX_);
}

double VideoTrackLane::beatToSeconds(double beat) const
{
    if (!editMgr_ || !editMgr_->edit()) return 0.0;
    auto& ts = editMgr_->edit()->tempoSequence;
    return ts.toTime(tracktion::BeatPosition::fromBeats(beat)).inSeconds();
}

double VideoTrackLane::secondsToBeats(double sec) const
{
    if (!editMgr_ || !editMgr_->edit()) return 0.0;
    auto& ts = editMgr_->edit()->tempoSequence;
    return ts.toBeats(tracktion::TimePosition::fromSeconds(sec)).inBeats();
}

void VideoTrackLane::rebuildFilmstrip()
{
    if (!cache_->isOpen()) return;

    auto vInfo = cache_->videoInfo();
    if (vInfo.width <= 0 || vInfo.height <= 0) return;

    int drawHeight = height() - kResizeGrabHeight;
    if (drawHeight < 10) return;

    double aspect = static_cast<double>(vInfo.width) / vInfo.height;
    int thumbWidth = std::max(10, static_cast<int>(drawHeight * aspect));
    QSize thumbSize(thumbWidth, drawHeight);

    double videoDur = vInfo.durationSeconds;
    double videoBeats = secondsToBeats(videoDur);
    double totalPixels = videoBeats * pixelsPerBeat_;
    int numFrames = std::max(1, static_cast<int>(std::ceil(totalPixels / thumbWidth)));

    double timePerFrame = videoDur / numFrames;

    std::vector<double> times;
    times.reserve(numFrames);
    for (int i = 0; i < numFrames; ++i)
        times.push_back(i * timePerFrame);

    cache_->renderFilmstrip(times, thumbSize, pixelsPerBeat_);
}

void VideoTrackLane::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    auto& theme = ThemeManager::instance().current();

    p.fillRect(rect(), theme.surface);

    if (!cache_->isOpen()) {
        p.setPen(theme.textDim);
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, "No video loaded \u2014 right-click to import");

        p.setPen(QPen(theme.border, 1));
        p.drawLine(0, height() - 1, width(), height() - 1);
        return;
    }

    QPixmap strip = cache_->filmstrip();
    int drawH = height() - kResizeGrabHeight;

    auto vInfo = cache_->videoInfo();
    double videoDur = vInfo.durationSeconds;
    double videoBeats = secondsToBeats(videoDur);
    int videoEndX = beatToX(videoBeats);

    int filmStartX = beatToX(0.0);

    if (!strip.isNull() && strip.width() > 0) {
        double renderedPpb = cache_->filmstripPpb();
        bool sameZoom = renderedPpb > 0.0 &&
                        std::abs(renderedPpb - pixelsPerBeat_) < 0.01;

        if (sameZoom) {
            int srcX = 0;
            int dstX = filmStartX;

            if (dstX < 0) {
                srcX = -dstX;
                dstX = 0;
            }

            int clipRight = std::max(0, videoEndX);
            int drawW = std::min(strip.width() - srcX, clipRight - dstX);
            drawW = std::min(drawW, width() - dstX);

            if (drawW > 0 && srcX < strip.width())
                p.drawPixmap(dstX, 0, drawW, drawH,
                             strip, srcX, 0, drawW, drawH);
        } else {
            double scale = (renderedPpb > 0.0)
                ? pixelsPerBeat_ / renderedPpb : 1.0;

            int dstX = filmStartX;
            int dstW = std::min(videoEndX - filmStartX, width() - filmStartX);
            dstW = std::min(dstW, width() - std::max(0, dstX));

            int srcX = 0;
            if (dstX < 0) {
                srcX = static_cast<int>(-dstX / scale);
                dstW += dstX;
                dstX = 0;
            }

            int srcW = static_cast<int>(dstW / scale);
            srcW = std::min(srcW, strip.width() - srcX);
            dstW = std::min(dstW, width() - dstX);

            if (dstW > 0 && srcW > 0 && srcX < strip.width())
                p.drawPixmap(dstX, 0, dstW, drawH,
                             strip, srcX, 0, srcW, drawH);
        }
    } else if (cache_->isRendering()) {
        p.setPen(theme.textDim);
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, "Rendering video thumbnails...");
    }

    // Playhead
    int playheadX = beatToX(playheadBeat_);
    if (playheadX >= 0 && playheadX <= width()) {
        p.setPen(QPen(theme.playhead, 1.5));
        p.drawLine(playheadX, 0, playheadX, height());
    }

    // Border
    p.setPen(QPen(theme.border, 1));
    p.drawLine(0, height() - 1, width(), height() - 1);

    // Resize handle
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(theme.border.red(), theme.border.green(),
                       theme.border.blue(), 120));
    p.drawRect(0, height() - kResizeGrabHeight, width(), kResizeGrabHeight);
}

void VideoTrackLane::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu;
    menu.setAccessibleName("Video Track Lane Context Menu");

    if (cache_->isOpen()) {
        QString name = QFileInfo(cache_->filePath()).fileName();
        menu.addAction(QString("Video: %1").arg(name))->setEnabled(false);
        menu.addSeparator();
        menu.addAction("Replace Video...", this, [this]() {
            QString path = QFileDialog::getOpenFileName(
                this, "Open Video File", {},
                "Video Files (*.mp4 *.mkv *.avi *.mov *.webm *.wmv);;All Files (*)");
            if (!path.isEmpty()) loadVideo(path);
        });
        menu.addAction("Remove Video", this, [this]() { closeVideo(); });
    } else {
        menu.addAction("Import Video...", this, [this]() {
            QString path = QFileDialog::getOpenFileName(
                this, "Open Video File", {},
                "Video Files (*.mp4 *.mkv *.avi *.mov *.webm *.wmv);;All Files (*)");
            if (!path.isEmpty()) loadVideo(path);
        });
    }

    menu.exec(event->globalPos());
}

void VideoTrackLane::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;

    if (event->pos().y() >= height() - kResizeGrabHeight - 2) {
        dragMode_ = DragMode::ResizeHeight;
        resizeStartY_ = event->globalPosition().toPoint().y();
        resizeStartHeight_ = laneHeight_;
        setCursor(Qt::SizeVerCursor);
        event->accept();
        return;
    }
}

void VideoTrackLane::mouseMoveEvent(QMouseEvent* event)
{
    if (dragMode_ == DragMode::None) {
        if (event->pos().y() >= height() - kResizeGrabHeight - 2)
            setCursor(Qt::SizeVerCursor);
        else
            setCursor(Qt::ArrowCursor);
    }

    if (dragMode_ == DragMode::ResizeHeight) {
        int delta = event->globalPosition().toPoint().y() - resizeStartY_;
        int newHeight = std::clamp(resizeStartHeight_ + delta, kMinHeight, kMaxHeight);
        laneHeight_ = newHeight;
        setFixedHeight(newHeight);
        lastRenderHeight_ = 0;
        rebuildTimer_.start();
        update();
    }
}

void VideoTrackLane::mouseReleaseEvent(QMouseEvent*)
{
    dragMode_ = DragMode::None;
    setCursor(Qt::ArrowCursor);
}

} // namespace OpenDaw
