#include "ChannelStrip.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QMouseEvent>
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
    : QWidget(parent), track_(track),
      trackId_(track ? track->itemID.getRawID() : 0),
      editMgr_(editMgr)
{
    setAccessibleName("Channel Strip");
    setupUI();

    reconnectLevelMeterSource();

    if (editMgr_) {
        connect(editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
            if (levelMeterPlugin_) {
                levelMeterPlugin_->measurer.removeClient(meterClient_);
                levelMeterPlugin_ = nullptr;
            }
        });
        connect(editMgr_, &EditManager::editChanged, this, [this]() {
            reconnectLevelMeterSource();
        });
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
    strip->reconnectLevelMeterSource();
    strip->setupMasterUI();
    return strip;
}

void ChannelStrip::setupUI()
{
    auto& theme = ThemeManager::instance().current();
    setObjectName("channelStrip");
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(88);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.surface);
    setPalette(pal);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(3);

    // ── Top section: input routing, name, instrument, pan ──

    bool isMidi = track_ && editMgr_ && editMgr_->isMidiTrack(track_);

    inputCombo_ = new QComboBox(this);
    inputCombo_->setAccessibleName("Input Source");
    inputCombo_->setToolTip("Select audio input source");
    inputCombo_->setFixedHeight(20);
    inputCombo_->setStyleSheet(
        QString("QComboBox { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; font-size: 8px; padding: 1px 2px; }"
                "QComboBox:hover { border: 1px solid %4; }"
                "QComboBox::drop-down { width: 12px; }"
                "QComboBox QAbstractItemView { background: %1; color: %2; "
                "selection-background-color: %5; font-size: 8px; }")
            .arg(theme.background.name(), theme.text.name(),
                 theme.border.name(), theme.accent.name(),
                 theme.surfaceLight.name()));
    populateInputCombo();
    connect(inputCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChannelStrip::onInputComboChanged);
    layout->addWidget(inputCombo_);

    outputCombo_ = new QComboBox(this);
    outputCombo_->setAccessibleName("Output Destination");
    outputCombo_->setToolTip("Select output destination");
    outputCombo_->setFixedHeight(20);
    outputCombo_->setStyleSheet(
        QString("QComboBox { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; font-size: 8px; padding: 1px 2px; }"
                "QComboBox:hover { border: 1px solid %4; }"
                "QComboBox::drop-down { width: 12px; }"
                "QComboBox QAbstractItemView { background: %1; color: %2; "
                "selection-background-color: %5; font-size: 8px; }")
            .arg(theme.background.name(), theme.text.name(),
                 theme.border.name(), QColor(255, 152, 0).name(),
                 theme.surfaceLight.name()));
    populateOutputCombo();
    connect(outputCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChannelStrip::onOutputComboChanged);
    layout->addWidget(outputCombo_);

    nameLabel_ = new QLabel(this);
    nameLabel_->setAccessibleName("Track Name");
    nameLabel_->setToolTip("Double-click to rename");
    if (track_)
        nameLabel_->setText(QString::fromStdString(track_->getName().toStdString()));
    nameLabel_->setAlignment(Qt::AlignCenter);
    QColor nameBg = isMidi ? theme.midiClipBody : theme.background;
    nameLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                "background: %2; border-radius: 2px; padding: 2px; }")
            .arg(theme.text.name(), nameBg.name()));
    nameLabel_->installEventFilter(this);
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

    panKnob_ = new RotaryKnob(this);
    panKnob_->setAccessibleName("Pan");
    panKnob_->setRange(-1.0, 1.0);
    panKnob_->setValue(0.0);
    panKnob_->setLabel(QString());
    panKnob_->setFixedSize(48, 48);
    auto* panRow = new QHBoxLayout();
    panRow->addStretch();
    panRow->addWidget(panKnob_);
    panRow->addStretch();
    layout->addLayout(panRow);

    connect(panKnob_, &RotaryKnob::valueChanged, this, [this](double v) {
        if (isMaster_) {
            if (editMgr_ && editMgr_->edit()) {
                if (auto masterVol = editMgr_->edit()->getMasterVolumePlugin())
                    masterVol->setPan(float(v));
            }
            return;
        }

        if (!track_) return;
        for (auto* plugin : track_->pluginList.getPlugins()) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                volPan->panParam->setParameter(float(v), juce::sendNotification);
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

    volumeLabel_ = new QLabel("0.0", this);
    volumeLabel_->setAccessibleName("Volume Level dB");
    volumeLabel_->setToolTip("Volume in dB – double-click to type exact value");
    volumeLabel_->setAlignment(Qt::AlignCenter);
    volumeLabel_->setFixedHeight(16);
    volumeLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 9px; font-family: monospace; "
                "background: %2; border: 1px solid %3; border-radius: 2px; padding: 0px 2px; }")
            .arg(theme.text.name(), theme.background.name(), theme.border.name()));
    volumeLabel_->installEventFilter(this);
    layout->addWidget(volumeLabel_);
    updateVolumeLabel();

    connect(fader_, &VolumeFader::valueChanged, this, [this](double v) {
        updateVolumeLabel();
        if (isMaster_) {
            if (editMgr_ && editMgr_->edit())
                editMgr_->edit()->setMasterVolumeSliderPos(float(v));
            return;
        }

        if (!track_) return;
        for (auto* plugin : track_->pluginList.getPlugins()) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                if (v <= 0.001) {
                    volPan->volParam->setParameter(0.0f, juce::sendNotification);
                } else {
                    double db = -60.0 + v * 66.0;
                    volPan->volParam->setParameter(te::decibelsToVolumeFaderPosition(float(db)), juce::sendNotification);
                }
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
    muteBtn_->setFont(icons::fontAudio(13));
    muteBtn_->setText(QString(icons::fa::Mute));
    applyToggleStyle(muteBtn_, theme.muteButton);
    connect(muteBtn_, &QPushButton::toggled, this, [this](bool m) {
        if (track_) track_->setMute(m);
    });

    soloBtn_ = new QPushButton(this);
    soloBtn_->setAccessibleName("Solo");
    soloBtn_->setCheckable(true);
    soloBtn_->setFixedSize(26, 24);
    soloBtn_->setFont(icons::fontAudio(13));
    soloBtn_->setText(QString(icons::fa::Solo));
    applyToggleStyle(soloBtn_, theme.soloButton);
    connect(soloBtn_, &QPushButton::toggled, this, [this](bool s) {
        if (track_) track_->setSolo(s);
    });

    armBtn_ = new QPushButton(this);
    armBtn_->setAccessibleName("Record Arm");
    armBtn_->setCheckable(true);
    armBtn_->setFixedSize(26, 24);
    armBtn_->setFont(icons::fontAudio(13));
    armBtn_->setText(QString(icons::fa::Armrecording));
    applyToggleStyle(armBtn_, theme.recordArm);
    connect(armBtn_, &QPushButton::toggled, this, &ChannelStrip::onArmToggled);

    monoBtn_ = new QPushButton(this);
    monoBtn_->setAccessibleName("Mono Or Stereo");
    monoBtn_->setCheckable(true);
    monoBtn_->setFixedSize(26, 24);
    monoBtn_->setFont(icons::fontAudio(13));
    applyToggleStyle(monoBtn_, theme.accent);
    connect(monoBtn_, &QPushButton::toggled, this, [this](bool mono) {
        if (!track_ || !editMgr_) return;
        editMgr_->setTrackMono(*track_, mono);
        updateMonoButtonVisual(mono);
    });

    btnRow->addWidget(muteBtn_);
    btnRow->addWidget(soloBtn_);
    btnRow->addWidget(armBtn_);
    if (!isMaster_)
        btnRow->addWidget(monoBtn_);
    layout->addLayout(btnRow);

    // Automation mode button (Off -> Read -> Write cycle)
    autoModeBtn_ = new QPushButton("OFF", this);
    autoModeBtn_->setAccessibleName("Automation Mode");
    autoModeBtn_->setToolTip("Automation: Off / Read / Write (click to cycle)");
    autoModeBtn_->setFixedHeight(18);
    autoModeBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; font-size: 7px; font-weight: bold; padding: 0px 2px; }"
                "QPushButton:hover { border: 1px solid %4; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.accent.name()));
    connect(autoModeBtn_, &QPushButton::clicked, this, [this]() {
        if (!editMgr_ || !editMgr_->edit() || !track_) return;
        auto currentMode = track_->automationMode.get();
        te::AutomationMode newMode;
        if (currentMode == te::AutomationMode::read)
            newMode = te::AutomationMode::write;
        else if (currentMode == te::AutomationMode::write)
            newMode = te::AutomationMode::latch;
        else if (currentMode == te::AutomationMode::latch)
            newMode = te::AutomationMode::read;
        else
            newMode = te::AutomationMode::read;
        track_->automationMode = newMode;

        auto& arm = editMgr_->edit()->getAutomationRecordManager();
        if (newMode == te::AutomationMode::write || newMode == te::AutomationMode::latch) {
            arm.setReadingAutomation(true);
            arm.setWritingAutomation(true);
        } else {
            arm.setReadingAutomation(true);
            arm.setWritingAutomation(false);
        }
        updateAutoModeButton();
    });
    layout->addWidget(autoModeBtn_);
    updateAutoModeButton();

    if (track_ && editMgr_ && monoBtn_) {
        const bool mono = editMgr_->isTrackMono(track_);
        {
            QSignalBlocker block(monoBtn_);
            monoBtn_->setChecked(mono);
        }
        updateMonoButtonVisual(mono);
    }

    if (track_ && editMgr_) {
        connect(editMgr_, &EditManager::tracksChanged, this, [this]() {
            if (!monoBtn_ || !editMgr_) return;
            te::AudioTrack* t = nullptr;
            for (auto* tr : editMgr_->getAudioTracks()) {
                if (tr->itemID.getRawID() == trackId_) { t = tr; break; }
            }
            if (!t) return;
            track_ = t;
            const bool mono = editMgr_->isTrackMono(track_);
            QSignalBlocker block(monoBtn_);
            monoBtn_->setChecked(mono);
            updateMonoButtonVisual(mono);
        });
    }

    frozenLabel_ = new QLabel("FROZEN", this);
    frozenLabel_->setAccessibleName("Track Frozen Indicator");
    frozenLabel_->setAlignment(Qt::AlignCenter);
    frozenLabel_->setStyleSheet(
        QString("QLabel { color: #fff; font-size: 8px; font-weight: bold; "
                "background: %1; border-radius: 2px; padding: 1px 4px; }")
            .arg(QColor(0, 150, 200).name()));
    frozenLabel_->setFixedHeight(16);
    bool isFrozen = track_ && editMgr_ && editMgr_->isTrackFrozen(track_);
    frozenLabel_->setVisible(isFrozen);
    layout->addWidget(frozenLabel_);

    if (track_ && editMgr_) {
        connect(editMgr_, &EditManager::trackFreezeStateChanged, this, [this](te::AudioTrack* t) {
            if (t != track_ || !frozenLabel_ || !editMgr_) return;
            frozenLabel_->setVisible(editMgr_->isTrackFrozen(track_));
        });
    }

    updateSelectionStyle();
}

