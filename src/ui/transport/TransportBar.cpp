#include "TransportBar.h"
#include "ui/timeline/GridSnapper.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include <QDialog>
#include <QSettings>
#include <QVBoxLayout>
#include <cmath>

namespace OpenDaw {

TransportBar::TransportBar(EditManager* editMgr, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("Transport Bar");
    setFixedHeight(56);

    auto& theme = ThemeManager::instance().current();
    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.surface);
    setPalette(pal);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(8);

    const QFont iconFont = icons::fontAudio(16);
    const QSize transportButtonSize(36, 34);

    auto makeTransportBtn = [&](const QChar& glyph, const QString& name,
                                const QColor& activeColor, bool checkable = false) {
        auto* btn = new QPushButton(glyph, this);
        btn->setAccessibleName(name);
        btn->setToolTip(name);
        btn->setFont(iconFont);
        btn->setCheckable(checkable);
        btn->setFixedSize(transportButtonSize);
        applyButtonStyle(btn, activeColor);
        return btn;
    };

    stopBtn_   = makeTransportBtn(icons::fa::Stop,   "Stop",   theme.transportStop);
    playBtn_   = makeTransportBtn(icons::fa::Play,   "Play",   theme.transportPlay, true);
    recordBtn_ = makeTransportBtn(icons::fa::Record, "Record", theme.transportRecord, true);
    loopBtn_   = makeTransportBtn(icons::fa::Loop,   "Loop",   theme.accent, true);
    metronomeBtn_ = makeTransportBtn(icons::fa::Metronome, "Metronome", QColor(60, 180, 200), true);

    countInBtn_ = new QPushButton("1-2", this);
    countInBtn_->setAccessibleName("Count-In Before Recording");
    countInBtn_->setToolTip("Count-in before recording");
    countInBtn_->setCheckable(true);
    countInBtn_->setFixedSize(transportButtonSize);
    applyButtonStyle(countInBtn_, QColor(200, 160, 60));

    countInCombo_ = new QComboBox(this);
    countInCombo_->setAccessibleName("Count-In Length");
    countInCombo_->setToolTip("Count-in length");
    countInCombo_->addItem("1 Bar",  1);
    countInCombo_->addItem("2 Bars", 2);
    countInCombo_->addItem("1 Beat", 4);
    countInCombo_->addItem("2 Beats", 3);
    countInCombo_->setFixedWidth(80);
    countInCombo_->setFixedHeight(28);
    countInCombo_->setEnabled(false);

