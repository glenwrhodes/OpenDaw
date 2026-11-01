#include "TakeLanes.h"
#include "GridSnapper.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include "utils/WaveformCache.h"
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QMenu>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace OpenDaw {

namespace {
QColor mixColours(const QColor& a, const QColor& b, double t)
{
    return QColor(int(a.red() * t + b.red() * (1 - t)),
                  int(a.green() * t + b.green() * (1 - t)),
                  int(a.blue() * t + b.blue() * (1 - t)));
}
}

// ── TakeLaneItem ─────────────────────────────────────────────────────────────

TakeLaneItem::TakeLaneItem(EditManager* editMgr, te::AudioTrack* track,
                           const EditManager::TakeInfo& take, double pixelsPerBeat,
                           GridSnapper* snapper)
    : editMgr_(editMgr), trackId_(track->itemID),
      trackColour_(editMgr->getTrackColour(track)), take_(take),
      pixelsPerBeat_(pixelsPerBeat), snapper_(snapper)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setPen(Qt::NoPen);
    setToolTip(QString("%1: drag across it to use that part in the comp, "
                       "double-click to use all of it").arg(take.name));

    usedRanges_ = editMgr->takeUsedRanges(track, take.id);

    if (take.file.existsAsFile()) {
        fileSecs_ = te::AudioFile(editMgr->edit()->engine, take.file).getLength();
        // Enough points for the take's width at this zoom, over the whole file.
        const double blockPx = timeToX(take.start + take.length) - timeToX(take.start);
        const double ratio = (take.length > 0.0 && fileSecs_ > 0.0) ? fileSecs_ / take.length : 1.0;
        const int points = std::clamp(int(blockPx * ratio / 1.5), 64, 8192);
        auto data = WaveformCache::instance().getWaveform(take.file, points);
        waveMin_ = std::move(data.minValues);
        waveMax_ = std::move(data.maxValues);
    }
}

void TakeLaneItem::setSceneWidth(double width)
{
    setRect(0, 0, width, kLaneHeight);
}

double TakeLaneItem::timeToX(double secs) const
{
    if (!editMgr_->edit()) return 0.0;
    auto& ts = editMgr_->edit()->tempoSequence;
    return ts.toBeats(tracktion::TimePosition::fromSeconds(secs)).inBeats() * pixelsPerBeat_;
}

double TakeLaneItem::xToTime(double x) const
{
    if (!editMgr_->edit()) return 0.0;
    auto& ts = editMgr_->edit()->tempoSequence;
    return ts.toTime(tracktion::BeatPosition::fromBeats(std::max(0.0, x) / pixelsPerBeat_)).inSeconds();
}

double TakeLaneItem::snappedX(double x) const
{
    double beat = x / pixelsPerBeat_;
    if (snapper_)
        beat = snapper_->snapBeat(beat);
    const double x0 = timeToX(take_.start);
    const double x1 = timeToX(take_.start + take_.length);
    return std::clamp(beat * pixelsPerBeat_, x0, x1);
}

void TakeLaneItem::runDeferred(std::function<void(EditManager&, te::AudioTrack&)> action) const
{
    auto* mgr = editMgr_;
    const auto id = trackId_;
    QTimer::singleShot(0, mgr, [mgr, id, action]() {
        if (auto* track = mgr->findAudioTrack(id))
            action(*mgr, *track);
    });
}

void TakeLaneItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    auto& theme = ThemeManager::instance().current();
    const QRectF r = rect();
    const QColor laneBg = theme.surface.darker(theme.isLight ? 104 : 118);
    p->fillRect(r, laneBg);
    p->setPen(QPen(theme.border, 1.0));
    p->drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));

    const double x0 = timeToX(take_.start);
    const double x1 = timeToX(take_.start + take_.length);
    if (x1 - x0 < 1.0) return;
    const QRectF block(x0, 3.0, x1 - x0, r.height() - 6.0);

    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(mixColours(trackColour_, laneBg, 0.22));
    p->drawRoundedRect(block, 3, 3);

    // The parts of this take that are in the comp.
    const QColor usedFill = mixColours(trackColour_, laneBg, 0.5);
    for (const auto& [s, e] : usedRanges_) {
        const double ux0 = std::max(block.left(), timeToX(s));
        const double ux1 = std::min(block.right(), timeToX(e));
        if (ux1 - ux0 < 0.5) continue;
        p->fillRect(QRectF(ux0, block.top(), ux1 - ux0, block.height()), usedFill);
        p->fillRect(QRectF(ux0, block.top(), ux1 - ux0, 2.5), theme.accent);
    }

    if (!waveMin_.empty() && fileSecs_ > 0.0) {
        const int n = int(waveMin_.size());
        const double midY = block.center().y();
        const double amp = block.height() / 2.0 * 0.85;
        const double secsPerPoint = fileSecs_ / n;
        // Only the part of the file the take covers.
        const int first = std::clamp(int(take_.offset / secsPerPoint), 0, n - 1);
        const int last = std::clamp(int((take_.offset + take_.length) / secsPerPoint), first, n - 1);
        auto xFor = [&](int i) { return timeToX(take_.start + i * secsPerPoint - take_.offset); };

        QPainterPath path;
        path.moveTo(xFor(first), midY);
        for (int i = first; i <= last; ++i)
            path.lineTo(xFor(i), midY - waveMax_[size_t(i)] * amp);
        for (int i = last; i >= first; --i)
            path.lineTo(xFor(i), midY - waveMin_[size_t(i)] * amp);
        path.closeSubpath();

        QColor wave = theme.isLight ? trackColour_.darker(170) : trackColour_.lighter(170);
        wave.setAlpha(210);
        p->save();
        p->setClipRect(block);
        p->setBrush(wave);
        p->drawPath(path);
        p->restore();
    }

    p->setPen(QPen(trackColour_.darker(150), 1.0));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(block, 3, 3);

    QFont f = p->font();
    f.setPixelSize(10);
    f.setBold(true);
    p->setFont(f);
    const QString label = take_.file.existsAsFile() ? take_.name : take_.name + " (file missing)";
    const QFontMetrics fm(f);
    const QRectF labelRect(block.left() + 3, block.top() + 3,
                           std::min(block.width() - 6, double(fm.horizontalAdvance(label) + 8)), 14);
    if (labelRect.width() > 12) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, theme.isLight ? 40 : 110));
        p->drawRoundedRect(labelRect, 3, 3);
        p->setPen(theme.isLight ? theme.text : QColor(240, 242, 245));
        p->drawText(labelRect.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(label, Qt::ElideRight, int(labelRect.width() - 6)));
    }

    if (selecting_) {
        QColor sel = theme.accent;
        sel.setAlpha(80);
        const double sx = std::min(selStartX_, selEndX_);
        const double sw = std::abs(selEndX_ - selStartX_);
        p->setPen(QPen(theme.accent, 1.0));
        p->setBrush(sel);
        p->drawRect(QRectF(sx, block.top(), sw, block.height()));
    }
}

void TakeLaneItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !take_.file.existsAsFile()) {
        event->ignore();
        return;
    }
    selecting_ = true;
    selStartX_ = selEndX_ = snappedX(event->scenePos().x());
    update();
    event->accept();
}

void TakeLaneItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (!selecting_) return;
    selEndX_ = snappedX(event->scenePos().x());
    update();
    event->accept();
}

void TakeLaneItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (!selecting_ || event->button() != Qt::LeftButton) return;
    selecting_ = false;
    update();
    if (std::abs(selEndX_ - selStartX_) < 3.0) return;

    const double t0 = xToTime(std::min(selStartX_, selEndX_));
    const double t1 = xToTime(std::max(selStartX_, selEndX_));
    const int takeId = take_.id;
    runDeferred([takeId, t0, t1](EditManager& mgr, te::AudioTrack& track) {
        mgr.compTake(track, takeId, t0, t1);
    });
    event->accept();
}

void TakeLaneItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    selecting_ = false;
    const int takeId = take_.id;
    runDeferred([takeId](EditManager& mgr, te::AudioTrack& track) {
        mgr.useWholeTake(track, takeId);
    });
    event->accept();
}

void TakeLaneItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    const int takeId = take_.id;
    QMenu menu;
    menu.setAccessibleName("Take Menu");
    auto* useAll = menu.addAction("Use Whole " + take_.name, [this, takeId]() {
        runDeferred([takeId](EditManager& mgr, te::AudioTrack& track) {
            mgr.useWholeTake(track, takeId);
        });
    });
    useAll->setEnabled(take_.file.existsAsFile());
    menu.addSeparator();
    menu.addAction("Delete " + take_.name, [this, takeId]() {
        runDeferred([takeId](EditManager& mgr, te::AudioTrack& track) {
            mgr.deleteTake(track, takeId);
        });
    });
    menu.addAction("Delete Takes Not in the Comp", [this]() {
        runDeferred([](EditManager& mgr, te::AudioTrack& track) {
            mgr.deleteUnusedTakes(track);
        });
    });
    menu.exec(event->screenPos());
    event->accept();
}

void TakeLaneItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    const double x = event->scenePos().x();
    const bool overTake = x >= timeToX(take_.start) && x <= timeToX(take_.start + take_.length);
    setCursor(overTake ? Qt::IBeamCursor : Qt::ArrowCursor);
}

