#include "RoutingMatrixView.h"
#include "ui/mixer/SendsPanel.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QMenu>
#include <QElapsedTimer>
#include <cmath>
#include <utility>

namespace OpenDaw {

namespace {
QString shortDb(float db)
{
    if (db <= -59.9f) return QStringLiteral("-inf");
    const bool whole = std::abs(db - std::round(db)) < 0.05f;
    return (db > 0.05f ? "+" : "") + QString::number(db, 'f', whole ? 0 : 1);
}

QString trackName(te::AudioTrack* t)
{
    return t ? QString::fromStdString(t->getName().toStdString()) : QString();
}
}

RoutingMatrixView::RoutingMatrixView(EditManager* editMgr, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("Routing Matrix");
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    reloadTimer_.setSingleShot(true);
    reloadTimer_.setInterval(30);
    connect(&reloadTimer_, &QTimer::timeout, this, &RoutingMatrixView::reload);

    connect(editMgr_, &EditManager::tracksChanged, this, &RoutingMatrixView::scheduleReload);
    connect(editMgr_, &EditManager::routingChanged, this, &RoutingMatrixView::scheduleReload);
    connect(editMgr_, &EditManager::editChanged, this, &RoutingMatrixView::scheduleReload);
    connect(editMgr_, &EditManager::sendChanged, this, &RoutingMatrixView::scheduleReload);
    connect(editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
        rows_.clear();
        columns_.clear();
        sends_.clear();
        hover_ = {};
        dragging_ = {};
        update();
    });
}

void RoutingMatrixView::scheduleReload()
{
    if (!reloadTimer_.isActive())
        reloadTimer_.start();
}

void RoutingMatrixView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (reloadPending_)
        reload();
}

void RoutingMatrixView::reload()
{
    if (!isVisible()) {
        reloadPending_ = true;
        return;
    }
    // Mid-drag the cell under the mouse must stay put.
    if (dragging_.valid()) {
        reloadTimer_.start();
        return;
    }
    reloadPending_ = false;

    rows_.clear();
    columns_.clear();
    sends_.clear();
    if (!editMgr_->edit()) {
        updateGeometry();
        update();
        return;
    }

    for (auto* track : editMgr_->getAudioTracksInDisplayOrder()) {
        Row row;
        row.track = track;
        row.name = trackName(track);
        row.colour = editMgr_->getTrackColour(track);
        row.isBus = editMgr_->isBusTrack(track);
        row.outputKey = editMgr_->currentOutputKey(track);
        for (const auto& choice : editMgr_->getOutputChoices(track))
            row.validOutputs << choice.key;
        rows_.push_back(row);
        sends_.push_back(editMgr_->getTrackSends(track));
    }

    columns_.push_back({ false, "master", nullptr, "Master" });
    const auto buses = editMgr_->getBusTracks();
    for (auto* bus : buses)
        columns_.push_back({ false, "track:" + QString::number(bus->itemID.getRawID()), bus, trackName(bus) });
    for (const auto& dev : editMgr_->getAudioOutputDevices())
        if (!dev.isMain)
            columns_.push_back({ false, "device:" + QString::fromStdString(dev.id.toStdString()),
                                 nullptr, dev.name });
    columns_.push_back({ false, "none", nullptr, "None" });
    firstSendColumn_ = static_cast<int>(columns_.size());
    for (auto* bus : buses)
        columns_.push_back({ true, "track:" + QString::number(bus->itemID.getRawID()), bus, trackName(bus) });

    updateGeometry();
    resize(sizeHint());
    update();
}

QSize RoutingMatrixView::sizeHint() const
{
    const int sendCols = static_cast<int>(columns_.size()) - firstSendColumn_;
    const int w = kRowHeaderWidth + static_cast<int>(columns_.size()) * kColumnWidth + kGroupGap
                  + (sendCols == 0 ? 160 : 0) + 12;
    const int h = kGroupHeaderHeight + kColumnHeaderHeight
                  + std::max<int>(1, static_cast<int>(rows_.size())) * kRowHeight + 12;
    return { w, h };
}

