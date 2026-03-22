#include "ChannelStrip.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QToolTip>
#include <algorithm>
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

namespace OpenDaw {

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

void ChannelStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!stripBgPixmap_.isNull()) {
        if (stripBgScaled_.size() != size())
            stripBgScaled_ = stripBgPixmap_.scaled(
                size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap(0, 0, stripBgScaled_);
    } else {
        auto& theme = ThemeManager::instance().current();
        p.fillRect(rect(), theme.surface);
    }
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
    setFixedWidth(92);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAutoFillBackground(false);

    stripBgPixmap_ = QPixmap(":/mixerStripBG.png");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(4);

    // ── Top section: input routing, name, instrument, pan ──

    bool isMidi = track_ && editMgr_ && editMgr_->isMidiTrack(track_);

    inputCombo_ = new QComboBox(this);
    inputCombo_->setAccessibleName("Input Source");
    inputCombo_->setToolTip("Select audio input source");
    inputCombo_->setFixedHeight(22);
    inputCombo_->setStyleSheet(
        QString("QComboBox { background: rgba(0,0,0,100); color: %1; border: 1px solid rgba(255,255,255,18); "
                "border-radius: 3px; font-size: 9px; padding: 2px 3px; }"
                "QComboBox:hover { border: 1px solid %2; }"
                "QComboBox::drop-down { width: 14px; }"
                "QComboBox QAbstractItemView { background: %3; color: %1; "
                "selection-background-color: %4; font-size: 9px; }")
            .arg(theme.text.name(), theme.accent.name(),
                 theme.surface.name(), theme.surfaceLight.name()));
    populateInputCombo();
    connect(inputCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChannelStrip::onInputComboChanged);
    layout->addWidget(inputCombo_);

    outputCombo_ = new QComboBox(this);
    outputCombo_->setAccessibleName("Output Destination");
    outputCombo_->setToolTip("Select output destination");
    outputCombo_->setFixedHeight(22);
    outputCombo_->setStyleSheet(
        QString("QComboBox { background: rgba(0,0,0,100); color: %1; border: 1px solid rgba(255,255,255,18); "
                "border-radius: 3px; font-size: 9px; padding: 2px 3px; }"
                "QComboBox:hover { border: 1px solid %2; }"
                "QComboBox::drop-down { width: 14px; }"
                "QComboBox QAbstractItemView { background: %3; color: %1; "
                "selection-background-color: %4; font-size: 9px; }")
            .arg(theme.text.name(), theme.accent.name(),
                 theme.surface.name(), theme.surfaceLight.name()));
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
    QString nameBgColor = isMidi ? "rgba(60,95,145,160)" : "rgba(0,0,0,120)";
    nameLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                "background: %2; border-radius: 3px; padding: 3px; }")
            .arg(theme.text.name(), nameBgColor));
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
            QString("QPushButton { background: rgba(0,0,0,100); color: %1; "
                    "border: 1px solid rgba(255,255,255,18); "
                    "border-radius: 3px; font-size: 8px; padding: 1px 3px; }"
                    "QPushButton:hover { background: rgba(255,255,255,25); }")
                .arg(theme.text.name()));
        connect(instrumentBtn_, &QPushButton::clicked, this, [this]() {
            emit instrumentSelectRequested(track_);
        });
        layout->addWidget(instrumentBtn_);

        midiChannelCombo_ = new QComboBox(this);
        midiChannelCombo_->setAccessibleName("MIDI Channel");
        midiChannelCombo_->setToolTip("MIDI output channel (1-16)");
        midiChannelCombo_->setFixedHeight(20);
        for (int ch = 1; ch <= 16; ++ch)
            midiChannelCombo_->addItem(QString("Ch %1").arg(ch), ch);
        midiChannelCombo_->setStyleSheet(
            QString("QComboBox { background: rgba(0,0,0,100); color: %1; "
                    "border: 1px solid rgba(255,255,255,18); "
                    "border-radius: 3px; font-size: 8px; padding: 1px 2px; }"
                    "QComboBox:hover { border: 1px solid %2; }"
                    "QComboBox::drop-down { width: 12px; }"
                    "QComboBox QAbstractItemView { background: %3; color: %1; "
                    "selection-background-color: %4; font-size: 8px; }")
                .arg(theme.text.name(), theme.accent.name(),
                     theme.surface.name(), theme.surfaceLight.name()));

        int currentCh = 1;
        for (auto* clip : track_->getClips()) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                currentCh = mc->getMidiChannel().getChannelNumber();
                break;
            }
        }
        midiChannelCombo_->setCurrentIndex(std::clamp(currentCh, 1, 16) - 1);

        connect(midiChannelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ChannelStrip::onMidiChannelChanged);
        layout->addWidget(midiChannelCombo_);
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
            if (editMgr_ && editMgr_->edit())
                editMgr_->edit()->setMasterPanPos(float(v));
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
    volumeLabel_->setToolTip("Volume in dB \xe2\x80\x93 double-click to type exact value");
    volumeLabel_->setAlignment(Qt::AlignCenter);
    volumeLabel_->setFixedHeight(18);
    volumeLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 9px; "
                "font-family: 'Consolas', 'Courier New', monospace; "
                "background: rgba(0,0,0,140); border: 1px solid rgba(255,255,255,15); "
                "border-radius: 3px; padding: 1px 3px; }")
            .arg(theme.text.name()));
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
    muteBtn_->setFixedHeight(22);
    muteBtn_->setFont(icons::fontAudio(11));
    muteBtn_->setText(QString(icons::fa::Mute));
    muteBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    applyToggleStyle(muteBtn_, theme.muteButton);
    connect(muteBtn_, &QPushButton::toggled, this, [this](bool m) {
        if (track_) track_->setMute(m);
    });

    soloBtn_ = new QPushButton(this);
    soloBtn_->setAccessibleName("Solo");
    soloBtn_->setCheckable(true);
    soloBtn_->setFixedHeight(22);
    soloBtn_->setFont(icons::fontAudio(11));
    soloBtn_->setText(QString(icons::fa::Solo));
    soloBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    applyToggleStyle(soloBtn_, theme.soloButton);
    connect(soloBtn_, &QPushButton::toggled, this, [this](bool s) {
        if (track_) track_->setSolo(s);
    });

    armBtn_ = new QPushButton(this);
    armBtn_->setAccessibleName("Record Arm");
    armBtn_->setCheckable(true);
    armBtn_->setFixedHeight(22);
    armBtn_->setFont(icons::fontAudio(11));
    armBtn_->setText(QString(icons::fa::Armrecording));
    armBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    applyToggleStyle(armBtn_, theme.recordArm);
    connect(armBtn_, &QPushButton::toggled, this, &ChannelStrip::onArmToggled);

    monoBtn_ = new QPushButton(this);
    monoBtn_->setAccessibleName("Mono Or Stereo");
    monoBtn_->setCheckable(true);
    monoBtn_->setFixedHeight(22);
    monoBtn_->setFont(icons::fontAudio(11));
    monoBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
    autoModeBtn_->setToolTip("Automation mode (click to cycle):\n"
                              "READ - Play existing automation\n"
                              "TOUCH - Record only while moving a control\n"
                              "LATCH - Record from first touch, hold last value\n"
                              "WRITE - Record all params from playback start");
    autoModeBtn_->setFixedHeight(18);
    autoModeBtn_->setStyleSheet(
        QString("QPushButton { background: rgba(0,0,0,100); color: %1; "
                "border: 1px solid rgba(255,255,255,18); "
                "border-radius: 3px; font-size: 7px; font-weight: bold; padding: 0px 2px; }"
                "QPushButton:hover { border: 1px solid %2; }")
            .arg(theme.textDim.name(), theme.accent.name()));
    connect(autoModeBtn_, &QPushButton::clicked, this, [this]() {
        if (!editMgr_ || !editMgr_->edit()) return;

        te::Track* modeTrack = track_;
        if (isMaster_)
            modeTrack = editMgr_->edit()->getMasterTrack();
        if (!modeTrack) return;

        auto currentMode = modeTrack->automationMode.get();
        te::AutomationMode newMode;
        if (currentMode == te::AutomationMode::read)
            newMode = te::AutomationMode::touch;
        else if (currentMode == te::AutomationMode::touch)
            newMode = te::AutomationMode::latch;
        else if (currentMode == te::AutomationMode::latch)
            newMode = te::AutomationMode::write;
        else
            newMode = te::AutomationMode::read;
        modeTrack->automationMode = newMode;

        auto& arm = editMgr_->edit()->getAutomationRecordManager();
        bool anyWriting = (newMode != te::AutomationMode::read);
        arm.setReadingAutomation(true);
        arm.setWritingAutomation(anyWriting);
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
        "QLabel { color: #fff; font-size: 8px; font-weight: bold; "
        "background: rgba(0,150,200,180); border-radius: 2px; padding: 1px 4px; }");
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
    if (midiChannelCombo_ && track_) {
        int ch = 1;
        for (auto* clip : track_->getClips()) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                ch = mc->getMidiChannel().getChannelNumber();
                break;
            }
        }
        QSignalBlocker block(midiChannelCombo_);
        midiChannelCombo_->setCurrentIndex(std::clamp(ch, 1, 16) - 1);
    }
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

    // Only track automation values to controls when playing, or when the
    // playhead has been scrubbed to a new position. When stopped and
    // stationary, don't fight with the user's manual fader/knob drags.
    bool isPlaying = editMgr_->transport().isPlaying();
    double currentSecs = editMgr_->transport().getPosition().inSeconds();
    bool playheadMoved = std::abs(currentSecs - lastTrackedPlayheadSecs_) > 0.001;
    bool shouldTrack = isPlaying || playheadMoved;
    lastTrackedPlayheadSecs_ = currentSecs;

    if (!shouldTrack) return;

    if (isMaster_) {
        if (auto masterVol = editMgr_->edit()->getMasterVolumePlugin()) {
            auto* mt = editMgr_->edit()->getMasterTrack();
            auto mode = mt ? mt->automationMode.get() : te::AutomationMode::read;
            bool isWriting = isPlaying && (mode == te::AutomationMode::write);
            if (fader_ && !isWriting) {
                QSignalBlocker block(fader_);
                fader_->setValue(masterVol->getSliderPos());
            }
            if (panKnob_ && !isWriting) {
                QSignalBlocker block(panKnob_);
                panKnob_->setValue(masterVol->getPan());
            }
            updateVolumeLabel();
        }
        return;
    }

    if (!track_) return;

    auto mode = track_->automationMode.get();
    bool isWriting = isPlaying && (mode == te::AutomationMode::write);

    for (auto* plugin : track_->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
            if (fader_ && !isWriting) {
                float dbVal = te::volumeFaderPositionToDB(vp->volParam->getCurrentValue());
                double norm = (dbVal + 60.0) / 66.0;
                QSignalBlocker block(fader_);
                fader_->setValue(std::clamp(norm, 0.0, 1.0));
            }
            if (panKnob_ && !isWriting) {
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
        QString("QPushButton { background: rgba(0,0,0,100); color: %1; "
                "border: 1px solid rgba(255,255,255,18); "
                "border-radius: 4px; font-weight: bold; font-size: 10px; padding: 1px 2px; }"
                "QPushButton:hover { background: rgba(255,255,255,25); "
                "border-color: rgba(255,255,255,35); }"
                "QPushButton:checked { background: %2; color: #000; border-color: %3; }")
            .arg(theme.textDim.name(), activeColor.name(),
                 activeColor.darker(130).name()));
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
    if (track_) {
        mode = track_->automationMode.get();
    } else if (isMaster_ && editMgr_ && editMgr_->edit()) {
        if (auto* mt = editMgr_->edit()->getMasterTrack())
            mode = mt->automationMode.get();
    }

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
    QColor bgAlpha(bg.red(), bg.green(), bg.blue(), 180);
    autoModeBtn_->setStyleSheet(
        QString("QPushButton { background: rgba(%1,%2,%3,%4); color: %5; "
                "border: 1px solid rgba(255,255,255,18); "
                "border-radius: 3px; font-size: 7px; font-weight: bold; padding: 0px 2px; }"
                "QPushButton:hover { border: 1px solid %6; }")
            .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(180)
            .arg(fg.name(), theme.accent.name()));
}