void ChannelStrip::setupMasterUI()
{
    if (nameLabel_)
        nameLabel_->setText("Master");

    if (editMgr_ && editMgr_->edit()) {
        if (auto masterVol = editMgr_->edit()->getMasterVolumePlugin()) {
            if (fader_)
                fader_->setValue(masterVol->getSliderPos());
            if (panKnob_)
                panKnob_->setValue(masterVol->getPan());
        }
    }

    if (inputCombo_)
        inputCombo_->setVisible(false);
    if (outputCombo_)
        outputCombo_->setVisible(false);
    if (armBtn_)
        armBtn_->setVisible(false);
    if (monoBtn_)
        monoBtn_->setVisible(false);
    if (muteBtn_)
        muteBtn_->setVisible(false);
    if (soloBtn_)
        soloBtn_->setVisible(false);
}

void ChannelStrip::setSelected(bool selected)
{
    if (selected_ == selected)
        return;
    selected_ = selected;
    updateSelectionStyle();
}

void ChannelStrip::refresh()
{
    if (isMaster_) {
        if (editMgr_ && editMgr_->edit()) {
            if (auto masterVol = editMgr_->edit()->getMasterVolumePlugin()) {
                if (fader_) {
                    QSignalBlocker block(fader_);
                    fader_->setValue(masterVol->getSliderPos());
                }
                if (panKnob_) {
                    QSignalBlocker block(panKnob_);
                    panKnob_->setValue(masterVol->getPan());
                }
            }
        }
        updateVolumeLabel();
        return;
    }

    if (!track_) return;
    nameLabel_->setText(QString::fromStdString(track_->getName().toStdString()));
    {
        QSignalBlocker block(muteBtn_);
        muteBtn_->setChecked(track_->isMuted(false));
    }
    {
        QSignalBlocker block(soloBtn_);
        soloBtn_->setChecked(track_->isSolo(false));
    }
    if (editMgr_ && monoBtn_) {
        const bool mono = editMgr_->isTrackMono(track_);
        QSignalBlocker block(monoBtn_);
        monoBtn_->setChecked(mono);
        updateMonoButtonVisual(mono);
    }
    if (editMgr_ && armBtn_) {
        QSignalBlocker block(armBtn_);
        armBtn_->setChecked(editMgr_->isTrackRecordEnabled(track_));
    }

    for (auto* plugin : track_->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
            if (fader_) {
                float dbVal = te::volumeFaderPositionToDB(vp->volParam->getCurrentValue());
                double norm = (dbVal + 60.0) / 66.0;
                QSignalBlocker block(fader_);
                fader_->setValue(norm);
            }
            if (panKnob_) {
                QSignalBlocker block(panKnob_);
                panKnob_->setValue(vp->pan.get());
            }
            break;
        }
    }
    updateVolumeLabel();
    updateAutoModeButton();

    if (editMgr_ && inputCombo_)
        populateInputCombo();
    if (editMgr_ && outputCombo_)
        populateOutputCombo();
}