QRect RoutingMatrixView::cellRect(int row, int col) const
{
    const int x = kRowHeaderWidth + col * kColumnWidth + (col >= firstSendColumn_ ? kGroupGap : 0);
    const int y = kGroupHeaderHeight + kColumnHeaderHeight + row * kRowHeight;
    return QRect(x, y, kColumnWidth, kRowHeight);
}

RoutingMatrixView::Cell RoutingMatrixView::cellAt(const QPoint& pos) const
{
    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const QRect rowBand(0, cellRect(r, 0).top(), width(), kRowHeight);
        if (!rowBand.contains(pos)) continue;
        for (int c = 0; c < static_cast<int>(columns_.size()); ++c)
            if (cellRect(r, c).contains(pos))
                return { r, c };
        return { r, -1 };
    }
    return {};
}

const EditManager::SendInfo* RoutingMatrixView::sendFor(int row, int col) const
{
    if (row < 0 || row >= static_cast<int>(sends_.size())) return nullptr;
    if (col < firstSendColumn_ || col >= static_cast<int>(columns_.size())) return nullptr;
    for (const auto& s : sends_[row])
        if (s.bus == columns_[col].bus)
            return &s;
    return nullptr;
}

bool RoutingMatrixView::cellEnabled(int row, int col) const
{
    const auto& r = rows_[row];
    const auto& c = columns_[col];
    if (!c.isSend)
        return r.validOutputs.contains(c.key);
    if (c.bus == r.track) return false;
    return sendFor(row, col) || editMgr_->canSendTo(r.track, c.bus);
}

QString RoutingMatrixView::cellDescription(int row, int col) const
{
    const auto& r = rows_[row];
    const auto& c = columns_[col];
    if (!c.isSend) {
        if (!cellEnabled(row, col))
            return c.bus == r.track ? r.name + " can't feed itself"
                                    : "That would feed " + c.label + " back into " + r.name;
        if (r.outputKey == c.key)
            return r.name + " plays " + (c.key == "none" ? QString("nowhere (unplugged)")
                                                         : "into " + c.label);
        return "Click to route " + r.name + (c.key == "none" ? QString(" nowhere") : " into " + c.label);
    }
    if (!cellEnabled(row, col))
        return c.bus == r.track ? r.name + " can't send to itself"
                                : "That would feed " + c.label + " back into " + r.name;
    if (auto* s = sendFor(row, col))
        return QString("%1 sends to %2 at %3 dB, %4-fader%5.\n"
                       "Drag up or down, or scroll, to change it. Double-click for all sends,\n"
                       "right-click for pre/post, mute, phase and remove.")
            .arg(r.name, c.label, shortDb(s->gainDb), s->preFader ? "pre" : "post",
                 s->muted ? " (muted)" : "");
    return "Click to send " + r.name + " to " + c.label + " (its output stays where it is)";
}

