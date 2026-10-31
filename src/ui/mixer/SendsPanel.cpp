#include "SendsPanel.h"
#include "utils/ThemeManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>

namespace OpenDaw {

namespace {
QString trackName(te::AudioTrack* t)
{
    return t ? QString::fromStdString(t->getName().toStdString()) : QString();
}

QString formatDb(float db)
{
    if (db <= -59.9f) return QStringLiteral("-inf");
    return QString::number(db, 'f', 1) + " dB";
}
}

void SendsPanel::showFor(EditManager* editMgr, te::AudioTrack* track,
                         const QPoint& globalPos, QWidget* parent)
{
    if (!editMgr || !track) return;
    auto* panel = new SendsPanel(editMgr, track, parent);
    panel->adjustSize();

    QPoint pos = globalPos;
    if (auto* screen = QGuiApplication::screenAt(globalPos)) {
        const QRect avail = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), avail.left(), avail.right() - panel->width()));
        if (pos.y() + panel->height() > avail.bottom())
            pos.setY(std::max(avail.top(), pos.y() - panel->height()));
    }
    panel->move(pos);
    panel->show();
}

void SendsPanel::fillAddSendMenu(QMenu* menu, EditManager* editMgr, te::AudioTrack* track)
{
    if (!menu || !editMgr || !track) return;

    std::vector<te::AudioTrack*> existing;
    for (const auto& s : editMgr->getTrackSends(track))
        existing.push_back(s.bus);

    bool any = false;
    for (auto* bus : editMgr->getBusTracks()) {
        if (std::find(existing.begin(), existing.end(), bus) != existing.end()) continue;
        if (!editMgr->canSendTo(track, bus)) continue;
        menu->addAction(trackName(bus), [editMgr, track, bus]() {
            editMgr->addSend(*track, *bus);
        });
        any = true;
    }
    if (!any) {
        auto* none = menu->addAction("No other buses yet");
        none->setEnabled(false);
    }

    menu->addSeparator();
    for (const QString fx : { QStringLiteral("Reverb"), QStringLiteral("Delay"), QStringLiteral("Chorus") }) {
        menu->addAction("New " + fx + " Bus", [editMgr, track, fx]() {
            if (auto* bus = editMgr->addFxBus(fx))
                editMgr->addSend(*track, *bus);
        });
    }
    menu->addAction("New Empty Bus", [editMgr, track]() {
        if (auto* bus = editMgr->addBusTrack())
            editMgr->addSend(*track, *bus);
    });
}

SendsPanel::SendsPanel(EditManager* editMgr, te::AudioTrack* track, QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint),
      editMgr_(editMgr), track_(track), trackId_(track->itemID.getRawID())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAccessibleName("Sends Panel");
    setObjectName("sendsPanel");
    setFixedWidth(380);

    auto& theme = ThemeManager::instance().current();
    setStyleSheet(QString(
        "QFrame#sendsPanel { background: %1; border: 1px solid %2; border-radius: 4px; }"
        "QLabel { color: %3; background: transparent; }"
        "QLabel#dim { color: %4; }"
        "QPushButton { background: %5; color: %3; border: 1px solid %2; border-radius: 3px;"
        " padding: 2px 8px; font-size: 11px; }"
        "QPushButton:hover { border-color: %6; }"
        "QPushButton:checked { background: %6; color: %7; }")
        .arg(theme.surface.name(), theme.border.name(), theme.text.name(),
             theme.textDim.name(), theme.background.name(), theme.accent.name(),
             ThemeManager::readableTextOn(theme.accent).name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);

    auto* title = new QLabel("<b>Sends from " + trackName(track_).toHtmlEscaped() + "</b>", this);
    layout->addWidget(title);

    rowsLayout_ = new QVBoxLayout();
    rowsLayout_->setSpacing(4);
    layout->addLayout(rowsLayout_);

    auto* addBtn = new QPushButton("+ Add Send", this);
    addBtn->setAccessibleName("Add Send");
    addBtn->setToolTip("Send a copy of this track to a bus, such as a shared reverb");
    auto* addMenu = new QMenu(addBtn);
    connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu]() {
        addMenu->clear();
        fillAddSendMenu(addMenu, editMgr_, track_);
    });
    addBtn->setMenu(addMenu);
    layout->addWidget(addBtn, 0, Qt::AlignLeft);

    // Rebuild after the change settles: the change may come from a button in a row.
    auto later = [this]() { QTimer::singleShot(0, this, &SendsPanel::rebuild); };
    connect(editMgr_, &EditManager::routingChanged, this, later);
    connect(editMgr_, &EditManager::tracksChanged, this, later);
    connect(editMgr_, &EditManager::aboutToChangeEdit, this, &QWidget::close);

    rebuild();
}