void ChannelStrip::updateMeter()
{
    if (!editMgr_ || !editMgr_->edit())
        return;

    if (levelMeterPlugin_) {
        auto levelL = meterClient_.getAndClearAudioLevel(0);
        auto levelR = meterClient_.getAndClearAudioLevel(1);

        if (isMaster_) {
            if (auto masterVol = editMgr_->edit()->getMasterVolumePlugin()) {
                const float masterDb = masterVol->getVolumeDb();
                levelL.dB += masterDb;
                levelR.dB += masterDb;
            }
        }

        meter_->setLevel(dbToNormalized(levelL.dB), dbToNormalized(levelR.dB));
    }

    if (!track_ || isMaster_) return;
    if (!editMgr_->transport().isPlaying()) return;

    auto mode = track_->automationMode.get();
    if (mode == te::AutomationMode::write || mode == te::AutomationMode::latch)
        return;

    for (auto* plugin : track_->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
            if (fader_) {
                float dbVal = te::volumeFaderPositionToDB(vp->volParam->getCurrentValue());
                double norm = (dbVal + 60.0) / 66.0;
                QSignalBlocker block(fader_);
                fader_->setValue(std::clamp(norm, 0.0, 1.0));
            }
            if (panKnob_) {
                QSignalBlocker block(panKnob_);
                panKnob_->setValue(vp->panParam->getCurrentValue());
            }
            updateVolumeLabel();
            break;
        }
    }
}

