#include "MixerView.h"
#include "utils/ThemeManager.h"
#include <QFrame>
#include <QMouseEvent>

namespace freedaw {
namespace {
ChannelStrip* findParentStrip(QWidget* w)
{
    while (w) {
        if (auto* strip = qobject_cast<ChannelStrip*>(w))
            return strip;
        w = w->parentWidget();
    }
    return nullptr;
}
}

MixerView::MixerView(EditManager* editMgr, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("Mixer View");
    auto& theme = ThemeManager::instance().current();

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setAutoFillBackground(true);
    QPalette viewPal;
    viewPal.setColor(QPalette::Window, theme.background);
    setPalette(viewPal);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameStyle(QFrame::NoFrame);
    scrollArea_->setStyleSheet(
        QString("QScrollArea { background: %1; border: none; }")
            .arg(theme.background.name()));

    stripContainer_ = new QWidget();
    stripContainer_->setAutoFillBackground(true);
    stripContainer_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    QPalette contPal;
    contPal.setColor(QPalette::Window, theme.background);
    stripContainer_->setPalette(contPal);
    stripLayout_ = new QHBoxLayout(stripContainer_);
    stripLayout_->setContentsMargins(4, 4, 4, 4);
    stripLayout_->setSpacing(2);
    stripLayout_->setAlignment(Qt::AlignLeft);
    stripContainer_->installEventFilter(this);
    scrollArea_->viewport()->installEventFilter(this);

    scrollArea_->setWidget(stripContainer_);
    mainLayout->addWidget(scrollArea_, 1);

    // Master strip on the right
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet(QString("color: %1;").arg(theme.border.name()));
    mainLayout->addWidget(sep);

    masterStrip_ = ChannelStrip::createMasterStrip(editMgr_, this);
    mainLayout->addWidget(masterStrip_);

    connect(editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
        for (auto* s : strips_) {
            stripLayout_->removeWidget(s);
            delete s;
        }
        strips_.clear();
    });

    connect(editMgr_, &EditManager::tracksChanged,
            this, &MixerView::rebuildStrips);
    connect(editMgr_, &EditManager::routingChanged,
            this, &MixerView::rebuildStrips, Qt::QueuedConnection);
    connect(editMgr_, &EditManager::editChanged,
            this, &MixerView::refreshStrips);

    rebuildStrips();
}

void MixerView::rebuildStrips()
{
    for (auto* s : strips_) {
        stripLayout_->removeWidget(s);
        delete s;
    }
    strips_.clear();

    auto tracks = editMgr_->getAudioTracks();
    bool selectedTrackStillExists = false;
    for (auto* track : tracks) {
        if (track == selectedTrack_) {
            selectedTrackStillExists = true;
            break;
        }
    }
    if (!selectedTrackStillExists)
        selectedTrack_ = nullptr;

    for (auto* track : tracks) {
        auto* strip = new ChannelStrip(track, editMgr_, stripContainer_);
        connect(strip, &ChannelStrip::effectInsertRequested,
                this, &MixerView::effectInsertRequested);
        connect(strip, &ChannelStrip::instrumentSelectRequested,
                this, &MixerView::instrumentSelectRequested);
        strip->setSelected(track == selectedTrack_);
        stripLayout_->addWidget(strip);
        strips_.push_back(strip);
    }
}

void MixerView::refreshStrips()
{
    for (auto* strip : strips_)
        strip->refresh();
}

void MixerView::setSelectedTrack(te::AudioTrack* track)
{
    selectedTrack_ = track;
    for (auto* strip : strips_)
        strip->setSelected(strip->track() == selectedTrack_);
}

bool MixerView::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == stripContainer_ || watched == scrollArea_->viewport())
        && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() != Qt::LeftButton)
            return QWidget::eventFilter(watched, event);

        QPoint containerPos = mouseEvent->pos();
        if (watched == scrollArea_->viewport())
            containerPos = stripContainer_->mapFrom(scrollArea_->viewport(), mouseEvent->pos());

        auto* clickedWidget = stripContainer_->childAt(containerPos);
        auto* clickedStrip = findParentStrip(clickedWidget);

        if (clickedStrip && clickedStrip->track()) {
            emit trackSelected(clickedStrip->track());
        } else {
            emit trackSelected(nullptr);
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace freedaw
