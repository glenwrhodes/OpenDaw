#include "TransportBar.h"
#include "ui/timeline/GridSnapper.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QSettings>
#include <QVBoxLayout>
#include <cmath>

namespace OpenDaw {

TransportBar::TransportBar(EditManager* editMgr, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr),
      deferredLoopActivation_(editMgr->loopActivationDeferred())
{
    setAccessibleName("Transport Bar");
    setFixedHeight(58);

    auto& theme = ThemeManager::instance().current();
    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.surface);
    setPalette(pal);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    const QFont iconFont = icons::fontAudio(17);
    // Every button and box in the row shares one height, so they line up.
    constexpr int kRowHeight = 34;
    const QSize transportButtonSize(38, kRowHeight);

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

    auto makeSeparator = [&]() {
        auto* sep = new QFrame(this);
        sep->setFixedSize(1, kRowHeight);
        sep->setStyleSheet(QString("QFrame { background: %1; border: none; }")
                               .arg(theme.separator.name()));
        return sep;
    };

    // ── Transport buttons ──
    rtzBtn_    = makeTransportBtn(icons::fa::Prev,   "Return to Start", theme.text);
    stopBtn_   = makeTransportBtn(icons::fa::Stop,   "Stop",   theme.transportStop);
    playBtn_   = makeTransportBtn(icons::fa::Play,   "Play (Space)",   theme.transportPlay, true);
    recordBtn_ = makeTransportBtn(icons::fa::Record, "Record (R)", theme.transportRecord, true);
    loopBtn_   = makeTransportBtn(icons::fa::Loop,   "Loop", theme.accent, true);
    metronomeBtn_ = makeTransportBtn(icons::fa::Metronome, "Metronome", theme.accent, true);

    // The record glyph reads as "record" only in red.
    recordBtn_->setStyleSheet(recordBtn_->styleSheet()
        + QString("QPushButton { color: %1; } QPushButton:checked { color: white; }")
              .arg(theme.transportRecord.name()));

    countInBtn_ = new QPushButton("Count-in", this);
    countInBtn_->setAccessibleName("Count-In Before Recording");
    countInBtn_->setToolTip("Count-in before recording");
    countInBtn_->setCheckable(true);
    countInBtn_->setFixedSize(72, kRowHeight);
    applyButtonStyle(countInBtn_, theme.accent);

    countInCombo_ = new QComboBox(this);
    countInCombo_->setAccessibleName("Count-In Length");
    countInCombo_->setToolTip("Count-in length");
    countInCombo_->addItem("1 Bar",  1);
    countInCombo_->addItem("2 Bars", 2);
    countInCombo_->addItem("1 Beat", 4);
    countInCombo_->addItem("2 Beats", 3);
    countInCombo_->setFixedSize(84, kRowHeight);
    countInCombo_->setEnabled(false);