void RoutingMatrixView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto& theme = ThemeManager::instance().current();
    p.fillRect(rect(), theme.background.darker(110));

    QFont groupFont;
    groupFont.setPixelSize(10);
    groupFont.setBold(true);
    QFont labelFont;
    labelFont.setPixelSize(10);
    QFont cellFont;
    cellFont.setPixelSize(10);
    cellFont.setBold(true);

    if (rows_.empty()) {
        p.setPen(theme.textDim);
        p.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignLeft | Qt::AlignTop,
                   "Add a track to route it here.");
        return;
    }

    // Group headers
    const int sendCols = static_cast<int>(columns_.size()) - firstSendColumn_;
    const QRect outGroup(cellRect(0, 0).left(), 0, firstSendColumn_ * kColumnWidth, kGroupHeaderHeight);
    const QRect sendGroup(kRowHeaderWidth + firstSendColumn_ * kColumnWidth + kGroupGap, 0,
                          std::max(sendCols, 1) * kColumnWidth + (sendCols == 0 ? 160 : 0),
                          kGroupHeaderHeight);
    p.setFont(groupFont);
    for (const auto& [r, text] : { std::pair{ outGroup, QStringLiteral("OUTPUT TO") },
                                   std::pair{ sendGroup, QStringLiteral("SEND TO") } }) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme.surface);
        p.drawRoundedRect(QRectF(r).adjusted(1, 2, -1, -2), 3, 3);
        p.setPen(theme.textDim);
        p.drawText(r, Qt::AlignCenter, text);
    }
    if (sendCols == 0) {
        p.setFont(labelFont);
        p.setPen(theme.textDim);
        p.drawText(QRect(sendGroup.left(), kGroupHeaderHeight, sendGroup.width(), kColumnHeaderHeight + kRowHeight * 2),
                   Qt::AlignCenter | Qt::TextWordWrap, "Add a bus to send tracks to it");
    }

    // Column headers
    p.setFont(labelFont);
    QFontMetrics lfm(labelFont);
    for (int c = 0; c < static_cast<int>(columns_.size()); ++c) {
        const QRect cr = cellRect(0, c);
        const QRect hr(cr.left() + 2, kGroupHeaderHeight + 2, kColumnWidth - 4, kColumnHeaderHeight - 6);
        const bool hot = hover_.col == c;
        p.setPen(hot ? theme.text : theme.textDim);
        QString label = columns_[c].label;
        if (lfm.horizontalAdvance(label) > hr.width() * 2)
            label = lfm.elidedText(label, Qt::ElideRight, hr.width() * 2 - 8);
        p.drawText(hr, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, label);
    }

    QFont nameFont;
    nameFont.setPixelSize(11);
    QFont badgeFont;
    badgeFont.setPixelSize(8);
    badgeFont.setBold(true);
    QFontMetrics nfm(nameFont);

    for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
        const auto& row = rows_[r];
        const int y = cellRect(r, 0).top();
        const QRect band(0, y, width(), kRowHeight);
        if (r % 2 == 1)
            p.fillRect(band, theme.surface.darker(105));
        if (hover_.row == r) {
            QColor h = theme.surfaceLight;
            h.setAlpha(120);
            p.fillRect(band, h);
        }

        // Row header
        p.fillRect(QRect(6, y + 4, 4, kRowHeight - 8), row.colour);
        int textLeft = 16;
        if (row.isBus) {
            const QRect badge(textLeft, y + (kRowHeight - 14) / 2, 26, 14);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 152, 0));
            p.drawRoundedRect(badge, 3, 3);
            p.setFont(badgeFont);
            p.setPen(QColor(20, 20, 22));
            p.drawText(badge, Qt::AlignCenter, "BUS");
            textLeft += 32;
        }
        p.setFont(nameFont);
        p.setPen(theme.text);
        const int nameWidth = kRowHeaderWidth - textLeft - 8;
        p.drawText(QRect(textLeft, y, nameWidth, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                   nfm.elidedText(row.name, Qt::ElideRight, nameWidth));

        for (int c = 0; c < static_cast<int>(columns_.size()); ++c) {
            const QRect cr = cellRect(r, c);
            const auto& col = columns_[c];
            p.setPen(QPen(theme.border, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cr.adjusted(0, 0, -1, -1));

            const bool enabled = cellEnabled(r, c);
            const bool hot = hover_.row == r && hover_.col == c;
            const QPointF centre = QRectF(cr).center();

            if (!enabled) {
                p.setPen(QPen(theme.border, 1));
                p.drawLine(QPointF(centre.x() - 5, centre.y()), QPointF(centre.x() + 5, centre.y()));
                continue;
            }

            if (!col.isSend) {
                const bool on = row.outputKey == col.key;
                if (on) {
                    p.setPen(QPen(row.colour.darker(140), 1.5));
                    p.setBrush(col.key == "none" ? QBrush(QColor(220, 140, 30)) : QBrush(row.colour));
                    p.drawEllipse(centre, 6.5, 6.5);
                } else if (hot) {
                    p.setPen(QPen(theme.accent, 1.5));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(centre, 6.0, 6.0);
                } else {
                    p.setPen(QPen(theme.border.lighter(130), 1));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(centre, 3.0, 3.0);
                }
                continue;
            }

            if (auto* s = sendFor(r, c)) {
                const QRectF pill = QRectF(cr).adjusted(4, 4, -4, -4);
                QColor fill = s->muted ? theme.surfaceLight : row.colour;
                const float amount = std::clamp((s->gainDb + 60.0f) / 66.0f, 0.0f, 1.0f);
                fill.setAlpha(s->muted ? 255 : static_cast<int>(70 + 150 * amount));
                p.setPen(QPen(hot ? theme.accent : row.colour.darker(130), hot ? 1.5 : 1.0));
                p.setBrush(fill);
                p.drawRoundedRect(pill, 4, 4);
                p.setFont(cellFont);
                p.setPen(s->muted ? theme.textDim : ThemeManager::readableTextOn(row.colour));
                QString text = s->muted ? QStringLiteral("M") : shortDb(s->gainDb);
                if (s->preFader) text += " P";
                p.drawText(pill, Qt::AlignCenter, text);
            } else if (hot) {
                p.setPen(theme.accent);
                p.setFont(cellFont);
                p.drawText(cr, Qt::AlignCenter, "+");
            }
        }
    }
}

