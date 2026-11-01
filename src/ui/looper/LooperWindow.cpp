#include "LooperWindow.h"
#include "engine/EditManager.h"
#include "utils/ThemeManager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QDateTime>
#include <cmath>

namespace OpenDaw {

namespace {
QColor mix(const QColor& a, const QColor& b, double t)
{
    return QColor(int(a.red() * t + b.red() * (1 - t)),
                  int(a.green() * t + b.green() * (1 - t)),
                  int(a.blue() * t + b.blue() * (1 - t)));
}

const QColor kOverdubColour(240, 150, 40);

bool blinkOn()
{
    return (QDateTime::currentMSecsSinceEpoch() / 250) % 2 == 0;
}
}

// ── Pad ──────────────────────────────────────────────────────────────────────

LooperPadWidget::LooperPadWidget(LooperEngine& engine, int index, QWidget* parent)
    : QWidget(parent), engine_(engine), index_(index)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(QString("Loop pad %1").arg(index + 1));
    setToolTip("Click: record, close the loop, stop or play\n"
               "Shift+click: overdub\nRight-click: undo, clear, one-shot, group");
}

void LooperPadWidget::paintEvent(QPaintEvent*)
{
    const auto& theme = ThemeManager::instance().current();
    const auto v = engine_.padView(index_);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double side = std::min(width(), height()) - 10.0;
    const QRectF circle((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    const double ringW = std::max(4.0, side * 0.07);
    const QRectF ring = circle.adjusted(ringW / 2, ringW / 2, -ringW / 2, -ringW / 2);

    const QColor rec = theme.transportRecord;
    const QColor padColour = v.colour.isValid() ? v.colour : theme.accent;
    const bool hasLoop = v.state != PadState::Empty;
    const bool recording = v.state == PadState::Recording;
    const bool sounding = v.state == PadState::Playing || v.state == PadState::Overdubbing;

    // Body.
    QColor body = theme.surfaceLight;
    if (recording) body = mix(rec, theme.surface, 0.55);
    else if (sounding) body = mix(padColour, theme.surface, 0.42);
    else if (hasLoop) body = mix(padColour, theme.surface, 0.20);
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawEllipse(circle.adjusted(ringW, ringW, -ringW, -ringW));

    // Ring: the whole loop dimly, the way through it brightly.
    QColor track = hasLoop ? mix(padColour, theme.background, 0.35) : theme.border;
    p.setBrush(Qt::NoBrush);
    QPen trackPen(track, ringW);
    if (!hasLoop) trackPen.setStyle(Qt::DashLine);
    p.setPen(trackPen);
    p.drawEllipse(ring);

    QColor arc;
    if (recording) arc = rec;
    else if (v.state == PadState::Overdubbing) arc = kOverdubColour;
    else if (sounding) arc = padColour.lighter(130);
    if (arc.isValid()) {
        const double phase = recording && v.phase <= 0.0 ? 1.0 : v.phase;
        p.setPen(QPen(arc, ringW, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(ring, 90 * 16, -int(phase * 360.0 * 16.0));
    }

    // Waiting for the beat: the outline blinks in the colour of what's coming.
    if (v.queued != LooperEngine::Queued::None && blinkOn()) {
        QColor q = v.queued == LooperEngine::Queued::Play ? padColour.lighter(140)
                 : v.queued == LooperEngine::Queued::Stop ? theme.textDim
                 : rec;
        p.setPen(QPen(q, 3.0));
        p.drawEllipse(circle.adjusted(1.5, 1.5, -1.5, -1.5));
    }

    // Text.
    const QColor text = theme.isLight && !recording && !sounding ? theme.text : QColor(245, 246, 248);
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(int(std::clamp(side * 0.26, 14.0, 40.0)));
    p.setFont(f);
    p.setPen(hasLoop || recording ? text : theme.textDim);
    QRectF numRect = circle.adjusted(0, -side * 0.12, 0, -side * 0.12);
    p.drawText(numRect, Qt::AlignCenter, QString::number(index_ + 1));

    QFont small = font();
    small.setPixelSize(int(std::clamp(side * 0.095, 9.0, 14.0)));
    p.setFont(small);
    QString line;
    if (recording) line = v.queued == LooperEngine::Queued::Record ? "Waiting…" : "Recording";
    else if (v.state == PadState::Overdubbing) line = "Overdub";
    else if (hasLoop) {
        line = v.bars > 0.0 ? LooperEngine::lengthText(v.bars, v.beats) : QString();
        if (v.undoDepth > 1) line += QString("  ·  %1 layers").arg(v.undoDepth);
    } else {
        line = "Tap to record";
    }
    p.setPen(hasLoop || recording ? text : theme.textDim);
    QRectF lineRect(circle.left(), circle.center().y() + side * 0.10, circle.width(), side * 0.14);
    p.drawText(lineRect, Qt::AlignCenter, line);

    // Badges: one-shot and group.
    QStringList badges;
    if (v.oneShot) badges << "1×";
    if (v.group > 0) badges << QString(QChar('A' + v.group - 1));
    if (!badges.isEmpty()) {
        QRectF badgeRect(circle.left(), circle.center().y() + side * 0.23, circle.width(), side * 0.13);
        p.setPen(theme.textDim);
        p.drawText(badgeRect, Qt::AlignCenter, badges.join("  "));
    }
}

void LooperPadWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) engine_.overdubPad(index_);
    else engine_.tapPad(index_);
    update();
}

void LooperPadWidget::contextMenuEvent(QContextMenuEvent* event)
{
    const auto v = engine_.padView(index_);
    QMenu menu(this);
    const bool hasLoop = v.state != PadState::Empty;
    const bool sounding = v.state == PadState::Playing || v.state == PadState::Overdubbing;

    QString tapText = !hasLoop ? "Record"
                    : v.state == PadState::Recording ? "Close the Loop"
                    : sounding ? "Stop" : "Play";
    menu.addAction(tapText, [this]() { engine_.tapPad(index_); });
    auto* od = menu.addAction(v.overdubbing ? "End Overdub" : "Overdub",
                              [this]() { engine_.overdubPad(index_); });
    od->setEnabled(hasLoop && v.state != PadState::Recording && !v.oneShot);
    menu.addSeparator();
    auto* undo = menu.addAction("Undo Layer", [this]() { engine_.undoPad(index_); });
    undo->setEnabled(v.undoDepth > 0 || v.overdubbing);
    auto* redo = menu.addAction("Redo Layer", [this]() { engine_.redoPad(index_); });
    redo->setEnabled(v.redoDepth > 0);
    auto* clear = menu.addAction("Clear Pad", [this]() { engine_.clearPad(index_); });
    clear->setEnabled(hasLoop);
    menu.addSeparator();
    auto* oneShot = menu.addAction("Play Once (One-Shot)", [this](bool on) {
        engine_.setPadOneShot(index_, on);
    });
    oneShot->setCheckable(true);
    oneShot->setChecked(v.oneShot);
    auto* groupMenu = menu.addMenu("Group");
    const QStringList groups { "None", "A", "B", "C", "D" };
    for (int g = 0; g < groups.size(); ++g) {
        auto* a = groupMenu->addAction(groups[g], [this, g]() { engine_.setPadGroup(index_, g); });
        a->setCheckable(true);
        a->setChecked(v.group == g);
    }
    groupMenu->setToolTipsVisible(true);
    groupMenu->setToolTip("Pads in the same group play one at a time");
    menu.exec(event->globalPos());
}

// ── Input meter ──────────────────────────────────────────────────────────────

LooperInputMeter::LooperInputMeter(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(10);
    setMinimumWidth(70);
    setToolTip("Input level");
}

void LooperInputMeter::setLevel(float peak)
{
    // Falls back slowly so short peaks stay visible.
    level_ = std::max(peak, level_ * 0.85f);
    update();
}

void LooperInputMeter::paintEvent(QPaintEvent*)
{
    const auto& theme = ThemeManager::instance().current();
    QPainter p(this);
    p.fillRect(rect(), theme.controlTrack);
    const double db = level_ > 1.0e-6f ? 20.0 * std::log10(level_) : -100.0;
    const double frac = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
    const QColor c = db > -3.0 ? theme.meterRed : db > -12.0 ? theme.meterYellow : theme.meterGreen;
    p.fillRect(QRectF(0, 0, width() * frac, height()), c);
}

// ── Window ───────────────────────────────────────────────────────────────────

LooperWindow::LooperWindow(EditManager& editMgr, QWidget* parent)
    : QWidget(parent, Qt::Tool), editMgr_(editMgr), engine_(editMgr.looper())
{
    setWindowTitle("Looper");
    setAccessibleName("Looper");
    setFocusPolicy(Qt::StrongFocus);
    resize(600, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // Input.
    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(new QLabel("Input"));
    inputCombo_ = new QComboBox();
    inputCombo_->setAccessibleName("Looper Input");
    inputCombo_->setMinimumWidth(150);
    inputRow->addWidget(inputCombo_);
    meter_ = new LooperInputMeter();
    inputRow->addWidget(meter_, 1);
    monitorCheck_ = new QCheckBox("Hear input");
    monitorCheck_->setToolTip("Play the input through the speakers while you loop");
    inputRow->addWidget(monitorCheck_);
    root->addLayout(inputRow);

    // Timing.
    auto* timingRow = new QHBoxLayout();
    timingRow->addWidget(new QLabel("Start/stop on"));
    quantizeCombo_ = new QComboBox();
    quantizeCombo_->setAccessibleName("Looper Quantize");
    quantizeCombo_->addItems({ "Beat", "Bar", "Loop" });
    quantizeCombo_->setToolTip("Where taps land: the next beat, bar, or start of the first loop");
    timingRow->addWidget(quantizeCombo_);
    timingRow->addSpacing(8);
    timingRow->addWidget(new QLabel("Record"));
    lengthCombo_ = new QComboBox();
    lengthCombo_->setAccessibleName("Looper Record Length");
    lengthCombo_->addItem("Until you tap", 0);
    for (int bars : { 1, 2, 4, 8, 16 })
        lengthCombo_->addItem(QString("%1 bar%2").arg(bars).arg(bars == 1 ? "" : "s"), bars);
    timingRow->addWidget(lengthCombo_);
    timingRow->addStretch(1);
    root->addLayout(timingRow);

    auto* optionsRow = new QHBoxLayout();
    tempoCheck_ = new QCheckBox("First loop sets the tempo");
    tempoCheck_->setToolTip("Record the first loop freely; its length sets the tempo.\n"
                            "Off: loops follow the project tempo.");
    countInCheck_ = new QCheckBox("Count-in");
    countInCheck_->setToolTip("A bar of click before recording starts from stopped");
    clickCheck_ = new QCheckBox("Click");
    optionsRow->addWidget(tempoCheck_);
    optionsRow->addWidget(countInCheck_);
    optionsRow->addWidget(clickCheck_);
    optionsRow->addStretch(1);
    root->addLayout(optionsRow);

    status_ = new QLabel();
    status_->setAccessibleName("Looper Status");
    QFont sf = status_->font();
    sf.setBold(true);
    status_->setFont(sf);
    root->addWidget(status_);

    grid_ = new QGridLayout();
    grid_->setSpacing(10);
    root->addLayout(grid_, 1);

    // Transport and whole-looper actions.
    auto* buttons = new QHBoxLayout();
    playBtn_ = new QPushButton("Play");
    playBtn_->setToolTip("Start, bringing back the loops that were playing (Space)");
    stopBtn_ = new QPushButton("Stop");
    stopBtn_->setToolTip("Stop, and put the performance on the timeline (Space)");
    auto* stopLoops = new QPushButton("Stop Loops");
    stopLoops->setToolTip("Stop every loop on the next beat (Esc)");
    auto* undo = new QPushButton("Undo");
    undo->setToolTip("Undo the last layer on the last pad you used (Ctrl+Z)");
    auto* redo = new QPushButton("Redo");
    redo->setToolTip("Redo it (Ctrl+Y)");
    auto* clearAll = new QPushButton("Clear All");
    auto* morePads = new QPushButton("More Pads");
    for (auto* b : { playBtn_, stopBtn_, stopLoops, undo, redo, clearAll, morePads }) {
        b->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(b);
    }
    root->addLayout(buttons);

    auto* hint = new QLabel("Click a pad to record, click again to close the loop, then to stop and play it. "
                            "Shift+click overdubs. Keys 1–9 and 0 tap pads (Shift: overdub). "
                            "When you stop, the performance goes onto the pads' tracks.");
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    root->addWidget(hint);

    connect(inputCombo_, &QComboBox::activated, this, [this](int i) {
        engine_.setInputDevice(inputCombo_->itemData(i).toString());
    });
    connect(monitorCheck_, &QCheckBox::toggled, this, [this](bool on) { engine_.setMonitor(on); });
    connect(quantizeCombo_, &QComboBox::activated, this, [this](int i) {
        engine_.setQuantize(LooperEngine::Quantize(i));
    });
    connect(lengthCombo_, &QComboBox::activated, this, [this](int i) {
        engine_.setRecordBars(lengthCombo_->itemData(i).toInt());
    });
    connect(tempoCheck_, &QCheckBox::toggled, this, [this](bool on) { engine_.setFirstLoopSetsTempo(on); });
    connect(countInCheck_, &QCheckBox::toggled, this, [this](bool on) { engine_.setCountIn(on); });
    connect(clickCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (on != engine_.click()) engine_.setClick(on);
    });
    connect(playBtn_, &QPushButton::clicked, this, [this]() { engine_.play(); });
    connect(stopBtn_, &QPushButton::clicked, this, [this]() { engine_.stop(); });
    connect(stopLoops, &QPushButton::clicked, this, [this]() { engine_.stopAllPads(); });
    connect(undo, &QPushButton::clicked, this, [this]() { engine_.undoPad(engine_.lastPad()); });
    connect(redo, &QPushButton::clicked, this, [this]() { engine_.redoPad(engine_.lastPad()); });
    connect(clearAll, &QPushButton::clicked, this, [this]() { engine_.clearAll(); });
    connect(morePads, &QPushButton::clicked, this, [this]() {
        engine_.setPadCount(engine_.padCount() >= LooperEngine::kMaxPads ? 8 : engine_.padCount() + 4);
    });
    connect(&engine_, &LooperEngine::changed, this, [this]() {
        if (int(pads_.size()) != engine_.padCount()) rebuildPads();
        syncControls();
    });
    connect(&editMgr_, &EditManager::editChanged, this, &LooperWindow::refreshInputs);

    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &LooperWindow::tick);

    refreshInputs();
    syncControls();
    rebuildPads();

    QSettings s;
    if (s.contains("looper/geometry")) restoreGeometry(s.value("looper/geometry").toByteArray());
}

void LooperWindow::rebuildPads()
{
    for (auto* p : pads_) {
        grid_->removeWidget(p);
        p->deleteLater();
    }
    pads_.clear();
    const int count = engine_.padCount();
    for (int i = 0; i < count; ++i) {
        auto* pad = new LooperPadWidget(engine_, i, this);
        grid_->addWidget(pad, i / 4, i % 4);
        pads_.push_back(pad);
    }
}

void LooperWindow::refreshInputs()
{
    QSignalBlocker block(inputCombo_);
    inputCombo_->clear();
    for (const auto& src : editMgr_.getAvailableInputSources())
        inputCombo_->addItem(src.displayName, QString::fromStdString(src.deviceName.toStdString()));
    const int idx = inputCombo_->findData(engine_.inputDevice());
    if (idx >= 0) inputCombo_->setCurrentIndex(idx);
}

void LooperWindow::syncControls()
{
    QSignalBlocker b1(monitorCheck_), b2(quantizeCombo_), b3(lengthCombo_),
                   b4(tempoCheck_), b5(countInCheck_), b6(clickCheck_), b7(inputCombo_);
    monitorCheck_->setChecked(engine_.monitor());
    quantizeCombo_->setCurrentIndex(int(engine_.quantize()));
    const int li = lengthCombo_->findData(engine_.recordBars());
    lengthCombo_->setCurrentIndex(li >= 0 ? li : 0);
    tempoCheck_->setChecked(engine_.firstLoopSetsTempo());
    tempoCheck_->setEnabled(!engine_.hasMasterLoop());
    countInCheck_->setChecked(engine_.countIn());
    clickCheck_->setChecked(engine_.click());
    const int ii = inputCombo_->findData(engine_.inputDevice());
    if (ii >= 0) inputCombo_->setCurrentIndex(ii);
}

void LooperWindow::tick()
{
    for (auto* p : pads_) p->update();
    meter_->setLevel(engine_.takeInputPeak());
    status_->setText(engine_.statusText());
    const bool running = engine_.isRunning();
    playBtn_->setEnabled(!running);
    stopBtn_->setEnabled(running);
    if (clickCheck_->isChecked() != engine_.click()) {
        QSignalBlocker b(clickCheck_);
        clickCheck_->setChecked(engine_.click());
    }
    if (tempoCheck_->isEnabled() == engine_.hasMasterLoop()) {
        tempoCheck_->setEnabled(!engine_.hasMasterLoop());
    }
}

void LooperWindow::keyPressEvent(QKeyEvent* event)
{
    // Digit keys by their physical key, so Shift+1 is still "1".
    int digit = -1;
    const auto vk = event->nativeVirtualKey();
    if (vk >= 0x30 && vk <= 0x39) digit = int(vk - 0x30);
    else if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) digit = event->key() - Qt::Key_0;

    const auto mods = event->modifiers();
    if (digit >= 0 && !(mods & (Qt::ControlModifier | Qt::AltModifier))) {
        const int pad = digit == 0 ? 9 : digit - 1;
        if (pad < engine_.padCount()) {
            if (mods & Qt::ShiftModifier) engine_.overdubPad(pad);
            else engine_.tapPad(pad);
        }
        return;
    }
    if (event->key() == Qt::Key_Space) {
        if (engine_.isRunning()) engine_.stop();
        else engine_.play();
        return;
    }
    if (event->key() == Qt::Key_Escape) { engine_.stopAllPads(); return; }
    if (event->matches(QKeySequence::Undo)) { engine_.undoPad(engine_.lastPad()); return; }
    if (event->matches(QKeySequence::Redo)) { engine_.redoPad(engine_.lastPad()); return; }
    QWidget::keyPressEvent(event);
}

void LooperWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshInputs();
    syncControls();
    timer_.start();
    emit visibilityChanged(true);
}

void LooperWindow::closeEvent(QCloseEvent* event)
{
    QSettings().setValue("looper/geometry", saveGeometry());
    timer_.stop();
    QWidget::closeEvent(event);
    emit visibilityChanged(false);
}

} // namespace OpenDaw
