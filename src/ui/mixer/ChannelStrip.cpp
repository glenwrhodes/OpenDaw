#include "ChannelStrip.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
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

ChannelStrip::ChannelStrip(te::AudioTrack* track, EditManager* editMgr,
                           QWidget* parent)
    : QWidget(parent), track_(track), editMgr_(editMgr)
{
    setAccessibleName("Channel Strip");
    setupUI();

    if (track_) {
        levelMeterPlugin_ = track_->getLevelMeterPlugin();
        if (levelMeterPlugin_)
            levelMeterPlugin_->measurer.addClient(meterClient_);
    }

    connect(&meterTimer_, &QTimer::timeout, this, &ChannelStrip::updateMeter);
    meterTimer_.start(30);
}

ChannelStrip::~ChannelStrip()
{
    meterTimer_.stop();
    if (levelMeterPlugin_)
        levelMeterPlugin_->measurer.removeClient(meterClient_);
}

ChannelStrip* ChannelStrip::createMasterStrip(EditManager* editMgr,
                                               QWidget* parent)
{
    auto* strip = new ChannelStrip(nullptr, editMgr, parent);
    strip->isMaster_ = true;
    strip->setupMasterUI();
    return strip;
}

void ChannelStrip::setupUI()
{
    auto& theme = ThemeManager::instance().current();
    setFixedWidth(88);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.surface);
    setPalette(pal);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(3);

    // ── Top section: name, FX slots, pan ──

    bool isMidi = track_ && editMgr_ && editMgr_->isMidiTrack(track_);

    nameLabel_ = new QLabel(this);
    nameLabel_->setAccessibleName("Track Name");
    if (track_)
        nameLabel_->setText(QString::fromStdString(track_->getName().toStdString()));
    nameLabel_->setAlignment(Qt::AlignCenter);
    QColor nameBg = isMidi ? theme.midiClipBody : theme.background;
    nameLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                "background: %2; border-radius: 2px; padding: 2px; }")
            .arg(theme.text.name(), nameBg.name()));
    layout->addWidget(nameLabel_);

    if (isMidi) {
        auto* instrument = editMgr_->getTrackInstrument(track_);
        QString instrName = instrument
            ? QString::fromStdString(instrument->getName().toStdString())
            : "No Instrument";
        instrumentBtn_ = new QPushButton(instrName, this);
        instrumentBtn_->setAccessibleName("Select Instrument");
        instrumentBtn_->setFixedHeight(22);
        instrumentBtn_->setStyleSheet(
            QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 2px; font-size: 8px; padding: 1px 3px; }"
                    "QPushButton:hover { background: %4; }")
                .arg(theme.background.name(), theme.text.name(),
                     theme.border.name(), theme.surfaceLight.name()));
        connect(instrumentBtn_, &QPushButton::clicked, this, [this]() {
            emit instrumentSelectRequested(track_);
        });
        layout->addWidget(instrumentBtn_);
    }

    fxSlot1_ = new QComboBox(this);
    fxSlot1_->setAccessibleName("Effect Slot 1");
    fxSlot1_->addItem("-- FX 1 --");
    fxSlot1_->addItem("Reverb");
    fxSlot1_->addItem("EQ");
    fxSlot1_->addItem("Compressor");
    fxSlot1_->setFixedHeight(24);
    fxSlot1_->setStyleSheet(
        QString("QComboBox { font-size: 9px; background: %1; color: %2; border: 1px solid %3; padding: 2px 4px; }"
                "QComboBox QAbstractItemView { background: %4; color: %2; selection-background-color: %5; }")
            .arg(theme.background.name(), theme.text.name(), theme.border.name(),
                 theme.surface.name(), theme.accent.name()));
    connect(fxSlot1_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit effectInsertRequested(track_, 0); });
    layout->addWidget(fxSlot1_);

    fxSlot2_ = new QComboBox(this);
    fxSlot2_->setAccessibleName("Effect Slot 2");
    fxSlot2_->addItem("-- FX 2 --");
    fxSlot2_->addItem("Reverb");
    fxSlot2_->addItem("EQ");
    fxSlot2_->addItem("Compressor");
    fxSlot2_->setFixedHeight(24);
    fxSlot2_->setStyleSheet(fxSlot1_->styleSheet());
    connect(fxSlot2_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit effectInsertRequested(track_, 1); });
    layout->addWidget(fxSlot2_);

    panKnob_ = new RotaryKnob(this);
    panKnob_->setAccessibleName("Pan");
    panKnob_->setRange(-1.0, 1.0);
    panKnob_->setValue(0.0);
    panKnob_->setLabel("Pan");
    panKnob_->setFixedSize(48, 48);
    auto* panRow = new QHBoxLayout();
    panRow->addStretch();
    panRow->addWidget(panKnob_);
    panRow->addStretch();
    layout->addLayout(panRow);

    connect(panKnob_, &RotaryKnob::valueChanged, this, [this](double v) {
        if (!track_) return;
        for (auto* plugin : track_->pluginList.getPlugins()) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                volPan->pan.setValue(float(v), nullptr);
                break;
            }
        }
    });

    // ── Middle section: fader + meter (stretches to fill) ──

    auto* faderRow = new QHBoxLayout();
    faderRow->setSpacing(2);

    meter_ = new LevelMeter(this);
    meter_->setAccessibleName("Level Meter");
    meter_->setFixedWidth(14);
    meter_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    faderRow->addWidget(meter_);

    fader_ = new VolumeFader(this);
    fader_->setAccessibleName("Volume Fader");
    fader_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    faderRow->addWidget(fader_);

    layout->addLayout(faderRow, 1);

    connect(fader_, &VolumeFader::valueChanged, this, [this](double v) {
        if (!track_) return;
        for (auto* plugin : track_->pluginList.getPlugins()) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                double db = -60.0 + v * 66.0;
                volPan->volParam->setParameter(te::decibelsToVolumeFaderPosition(float(db)), juce::sendNotification);
                break;
            }
        }
    });

    // ── Bottom section: M / S / R buttons ──

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(2);

    muteBtn_ = new QPushButton("M", this);
    muteBtn_->setAccessibleName("Mute");
    muteBtn_->setCheckable(true);
    muteBtn_->setFixedSize(26, 24);
    applyToggleStyle(muteBtn_, theme.muteButton);
    connect(muteBtn_, &QPushButton::toggled, this, [this](bool m) {
        if (track_) track_->setMute(m);
    });

    soloBtn_ = new QPushButton("S", this);
    soloBtn_->setAccessibleName("Solo");
    soloBtn_->setCheckable(true);
    soloBtn_->setFixedSize(26, 24);
    applyToggleStyle(soloBtn_, theme.soloButton);
    connect(soloBtn_, &QPushButton::toggled, this, [this](bool s) {
        if (track_) track_->setSolo(s);
    });

    armBtn_ = new QPushButton("R", this);
    armBtn_->setAccessibleName("Record Arm");
    armBtn_->setCheckable(true);
    armBtn_->setFixedSize(26, 24);
    applyToggleStyle(armBtn_, theme.recordArm);

    btnRow->addWidget(muteBtn_);
    btnRow->addWidget(soloBtn_);
    btnRow->addWidget(armBtn_);
    layout->addLayout(btnRow);
}