    panicBtn_ = new QPushButton(this);
    panicBtn_->setAccessibleName("MIDI Panic - Stop All Notes");
    panicBtn_->setToolTip("MIDI panic: stop all notes and reset (Ctrl+Shift+P)");
    panicBtn_->setText("PANIC");
    panicBtn_->setFixedSize(58, kRowHeight);
    panicBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; font-size: 9px; font-weight: bold; letter-spacing: 1px; }"
                "QPushButton:hover { color: %4; border-color: %4; }"
                "QPushButton:pressed { background: %4; color: white; }")
            .arg(theme.background.name(), theme.textDim.name(),
                 theme.border.name(), theme.recordArm.name()));

    engineBtn_ = new QPushButton(this);
    engineBtn_->setAccessibleName("Audio Engine Toggle");
    engineBtn_->setToolTip("Audio engine: click to turn off/on");
    engineBtn_->setCheckable(true);
    engineBtn_->setChecked(true);
    engineBtn_->setFixedSize(88, kRowHeight);
    updateEngineButtonStyle();

    connect(rtzBtn_,       &QPushButton::clicked, this, [this]() {
        if (!editMgr_ || !editMgr_->edit()) return;
        editMgr_->transport().setPosition(tracktion::TimePosition::fromSeconds(0.0));
    });
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

    layout->addWidget(rtzBtn_);
    layout->addWidget(stopBtn_);
    layout->addWidget(playBtn_);
    layout->addWidget(recordBtn_);
    layout->addSpacing(6);
    layout->addWidget(loopBtn_);
    layout->addWidget(metronomeBtn_);
    layout->addWidget(countInBtn_);
    layout->addWidget(countInCombo_);
    layout->addSpacing(4);
    layout->addWidget(makeSeparator());
    layout->addSpacing(4);

    // ── Counter: bars|beats large, min:sec underneath ──
    auto* counter = new QFrame(this);
    counter->setObjectName("transportCounter");
    counter->setFixedSize(172, 46);
    counter->setStyleSheet(
        QString("QFrame#transportCounter { background: %1; border: 1px solid %2; "
                "border-radius: 5px; }")
            .arg(theme.lcdBackground.name(), theme.separator.name()));
    auto* counterLayout = new QVBoxLayout(counter);
    counterLayout->setContentsMargins(10, 2, 10, 3);
    counterLayout->setSpacing(0);

    beatLabel_ = new QLabel("1 | 1 | 000", counter);
    beatLabel_->setAccessibleName("Position Bars Beats");
    beatLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    beatLabel_->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; "
                "font-family: 'Consolas', 'Cascadia Mono', monospace; "
                "font-size: 21px; font-weight: bold; }")
            .arg(theme.lcdText.name()));

    positionLabel_ = new QLabel("0:00.000", counter);
    positionLabel_->setAccessibleName("Position Time");
    positionLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    positionLabel_->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; "
                "font-family: 'Consolas', 'Cascadia Mono', monospace; font-size: 11px; }")
            .arg(theme.textDim.name()));

    counterLayout->addWidget(beatLabel_);
    counterLayout->addWidget(positionLabel_);
    layout->addWidget(counter);
    layout->addSpacing(8);

    // ── Tempo, meter, grid: a second display panel beside the counter ──
    auto* info = new QFrame(this);
    info->setObjectName("transportInfo");
    info->setFixedHeight(counter->height());
    const QColor& lcd = theme.lcdText;
    auto lcdAlpha = [&](int a) {
        return QString("rgba(%1,%2,%3,%4)").arg(lcd.red()).arg(lcd.green()).arg(lcd.blue()).arg(a);
    };
    info->setStyleSheet(
        QString("QFrame#transportInfo { background: %1; border: 1px solid %2; border-radius: 5px; }"
                "QLabel { color: %3; background: transparent; font-size: 9px; "
                "  font-weight: bold; letter-spacing: 1px; }"
                "QLabel#meterSlash { font-size: 14px; letter-spacing: 0px; }"
                "QAbstractSpinBox, QComboBox { background: transparent; color: %4; "
                "  border: 1px solid transparent; border-radius: 3px; padding: 0px 3px; "
                "  min-height: 0px; font-family: 'Consolas', 'Cascadia Mono', monospace; "
                "  font-size: 14px; font-weight: bold; }"
                "QAbstractSpinBox:hover, QComboBox:hover { background: %5; border-color: %6; }"
                "QAbstractSpinBox:focus { border-color: %7; }"
                "QComboBox::drop-down { border: none; width: 12px; }"
                "QComboBox::down-arrow { image: url(%8); width: 9px; height: 6px; }")
            .arg(theme.lcdBackground.name(), theme.separator.name(), lcdAlpha(150),
                 lcd.name(), lcdAlpha(28), lcdAlpha(60), theme.accent.name(),
                 ThemeManager::dropDownArrowPath(lcd)));
    auto* infoLayout = new QGridLayout(info);
    infoLayout->setContentsMargins(8, 4, 8, 4);
    infoLayout->setHorizontalSpacing(10);
    infoLayout->setVerticalSpacing(0);

    constexpr int kValueHeight = 22;
    // Values are typed, scrolled or nudged with the arrow keys; spin buttons would not fit.
    const QString valueHint = "Type a value, or scroll over it to change it";

    bpmSpin_ = new QDoubleSpinBox(info);
    bpmSpin_->setAccessibleName("Tempo BPM");
    bpmSpin_->setToolTip("Tempo (BPM). " + valueHint);
    bpmSpin_->setRange(20.0, 300.0);
    bpmSpin_->setDecimals(1);
    bpmSpin_->setValue(editMgr_->getBpm());
    bpmSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    bpmSpin_->setFixedSize(58, kValueHeight);
    connect(bpmSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &TransportBar::onBpmChanged);

    timeSigNumSpin_ = new QSpinBox(info);
    timeSigNumSpin_->setAccessibleName("Time Signature Numerator");
    timeSigNumSpin_->setToolTip("Beats per bar. " + valueHint);
    timeSigNumSpin_->setRange(1, 16);
    timeSigNumSpin_->setValue(editMgr_->getTimeSigNumerator());
    timeSigNumSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    timeSigNumSpin_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeSigNumSpin_->setFixedSize(28, kValueHeight);
    connect(timeSigNumSpin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &TransportBar::onTimeSigNumChanged);

    timeSigDenCombo_ = new QComboBox(info);
    timeSigDenCombo_->setAccessibleName("Time Signature Denominator");
    for (int d : {1, 2, 4, 8, 16})
        timeSigDenCombo_->addItem(QString::number(d), d);
    int denIdx = timeSigDenCombo_->findData(editMgr_->getTimeSigDenominator());
    if (denIdx >= 0) timeSigDenCombo_->setCurrentIndex(denIdx);
    timeSigDenCombo_->setToolTip("Beat unit");
    timeSigDenCombo_->setFixedSize(40, kValueHeight);
    connect(timeSigDenCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                if (!editMgr_ || !editMgr_->edit()) return;
                int den = timeSigDenCombo_->itemData(idx).toInt();
                editMgr_->setTimeSignature(timeSigNumSpin_->value(), den);
            });

    auto* meterLayout = new QHBoxLayout;
    meterLayout->setContentsMargins(0, 0, 0, 0);
    meterLayout->setSpacing(0);
    auto* slash = new QLabel("/", info);
    slash->setObjectName("meterSlash");
    meterLayout->addWidget(timeSigNumSpin_);
    meterLayout->addWidget(slash);
    meterLayout->addWidget(timeSigDenCombo_);

    snapCombo_ = new QComboBox(info);
    snapCombo_->setAccessibleName("Grid Snap Mode");
    snapCombo_->setToolTip("Snap to grid");
    snapCombo_->addItem("Off",  int(SnapMode::Off));
    snapCombo_->addItem("Bar",  int(SnapMode::Bar));
    snapCombo_->addItem("1/2",  int(SnapMode::HalfNote));
    snapCombo_->addItem("1/4",  int(SnapMode::Beat));
    snapCombo_->addItem("1/8",  int(SnapMode::HalfBeat));
    snapCombo_->addItem("1/16", int(SnapMode::QuarterBeat));
    snapCombo_->addItem("1/32", int(SnapMode::EighthBeat));
    snapCombo_->setCurrentIndex(3);
    snapCombo_->setFixedSize(58, kValueHeight);
    connect(snapCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                emit snapModeRequested(snapCombo_->itemData(idx).toInt());
            });

    // Captions sit over the value text (inset past the box's border and padding).
    auto addCaption = [&](const QString& text, int column) {
        auto* label = new QLabel(text, info);
        label->setContentsMargins(4, 0, 0, 0);
        infoLayout->addWidget(label, 0, column);
    };
    addCaption("TEMPO", 0);
    addCaption("METER", 1);
    addCaption("GRID", 2);
    infoLayout->addWidget(bpmSpin_, 1, 0);
    infoLayout->addLayout(meterLayout, 1, 1);
    infoLayout->addWidget(snapCombo_, 1, 2);
    layout->addWidget(info);

    layout->addStretch();

    // ── Rarely used: MIDI panic, engine power ──
    layout->addWidget(panicBtn_);
    layout->addWidget(engineBtn_);

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

    // A theme change rebuilds this bar mid-session; pick up a recording in progress.
    if (editMgr_->edit() && editMgr_->transport().isRecording()) {
        isRecording_ = true;
        recordBtn_->setChecked(true);
    }

    connect(editMgr_, &EditManager::editChanged, this, [this]() {
        if (!editMgr_ || !editMgr_->edit()) return;
        double bpm = editMgr_->getBpm();
        if (bpmSpin_->value() != bpm) {
            QSignalBlocker blocker(bpmSpin_);
            bpmSpin_->setValue(bpm);
        }
        int num = editMgr_->getTimeSigNumerator();
        if (timeSigNumSpin_->value() != num) {
            QSignalBlocker blocker(timeSigNumSpin_);
            timeSigNumSpin_->setValue(num);
        }
        int den = editMgr_->getTimeSigDenominator();
        int denIdx = timeSigDenCombo_->findText(QString::number(den));
        if (denIdx >= 0 && timeSigDenCombo_->currentIndex() != denIdx) {
            QSignalBlocker blocker(timeSigDenCombo_);
            timeSigDenCombo_->setCurrentIndex(denIdx);
        }
    });

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
    const bool active = engineBtn_->isChecked();
    engineBtn_->setText(active ? "ENGINE ON" : "ENGINE OFF");
    engineBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; font-size: 9px; font-weight: bold; letter-spacing: 1px; }"
                "QPushButton:hover { background: %4; }")
            .arg(theme.background.name(),
                 (active ? theme.meterGreen : theme.textDim).name(),
                 theme.border.name(), theme.surfaceLight.name()));
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
        positionLabel_->setText("engine off");
        beatLabel_->setText("- | - | ---");
        return;
    }

    auto pos = editMgr_->transport().getPosition();
    double secs = std::max(0.0, pos.inSeconds());

    int mins = int(secs) / 60;
    int wholeSecs = int(secs) % 60;
    int millis = int((secs - std::floor(secs)) * 1000);
    positionLabel_->setText(
        QString("%1:%2.%3")
            .arg(mins)
            .arg(wholeSecs, 2, 10, QChar('0'))
            .arg(millis, 3, 10, QChar('0')));

    auto& ts2 = editMgr_->edit()->tempoSequence;
    double beat = std::max(0.0, ts2.toBeats(pos).inBeats());
    int beatsPerBar = editMgr_->getTimeSigNumerator();
    int bar = int(beat / beatsPerBar) + 1;
    int beatInBar = int(std::fmod(beat, beatsPerBar)) + 1;
    int ticks = int(std::fmod(beat, 1.0) * 960);   // 960 ticks per quarter note

    beatLabel_->setText(
        QString("%1 | %2 | %3")
            .arg(bar)
            .arg(beatInBar)
            .arg(ticks, 3, 10, QChar('0')));

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
    const QColor checkedText = activeColor.lightnessF() > 0.6 ? QColor(20, 20, 22) : QColor(Qt::white);
    btn->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; font-weight: bold; font-size: 10px; }"
                "QPushButton:hover { background: %4; border-color: %5; }"
                "QPushButton:checked { background: %6; color: %8; border-color: %7; }"
                "QPushButton:pressed { background: %4; }")
            .arg(theme.background.name(), theme.text.name(), theme.separator.name(),
                 theme.surfaceLight.name(), theme.border.name(),
                 activeColor.name(), activeColor.darker(130).name(), checkedText.name()));
}

} // namespace OpenDaw
