#include "MixerView.h"
#include "utils/ThemeManager.h"
#include <QFrame>

namespace freedaw {

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

    scrollArea_->setWidget(stripContainer_);
    mainLayout->addWidget(scrollArea_, 1);

    // Master strip on the right
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet(QString("color: %1;").arg(theme.border.name()));
    mainLayout->addWidget(sep);

    masterStrip_ = ChannelStrip::createMasterStrip(editMgr_, this);
    mainLayout->addWidget(masterStrip_);

    connect(editMgr_, &EditManager::tracksChanged,
            this, &MixerView::rebuildStrips);

    rebuildStrips();
}

void MixerView::rebuildStrips()
{
    for (auto* s : strips_) {
        stripLayout_->removeWidget(s);
        s->deleteLater();
    }
    strips_.clear();

    auto tracks = editMgr_->getAudioTracks();
    for (auto* track : tracks) {
        auto* strip = new ChannelStrip(track, editMgr_, stripContainer_);
        connect(strip, &ChannelStrip::effectInsertRequested,
                this, &MixerView::effectInsertRequested);
        stripLayout_->addWidget(strip);
        strips_.push_back(strip);
    }
}

} // namespace freedaw
