#include "TrackHeaderWidget.h"
#include "ui/effects/PluginEditorWindow.h"
#include "utils/IconFont.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QMenu>
#include <QSignalBlocker>
#include <QApplication>
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
    nameLabel_->setToolTip("Double-click to rename");
    nameLabel_->setAlignment(Qt::AlignCenter);
    nameLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                "background: %2; border-radius: 2px; padding: 2px; }")
            .arg(theme.text.name(), theme.background.name()));
    nameLabel_->installEventFilter(this);
    topRow->addWidget(nameLabel_, 1);

    bool isBus = editMgr_->isBusTrack(track_);
    QString typeText = isMidi ? "MIDI" : (isBus ? "BUS" : "AUDIO");
    auto* typeBadge = new QLabel(typeText, this);
    typeBadge->setAccessibleName("Track Type");
    typeBadge->setAlignment(Qt::AlignCenter);
    QColor badgeBg = isMidi ? theme.midiClipBody : (isBus ? QColor(255, 152, 0) : theme.accent);
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

    inputCombo_ = new QComboBox(this);
    inputCombo_->setAccessibleName("Input Source");
    inputCombo_->setToolTip("Select audio input source");
    inputCombo_->setFixedHeight(18);
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
            this, &TrackHeaderWidget::onInputComboChanged);
    mainLayout->addWidget(inputCombo_);

    outputCombo_ = new QComboBox(this);
    outputCombo_->setAccessibleName("Output Destination");
    outputCombo_->setToolTip("Select output destination");
    outputCombo_->setFixedHeight(18);
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
            this, &TrackHeaderWidget::onOutputComboChanged);
    mainLayout->addWidget(outputCombo_);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(2);

    muteBtn_ = new QPushButton(this);
    muteBtn_->setAccessibleName("Mute");
    muteBtn_->setCheckable(true);
    muteBtn_->setFixedSize(26, 20);
    muteBtn_->setFont(icons::fontAudio(12));
    muteBtn_->setText(QString(icons::fa::Mute));
    applyToggleStyle(muteBtn_, theme.muteButton);
    connect(muteBtn_, &QPushButton::toggled, this, [this](bool m) {
        if (track_) track_->setMute(m);
    });

    soloBtn_ = new QPushButton(this);
    soloBtn_->setAccessibleName("Solo");
    soloBtn_->setCheckable(true);
    soloBtn_->setFixedSize(26, 20);
    soloBtn_->setFont(icons::fontAudio(12));
    soloBtn_->setText(QString(icons::fa::Solo));
    applyToggleStyle(soloBtn_, theme.soloButton);
    connect(soloBtn_, &QPushButton::toggled, this, [this](bool s) {
        if (track_) track_->setSolo(s);
    });

    armBtn_ = new QPushButton(this);
    armBtn_->setAccessibleName("Record Arm");
    armBtn_->setCheckable(true);
    armBtn_->setFixedSize(26, 20);
    armBtn_->setFont(icons::fontAudio(12));
    armBtn_->setText(QString(icons::fa::Armrecording));
    applyToggleStyle(armBtn_, theme.recordArm);
    connect(armBtn_, &QPushButton::toggled, this, &TrackHeaderWidget::onArmToggled);

    monoBtn_ = new QPushButton(this);
    monoBtn_->setAccessibleName("Mono Or Stereo");
    monoBtn_->setCheckable(true);
    monoBtn_->setFixedSize(26, 20);
    monoBtn_->setFont(icons::fontAudio(12));
    applyToggleStyle(monoBtn_, theme.accent);
    connect(monoBtn_, &QPushButton::toggled, this, [this](bool mono) {
        if (!track_ || !editMgr_) return;
        editMgr_->setTrackMono(*track_, mono);
        updateMonoButtonVisual(mono);
    });

    freezeBtn_ = new QPushButton(this);
    freezeBtn_->setAccessibleName("Freeze Track");
    freezeBtn_->setCheckable(false);
    freezeBtn_->setFixedSize(26, 20);
    freezeBtn_->setText("F");
    applyToggleStyle(freezeBtn_, QColor(0, 150, 200));
    connect(freezeBtn_, &QPushButton::clicked, this, [this]() {
        if (!track_ || !editMgr_) return;
        if (editMgr_->isRenderInProgress()) return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool frozen = editMgr_->isTrackFrozen(track_);
        if (frozen)
            editMgr_->unfreezeTrack(*track_);
        else
            editMgr_->freezeTrack(*track_);
        QApplication::restoreOverrideCursor();
    });

    btnRow->addWidget(muteBtn_);
    btnRow->addWidget(soloBtn_);
    btnRow->addWidget(armBtn_);
    btnRow->addWidget(monoBtn_);
    btnRow->addWidget(freezeBtn_);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    const bool mono = editMgr_ && editMgr_->isTrackMono(track_);
    {
        QSignalBlocker block(monoBtn_);
        monoBtn_->setChecked(mono);
    }
    updateMonoButtonVisual(mono);

    if (editMgr_ && track_) {
        updateFreezeButtonVisual(editMgr_->isTrackFrozen(track_));
    }

    connect(editMgr_, &EditManager::tracksChanged, this, [this]() {
        if (!track_ || !monoBtn_ || !editMgr_)
            return;
        const bool isMono = editMgr_->isTrackMono(track_);
        QSignalBlocker block(monoBtn_);
        monoBtn_->setChecked(isMono);
        updateMonoButtonVisual(isMono);
    });

    connect(editMgr_, &EditManager::trackFreezeStateChanged, this, [this](te::AudioTrack* t) {
        if (t != track_ || !freezeBtn_ || !editMgr_) return;
        updateFreezeButtonVisual(editMgr_->isTrackFrozen(track_));
    });

    auto* controlRow = new QHBoxLayout();
    controlRow->setSpacing(2);

    panKnob_ = new RotaryKnob(this);
    panKnob_->setAccessibleName("Pan");
    panKnob_->setRange(-1.0, 1.0);
    panKnob_->setValue(0.0);
    panKnob_->setLabel(QString());
    panKnob_->setFixedSize(36, 40);
    connect(panKnob_, &RotaryKnob::valueChanged, this, [this](double v) {
        if (!track_) return;
        for (auto* plugin : track_->pluginList.getPlugins()) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                volPan->panParam->setParameter(float(v), juce::sendNotification);
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

    automationBtn_ = new QPushButton(this);
    automationBtn_->setAccessibleName("Toggle Automation Lane");
    automationBtn_->setToolTip("Show/Hide Automation (A)");
    automationBtn_->setCheckable(true);
    automationBtn_->setFixedHeight(16);
    automationBtn_->setText(QString::fromUtf8("\xe2\x96\xb6") + " Auto");
    automationBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 2px; font-size: 8px; padding: 0px 4px; text-align: left; }"
                "QPushButton:checked { background: %4; color: #fff; }"
                "QPushButton:hover { border: 1px solid %5; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.accent.darker(150).name(),
                 theme.accent.name()));
    connect(automationBtn_, &QPushButton::toggled, this, [this](bool checked) {
        automationVisible_ = checked;
        automationBtn_->setText(checked
            ? (QString::fromUtf8("\xe2\x96\xbc") + " Auto")
            : (QString::fromUtf8("\xe2\x96\xb6") + " Auto"));
        emit automationToggled(track_, checked);
    });
    mainLayout->addWidget(automationBtn_);

    rowSeparator_ = new QFrame(this);
    rowSeparator_->setAccessibleName("Track Row Separator");
    rowSeparator_->setFrameShape(QFrame::NoFrame);
    rowSeparator_->setFixedHeight(1);
    rowSeparator_->setStyleSheet(
        QString("background: %1;")
            .arg(theme.border.lighter(145).name()));
    mainLayout->addWidget(rowSeparator_);

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