// ── TakeLanesHeader ──────────────────────────────────────────────────────────

TakeLanesHeader::TakeLanesHeader(EditManager* editMgr, te::AudioTrack* track, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr), trackId_(track->itemID),
      takes_(editMgr->getTakes(track))
{
    setAccessibleName("Take Lanes");
    setMouseTracking(true);
    for (const auto& take : takes_)
        inComp_.push_back(!editMgr->takeUsedRanges(track, take.id).empty());
    // The header column puts 1px between widgets; the lanes have no gaps.
    setFixedHeight(std::max(1, int(takes_.size() * TakeLaneItem::kLaneHeight) - 1));
}

int TakeLanesHeader::rowAt(int y) const
{
    const int row = int(y / TakeLaneItem::kLaneHeight);
    return (row >= 0 && row < int(takes_.size())) ? row : -1;
}

QRect TakeLanesHeader::useRect(int row) const
{
    const int top = int(row * TakeLaneItem::kLaneHeight);
    return QRect(width() - 48, top + 10, 20, 20);
}

QRect TakeLanesHeader::deleteRect(int row) const
{
    const int top = int(row * TakeLaneItem::kLaneHeight);
    return QRect(width() - 25, top + 10, 20, 20);
}

void TakeLanesHeader::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter p(this);
    p.fillRect(rect(), theme.surface.darker(theme.isLight ? 104 : 118));

    QFont nameFont = font();
    nameFont.setPixelSize(11);
    QFont iconFont = icons::materialIcons(15);

    for (int row = 0; row < int(takes_.size()); ++row) {
        const int top = int(row * TakeLaneItem::kLaneHeight);
        const QRect rowRect(0, top, width(), int(TakeLaneItem::kLaneHeight));
        if (row == hoverRow_)
            p.fillRect(rowRect, theme.surfaceLight);

        // A dot marks takes that are (partly) in the comp.
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(inComp_[size_t(row)] ? theme.accent : theme.border);
        p.drawEllipse(QPointF(metrics::kTrackColourStrip + 12, top + TakeLaneItem::kLaneHeight / 2), 3.5, 3.5);

        p.setFont(nameFont);
        p.setPen(inComp_[size_t(row)] ? theme.text : theme.textDim);
        const QRect textRect(metrics::kTrackColourStrip + 22, top, width() - 80, rowRect.height());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(nameFont).elidedText(takes_[size_t(row)].name, Qt::ElideRight,
                                                     textRect.width()));

        p.setFont(iconFont);
        p.setPen(row == hoverRow_ ? theme.text : theme.textDim);
        p.drawText(useRect(row), Qt::AlignCenter, QString(icons::mi::Check));
        p.drawText(deleteRect(row), Qt::AlignCenter, QString(icons::mi::Close));

        p.setPen(theme.border);
        p.drawLine(0, rowRect.bottom(), width(), rowRect.bottom());
    }
}

void TakeLanesHeader::mousePressEvent(QMouseEvent* event)
{
    const int row = rowAt(event->pos().y());
    if (row < 0 || event->button() != Qt::LeftButton) return;
    const int takeId = takes_[size_t(row)].id;
    const bool use = useRect(row).contains(event->pos());
    const bool del = deleteRect(row).contains(event->pos());
    if (!use && !del) return;

    // Both rebuild the take lanes, which deletes this header.
    auto* mgr = editMgr_;
    const auto id = trackId_;
    QTimer::singleShot(0, mgr, [mgr, id, takeId, use]() {
        if (auto* track = mgr->findAudioTrack(id)) {
            if (use) mgr->useWholeTake(*track, takeId);
            else     mgr->deleteTake(*track, takeId);
        }
    });
    event->accept();
}

void TakeLanesHeader::mouseMoveEvent(QMouseEvent* event)
{
    const int row = rowAt(event->pos().y());
    if (row != hoverRow_) {
        hoverRow_ = row;
        update();
    }
    const bool overButton = row >= 0 && (useRect(row).contains(event->pos())
                                         || deleteRect(row).contains(event->pos()));
    setCursor(overButton ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void TakeLanesHeader::leaveEvent(QEvent*)
{
    hoverRow_ = -1;
    update();
}

bool TakeLanesHeader::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int row = rowAt(help->pos().y());
        if (row >= 0 && useRect(row).contains(help->pos()))
            QToolTip::showText(help->globalPos(), "Use all of " + takes_[size_t(row)].name);
        else if (row >= 0 && deleteRect(row).contains(help->pos()))
            QToolTip::showText(help->globalPos(), "Delete " + takes_[size_t(row)].name
                               + " (its audio already in the comp stays)");
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

} // namespace OpenDaw
