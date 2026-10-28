#include "AudioWaveformView.h"
#include "engine/EditManager.h"
#include "utils/ThemeManager.h"
#include "utils/WaveformCache.h"
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QGraphicsSceneMouseEvent>
#include <cmath>
#include <algorithm>

namespace OpenDaw {

static constexpr double kHandleWidth = 8.0;
static constexpr double kFadeHandleSize = 10.0;
static constexpr double kLoopHandleWidth = 6.0;
static constexpr double kWaveHeight = 200.0;
static constexpr double kMinPpb = 10.0;
static constexpr double kMaxPpb = 50000.0;
static constexpr double kSelHandleW = 6.0;
static constexpr double kSelHandleH = 14.0;

AudioWaveformView::AudioWaveformView(QWidget* parent)
    : QGraphicsView(parent)
{
    setAccessibleName("Audio Waveform View");
    setFocusPolicy(Qt::ClickFocus);
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRenderHint(QPainter::Antialiasing, true);

    playheadTimer_.setInterval(33);
    connect(&playheadTimer_, &QTimer::timeout, this, &AudioWaveformView::updatePlayhead);
}

void AudioWaveformView::setClip(te::WaveAudioClip* clip, EditManager* editMgr)
{
    clip_ = clip;
    editMgr_ = editMgr;
    selection_ = {};

    if (clip_) {
        gainMultiplier_ = std::pow(10.0, clip_->getGainDB() / 20.0);
        reversed_ = clip_->getIsReversed();
        playheadTimer_.start();
        rebuild();
        horizontalScrollBar()->setValue(0);
    } else {
        playheadTimer_.stop();
        scene_->clear();
        waveformItem_ = nullptr;
        playheadLine_ = nullptr;
        trimLeftHandle_ = nullptr;
        trimRightHandle_ = nullptr;
        trimLeftOverlay_ = nullptr;
        trimRightOverlay_ = nullptr;
        fadeInOverlay_ = nullptr;
        fadeOutOverlay_ = nullptr;
        fadeInHandle_ = nullptr;
        fadeOutHandle_ = nullptr;
        loopStartLine_ = nullptr;
        loopEndLine_ = nullptr;
        loopStartHandle_ = nullptr;
        loopEndHandle_ = nullptr;
        loopHighlight_ = nullptr;
        selectionOverlay_ = nullptr;
        selHandleL_ = nullptr;
        selHandleR_ = nullptr;
        clipStartBeat_ = 0.0;
        waveMin_.clear();
        waveMax_.clear();
        chanMin_.clear();
        chanMax_.clear();
    }
}

void AudioWaveformView::rebuild()
{
    scene_->clear();
    waveformItem_ = nullptr;
    waveformItemR_ = nullptr;
    playheadLine_ = nullptr;
    trimLeftHandle_ = nullptr;
    trimRightHandle_ = nullptr;
    trimLeftOverlay_ = nullptr;
    trimRightOverlay_ = nullptr;
    fadeInOverlay_ = nullptr;
    fadeOutOverlay_ = nullptr;
    fadeInHandle_ = nullptr;
    fadeOutHandle_ = nullptr;
    loopStartLine_ = nullptr;
    loopEndLine_ = nullptr;
    loopStartHandle_ = nullptr;
    loopEndHandle_ = nullptr;
    loopHighlight_ = nullptr;
    selectionOverlay_ = nullptr;
    selHandleL_ = nullptr;
    selHandleR_ = nullptr;

    if (!clip_) return;

    auto& ts = clip_->edit.tempoSequence;
    clipStartBeat_ = ts.toBeats(clip_->getPosition().getStart()).inBeats();

    loadWaveformData();
    redrawWaveformPath();
    rebuildOverlays();
    updateSelectionOverlay();
    updateSceneSize();
}

void AudioWaveformView::setPixelsPerBeat(double ppb)
{
    ppb = std::clamp(ppb, kMinPpb, kMaxPpb);
    if (std::abs(ppb - pixelsPerBeat_) < 0.01) return;
    pixelsPerBeat_ = ppb;
    rebuild();
}

void AudioWaveformView::setGainMultiplier(double linear)
{
    linear = std::clamp(linear, 0.0, 10.0);
    if (std::abs(linear - gainMultiplier_) < 0.001) return;
    gainMultiplier_ = linear;
    if (!waveMin_.empty()) {
        if (waveformItem_) { scene_->removeItem(waveformItem_); delete waveformItem_; waveformItem_ = nullptr; }
        if (waveformItemR_) { scene_->removeItem(waveformItemR_); delete waveformItemR_; waveformItemR_ = nullptr; }
        redrawWaveformPath();
    }
}

void AudioWaveformView::setReversed(bool reversed)
{
    if (reversed == reversed_) return;
    reversed_ = reversed;
    if (!waveMin_.empty()) {
        if (waveformItem_) { scene_->removeItem(waveformItem_); delete waveformItem_; waveformItem_ = nullptr; }
        if (waveformItemR_) { scene_->removeItem(waveformItemR_); delete waveformItemR_; waveformItemR_ = nullptr; }
        redrawWaveformPath();
    }
}

void AudioWaveformView::setSplitStereo(bool split)
{
    if (split == splitStereo_) return;
    splitStereo_ = split;
    if (!waveMin_.empty()) {
        if (waveformItem_) { scene_->removeItem(waveformItem_); delete waveformItem_; waveformItem_ = nullptr; }
        if (waveformItemR_) { scene_->removeItem(waveformItemR_); delete waveformItemR_; waveformItemR_ = nullptr; }
        redrawWaveformPath();
    }
}

void AudioWaveformView::setSelection(const WaveformSelection& sel)
{
    selection_ = sel;
    selection_.normalize();
    updateSelectionOverlay();
    emit selectionChanged();
}

void AudioWaveformView::clearSelection()
{
    selection_ = {};
    updateSelectionOverlay();
    emit selectionChanged();
}

void AudioWaveformView::selectAll()
{
    selection_.startSample = 0;
    selection_.endSample = totalSamples_;
    updateSelectionOverlay();
    emit selectionChanged();
}

double AudioWaveformView::beatToX(double beat) const { return (beat - clipStartBeat_) * pixelsPerBeat_; }
double AudioWaveformView::xToBeat(double x) const { return x / pixelsPerBeat_ + clipStartBeat_; }

double AudioWaveformView::timeToX(double seconds) const
{
    if (!clip_) return 0.0;
    auto& ts = clip_->edit.tempoSequence;
    double beat = ts.toBeats(tracktion::TimePosition::fromSeconds(seconds)).inBeats();
    return beatToX(beat);
}

double AudioWaveformView::xToTime(double x) const
{
    if (!clip_) return 0.0;
    double beat = xToBeat(x);
    auto& ts = clip_->edit.tempoSequence;
    return ts.toTime(tracktion::BeatPosition::fromBeats(beat)).inSeconds();
}

double AudioWaveformView::sourceTimeToX(double srcSeconds) const
{
    if (!clip_) return 0.0;
    auto sourceLen = clip_->getSourceLength().inSeconds();
    if (sourceLen <= 0.0) return 0.0;
    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double frac = srcSeconds / sourceLen;
    return beatToX(clipStartBeat + frac * (clipEndBeat - clipStartBeat));
}

double AudioWaveformView::xToSourceTime(double x) const
{
    if (!clip_) return 0.0;
    auto sourceLen = clip_->getSourceLength().inSeconds();
    if (sourceLen <= 0.0) return 0.0;
    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double clipBeats = clipEndBeat - clipStartBeat;
    if (clipBeats <= 0.0) return 0.0;
    return ((xToBeat(x) - clipStartBeat) / clipBeats) * sourceLen;
}

juce::int64 AudioWaveformView::xToSample(double x) const
{
    if (!clip_ || totalSamples_ <= 0) return 0;
    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double clipBeats = clipEndBeat - clipStartBeat;
    if (clipBeats <= 0.0) return 0;

    double frac = std::clamp((xToBeat(x) - clipStartBeat) / clipBeats, 0.0, 1.0);
    if (reversed_)
        frac = 1.0 - frac;
    return static_cast<juce::int64>(frac * static_cast<double>(totalSamples_));
}

double AudioWaveformView::sampleToX(juce::int64 sample) const
{
    if (!clip_ || totalSamples_ <= 0) return 0.0;
    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();

    double frac = static_cast<double>(sample) / static_cast<double>(totalSamples_);
    if (reversed_)
        frac = 1.0 - frac;
    return beatToX(clipStartBeat + frac * (clipEndBeat - clipStartBeat));
}

void AudioWaveformView::loadWaveformData()
{
    waveMin_.clear();
    waveMax_.clear();
    chanMin_.clear();
    chanMax_.clear();
    if (!clip_) return;

    auto file = clip_->getOriginalFile();
    if (!file.existsAsFile()) return;

    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double clipWidthPx = (clipEndBeat - clipStartBeat) * pixelsPerBeat_;

    // One point per pixel, capped: at deep zoom a long clip would otherwise ask
    // for tens of millions of points (GBs of vectors + path). Drawing scales by
    // point count, so the cap only coarsens the extreme zoom levels.
    constexpr double kMaxWavePoints = 1 << 20;
    int numPoints = static_cast<int>(std::clamp(clipWidthPx, 100.0, kMaxWavePoints));
    auto wfData = WaveformCache::instance().getWaveform(file, numPoints);
    waveMin_ = wfData.minValues;
    waveMax_ = wfData.maxValues;
    chanMin_ = wfData.channelMin;
    chanMax_ = wfData.channelMax;
    sampleRate_ = wfData.sampleRate;
    totalSamples_ = wfData.totalSamples;
    numChannels_ = wfData.numChannels;
    bitsPerSample_ = wfData.bitsPerSample;
}

void AudioWaveformView::drawChannelWaveform(QPainterPath& path,
                                              const std::vector<float>& minV,
                                              const std::vector<float>& maxV,
                                              double xOff, double xScale,
                                              double topY, double height,
                                              double amp) const
{
    int n = static_cast<int>(minV.size());
    double midY = topY + height / 2.0;

    path.moveTo(xOff, midY);
    for (int i = 0; i < n; ++i) {
        int si = reversed_ ? (n - 1 - i) : i;
        path.lineTo(xOff + i * xScale, midY - maxV[size_t(si)] * amp);
    }
    for (int i = n - 1; i >= 0; --i) {
        int si = reversed_ ? (n - 1 - i) : i;
        path.lineTo(xOff + i * xScale, midY - minV[size_t(si)] * amp);
    }
    path.closeSubpath();
}

void AudioWaveformView::redrawWaveformPath()
{
    if (!clip_ || waveMin_.empty() || waveMax_.empty()) return;

    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double clipWidthPx = (clipEndBeat - clipStartBeat) * pixelsPerBeat_;

    int n = static_cast<int>(waveMin_.size());
    double xScale = clipWidthPx / n;
    double xOff = beatToX(clipStartBeat);
    double gainClamp = std::clamp(gainMultiplier_, 0.0, 4.0);
    auto& theme = ThemeManager::instance().current();

    if (splitStereo_ && numChannels_ >= 2
        && chanMin_.size() >= 2 && chanMax_.size() >= 2) {
        double chHeight = kWaveHeight / 2.0;
        double ampL = chHeight / 2.0 * 0.9 * gainClamp;
        double ampR = ampL;

        QPainterPath pathL;
        drawChannelWaveform(pathL, chanMin_[0], chanMax_[0],
                            xOff, xScale, 0.0, chHeight, ampL);
        waveformItem_ = scene_->addPath(pathL, Qt::NoPen, QBrush(theme.waveform));
        waveformItem_->setZValue(1);

        QPainterPath pathR;
        drawChannelWaveform(pathR, chanMin_[1], chanMax_[1],
                            xOff, xScale, chHeight, chHeight, ampR);
        QColor rColor(theme.waveform.red(), theme.waveform.green(),
                      std::min(255, theme.waveform.blue() + 40));
        waveformItemR_ = scene_->addPath(pathR, Qt::NoPen, QBrush(rColor));
        waveformItemR_->setZValue(1);

        // Channel divider line
        scene_->addLine(xOff, chHeight, xOff + clipWidthPx, chHeight,
                        QPen(theme.gridLineMajor, 1, Qt::DashLine))->setZValue(3);
    } else {
        double amp = kWaveHeight / 2.0 * 0.9 * gainClamp;
        QPainterPath path;
        drawChannelWaveform(path, waveMin_, waveMax_,
                            xOff, xScale, 0.0, kWaveHeight, amp);
        waveformItem_ = scene_->addPath(path, Qt::NoPen, QBrush(theme.waveform));
        waveformItem_->setZValue(1);
    }
}

void AudioWaveformView::rebuildOverlays()
{
    if (!clip_) return;

    auto& theme = ThemeManager::instance().current();
    auto& ts = clip_->edit.tempoSequence;

    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double xStart = beatToX(clipStartBeat);
    double xEnd = beatToX(clipEndBeat);

    QColor handleColor(theme.accent.red(), theme.accent.green(), theme.accent.blue(), 200);

    trimLeftHandle_ = scene_->addRect(xStart, 0, kHandleWidth, kWaveHeight,
                                       Qt::NoPen, QBrush(handleColor));
    trimLeftHandle_->setZValue(10);
    trimLeftHandle_->setCursor(Qt::SizeHorCursor);
    trimLeftHandle_->setToolTip("Drag to trim start");

    trimRightHandle_ = scene_->addRect(xEnd - kHandleWidth, 0, kHandleWidth, kWaveHeight,
                                        Qt::NoPen, QBrush(handleColor));
    trimRightHandle_->setZValue(10);
    trimRightHandle_->setCursor(Qt::SizeHorCursor);
    trimRightHandle_->setToolTip("Drag to trim end");

    double fadeInSec = clip_->getFadeIn().inSeconds();
    double fadeInX = xStart;
    if (fadeInSec > 0.0) {
        auto fadeEndTime = clip_->getPosition().getStart() + clip_->getFadeIn();
        double fadeEndX = beatToX(ts.toBeats(fadeEndTime).inBeats());

        QPainterPath fadePath;
        fadePath.moveTo(fadeInX, 0);
        fadePath.lineTo(fadeEndX, 0);
        fadePath.lineTo(fadeEndX, 0);
        fadePath.lineTo(fadeInX, kWaveHeight);
        fadePath.closeSubpath();
        fadeInOverlay_ = scene_->addPath(fadePath, Qt::NoPen, QBrush(QColor(0, 0, 0, 80)));
        fadeInOverlay_->setZValue(5);
    }

    double fadeInHandleX = fadeInX;
    if (fadeInSec > 0.0) {
        auto fadeEndTime = clip_->getPosition().getStart() + clip_->getFadeIn();
        fadeInHandleX = beatToX(ts.toBeats(fadeEndTime).inBeats());
    }
    fadeInHandle_ = scene_->addRect(fadeInHandleX - kFadeHandleSize / 2, 0,
                                     kFadeHandleSize, kFadeHandleSize,
                                     QPen(theme.accentLight), QBrush(theme.accentLight));
    fadeInHandle_->setZValue(11);
    fadeInHandle_->setCursor(Qt::SizeHorCursor);
    fadeInHandle_->setToolTip("Clip fade in (non-destructive, live)");

    double fadeOutSec = clip_->getFadeOut().inSeconds();
    if (fadeOutSec > 0.0) {
        auto fadeStartTime = clip_->getPosition().getEnd() - clip_->getFadeOut();
        double fadeStartX = beatToX(ts.toBeats(fadeStartTime).inBeats());

        QPainterPath fadePath;
        fadePath.moveTo(fadeStartX, 0);
        fadePath.lineTo(xEnd, 0);
        fadePath.lineTo(xEnd, kWaveHeight);
        fadePath.lineTo(fadeStartX, 0);
        fadePath.closeSubpath();
        fadeOutOverlay_ = scene_->addPath(fadePath, Qt::NoPen, QBrush(QColor(0, 0, 0, 80)));
        fadeOutOverlay_->setZValue(5);
    }

    double fadeOutHandleX = xEnd;
    if (fadeOutSec > 0.0) {
        auto fadeStartTime = clip_->getPosition().getEnd() - clip_->getFadeOut();
        fadeOutHandleX = beatToX(ts.toBeats(fadeStartTime).inBeats());
    }
    fadeOutHandle_ = scene_->addRect(fadeOutHandleX - kFadeHandleSize / 2, 0,
                                      kFadeHandleSize, kFadeHandleSize,
                                      QPen(theme.accentLight), QBrush(theme.accentLight));
    fadeOutHandle_->setZValue(11);
    fadeOutHandle_->setCursor(Qt::SizeHorCursor);
    fadeOutHandle_->setToolTip("Clip fade out (non-destructive, live)");

    if (clip_->isLooping()) {
        double loopBeats = 0.0;
        if (clip_->beatBasedLooping()) {
            loopBeats = clip_->getLoopLengthBeats().inBeats();
        } else {
            double loopSec = clip_->getLoopLength().inSeconds();
            auto loopEndTime = clip_->getPosition().getStart()
                               + tracktion::TimeDuration::fromSeconds(loopSec);
            loopBeats = (ts.toBeats(loopEndTime) - ts.toBeats(clip_->getPosition().getStart())).inBeats();
        }
        double loopEndX = xStart + loopBeats * pixelsPerBeat_;
        if (loopEndX > xEnd) loopEndX = xEnd;

        QColor loopColor(theme.accent.red(), theme.accent.green(), theme.accent.blue(), 40);
        loopHighlight_ = scene_->addRect(xStart, 0, loopEndX - xStart, kWaveHeight,
                                          Qt::NoPen, QBrush(loopColor));
        loopHighlight_->setZValue(2);

        QColor loopLineColor(theme.accentLight);
        loopStartLine_ = scene_->addLine(xStart, 0, xStart, kWaveHeight,
                                          QPen(loopLineColor, 2, Qt::DashLine));
        loopStartLine_->setZValue(8);
        loopEndLine_ = scene_->addLine(loopEndX, 0, loopEndX, kWaveHeight,
                                        QPen(loopLineColor, 2, Qt::DashLine));
        loopEndLine_->setZValue(8);

        QColor loopHandleColor(theme.accentLight.red(), theme.accentLight.green(),
                               theme.accentLight.blue(), 220);
        loopStartHandle_ = scene_->addRect(xStart, kWaveHeight - 16,
                                            kLoopHandleWidth, 16,
                                            Qt::NoPen, QBrush(loopHandleColor));
        loopStartHandle_->setZValue(12);
        loopStartHandle_->setCursor(Qt::SizeHorCursor);
        loopStartHandle_->setToolTip("Drag to adjust loop start");
        loopEndHandle_ = scene_->addRect(loopEndX - kLoopHandleWidth, kWaveHeight - 16,
                                          kLoopHandleWidth, 16,
                                          Qt::NoPen, QBrush(loopHandleColor));
        loopEndHandle_->setZValue(12);
        loopEndHandle_->setCursor(Qt::SizeHorCursor);
        loopEndHandle_->setToolTip("Drag to adjust loop length");
    }

    playheadLine_ = scene_->addLine(0, 0, 0, kWaveHeight, QPen(theme.playhead, 2));
    playheadLine_->setZValue(20);
    updatePlayhead();
}

void AudioWaveformView::updateSelectionOverlay()
{
    if (selectionOverlay_) { scene_->removeItem(selectionOverlay_); delete selectionOverlay_; selectionOverlay_ = nullptr; }
    if (selHandleL_) { scene_->removeItem(selHandleL_); delete selHandleL_; selHandleL_ = nullptr; }
    if (selHandleR_) { scene_->removeItem(selHandleR_); delete selHandleR_; selHandleR_ = nullptr; }

    if (selection_.isEmpty() || !clip_) return;

    double x1 = sampleToX(selection_.startSample);
    double x2 = sampleToX(selection_.endSample);

    QColor selColor(100, 180, 255, 50);
    selectionOverlay_ = scene_->addRect(x1, 0, x2 - x1, kWaveHeight,
                                         QPen(QColor(100, 180, 255, 160), 1),
                                         QBrush(selColor));
    selectionOverlay_->setZValue(15);

    QColor hc(100, 180, 255, 200);
    selHandleL_ = scene_->addRect(x1 - kSelHandleW / 2, 0, kSelHandleW, kSelHandleH,
                                   Qt::NoPen, QBrush(hc));
    selHandleL_->setZValue(16);
    selHandleL_->setCursor(Qt::SizeHorCursor);
    selHandleL_->setToolTip("Adjust selection start");

    selHandleR_ = scene_->addRect(x2 - kSelHandleW / 2, 0, kSelHandleW, kSelHandleH,
                                   Qt::NoPen, QBrush(hc));
    selHandleR_->setZValue(16);
    selHandleR_->setCursor(Qt::SizeHorCursor);
    selHandleR_->setToolTip("Adjust selection end");
}

void AudioWaveformView::updateSceneSize()
{
    if (!clip_) return;
    auto& ts = clip_->edit.tempoSequence;
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    scene_->setSceneRect(0, 0, beatToX(clipEndBeat + 4.0), kWaveHeight);
}

void AudioWaveformView::updatePlayhead()
{
    if (!clip_ || !editMgr_ || !playheadLine_) return;
    auto pos = editMgr_->transport().getPosition();
    auto& ts = clip_->edit.tempoSequence;
    double x = beatToX(ts.toBeats(pos).inBeats());
    playheadLine_->setLine(x, 0, x, kWaveHeight);
}

void AudioWaveformView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        double factor = std::pow(1.15, event->angleDelta().y() / 120.0);
        double oldBeat = xToBeat(event->position().x() + horizontalScrollBar()->value());
        setPixelsPerBeat(pixelsPerBeat_ * factor);
        horizontalScrollBar()->setValue(static_cast<int>(beatToX(oldBeat) - event->position().x()));
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void AudioWaveformView::mousePressEvent(QMouseEvent* event)
{
    if (!clip_ || !editMgr_) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    QPointF scenePos = mapToScene(event->pos());
    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double xStart = beatToX(clipStartBeat);
    double xEnd = beatToX(clipEndBeat);

    if (event->button() == Qt::RightButton) {
        if (!selection_.isEmpty() && contextMenuCb_)
            contextMenuCb_(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    // Selection handles
    if (selHandleL_ && selHandleL_->contains(selHandleL_->mapFromScene(scenePos))) {
        dragMode_ = DragMode::SelectHandleL;
        dragStartX_ = scenePos.x();
        event->accept();
        return;
    }
    if (selHandleR_ && selHandleR_->contains(selHandleR_->mapFromScene(scenePos))) {
        dragMode_ = DragMode::SelectHandleR;
        dragStartX_ = scenePos.x();
        event->accept();
        return;
    }

    // Fade handles
    if (fadeInHandle_ && fadeInHandle_->contains(fadeInHandle_->mapFromScene(scenePos))) {
        dragMode_ = DragMode::FadeIn;
        dragStartValue_ = clip_->getFadeIn().inSeconds();
        dragStartX_ = scenePos.x();
        event->accept();
        return;
    }
    if (fadeOutHandle_ && fadeOutHandle_->contains(fadeOutHandle_->mapFromScene(scenePos))) {
        dragMode_ = DragMode::FadeOut;
        dragStartValue_ = clip_->getFadeOut().inSeconds();
        dragStartX_ = scenePos.x();
        event->accept();
        return;
    }

    // Loop handles
    if (loopEndHandle_ && loopEndHandle_->contains(loopEndHandle_->mapFromScene(scenePos))) {
        dragMode_ = DragMode::LoopEnd;
        dragStartX_ = scenePos.x();
        if (clip_->beatBasedLooping())
            originalLoopBeats_ = clip_->getLoopLengthBeats().inBeats();
        else {
            double loopSec = clip_->getLoopLength().inSeconds();
            auto loopEndTime = clip_->getPosition().getStart()
                               + tracktion::TimeDuration::fromSeconds(loopSec);
            originalLoopBeats_ = (ts.toBeats(loopEndTime) - ts.toBeats(clip_->getPosition().getStart())).inBeats();
        }
        event->accept();
        return;
    }
    if (loopStartHandle_ && loopStartHandle_->contains(loopStartHandle_->mapFromScene(scenePos))) {
        dragMode_ = DragMode::LoopStart;
        dragStartX_ = scenePos.x();
        event->accept();
        return;
    }

    // Trim handles
    if (trimLeftHandle_ && std::abs(scenePos.x() - xStart) < kHandleWidth * 2) {
        dragMode_ = DragMode::TrimLeft;
        dragStartX_ = scenePos.x();
        dragStartValue_ = clipStartBeat_;
        event->accept();
        return;
    }
    if (trimRightHandle_ && std::abs(scenePos.x() - xEnd) < kHandleWidth * 2) {
        dragMode_ = DragMode::TrimRight;
        dragStartX_ = scenePos.x();
        event->accept();
        return;
    }

    // Click in waveform area: begin selection drag
    if (scenePos.x() >= xStart && scenePos.x() <= xEnd) {
        dragMode_ = DragMode::Select;
        double snappedX = scenePos.x();
        if (snapper_.mode() != SnapMode::Off) {
            double beat = snapper_.snapBeat(xToBeat(scenePos.x()));
            snappedX = beatToX(beat);
        }
        dragStartX_ = snappedX;
        juce::int64 s = xToSample(snappedX);
        selection_ = {s, s};
        updateSelectionOverlay();
        emit selectionChanged();
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void AudioWaveformView::mouseMoveEvent(QMouseEvent* event)
{
    if (dragMode_ == DragMode::None || !clip_ || !editMgr_) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    QPointF scenePos = mapToScene(event->pos());
    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();

    if (dragMode_ == DragMode::Select) {
        double snappedX = scenePos.x();
        if (snapper_.mode() != SnapMode::Off)
            snappedX = beatToX(snapper_.snapBeat(xToBeat(scenePos.x())));
        juce::int64 s1 = xToSample(dragStartX_);
        juce::int64 s2 = xToSample(snappedX);
        selection_ = {std::min(s1, s2), std::max(s1, s2)};
        updateSelectionOverlay();
        emit selectionChanged();
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::SelectHandleL) {
        double snappedX = scenePos.x();
        if (snapper_.mode() != SnapMode::Off)
            snappedX = beatToX(snapper_.snapBeat(xToBeat(scenePos.x())));
        selection_.startSample = xToSample(snappedX);
        selection_.normalize();
        updateSelectionOverlay();
        emit selectionChanged();
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::SelectHandleR) {
        double snappedX = scenePos.x();
        if (snapper_.mode() != SnapMode::Off)
            snappedX = beatToX(snapper_.snapBeat(xToBeat(scenePos.x())));
        selection_.endSample = xToSample(snappedX);
        selection_.normalize();
        updateSelectionOverlay();
        emit selectionChanged();
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::LoopEnd) {
        double rawBeat = xToBeat(scenePos.x()) - clipStartBeat;
        if (rawBeat < 0.25) rawBeat = 0.25;
        double interval = snapper_.gridIntervalBeats();
        if (interval > 0.0)
            rawBeat = std::max(interval, std::round(rawBeat / interval) * interval);
        if (clip_->beatBasedLooping()) {
            clip_->setLoopRangeBeats({
                tracktion::BeatPosition::fromBeats(0.0),
                tracktion::BeatDuration::fromBeats(rawBeat)
            });
        } else {
            auto startTime = ts.toTime(tracktion::BeatPosition::fromBeats(clipStartBeat));
            auto endTime = ts.toTime(tracktion::BeatPosition::fromBeats(clipStartBeat + rawBeat));
            clip_->setLoopRange({
                tracktion::TimePosition::fromSeconds(0.0),
                tracktion::TimeDuration::fromSeconds(endTime.inSeconds() - startTime.inSeconds())
            });
        }
        rebuild();
        emit clipModified();
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::FadeIn) {
        double beat = xToBeat(scenePos.x()) - clipStartBeat;
        if (beat < 0.0) beat = 0.0;
        auto startTime = clip_->getPosition().getStart();
        auto fadeEndTime = ts.toTime(tracktion::BeatPosition::fromBeats(clipStartBeat + beat));
        double fadeSec = std::max(0.0, fadeEndTime.inSeconds() - startTime.inSeconds());
        clip_->setFadeIn(tracktion::TimeDuration::fromSeconds(fadeSec));
        rebuild();
        emit clipModified();
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::FadeOut) {
        double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
        double beat = clipEndBeat - xToBeat(scenePos.x());
        if (beat < 0.0) beat = 0.0;
        auto endTime = clip_->getPosition().getEnd();
        auto fadeStartTime = ts.toTime(tracktion::BeatPosition::fromBeats(clipEndBeat - beat));
        double fadeSec = std::max(0.0, endTime.inSeconds() - fadeStartTime.inSeconds());
        clip_->setFadeOut(tracktion::TimeDuration::fromSeconds(fadeSec));
        rebuild();
        emit clipModified();
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::TrimRight) {
        double endBeat = xToBeat(scenePos.x());
        double interval = snapper_.gridIntervalBeats();
        if (interval > 0.0)
            endBeat = std::max(clipStartBeat + interval, std::round(endBeat / interval) * interval);
        else
            endBeat = std::max(clipStartBeat + 0.1, endBeat);
        clip_->setEnd(ts.toTime(tracktion::BeatPosition::fromBeats(endBeat)), false);
        rebuild();
        emit clipModified();
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::TrimLeft) {
        double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
        double startBeat = scenePos.x() / pixelsPerBeat_ + dragStartValue_;
        double interval = snapper_.gridIntervalBeats();
        if (interval > 0.0)
            startBeat = std::min(clipEndBeat - interval, std::round(startBeat / interval) * interval);
        else
            startBeat = std::min(clipEndBeat - 0.1, startBeat);
        if (startBeat < 0.0) startBeat = 0.0;
        auto newStart = ts.toTime(tracktion::BeatPosition::fromBeats(startBeat));
        auto oldStart = clip_->getPosition().getStart();
        clip_->setStart(newStart, false, false);
        clip_->setOffset(clip_->getPosition().getOffset() + (newStart - oldStart));
        rebuild();
        emit clipModified();
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void AudioWaveformView::mouseReleaseEvent(QMouseEvent* event)
{
    if (dragMode_ == DragMode::Select) {
        if (selection_.lengthSamples() < 10)
            clearSelection();
        dragMode_ = DragMode::None;
        event->accept();
        return;
    }
    if (dragMode_ != DragMode::None) {
        if (clip_ && editMgr_ && editMgr_->edit())
            editMgr_->edit()->restartPlayback();
        dragMode_ = DragMode::None;
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void AudioWaveformView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (clip_) {
        selectAll();
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void AudioWaveformView::drawBackground(QPainter* painter, const QRectF& rect)
{
    auto& theme = ThemeManager::instance().current();
    painter->fillRect(rect, theme.background);

    if (!clip_) return;

    auto& ts = clip_->edit.tempoSequence;
    double clipStartBeat = ts.toBeats(clip_->getPosition().getStart()).inBeats();
    double clipEndBeat = ts.toBeats(clip_->getPosition().getEnd()).inBeats();
    double xStart = beatToX(clipStartBeat);
    double xEnd = beatToX(clipEndBeat);

    QColor bodyColor(theme.clipBody.red(), theme.clipBody.green(), theme.clipBody.blue(), 60);
    painter->fillRect(QRectF(xStart, 0, xEnd - xStart, kWaveHeight), bodyColor);

    double interval = snapper_.gridIntervalBeats();
    if (interval <= 0.0) interval = 1.0;

    double firstBeat = std::floor(xToBeat(rect.left()));
    double lastBeat = std::ceil(xToBeat(rect.right()));

    int timeSigNum = 4;
    if (editMgr_) timeSigNum = editMgr_->getTimeSigNumerator();

    for (double b = firstBeat; b <= lastBeat; b += interval) {
        double x = beatToX(b);
        if (x < rect.left() || x > rect.right()) continue;
        double localBeat = b - clipStartBeat_;
        bool isMajor = (timeSigNum > 0 && std::fmod(localBeat, timeSigNum) < 0.001);
        painter->setPen(QPen(isMajor ? theme.gridLineMajor : theme.gridLine, 1));
        painter->drawLine(QPointF(x, 0), QPointF(x, kWaveHeight));
    }

    painter->setPen(theme.textDim);
    QFont f = painter->font();
    f.setPixelSize(9);
    painter->setFont(f);
    for (double b = firstBeat; b <= lastBeat; b += 1.0) {
        double x = beatToX(b);
        if (x < rect.left() || x > rect.right()) continue;
        int localBeat = static_cast<int>(std::round(b - clipStartBeat_));
        painter->drawText(QPointF(x + 2, 10), QString::number(localBeat + 1));
    }
}

} // namespace OpenDaw