void TrackHeaderWidget::refresh()
{
    if (!track_) return;
    nameLabel_->setText(QString::fromStdString(track_->getName().toStdString()));

    bool anyTrackSoloed = false;
    if (editMgr_ && editMgr_->edit()) {
        for (auto* t : te::getAudioTracks(*editMgr_->edit())) {
            if (t->isSolo(false)) { anyTrackSoloed = true; break; }
        }
    }

    {
        QSignalBlocker block(muteBtn_);
        muteBtn_->setChecked(track_->isMuted(false));
    }
    {
        QSignalBlocker block(soloBtn_);
        soloBtn_->setChecked(track_->isSolo(false));
    }

    bool soloOverridesMute = track_->isSolo(false) && track_->isMuted(false);
    muteBtn_->setEnabled(!soloOverridesMute);
    if (soloOverridesMute)
        muteBtn_->setToolTip("Mute overridden by Solo");
    else
        muteBtn_->setToolTip("Mute");

    if (anyTrackSoloed && !track_->isSolo(false)) {
        nameLabel_->setStyleSheet(nameLabel_->styleSheet() + " QLabel { opacity: 0.5; }");
    }
    if (editMgr_ && armBtn_) {
        QSignalBlocker block(armBtn_);
        armBtn_->setChecked(editMgr_->isTrackRecordEnabled(track_));
    }
    if (editMgr_ && monoBtn_) {
        QSignalBlocker block(monoBtn_);
        bool mono = editMgr_->isTrackMono(track_);
        monoBtn_->setChecked(mono);
        updateMonoButtonVisual(mono);
    }

    for (auto* plugin : track_->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
            if (volumeSlider_) {
                float dbVal = te::volumeFaderPositionToDB(vp->volParam->getCurrentValue());
                double norm = (dbVal + 60.0) / 66.0;
                QSignalBlocker block(volumeSlider_);
                volumeSlider_->setValue(static_cast<int>(norm * 100.0));
            }
            if (panKnob_) {
                QSignalBlocker block(panKnob_);
                panKnob_->setValue(vp->pan.get());
            }
            break;
        }
    }
}