void RoutingMatrixView::mousePressEvent(QMouseEvent* event)
{
    const Cell cell = cellAt(event->pos());
    if (!cell.valid()) {
        if (cell.row >= 0 && event->button() == Qt::LeftButton)
            emit trackSelected(rows_[cell.row].track);
        return;
    }
    if (event->button() == Qt::RightButton) {
        showCellMenu(cell.row, cell.col, event->globalPosition().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton || !cellEnabled(cell.row, cell.col)) return;

    auto* track = rows_[cell.row].track;
    const auto& col = columns_[cell.col];
    if (!col.isSend) {
        if (rows_[cell.row].outputKey != col.key)
            editMgr_->setTrackOutputByKey(*track, col.key);
        return;
    }
    if (auto* s = sendFor(cell.row, cell.col)) {
        dragging_ = cell;
        dragStartY_ = event->pos().y();
        dragStartDb_ = std::max(s->gainDb, -60.0f);
        if (editMgr_->edit())
            editMgr_->edit()->getUndoManager().beginNewTransaction("Send Level");
        return;
    }
    editMgr_->addSend(*track, *col.bus);
}

void RoutingMatrixView::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_.valid()) {
        const float db = std::round((dragStartDb_ + (dragStartY_ - event->pos().y()) / 4.0f) * 2.0f) / 2.0f;
        auto* track = rows_[dragging_.row].track;
        auto* bus = columns_[dragging_.col].bus;
        editMgr_->setSendGainDb(*track, *bus, db);
        // The reload waits for the drag; keep the shown value current meanwhile.
        for (auto& s : sends_[dragging_.row])
            if (s.bus == bus) s.gainDb = std::clamp(db, -60.0f, 6.0f);
        update();
        return;
    }
    const Cell cell = cellAt(event->pos());
    if (cell.row != hover_.row || cell.col != hover_.col) {
        hover_ = cell;
        setCursor(cell.valid() && cellEnabled(cell.row, cell.col)
                      ? (sendFor(cell.row, cell.col) ? Qt::SizeVerCursor : Qt::PointingHandCursor)
                      : Qt::ArrowCursor);
        update();
    }
}

void RoutingMatrixView::mouseReleaseEvent(QMouseEvent*)
{
    if (dragging_.valid()) {
        dragging_ = {};
        scheduleReload();
    }
}

void RoutingMatrixView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const Cell cell = cellAt(event->pos());
    if (cell.valid() && sendFor(cell.row, cell.col))
        SendsPanel::showFor(editMgr_, rows_[cell.row].track,
                            event->globalPosition().toPoint(), window());
}