void ChannelStrip::reconnectLevelMeterSource()
{
    if (levelMeterPlugin_) {
        levelMeterPlugin_->measurer.removeClient(meterClient_);
        levelMeterPlugin_ = nullptr;
    }

    if (!editMgr_ || !editMgr_->edit())
        return;

    if (isMaster_) {
        auto masterMeters = editMgr_->edit()
                                ->getMasterPluginList()
                                .getPluginsOfType<te::LevelMeterPlugin>();
        levelMeterPlugin_ = masterMeters.isEmpty() ? nullptr : masterMeters.getLast();
    } else if (track_) {
        levelMeterPlugin_ = track_->getLevelMeterPlugin();
    }

    if (levelMeterPlugin_)
        levelMeterPlugin_->measurer.addClient(meterClient_);
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

void ChannelStrip::updateMonoButtonVisual(bool mono)
{
    if (!monoBtn_)
        return;

    monoBtn_->setText(QString(mono ? icons::fa::Mono : icons::fa::Stereo));
    monoBtn_->setToolTip(mono ? "Track is mono" : "Track is stereo");
}

void ChannelStrip::updateAutoModeButton()
{
    if (!autoModeBtn_) return;
    auto& theme = ThemeManager::instance().current();

    auto mode = te::AutomationMode::read;
    if (track_)
        mode = track_->automationMode.get();

    QString text;
    QColor bg, fg;
    switch (mode) {
        case te::AutomationMode::write:
            text = "WRITE";
            bg = theme.recordArm;
            fg = QColor(Qt::white);
            break;
        case te::AutomationMode::latch:
            text = "LATCH";
            bg = theme.muteButton;
            fg = QColor(Qt::white);
            break;
        case te::AutomationMode::touch:
            text = "TOUCH";
            bg = theme.soloButton.darker(130);
            fg = QColor(Qt::white);
            break;
        case te::AutomationMode::read:
        default:
            text = "READ";
            bg = theme.meterGreen.darker(140);
            fg = theme.meterGreen;
            break;
    }

    autoModeBtn_->setText(text);
    autoModeBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; font-size: 7px; font-weight: bold; padding: 0px 2px; }"
                "QPushButton:hover { border: 1px solid %4; }")
            .arg(bg.name(), fg.name(), theme.border.name(), theme.accent.name()));
}