void TrackHeaderWidget::setAutomationVisible(bool visible)
{
    if (!automationBtn_) return;
    QSignalBlocker block(automationBtn_);
    automationBtn_->setChecked(visible);
    automationVisible_ = visible;
    automationBtn_->setText(visible
        ? (QString::fromUtf8("\xe2\x96\xbc") + " Auto")
        : (QString::fromUtf8("\xe2\x96\xb6") + " Auto"));
}

void TrackHeaderWidget::setTrackHeight(int h)
{
    int minH = minimumSizeHint().height();
    setFixedHeight(std::max(h, minH));
}

QSize TrackHeaderWidget::minimumSizeHint() const
{
    if (auto* l = layout())
        return QSize(width(), l->minimumSize().height());
    return QWidget::minimumSizeHint();
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
            "border-bottom: 0px solid transparent;")
                .arg(selectedBg.name(), theme.meterGreen.name(),
                     theme.border.lighter(115).name()));
    } else {
        setStyleSheet(QString(
            "background: %1; "
            "border-left: 1px solid transparent; "
            "border-top: 1px solid transparent; "
            "border-right: 1px solid transparent; "
            "border-bottom: 0px solid transparent;")
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

bool TrackHeaderWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == nameLabel_ && event->type() == QEvent::MouseButtonDblClick && track_) {
        startRenameEdit();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void TrackHeaderWidget::startRenameEdit()
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

    auto* track = track_;
    auto* mgr = editMgr_;
    auto* nameLbl = nameLabel_;

    connect(edit, &QLineEdit::editingFinished, edit, [edit, track, mgr, nameLbl]() {
        QString newName = edit->text().trimmed();
        edit->hide();
        edit->deleteLater();
        if (!newName.isEmpty() && track) {
            QTimer::singleShot(0, mgr, [track, mgr, newName]() {
                track->setName(juce::String(newName.toStdString()));
                emit mgr->tracksChanged();
            });
        } else if (nameLbl) {
            nameLbl->show();
        }
    });
}

void TrackHeaderWidget::updateMeter()
{
    if (!track_ || !editMgr_ || !editMgr_->edit())
        return;

    if (levelMeterPlugin_) {
        auto levelL = meterClient_.getAndClearAudioLevel(0);
        auto levelR = meterClient_.getAndClearAudioLevel(1);
        meter_->setLevel(dbToNormalized(levelL.dB), dbToNormalized(levelR.dB));
    }

    if (!editMgr_->transport().isPlaying()) return;

    auto mode = track_->automationMode.get();
    if (mode == te::AutomationMode::write || mode == te::AutomationMode::latch)
        return;

    for (auto* plugin : track_->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
            if (volumeSlider_) {
                float dbVal = te::volumeFaderPositionToDB(vp->volParam->getCurrentValue());
                double norm = (dbVal + 60.0) / 66.0;
                QSignalBlocker block(volumeSlider_);
                volumeSlider_->setValue(static_cast<int>(std::clamp(norm, 0.0, 1.0) * 100.0));
            }
            if (panKnob_) {
                QSignalBlocker block(panKnob_);
                panKnob_->setValue(vp->panParam->getCurrentValue());
            }
            break;
        }
    }
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

void TrackHeaderWidget::updateMonoButtonVisual(bool mono)
{
    if (!monoBtn_)
        return;

    monoBtn_->setText(QString(mono ? icons::fa::Mono : icons::fa::Stereo));
    monoBtn_->setToolTip(mono ? "Track is mono" : "Track is stereo");
}

void TrackHeaderWidget::updateFreezeButtonVisual(bool frozen)
{
    if (!freezeBtn_) return;

    auto& theme = ThemeManager::instance().current();
    QColor freezeColor(0, 150, 200);

    if (frozen) {
        freezeBtn_->setToolTip("Unfreeze Track (click to restore live processing)");
        freezeBtn_->setStyleSheet(
            QString("QPushButton { background: %1; color: #fff; font-size: 9px; "
                    "font-weight: bold; border: 1px solid %1; border-radius: 3px; }"
                    "QPushButton:hover { background: %2; }")
                .arg(freezeColor.name(), freezeColor.lighter(130).name()));

        QColor frozenBg = theme.surface;
        frozenBg = frozenBg.darker(110);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, frozenBg);
        setPalette(pal);

        if (nameLabel_) {
            nameLabel_->setStyleSheet(
                QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                        "background: %2; border: 1px solid %3; "
                        "border-radius: 2px; padding: 2px; }")
                    .arg(freezeColor.name(), theme.background.name(),
                         freezeColor.name()));
        }
    } else {
        freezeBtn_->setToolTip("Freeze Track (render effects to audio)");
        applyToggleStyle(freezeBtn_, freezeColor);

        QPalette pal = palette();
        pal.setColor(QPalette::Window, theme.surface);
        setPalette(pal);

        bool isMidi = editMgr_ && editMgr_->isMidiTrack(track_);
        if (nameLabel_) {
            QColor nameBg = isMidi ? theme.midiClipBody : theme.background;
            nameLabel_->setStyleSheet(
                QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                        "background: %2; border-radius: 2px; padding: 2px; }")
                    .arg(theme.text.name(), nameBg.name()));
        }
    }
}

void TrackHeaderWidget::populateInputCombo()
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

void TrackHeaderWidget::onInputComboChanged(int index)
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

void TrackHeaderWidget::populateOutputCombo()
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

void TrackHeaderWidget::onOutputComboChanged(int index)
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

void TrackHeaderWidget::onArmToggled(bool armed)
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

} // namespace freedaw