void SendsPanel::rebuild()
{
    bool alive = false;
    for (auto* t : editMgr_->getAudioTracks())
        if (t == track_ && t->itemID.getRawID() == trackId_) alive = true;
    if (!alive) {
        close();
        return;
    }

    while (auto* item = rowsLayout_->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    auto sends = editMgr_->getTrackSends(track_);
    if (sends.empty()) {
        auto* hint = new QLabel("No sends yet. A send copies this track into a bus "
                                "at its own level, so several tracks can share one "
                                "reverb or delay.", this);
        hint->setObjectName("dim");
        hint->setWordWrap(true);
        rowsLayout_->addWidget(hint);
    }

    for (const auto& send : sends) {
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);

        auto* bus = send.bus;
        auto* name = new QLabel(row);
        name->setFixedWidth(76);
        const QColor busColour = editMgr_->getTrackColour(bus);
        name->setText(QString("<span style='color:%1'>&#9679;</span> %2")
                          .arg(busColour.name(), trackName(bus).toHtmlEscaped()));
        name->setToolTip(trackName(bus));
        h->addWidget(name);

        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setAccessibleName("Send Level");
        slider->setRange(-120, 12);   // half-dB steps, -60 to +6
        slider->setValue(static_cast<int>(std::lround(send.gainDb * 2.0f)));
        slider->setToolTip("Send level. Scroll or drag to change it.");
        h->addWidget(slider, 1);

        auto* value = new QLabel(formatDb(send.gainDb), row);
        value->setFixedWidth(52);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(value);

        auto* pre = new QPushButton("PRE", row);
        pre->setAccessibleName("Pre-Fader");
        pre->setCheckable(true);
        pre->setChecked(send.preFader);
        pre->setFixedWidth(40);
        pre->setToolTip("Pre-fader: the send ignores the track's fader.\n"
                        "Off (post-fader): turning the track down turns the send down too.");
        h->addWidget(pre);

        auto* mute = new QPushButton("M", row);
        mute->setAccessibleName("Mute Send");
        mute->setCheckable(true);
        mute->setChecked(send.muted);
        mute->setFixedWidth(26);
        mute->setToolTip("Mute this send, keeping its level");
        h->addWidget(mute);

        auto* phase = new QPushButton(QString::fromUtf8("\u00D8"), row);
        phase->setAccessibleName("Invert Send Phase");
        phase->setCheckable(true);
        phase->setChecked(send.phaseInverted);
        phase->setFixedWidth(26);
        phase->setToolTip("Invert the polarity of this send");
        h->addWidget(phase);

        auto* remove = new QPushButton(QString::fromUtf8("\u2715"), row);
        remove->setAccessibleName("Remove Send");
        remove->setToolTip("Remove this send");
        remove->setFixedWidth(26);
        h->addWidget(remove);

        auto* track = track_;
        connect(slider, &QSlider::sliderPressed, this, [this]() {
            if (editMgr_->edit())
                editMgr_->edit()->getUndoManager().beginNewTransaction("Send Level");
        });
        connect(slider, &QSlider::valueChanged, this, [this, track, bus, value](int v) {
            const float db = v / 2.0f;
            editMgr_->setSendGainDb(*track, *bus, db);
            value->setText(formatDb(db));
        });
        connect(pre, &QPushButton::toggled, this, [this, track, bus](bool on) {
            editMgr_->setSendPreFader(*track, *bus, on);
        });
        connect(mute, &QPushButton::toggled, this, [this, track, bus](bool on) {
            editMgr_->setSendMuted(*track, *bus, on);
        });
        connect(phase, &QPushButton::toggled, this, [this, track, bus](bool on) {
            editMgr_->setSendPhaseInverted(*track, *bus, on);
        });
        connect(remove, &QPushButton::clicked, this, [this, track, bus]() {
            editMgr_->removeSend(*track, *bus);
        });

        rowsLayout_->addWidget(row);
    }
    adjustSize();
}

} // namespace OpenDaw