    const QFont miFont = icons::materialIcons(16);
    panicBtn_ = new QPushButton(this);
    panicBtn_->setAccessibleName("MIDI Panic - Stop All Notes");
    panicBtn_->setToolTip("MIDI Panic — stop all notes & reset (Ctrl+Shift+P)");
    panicBtn_->setFont(miFont);
    panicBtn_->setText(QString(icons::mi::Warning));
    panicBtn_->setFixedSize(transportButtonSize);
    panicBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 5px; font-weight: bold; }"
                "QPushButton:hover { background: %4; color: #ff4444; }"
                "QPushButton:pressed { background: #cc2222; color: white; }")
            .arg(theme.surface.name(), QColor(220, 80, 60).name(),
                 theme.border.name(), theme.surfaceLight.name()));

    engineBtn_ = new QPushButton(this);
    engineBtn_->setAccessibleName("Audio Engine Toggle");
    engineBtn_->setToolTip("Audio Engine — click to disable/enable");
    engineBtn_->setFont(iconFont);
    engineBtn_->setText(QString(icons::fa::Powerswitch));
    engineBtn_->setCheckable(true);
    engineBtn_->setChecked(true);
    engineBtn_->setFixedSize(transportButtonSize);
    updateEngineButtonStyle();

    connect(stopBtn_,      &QPushButton::clicked, this, &TransportBar::onStop);
    connect(playBtn_,      &QPushButton::clicked, this, &TransportBar::onPlay);
    connect(recordBtn_,    &QPushButton::clicked, this, &TransportBar::onRecord);
    connect(loopBtn_,      &QPushButton::clicked, this, &TransportBar::onLoop);
    connect(metronomeBtn_, &QPushButton::clicked, this, &TransportBar::onMetronome);
    connect(countInBtn_,   &QPushButton::clicked, this, &TransportBar::onCountIn);
    connect(countInCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TransportBar::onCountInModeChanged);
    connect(panicBtn_,     &QPushButton::clicked, this, &TransportBar::onPanic);
    connect(engineBtn_,    &QPushButton::clicked, this, &TransportBar::onEngineToggle);

    const QString lcdStyle =
        QString("QLabel { color: %1; background: %2; "
                "border: 1px solid %3; border-top-color: %4; "
                "border-radius: 4px; font-family: 'Consolas', 'Courier New', monospace; "
                "font-size: 12pt; font-weight: bold; padding: 4px 8px; letter-spacing: 1px; }")
            .arg(theme.accentLight.name(),
                 QColor(16, 16, 20).name(),
                 QColor(22, 22, 28).name(),
                 QColor(12, 12, 16).name());

    positionLabel_ = new QLabel("00:00:00", this);
    positionLabel_->setAccessibleName("Position Time");
    positionLabel_->setFixedWidth(120);
    positionLabel_->setAlignment(Qt::AlignCenter);
    positionLabel_->setStyleSheet(lcdStyle);

    beatLabel_ = new QLabel("001.1.00", this);
    beatLabel_->setAccessibleName("Position Bars Beats");
    beatLabel_->setFixedWidth(110);
    beatLabel_->setAlignment(Qt::AlignCenter);
    beatLabel_->setStyleSheet(lcdStyle);

    layout->addWidget(positionLabel_);
    layout->addWidget(beatLabel_);

    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet(QString("color: %1;").arg(theme.border.name()));
    layout->addWidget(sep1);

    layout->addWidget(stopBtn_);
    layout->addWidget(playBtn_);
    layout->addWidget(recordBtn_);
    layout->addWidget(loopBtn_);
    layout->addWidget(metronomeBtn_);
    layout->addWidget(countInBtn_);
    layout->addWidget(countInCombo_);

    auto* sepPanic = new QFrame(this);
    sepPanic->setFrameShape(QFrame::VLine);
    sepPanic->setStyleSheet(QString("color: %1;").arg(theme.border.name()));
    layout->addWidget(sepPanic);
    layout->addWidget(panicBtn_);
    layout->addWidget(engineBtn_);

    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet(sep1->styleSheet());
    layout->addWidget(sep2);

    auto* bpmLabel = new QLabel("BPM", this);
    bpmLabel->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; border: none; "
                "font-size: 10px; font-weight: bold; letter-spacing: 0.5px; }")
            .arg(theme.textDim.name()));
    bpmSpin_ = new QDoubleSpinBox(this);
    bpmSpin_->setAccessibleName("Tempo BPM");
    bpmSpin_->setRange(20.0, 300.0);
    bpmSpin_->setDecimals(1);
    bpmSpin_->setValue(editMgr_->getBpm());
    bpmSpin_->setFixedWidth(80);
    bpmSpin_->setFixedHeight(28);
    connect(bpmSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TransportBar::onBpmChanged);

    layout->addWidget(bpmLabel);
    layout->addWidget(bpmSpin_);

    auto* tsLabel = new QLabel("Time Sig", this);
    tsLabel->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; border: none; "
                "font-size: 10px; font-weight: bold; letter-spacing: 0.5px; }")
            .arg(theme.textDim.name()));
    timeSigNumSpin_ = new QSpinBox(this);
    timeSigNumSpin_->setAccessibleName("Time Signature Numerator");
    timeSigNumSpin_->setRange(1, 16);
    timeSigNumSpin_->setValue(editMgr_->getTimeSigNumerator());
    timeSigNumSpin_->setFixedWidth(50);
    timeSigNumSpin_->setFixedHeight(28);
    connect(timeSigNumSpin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &TransportBar::onTimeSigNumChanged);

    auto* slash = new QLabel("/", this);
    slash->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; border: none; "
                "font-size: 12px; font-weight: bold; }")
            .arg(theme.textDim.name()));

    timeSigDenCombo_ = new QComboBox(this);
    timeSigDenCombo_->setAccessibleName("Time Signature Denominator");
    for (int d : {1, 2, 4, 8, 16})
        timeSigDenCombo_->addItem(QString::number(d), d);
    int denIdx = timeSigDenCombo_->findData(editMgr_->getTimeSigDenominator());
    if (denIdx >= 0) timeSigDenCombo_->setCurrentIndex(denIdx);
    timeSigDenCombo_->setFixedWidth(50);
    timeSigDenCombo_->setFixedHeight(28);
    connect(timeSigDenCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                if (!editMgr_ || !editMgr_->edit()) return;
                int den = timeSigDenCombo_->itemData(idx).toInt();
                editMgr_->setTimeSignature(timeSigNumSpin_->value(), den);
            });

    layout->addWidget(tsLabel);
    layout->addWidget(timeSigNumSpin_);
    layout->addWidget(slash);
    layout->addWidget(timeSigDenCombo_);

    auto* snapLabel = new QLabel("Snap", this);
    snapLabel->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; border: none; "
                "font-size: 10px; font-weight: bold; letter-spacing: 0.5px; }")
            .arg(theme.textDim.name()));
    snapCombo_ = new QComboBox(this);
    snapCombo_->setAccessibleName("Grid Snap Mode");
    snapCombo_->addItem("Off",  int(SnapMode::Off));
    snapCombo_->addItem("Bar",  int(SnapMode::Bar));
    snapCombo_->addItem("1/2",  int(SnapMode::HalfNote));
    snapCombo_->addItem("1/4",  int(SnapMode::Beat));
    snapCombo_->addItem("1/8",  int(SnapMode::HalfBeat));
    snapCombo_->addItem("1/16", int(SnapMode::QuarterBeat));
    snapCombo_->addItem("1/32", int(SnapMode::EighthBeat));
    snapCombo_->setCurrentIndex(3);
    snapCombo_->setFixedWidth(100);
    snapCombo_->setFixedHeight(28);
    connect(snapCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                emit snapModeRequested(snapCombo_->itemData(idx).toInt());
            });

    layout->addWidget(snapLabel);
    layout->addWidget(snapCombo_);

    layout->addStretch();

    // Restore metronome and count-in state from settings
    {
        QSettings settings;
        bool metroOn = settings.value("transport/metronomeEnabled", false).toBool();
        metronomeBtn_->setChecked(metroOn);
        if (editMgr_ && editMgr_->edit())
            editMgr_->edit()->clickTrackEnabled = metroOn;

        bool countInOn = settings.value("transport/countInEnabled", false).toBool();
        int countInMode = settings.value("transport/countInMode", 1).toInt();
        int comboIdx = countInCombo_->findData(countInMode);
        if (comboIdx >= 0)
            countInCombo_->setCurrentIndex(comboIdx);
        countInBtn_->setChecked(countInOn);
        countInCombo_->setEnabled(countInOn);
        syncCountInToEngine();
    }

    connect(&positionTimer_, &QTimer::timeout, this, &TransportBar::updatePosition);
    positionTimer_.start(16);
}

