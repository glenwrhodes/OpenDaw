#include "AutomationLaneHeader.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QSignalBlocker>

namespace OpenDaw {

AutomationLaneHeader::AutomationLaneHeader(te::AudioTrack* track, EditManager* editMgr,
                                           QWidget* parent)
    : QWidget(parent), track_(track), editMgr_(editMgr)
{
    setAccessibleName("Automation Lane Header");
    auto& theme = ThemeManager::instance().current();

    setFixedWidth(metrics::kTrackHeaderWidth);
    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.surface.darker(110));
    setPalette(pal);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);
    mainLayout->setSpacing(2);

    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(2);

    paramLabel_ = new QLabel("AUTO", this);
    paramLabel_->setAccessibleName("Automation Label");
    paramLabel_->setAlignment(Qt::AlignCenter);
    paramLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 7px; font-weight: bold; "
                "background: %2; border-radius: 2px; padding: 1px 3px; }")
            .arg(QColor(Qt::white).name(), theme.accent.name()));
    paramLabel_->setFixedHeight(14);
    topRow->addWidget(paramLabel_);

    bypassBtn_ = new QPushButton("B", this);
    bypassBtn_->setAccessibleName("Bypass Automation");
    bypassBtn_->setToolTip("Bypass automation for this parameter");
    bypassBtn_->setCheckable(true);
    bypassBtn_->setFixedSize(18, 14);
    bypassBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; font-size: 7px; font-weight: bold; padding: 0; }"
                "QPushButton:checked { background: %4; color: #000; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.muteButton.name()));
    connect(bypassBtn_, &QPushButton::toggled, this, &AutomationLaneHeader::bypassToggled);
    topRow->addWidget(bypassBtn_);

    closeBtn_ = new QPushButton(QString::fromUtf8("\xC3\x97"), this); // multiplication sign as X
    closeBtn_->setAccessibleName("Close Automation Lane");
    closeBtn_->setToolTip("Hide automation lane");
    closeBtn_->setFixedSize(18, 14);
    closeBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; font-size: 9px; font-weight: bold; padding: 0; }"
                "QPushButton:hover { background: %4; color: #fff; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.recordArm.name()));
    connect(closeBtn_, &QPushButton::clicked, this, &AutomationLaneHeader::closeRequested);
    topRow->addWidget(closeBtn_);

    mainLayout->addLayout(topRow);

    paramCombo_ = new QComboBox(this);
    paramCombo_->setAccessibleName("Automation Parameter");
    paramCombo_->setToolTip("Select parameter to automate");
    paramCombo_->setFixedHeight(18);
    paramCombo_->setStyleSheet(
        QString("QComboBox { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; font-size: 8px; padding: 1px 2px; }"
                "QComboBox:hover { border: 1px solid %4; }"
                "QComboBox::drop-down { width: 12px; }"
                "QComboBox QAbstractItemView { background: %1; color: %2; "
                "selection-background-color: %5; font-size: 8px; }")
            .arg(theme.background.name(), theme.text.name(),
                 theme.border.name(), theme.accent.name(),
                 theme.surfaceLight.name()));
    populateParamCombo();
    connect(paramCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                if (idx >= 0 && idx < params_.size())
                    emit parameterChanged(params_[idx]);
            });
    mainLayout->addWidget(paramCombo_);

    if (editMgr_) {
        connect(editMgr_, &EditManager::tracksChanged, this, [this]() {
            auto* currentParam = selectedParam();
            populateParamCombo();
            if (currentParam)
                selectParam(currentParam);
        });
    }

    mainLayout->addStretch();

    auto* separator = new QFrame(this);
    separator->setAccessibleName("Automation Lane Separator");
    separator->setFrameShape(QFrame::NoFrame);
    separator->setFixedHeight(1);
    separator->setStyleSheet(
        QString("background: %1;").arg(theme.border.lighter(130).name()));
    mainLayout->addWidget(separator);
}

void AutomationLaneHeader::populateParamCombo()
{
    if (!paramCombo_ || !editMgr_ || !track_) return;

    QSignalBlocker block(paramCombo_);
    paramCombo_->clear();
    params_.clear();

    auto* volParam = editMgr_->getVolumeParam(track_);
    auto* panParam = editMgr_->getPanParam(track_);

    if (volParam) {
        params_.append(volParam);
        paramCombo_->addItem("Volume");
    }
    if (panParam) {
        params_.append(panParam);
        paramCombo_->addItem("Pan");
    }

    auto allParams = editMgr_->getAutomatableParamsForTrack(track_);
    for (auto* p : allParams) {
        if (p == volParam || p == panParam) continue;
        params_.append(p);
        paramCombo_->addItem(
            QString::fromStdString(p->getParameterName().toStdString()));
    }

    if (paramCombo_->count() > 0)
        paramCombo_->setCurrentIndex(0);
}

te::AutomatableParameter* AutomationLaneHeader::selectedParam() const
{
    int idx = paramCombo_ ? paramCombo_->currentIndex() : -1;
    if (idx >= 0 && idx < params_.size())
        return params_[idx];
    return nullptr;
}

void AutomationLaneHeader::setLaneHeight(int h)
{
    setFixedHeight(h);
}

void AutomationLaneHeader::selectParam(te::AutomatableParameter* param)
{
    if (!paramCombo_ || !param) return;
    QSignalBlocker block(paramCombo_);
    for (int i = 0; i < params_.size(); ++i) {
        if (params_[i] == param) {
            paramCombo_->setCurrentIndex(i);
            return;
        }
    }
}

void AutomationLaneHeader::refresh()
{
    populateParamCombo();
}

} // namespace OpenDaw