void ChannelStrip::populateInputCombo()
{
    if (!inputCombo_ || !editMgr_) return;

    QSignalBlocker block(inputCombo_);
    inputCombo_->clear();
    inputCombo_->addItem("No Input", QString());

    bool isMidi = track_ && editMgr_->isMidiTrack(track_);

    if (isMidi) {
        auto midiSources = editMgr_->getAvailableMidiInputSources();
        for (const auto& src : midiSources)
            inputCombo_->addItem(src.displayName,
                                 QString("midi:") + QString::fromStdString(
                                     src.deviceName.toStdString()));
    }

    auto sources = editMgr_->getAvailableInputSources();
    for (const auto& src : sources)
        inputCombo_->addItem(src.displayName,
                             QString::fromStdString(src.deviceName.toStdString()));

    QString currentInput = track_ ? editMgr_->getTrackInputName(track_) : QString();
    if (currentInput.isEmpty()) {
        inputCombo_->setCurrentIndex(0);
    } else {
        int idx = inputCombo_->findData(currentInput);
        if (idx < 0)
            idx = inputCombo_->findData("midi:" + currentInput);
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
    } else if (deviceName.startsWith("midi:")) {
        editMgr_->assignMidiInputToTrack(*track_,
            juce::String(deviceName.mid(5).toStdString()));
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
        QToolTip::showText(armBtn_->mapToGlobal(QPoint(0, -30)),
                           "Select an input source first",
                           armBtn_, {}, 2500);
        return;
    }

    editMgr_->setTrackRecordEnabled(*track_, armed);
}

void ChannelStrip::onMidiChannelChanged(int index)
{
    if (!track_ || index < 0) return;
    int ch = midiChannelCombo_->itemData(index).toInt();
    te::MidiChannel midiCh(ch);
    for (auto* clip : track_->getClips()) {
        if (auto* mc = dynamic_cast<te::MidiClip*>(clip))
            mc->setMidiChannel(midiCh);
    }
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
    if (selected_) {
        setStyleSheet(
            QString("background: transparent; "
                    "border-left: 3px solid %1; "
                    "border-top: 1px solid rgba(255,255,255,25); "
                    "border-right: 1px solid rgba(255,255,255,25); "
                    "border-bottom: 1px solid rgba(255,255,255,25); "
                    "border-radius: 4px;")
                .arg(theme.accent.name()));
    } else {
        setStyleSheet("background: transparent; border: none;");
    }
}

} // namespace OpenDaw