void TransportBar::onPlay()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& t = editMgr_->transport();
    if (t.isPlaying()) {
        t.stop(false, false);
        if (deferredLoopActivation_) {
            t.looping.setValue(true, nullptr);
            deferredLoopActivation_ = false;
        }
        if (isRecording_) {
            isRecording_ = false;
            recordBtn_->setChecked(false);
        }
    } else {
        // If looping is on and playhead is before the loop region,
        // temporarily disable engine looping so playback starts from
        // the current position rather than jumping to loop start.
        if (t.looping.get()) {
            auto pos = t.getPosition();
            auto loopStart = t.loopPoint1.get();
            if (pos < loopStart) {
                t.looping.setValue(false, nullptr);
                deferredLoopActivation_ = true;
            }
        }
        t.play(false);
    }
    playBtn_->setChecked(t.isPlaying());
}

void TransportBar::onStop()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& t = editMgr_->transport();
    t.stop(false, false);
    if (deferredLoopActivation_) {
        t.looping.setValue(true, nullptr);
        deferredLoopActivation_ = false;
    }
    t.setPosition(tracktion::TimePosition::fromSeconds(0));
    playBtn_->setChecked(false);
    recordBtn_->setChecked(false);
    isRecording_ = false;
}

void TransportBar::onRecord()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& t = editMgr_->transport();
    if (isRecording_) {
        t.stop(false, false);
        isRecording_ = false;
    } else {
        t.record(false);
        isRecording_ = true;
    }
    recordBtn_->setChecked(isRecording_);
    playBtn_->setChecked(t.isPlaying());
}