void RoutingMatrixView::wheelEvent(QWheelEvent* event)
{
    const Cell cell = cellAt(event->position().toPoint());
    auto* s = cell.valid() ? sendFor(cell.row, cell.col) : nullptr;
    if (!s || event->angleDelta().y() == 0) {
        event->ignore();   // let the scroll area scroll
        return;
    }
    static QElapsedTimer lastWheel;
    if (editMgr_->edit() && (!lastWheel.isValid() || lastWheel.elapsed() > 700))
        editMgr_->edit()->getUndoManager().beginNewTransaction("Send Level");
    lastWheel.restart();
    const float db = std::round(std::max(s->gainDb, -60.0f) + (event->angleDelta().y() > 0 ? 1.0f : -1.0f));
    editMgr_->setSendGainDb(*rows_[cell.row].track, *columns_[cell.col].bus, db);
    event->accept();
}

void RoutingMatrixView::leaveEvent(QEvent*)
{
    hover_ = {};
    update();
}

bool RoutingMatrixView::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const Cell cell = cellAt(help->pos());
        if (cell.valid())
            QToolTip::showText(help->globalPos(), cellDescription(cell.row, cell.col), this);
        else if (cell.row >= 0)
            QToolTip::showText(help->globalPos(), "Click to show " + rows_[cell.row].name + "'s effects", this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void RoutingMatrixView::showCellMenu(int row, int col, const QPoint& globalPos)
{
    auto* track = rows_[row].track;
    const auto& column = columns_[col];
    QMenu menu;
    menu.setAccessibleName("Routing Matrix Menu");

    if (!column.isSend) {
        auto* a = menu.addAction(column.key == "none" ? "Unplug " + rows_[row].name
                                                      : "Route " + rows_[row].name + " into " + column.label,
                                 this, [this, track, key = column.key]() {
                                     editMgr_->setTrackOutputByKey(*track, key);
                                 });
        a->setEnabled(cellEnabled(row, col) && rows_[row].outputKey != column.key);
    } else if (auto* s = sendFor(row, col)) {
        auto* bus = column.bus;
        const auto info = *s;
        auto* levels = menu.addMenu("Level");
        for (float db : { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f, -30.0f }) {
            auto* a = levels->addAction(shortDb(db) + " dB", this, [this, track, bus, db]() {
                if (editMgr_->edit())
                    editMgr_->edit()->getUndoManager().beginNewTransaction("Send Level");
                editMgr_->setSendGainDb(*track, *bus, db);
            });
            a->setCheckable(true);
            a->setChecked(std::abs(info.gainDb - db) < 0.05f);
        }
        auto* pre = menu.addAction("Pre-Fader", this, [this, track, bus, info]() {
            editMgr_->setSendPreFader(*track, *bus, !info.preFader);
        });
        pre->setCheckable(true);
        pre->setChecked(info.preFader);
        auto* mute = menu.addAction("Mute Send", this, [this, track, bus, info]() {
            editMgr_->setSendMuted(*track, *bus, !info.muted);
        });
        mute->setCheckable(true);
        mute->setChecked(info.muted);
        auto* phase = menu.addAction("Invert Phase", this, [this, track, bus, info]() {
            editMgr_->setSendPhaseInverted(*track, *bus, !info.phaseInverted);
        });
        phase->setCheckable(true);
        phase->setChecked(info.phaseInverted);
        menu.addSeparator();
        menu.addAction("Remove Send", this, [this, track, bus]() { editMgr_->removeSend(*track, *bus); });
    } else {
        auto* a = menu.addAction("Send " + rows_[row].name + " to " + column.label, this,
                                 [this, track, bus = column.bus]() { editMgr_->addSend(*track, *bus); });
        a->setEnabled(cellEnabled(row, col));
    }
    menu.exec(globalPos);
}

} // namespace OpenDaw