void ChannelStrip::populateInputCombo()
{
    if (!inputCombo_ || !editMgr_) return;

    QSignalBlocker block(inputCombo_);
    inputCombo_->clear();
    inputCombo_->addItem("No Input", QString());

    auto sources = editMgr_->getAvailableInputSources();
    for (const auto& src : sources)
        inputCombo_->addItem(src.displayName,
                             QString::fromStdString(src.deviceName.toStdString()));

    QString currentInput = track_ ? editMgr_->getTrackInputName(track_) : QString();
    if (currentInput.isEmpty()) {
        inputCombo_->setCurrentIndex(0);
    } else {
        int idx = inputCombo_->findData(currentInput);
        inputCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

void ChannelStrip::onInputComboChanged(int index)
{
    if (!track_ || !editMgr_ || index < 0) return;

    QString deviceName = inputCombo_->itemData(index).toString();
    if (deviceName.isEmpty()) {
        editMgr_->clearTrackInput(*track_);
        if (armBtn_) {
            QSignalBlocker block(armBtn_);
            armBtn_->setChecked(false);
        }
    } else {
        editMgr_->assignInputToTrack(*track_,
            juce::String(deviceName.toStdString()));
    }
}

void ChannelStrip::populateOutputCombo()
{
    if (!outputCombo_ || !editMgr_) return;

    QSignalBlocker block(outputCombo_);
    outputCombo_->clear();
    outputCombo_->addItem("Master", QString("__master__"));

    auto buses = editMgr_->getBusTracks();
    for (auto* bus : buses) {
        QString name = QString::fromStdString(bus->getName().toStdString());
        QString id = QString::number(bus->itemID.getRawID());
        outputCombo_->addItem(name, id);
    }

    if (!track_ || !editMgr_) return;
    auto* dest = editMgr_->getTrackOutputDestination(track_);
    if (!dest) {
        outputCombo_->setCurrentIndex(0);
    } else {
        QString destId = QString::number(dest->itemID.getRawID());
        int idx = outputCombo_->findData(destId);
        outputCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

void ChannelStrip::onOutputComboChanged(int index)
{
    if (!track_ || !editMgr_ || index < 0) return;

    QString destData = outputCombo_->itemData(index).toString();
    if (destData == "__master__") {
        editMgr_->setTrackOutputToMaster(*track_);
    } else {
        uint64_t rawId = destData.toULongLong();
        for (auto* bus : editMgr_->getBusTracks()) {
            if (bus->itemID.getRawID() == rawId) {
                editMgr_->setTrackOutputToTrack(*track_, *bus);
                break;
            }
        }
    }
}

void ChannelStrip::onArmToggled(bool armed)
{
    if (!track_ || !editMgr_) return;

    QString currentInput = editMgr_->getTrackInputName(track_);
    if (armed && currentInput.isEmpty()) {
        QSignalBlocker block(armBtn_);
        armBtn_->setChecked(false);
        return;
    }

    editMgr_->setTrackRecordEnabled(*track_, armed);
}

void ChannelStrip::updateVolumeLabel()
{
    if (!volumeLabel_ || !fader_) return;
    double db = fader_->valueDb();
    if (fader_->value() <= 0.001)
        volumeLabel_->setText(QString::fromUtf8("-\xe2\x88\x9e dB"));
    else
        volumeLabel_->setText(QString("%1 dB").arg(db, 0, 'f', 1));
}

void ChannelStrip::startVolumeEdit()
{
    if (!fader_) return;

    auto* edit = new QLineEdit(this);
    edit->setAccessibleName("Volume dB Input");
    double db = fader_->valueDb();
    edit->setText(fader_->value() <= 0.001 ? "-60" : QString::number(db, 'f', 1));
    edit->selectAll();
    edit->setGeometry(volumeLabel_->geometry());
    edit->setAlignment(Qt::AlignCenter);
    edit->setStyleSheet(
        "QLineEdit { background: #222; color: #eee; border: 1px solid #888; "
        "font-size: 9px; font-family: monospace; padding: 0px 2px; border-radius: 2px; }");
    edit->show();
    edit->setFocus();
    volumeLabel_->hide();

    auto* faderPtr = fader_;
    auto* volLabel = volumeLabel_;

    connect(edit, &QLineEdit::editingFinished, edit, [edit, faderPtr, volLabel]() {
        edit->hide();
        edit->deleteLater();
        if (volLabel) volLabel->show();

        bool ok = false;
        double db = edit->text().trimmed().remove("dB", Qt::CaseInsensitive).trimmed().toDouble(&ok);
        if (!ok) return;
        db = std::clamp(db, faderPtr->valueDb() - 60.0, 6.0);
        db = std::clamp(db, -60.0, 6.0);
        double norm = (db + 60.0) / 66.0;
        faderPtr->setValue(norm);
    });
}

bool ChannelStrip::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == nameLabel_ && event->type() == QEvent::MouseButtonDblClick && track_) {
        startRenameEdit();
        return true;
    }
    if (obj == volumeLabel_ && event->type() == QEvent::MouseButtonDblClick) {
        startVolumeEdit();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void ChannelStrip::startRenameEdit()
{
    if (!track_ || !editMgr_) return;

    auto* edit = new QLineEdit(this);
    edit->setAccessibleName("Rename Track");
    edit->setText(QString::fromStdString(track_->getName().toStdString()));
    edit->selectAll();
    edit->setGeometry(nameLabel_->geometry());
    edit->setAlignment(Qt::AlignCenter);
    edit->setStyleSheet(
        "QLineEdit { background: #222; color: #eee; border: 1px solid #888; "
        "font-size: 10px; font-weight: bold; padding: 1px 2px; border-radius: 2px; }");
    edit->show();
    edit->setFocus();
    nameLabel_->hide();

    auto trackId = trackId_;
    auto* mgr = editMgr_;
    auto* nameLbl = nameLabel_;

    connect(edit, &QLineEdit::editingFinished, edit, [edit, trackId, mgr, nameLbl]() {
        QString newName = edit->text().trimmed();
        edit->hide();
        edit->deleteLater();
        if (!newName.isEmpty() && mgr) {
            QTimer::singleShot(0, mgr, [trackId, mgr, newName]() {
                for (auto* t : mgr->getAudioTracks()) {
                    if (t->itemID.getRawID() == trackId) {
                        t->setName(juce::String(newName.toStdString()));
                        emit mgr->tracksChanged();
                        return;
                    }
                }
            });
        } else if (nameLbl) {
            nameLbl->show();
        }
    });
}

void ChannelStrip::updateSelectionStyle()
{
    auto& theme = ThemeManager::instance().current();
    const QColor selectedBg = theme.meterGreen.darker(360);
    QPalette pal = palette();
    if (selected_) {
        pal.setColor(QPalette::Window, selectedBg);
        setStyleSheet(
            QString("background: %1; "
                    "border-left: 3px solid %2; "
                    "border-top: 1px solid %3; "
                    "border-right: 1px solid %3; "
                    "border-bottom: 1px solid %3;")
                .arg(selectedBg.name(), theme.meterGreen.name(),
                     theme.border.lighter(115).name()));
    } else {
        pal.setColor(QPalette::Window, theme.surface);
        setStyleSheet(
            QString("background: %1; "
                    "border: 1px solid transparent;")
                .arg(theme.surface.name()));
    }
    setPalette(pal);
}

} // namespace freedaw