void TransportBar::onLoop()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& t = editMgr_->transport();
    bool enabling = !t.looping.get();

    // If enabling loop with no range set, create a default range of 4 bars from playhead
    if (enabling) {
        auto p1 = t.loopPoint1.get();
        auto p2 = t.loopPoint2.get();
        if (p1 == p2) {
            auto& ts = editMgr_->edit()->tempoSequence;
            double playBeat = ts.toBeats(t.getPosition()).inBeats();
            double beatsPerBar = editMgr_->getTimeSigNumerator();
            double barStart = std::floor(playBeat / beatsPerBar) * beatsPerBar;
            double loopIn = std::max(0.0, barStart);
            double loopOut = loopIn + beatsPerBar * 4.0;
            t.loopPoint1 = ts.toTime(tracktion::BeatPosition::fromBeats(loopIn));
            t.loopPoint2 = ts.toTime(tracktion::BeatPosition::fromBeats(loopOut));
        }
    }

    t.looping.setValue(enabling, nullptr);
    loopBtn_->setChecked(t.looping.get());
    emit loopToggled(t.looping.get());
}

void TransportBar::onMetronome()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    bool on = metronomeBtn_->isChecked();
    editMgr_->edit()->clickTrackEnabled = on;
    QSettings settings;
    settings.setValue("transport/metronomeEnabled", on);
}

void TransportBar::onCountIn()
{
    bool on = countInBtn_->isChecked();
    countInCombo_->setEnabled(on);
    syncCountInToEngine();
    QSettings settings;
    settings.setValue("transport/countInEnabled", on);
}

void TransportBar::onCountInModeChanged(int /*index*/)
{
    syncCountInToEngine();
    QSettings settings;
    settings.setValue("transport/countInMode", countInCombo_->currentData().toInt());
}

void TransportBar::syncCountInToEngine()
{
    if (!editMgr_ || !editMgr_->edit()) return;
    if (countInBtn_->isChecked()) {
        int mode = countInCombo_->currentData().toInt();
        editMgr_->edit()->setCountInMode(
            static_cast<te::Edit::CountIn>(mode));
    } else {
        editMgr_->edit()->setCountInMode(te::Edit::CountIn::none);
    }
}

void TransportBar::onPanic()
{
    if (!editMgr_) return;
    editMgr_->midiPanic();
    playBtn_->setChecked(false);
    recordBtn_->setChecked(false);
    isRecording_ = false;
}

void TransportBar::onEngineToggle()
{
    if (!editMgr_) return;

    bool turningOn = engineBtn_->isChecked();

    if (turningOn) {
        auto& theme = ThemeManager::instance().current();
        auto* busyDlg = new QDialog(window());
        busyDlg->setWindowTitle("Audio Engine");
        busyDlg->setAccessibleName("Audio Engine Initializing");
        busyDlg->setModal(true);
        busyDlg->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        busyDlg->setFixedSize(320, 100);

        auto* label = new QLabel("Initializing audio engine...", busyDlg);
        label->setAccessibleName("Engine Status");
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(
            QString("QLabel { color: %1; font-size: 12px; font-weight: bold; }")
                .arg(theme.text.name()));
        auto* dlgLayout = new QVBoxLayout(busyDlg);
        dlgLayout->addWidget(label);

        QTimer::singleShot(50, busyDlg, [this, busyDlg]() {
            editMgr_->resumeEngine();
            busyDlg->accept();
        });

        busyDlg->exec();
        delete busyDlg;
    } else {
        editMgr_->suspendEngine();
    }

    updateEngineButtonStyle();
    playBtn_->setChecked(false);
    recordBtn_->setChecked(false);
    isRecording_ = false;
}