void ChannelStrip::setupMasterUI()
{
    if (nameLabel_)
        nameLabel_->setText("Master");
    if (armBtn_)
        armBtn_->setVisible(false);
    if (fxSlot1_)
        fxSlot1_->setVisible(false);
    if (fxSlot2_)
        fxSlot2_->setVisible(false);
}

void ChannelStrip::refresh()
{
    if (!track_) return;
    nameLabel_->setText(QString::fromStdString(track_->getName().toStdString()));
    muteBtn_->setChecked(track_->isMuted(false));
    soloBtn_->setChecked(track_->isSolo(false));
}

void ChannelStrip::updateMeter()
{
    if (!track_ || !editMgr_ || !editMgr_->edit() || !levelMeterPlugin_)
        return;

    auto levelL = meterClient_.getAndClearAudioLevel(0);
    auto levelR = meterClient_.getAndClearAudioLevel(1);
    meter_->setLevel(dbToNormalized(levelL.dB), dbToNormalized(levelR.dB));
}

void ChannelStrip::applyToggleStyle(QPushButton* btn, const QColor& activeColor)
{
    auto& theme = ThemeManager::instance().current();
    btn->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 3px; font-weight: bold; font-size: 10px; padding: 0px 2px; }"
                "QPushButton:checked { background: %4; color: #000; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), activeColor.name()));
}

} // namespace freedaw
