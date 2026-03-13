#include "TrackHeaderWidget.h"
#include "ui/effects/PluginEditorWindow.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QMenu>
#include <cmath>

namespace {
float dbToNormalized(float db) {
    constexpr float minDb = -60.0f;
    constexpr float maxDb = 6.0f;
    if (db <= minDb) return 0.0f;
    if (db >= maxDb) return 1.0f;
    return (db - minDb) / (maxDb - minDb);
}
}

namespace freedaw {

TrackHeaderWidget::TrackHeaderWidget(te::AudioTrack* track, EditManager* editMgr,
                                     QWidget* parent)
    : QWidget(parent), track_(track), editMgr_(editMgr)
{
    setAccessibleName("Track Header");
    setObjectName("trackHeaderWidget");
    setAttribute(Qt::WA_StyledBackground, true);
    auto& theme = ThemeManager::instance().current();

    setFixedWidth(140);
    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.surface);
    setPalette(pal);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 3, 4, 3);
    mainLayout->setSpacing(2);

    bool isMidi = editMgr_->isMidiTrack(track_);

    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(3);

    nameLabel_ = new QLabel(
        QString::fromStdString(track_->getName().toStdString()), this);
    nameLabel_->setAccessibleName("Track Name");
    nameLabel_->setAlignment(Qt::AlignCenter);
    nameLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                "background: %2; border-radius: 2px; padding: 2px; }")
            .arg(theme.text.name(), theme.background.name()));
    topRow->addWidget(nameLabel_, 1);

    auto* typeBadge = new QLabel(isMidi ? "MIDI" : "AUDIO", this);
    typeBadge->setAccessibleName("Track Type");
    typeBadge->setAlignment(Qt::AlignCenter);
    QColor badgeBg = isMidi ? theme.midiClipBody : theme.accent;
    typeBadge->setStyleSheet(
        QString("QLabel { color: #fff; font-size: 7px; font-weight: bold; "
                "background: %1; border-radius: 2px; padding: 1px 3px; }")
            .arg(badgeBg.name()));
    typeBadge->setFixedHeight(16);
    topRow->addWidget(typeBadge);

    mainLayout->addLayout(topRow);

    if (isMidi) {
        auto* instrument = editMgr_->getTrackInstrument(track_);
        QString instrName = instrument
            ? QString::fromStdString(instrument->getName().toStdString())
            : "No Instrument";
        instrumentBtn_ = new QPushButton(instrName, this);
        instrumentBtn_->setAccessibleName("Instrument");
        instrumentBtn_->setToolTip("Click to open VST UI, right-click to change instrument");
        instrumentBtn_->setFixedHeight(18);
        instrumentBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
        instrumentBtn_->setStyleSheet(
            QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 2px; font-size: 8px; padding: 1px 3px; }"
                    "QPushButton:hover { background: %4; }")
                .arg(theme.background.name(), theme.text.name(),
                     theme.border.name(), theme.surfaceLight.name()));

        connect(instrumentBtn_, &QPushButton::clicked, this, [this]() {
            auto* instr = editMgr_->getTrackInstrument(track_);
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(instr)) {
                PluginEditorWindow::showForPlugin(*ext);
            } else {
                emit instrumentSelectRequested(track_);
            }
        });

        connect(instrumentBtn_, &QPushButton::customContextMenuRequested,
                this, [this](const QPoint& pos) {
                    QMenu menu;
                    menu.setAccessibleName("Instrument Menu");
                    menu.addAction("Change Instrument...", [this]() {
                        emit instrumentSelectRequested(track_);
                    });
                    auto* instr = editMgr_->getTrackInstrument(track_);
                    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(instr)) {
                        menu.addAction("Open VST Editor", [ext]() {
                            PluginEditorWindow::showForPlugin(*ext);
                        });
                    }
                    menu.exec(instrumentBtn_->mapToGlobal(pos));
                });

        mainLayout->addWidget(instrumentBtn_);
    }

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(2);

    muteBtn_ = new QPushButton("M", this);
    muteBtn_->setAccessibleName("Mute");
    muteBtn_->setCheckable(true);
    muteBtn_->setFixedSize(26, 20);
    applyToggleStyle(muteBtn_, theme.muteButton);
    connect(muteBtn_, &QPushButton::toggled, this, [this](bool m) {
        if (track_) track_->setMute(m);
    });

    soloBtn_ = new QPushButton("S", this);
    soloBtn_->setAccessibleName("Solo");
    soloBtn_->setCheckable(true);
    soloBtn_->setFixedSize(26, 20);
    applyToggleStyle(soloBtn_, theme.soloButton);
    connect(soloBtn_, &QPushButton::toggled, this, [this](bool s) {
        if (track_) track_->setSolo(s);
    });

    armBtn_ = new QPushButton("R", this);
    armBtn_->setAccessibleName("Record Arm");
    armBtn_->setCheckable(true);
    armBtn_->setFixedSize(26, 20);
    applyToggleStyle(armBtn_, theme.recordArm);

    btnRow->addWidget(muteBtn_);
    btnRow->addWidget(soloBtn_);
    btnRow->addWidget(armBtn_);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    auto* controlRow = new QHBoxLayout();
    controlRow->setSpacing(2);

    panKnob_ = new RotaryKnob(this);
    panKnob_->setAccessibleName("Pan");
    panKnob_->setRange(-1.0, 1.0);
    panKnob_->setValue(0.0);
    panKnob_->setLabel("Pan");
    panKnob_->setFixedSize(36, 40);
    connect(panKnob_, &RotaryKnob::valueChanged, this, [this](double v) {
        if (!track_) return;
        for (auto* plugin : track_->pluginList.getPlugins()) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                volPan->pan.setValue(float(v), nullptr);
                break;
            }
        }
    });
    controlRow->addWidget(panKnob_);

    volumeSlider_ = new QSlider(Qt::Horizontal, this);
    volumeSlider_->setAccessibleName("Volume");
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(75);
    volumeSlider_->setFixedHeight(18);
    volumeSlider_->setStyleSheet(
        QString("QSlider::groove:horizontal { background: %1; height: 4px; border-radius: 2px; }"
                "QSlider::handle:horizontal { background: %2; width: 10px; margin: -4px 0; border-radius: 5px; }"
                "QSlider::sub-page:horizontal { background: %3; border-radius: 2px; }")
            .arg(theme.border.name(), theme.text.name(), theme.accent.name()));
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int v) {
        if (!track_) return;
        for (auto* plugin : track_->pluginList.getPlugins()) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                double norm = v / 100.0;
                double db = -60.0 + norm * 66.0;
                volPan->volParam->setParameter(
                    te::decibelsToVolumeFaderPosition(float(db)),
                    juce::sendNotification);
                break;
            }
        }
    });
    controlRow->addWidget(volumeSlider_, 1);

    meter_ = new LevelMeter(this);
    meter_->setAccessibleName("Track Level Meter");
    meter_->setFixedWidth(12);
    controlRow->addWidget(meter_);

    mainLayout->addLayout(controlRow);
    mainLayout->addStretch();

    levelMeterPlugin_ = track_->getLevelMeterPlugin();
    if (levelMeterPlugin_)
        levelMeterPlugin_->measurer.addClient(meterClient_);

    connect(&meterTimer_, &QTimer::timeout, this, &TrackHeaderWidget::updateMeter);
    meterTimer_.start(30);
}