void TransportBar::updateEngineButtonStyle()
{
    auto& theme = ThemeManager::instance().current();
    bool active = engineBtn_->isChecked();

    if (active) {
        engineBtn_->setStyleSheet(
            QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 5px; font-weight: bold; }"
                    "QPushButton:hover { background: %4; }")
                .arg(QColor(20, 110, 48).name(), QColor(60, 220, 65).name(),
                     QColor(30, 140, 55).name(), QColor(25, 128, 52).name()));
    } else {
        engineBtn_->setStyleSheet(
            QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 5px; font-weight: bold; }"
                    "QPushButton:hover { background: %4; }")
                .arg(theme.surface.name(), theme.textDim.name(),
                     theme.border.name(), theme.surfaceLight.name()));
    }
}

void TransportBar::onBpmChanged(double bpm)
{
    if (!editMgr_ || !editMgr_->edit()) return;
    auto& ts = editMgr_->edit()->tempoSequence;
    auto& transport = editMgr_->transport();
    double beatBefore = ts.toBeats(transport.getPosition()).inBeats();
    editMgr_->setBpm(bpm);
    auto newTime = ts.toTime(tracktion::BeatPosition::fromBeats(beatBefore));
    transport.setPosition(newTime);
}

void TransportBar::onTimeSigNumChanged(int num)
{
    if (!editMgr_ || !editMgr_->edit()) return;
    int den = timeSigDenCombo_->currentData().toInt();
    editMgr_->setTimeSignature(num, den);
}

void TransportBar::updatePosition()
{
    if (!editMgr_ || !editMgr_->edit()) return;

    if (editMgr_->isEngineSuspended()) {
        positionLabel_->setText("-- OFF --");
        beatLabel_->setText("---");
        return;
    }

    auto pos = editMgr_->transport().getPosition();
    double secs = std::max(0.0, pos.inSeconds());

    int mins = int(secs) / 60;
    int wholeSecs = int(secs) % 60;
    int centisecs = int((secs - std::floor(secs)) * 100);
    positionLabel_->setText(
        QString("%1:%2:%3")
            .arg(mins, 2, 10, QChar('0'))
            .arg(wholeSecs, 2, 10, QChar('0'))
            .arg(centisecs, 2, 10, QChar('0')));

    auto& ts2 = editMgr_->edit()->tempoSequence;
    double beat = std::max(0.0, ts2.toBeats(pos).inBeats());
    int beatsPerBar = editMgr_->getTimeSigNumerator();
    int bar = int(beat / beatsPerBar) + 1;
    int beatInBar = int(std::fmod(beat, beatsPerBar)) + 1;
    int ticks = int(std::fmod(beat, 1.0) * 100);

    beatLabel_->setText(
        QString("%1.%2.%3")
            .arg(bar, 3, 10, QChar('0'))
            .arg(beatInBar)
            .arg(ticks, 2, 10, QChar('0')));

    playBtn_->setChecked(editMgr_->transport().isPlaying());

    // Re-enable looping once the playhead reaches the loop region
    if (deferredLoopActivation_ && editMgr_->transport().isPlaying()) {
        auto loopStart = editMgr_->transport().loopPoint1.get();
        if (pos >= loopStart) {
            editMgr_->transport().looping.setValue(true, nullptr);
            deferredLoopActivation_ = false;
        }
    }

    loopBtn_->setChecked(editMgr_->transport().looping.get() || deferredLoopActivation_);
}

void TransportBar::applyButtonStyle(QPushButton* btn, const QColor& activeColor)
{
    auto& theme = ThemeManager::instance().current();
    QColor hoverBg = theme.surfaceLight.lighter(115);
    btn->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 5px; font-weight: bold; font-size: 8pt; }"
                "QPushButton:hover { background: %4; border-color: %5; }"
                "QPushButton:checked { background: %6; color: white; border-color: %7; }"
                "QPushButton:pressed { background: %6; }")
            .arg(theme.surface.name(), theme.text.name(), theme.border.name(),
                 hoverBg.name(), QColor(theme.border).lighter(130).name(),
                 activeColor.name(), activeColor.darker(130).name()));
}

} // namespace OpenDaw