TrackHeaderWidget::~TrackHeaderWidget()
{
    meterTimer_.stop();
    if (levelMeterPlugin_)
        levelMeterPlugin_->measurer.removeClient(meterClient_);
}

void TrackHeaderWidget::setTrackHeight(int h)
{
    setFixedHeight(h);
}

void TrackHeaderWidget::setSelected(bool sel)
{
    if (selected_ == sel) return;
    selected_ = sel;
    updateSelectionStyle();
}

void TrackHeaderWidget::updateSelectionStyle()
{
    auto& theme = ThemeManager::instance().current();
    const QColor selectedBg = theme.meterGreen.darker(360);
    if (selected_) {
        setStyleSheet(QString(
            "background: %1; "
            "border-left: 3px solid %2; "
            "border-top: 1px solid %3; "
            "border-right: 1px solid %3; "
            "border-bottom: 1px solid %3;")
                .arg(selectedBg.name(), theme.meterGreen.name(),
                     theme.border.lighter(115).name()));
    } else {
        setStyleSheet(QString(
            "background: %1; "
            "border: 1px solid transparent;")
                .arg(theme.surface.name()));
        QPalette pal;
        pal.setColor(QPalette::Window, theme.surface);
        setPalette(pal);
    }
}

void TrackHeaderWidget::mousePressEvent(QMouseEvent* event)
{
    emit trackSelected(track_);
    QWidget::mousePressEvent(event);
}

void TrackHeaderWidget::updateMeter()
{
    if (!track_ || !editMgr_ || !editMgr_->edit() || !levelMeterPlugin_)
        return;

    auto levelL = meterClient_.getAndClearAudioLevel(0);
    auto levelR = meterClient_.getAndClearAudioLevel(1);
    meter_->setLevel(dbToNormalized(levelL.dB), dbToNormalized(levelR.dB));
}

void TrackHeaderWidget::applyToggleStyle(QPushButton* btn, const QColor& activeColor)
{
    auto& theme = ThemeManager::instance().current();
    btn->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 3px; font-weight: bold; font-size: 9px; padding: 0px 2px; }"
                "QPushButton:checked { background: %4; color: #000; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), activeColor.name()));
}

} // namespace freedaw
