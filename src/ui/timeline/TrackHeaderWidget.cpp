#include "TrackHeaderWidget.h"
#include "ui/effects/PluginEditorWindow.h"
#include "utils/IconFont.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QSignalBlocker>
#include <QApplication>
#include <QToolTip>
#include <QPainter>
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

TrackHeaderWidget::TrackHeaderWidget(te::AudioTrack* track, EditManager* editMgr,
                                     QWidget* parent)
    : QWidget(parent), track_(track), editMgr_(editMgr)
{
    setAccessibleName("Track Header");
    setObjectName("trackHeaderWidget");
    auto& theme = ThemeManager::instance().current();

    setFixedWidth(metrics::kTrackHeaderWidth);
    trackColour_ = editMgr_->getTrackColour(track_);

    // Background, colour strip and separators are painted in paintEvent; the
    // row containers stay transparent so nothing here cascades into the buttons.
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(kLeftMargin, kVMargin, 4, kVMargin);
    outer->setSpacing(5);

    auto* column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(kRowSpacing);
    outer->addLayout(column, 1);

    auto makeRow = [this](int height) {
        auto* row = new QWidget(this);
        row->setAttribute(Qt::WA_TranslucentBackground);
        row->setStyleSheet("background: transparent;");
        row->setFixedHeight(height);
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(3);
        return std::make_pair(row, l);
    };

    const QString smallComboStyle =
        QString("QComboBox { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 3px; font-size: 10px; padding: 0px 4px; }"
                "QComboBox:hover { border-color: %4; }"
                "QComboBox::drop-down { width: 12px; border: none; }"
                "QComboBox QAbstractItemView { background: %5; color: %2; "
                "selection-background-color: %4; font-size: 10px; }")
            .arg(theme.background.name(), theme.text.name(), theme.border.name(),
                 theme.accent.name(), theme.surface.name());

    // ── Row 1: collapse, grip, name, type ──
    auto [nameRow, nameLayout] = makeRow(kNameRowHeight);
    nameRow_ = nameRow;

    collapseBtn_ = new QPushButton(nameRow);
    collapseBtn_->setAccessibleName("Collapse Track");
    collapseBtn_->setToolTip("Collapse track");
    collapseBtn_->setFixedSize(14, kNameRowHeight);
    collapseBtn_->setText(QString::fromUtf8("\xe2\x96\xbe"));
    collapseBtn_->setStyleSheet(
        QString("QPushButton { background: transparent; color: %1; border: none; "
                "font-size: 11px; padding: 0; }"
                "QPushButton:hover { color: %2; }")
            .arg(theme.textDim.name(), theme.text.name()));
    connect(collapseBtn_, &QPushButton::clicked, this, [this]() {
        setCollapsed(!collapsed_);
        emit collapseToggled(track_, collapsed_);
    });
    nameLayout->addWidget(collapseBtn_);

    gripHandle_ = new QWidget(nameRow);
    gripHandle_->setFixedSize(9, kNameRowHeight);
    gripHandle_->setCursor(Qt::OpenHandCursor);
    gripHandle_->setToolTip("Drag to reorder track");
    gripHandle_->setAccessibleName("Track Reorder Grip");
    gripHandle_->installEventFilter(this);
    nameLayout->addWidget(gripHandle_);

    nameLabel_ = new QLabel(QString::fromStdString(track_->getName().toStdString()), nameRow);
    nameLabel_->setAccessibleName("Track Name");
    nameLabel_->setToolTip(nameLabel_->text() + "\nDouble-click to rename");
    nameLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    nameLabel_->setMinimumWidth(20);
    nameLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameLabel_->installEventFilter(this);
    nameLayout->addWidget(nameLabel_, 1);

    const bool isMidi = editMgr_->isMidiTrack(track_);
    const bool isBus = editMgr_->isBusTrack(track_);
    typeLabel_ = new QLabel(isMidi ? "MIDI" : (isBus ? "BUS" : "AUDIO"), nameRow);
    typeLabel_->setAccessibleName("Track Type");
    typeLabel_->setAlignment(Qt::AlignCenter);
    typeLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 9px; font-weight: bold; "
                "border: 1px solid %2; border-radius: 3px; padding: 0px 4px; }")
            .arg(theme.textDim.name(), theme.border.name()));
    nameLayout->addWidget(typeLabel_);
    column->addWidget(nameRow);

    // ── Row 2: record, solo, mute | mono, freeze, automation ──
    auto [buttonRow, buttonLayout] = makeRow(kButtonRowHeight);
    buttonRow_ = buttonRow;

    auto makeToggle = [&](const QString& text, const QString& name, const QString& tip,
                          const QColor& activeColour, const QFont* font = nullptr) {
        auto* btn = new QPushButton(text, buttonRow);
        btn->setAccessibleName(name);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setFixedSize(24, kButtonRowHeight);
        if (font) btn->setFont(*font);
        applyToggleStyle(btn, activeColour);
        buttonLayout->addWidget(btn);
        return btn;
    };

    armBtn_ = makeToggle("R", "Record Arm", "Record enable", theme.recordArm);
    connect(armBtn_, &QPushButton::toggled, this, &TrackHeaderWidget::onArmToggled);

    soloBtn_ = makeToggle("S", "Solo", "Solo", theme.soloButton);
    connect(soloBtn_, &QPushButton::toggled, this, [this](bool s) {
        if (track_) track_->setSolo(s);
    });

    muteBtn_ = makeToggle("M", "Mute", "Mute", theme.muteButton);
    connect(muteBtn_, &QPushButton::toggled, this, [this](bool m) {
        if (track_) track_->setMute(m);
    });

    buttonLayout->addSpacing(6);

    const QFont audioIconFont = icons::fontAudio(12);
    monoBtn_ = makeToggle(QString(), "Mono Or Stereo", QString(), theme.accent, &audioIconFont);
    connect(monoBtn_, &QPushButton::toggled, this, [this](bool mono) {
        if (!track_ || !editMgr_) return;
        updateMonoButtonVisual(mono);
        // setTrackMono emits tracksChanged, which deletes and rebuilds this
        // header, so nothing may touch `this` after it.
        editMgr_->setTrackMono(*track_, mono);
    });

    const QFont materialFont = icons::materialIcons(13);
    freezeBtn_ = makeToggle(QString(icons::mi::AcUnit), "Freeze Track",
                            "Freeze track (render effects to audio)", QColor(70, 170, 220),
                            &materialFont);
    freezeBtn_->setCheckable(false);
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

    automationBtn_ = makeToggle(QString(icons::mi::ShowChart), "Toggle Automation Lane",
                                "Show automation lane (A)", theme.accent, &materialFont);
    connect(automationBtn_, &QPushButton::toggled, this, [this](bool checked) {
        automationVisible_ = checked;
        emit automationToggled(track_, checked);
    });
    buttonLayout->addStretch();
    column->addWidget(buttonRow);

    const bool mono = editMgr_ && editMgr_->isTrackMono(track_);
    {
        QSignalBlocker block(monoBtn_);
        monoBtn_->setChecked(mono);
    }
    updateMonoButtonVisual(mono);

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

    // ── Optional rows, shown as the track gets taller (in this order) ──
    if (isMidi) {
        auto [instrumentRow, instrumentLayout] = makeRow(kDetailRowHeight);
        auto* instrument = editMgr_->getTrackInstrument(track_);
        QString instrName = instrument
            ? QString::fromStdString(instrument->getName().toStdString())
            : "No Instrument";
        instrumentBtn_ = new QPushButton(instrName, instrumentRow);
        instrumentBtn_->setAccessibleName("Instrument");
        instrumentBtn_->setToolTip("Click to open the instrument, right-click to change it");
        instrumentBtn_->setFixedHeight(kDetailRowHeight);
        instrumentBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
        instrumentBtn_->setStyleSheet(
            QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 3px; font-size: 10px; padding: 0px 6px; text-align: left; }"
                    "QPushButton:hover { background: %4; border-color: %5; }")
                .arg(theme.background.name(), theme.text.name(), theme.border.name(),
                     theme.surfaceLight.name(), theme.accent.name()));

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

        instrumentLayout->addWidget(instrumentBtn_, 1);
        column->addWidget(instrumentRow);
        optionalRows_.push_back(instrumentRow);
    }

    auto [mixRow, mixLayout] = makeRow(kDetailRowHeight);
    const QString sliderStyle =
        QString("QSlider { background: transparent; }"
                "QSlider::groove:horizontal { background: %1; height: 4px; border-radius: 2px; }"
                "QSlider::handle:horizontal { background: %2; width: 9px; margin: -5px 0; "
                "border-radius: 3px; border: 1px solid %1; }"
                "QSlider::handle:horizontal:hover { background: %4; }"
                "QSlider::sub-page:horizontal { background: %3; border-radius: 2px; }")
            .arg(theme.controlTrack.name(), theme.controlHandle.name(),
                 "%SUBPAGE%", theme.controlHandle.lighter(115).name());

    volumeSlider_ = new QSlider(Qt::Horizontal, mixRow);
    volumeSlider_->setAccessibleName("Volume");
    volumeSlider_->setToolTip("Volume");
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(91);
    volumeSlider_->setStyleSheet(QString(sliderStyle).replace("%SUBPAGE%", theme.accent.name()));
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
    mixLayout->addWidget(volumeSlider_, 1);
    mixLayout->addSpacing(4);

    // Pan: a short slider with no fill, so the centre reads as "nothing set".
    panKnob_ = new QSlider(Qt::Horizontal, mixRow);
    panKnob_->setAccessibleName("Pan");
    panKnob_->setToolTip("Pan (double-click to centre)");
    panKnob_->setRange(-100, 100);
    panKnob_->setValue(0);
    panKnob_->setFixedWidth(46);
    panKnob_->setStyleSheet(QString(sliderStyle).replace("%SUBPAGE%", theme.controlTrack.name()));
    panKnob_->installEventFilter(this);
    connect(panKnob_, &QSlider::valueChanged, this, [this](int v) {
        if (!track_) return;
        panKnob_->setToolTip(v == 0 ? QString("Pan: centre")
                                    : QString("Pan: %1 %2").arg(std::abs(v)).arg(v < 0 ? "L" : "R"));
        for (auto* plugin : track_->pluginList.getPlugins()) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
                volPan->panParam->setParameter(float(v / 100.0), juce::sendNotification);
                break;
            }
        }
    });
    mixLayout->addWidget(panKnob_);
    column->addWidget(mixRow);
    optionalRows_.push_back(mixRow);

    auto [ioRow, ioLayout] = makeRow(kDetailRowHeight);
    inputCombo_ = new QComboBox(ioRow);
    inputCombo_->setAccessibleName("Input Source");
    inputCombo_->setToolTip("Input");
    inputCombo_->setFixedHeight(kDetailRowHeight);
    inputCombo_->setMinimumWidth(10);
    inputCombo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    inputCombo_->setStyleSheet(smallComboStyle);
    populateInputCombo();
    connect(inputCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackHeaderWidget::onInputComboChanged);
    ioLayout->addWidget(inputCombo_, 1);

    outputCombo_ = new QComboBox(ioRow);
    outputCombo_->setAccessibleName("Output Destination");
    outputCombo_->setToolTip("Output");
    outputCombo_->setFixedHeight(kDetailRowHeight);
    outputCombo_->setMinimumWidth(10);
    outputCombo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    outputCombo_->setStyleSheet(smallComboStyle);
    populateOutputCombo();
    connect(outputCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackHeaderWidget::onOutputComboChanged);
    ioLayout->addWidget(outputCombo_, 1);
    column->addWidget(ioRow);
    optionalRows_.push_back(ioRow);

    column->addStretch();

    // Full-height meter on the right edge.
    meter_ = new LevelMeter(this);
    meter_->setAccessibleName("Track Level Meter");
    meter_->setFixedWidth(7);
    meter_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    outer->addWidget(meter_);

    refreshNameStyle();
    if (editMgr_ && track_)
        updateFreezeButtonVisual(editMgr_->isTrackFrozen(track_));
    updateRowVisibility();

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
    nameLabel_->setToolTip(nameLabel_->text() + "\nDouble-click to rename");

    const QColor colour = editMgr_ ? editMgr_->getTrackColour(track_) : trackColour_;
    if (colour != trackColour_) {
        trackColour_ = colour;
        update();
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
    muteBtn_->setToolTip(soloOverridesMute ? "Mute overridden by Solo" : "Mute");

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
                volumeSlider_->setValue(static_cast<int>(std::clamp(norm, 0.0, 1.0) * 100.0));
            }
            if (panKnob_) {
                QSignalBlocker block(panKnob_);
                panKnob_->setValue(int(std::lround(vp->pan.get() * 100.0)));
            }
            break;
        }
    }

    refreshNameStyle();
}

void TrackHeaderWidget::refreshNameStyle()
{
    auto& theme = ThemeManager::instance().current();

    // Dim tracks that are silenced by another track's solo.
    bool silencedBySolo = false;
    if (editMgr_ && editMgr_->edit() && track_ && !track_->isSolo(false)) {
        for (auto* t : te::getAudioTracks(*editMgr_->edit()))
            if (t->isSolo(false)) { silencedBySolo = true; break; }
    }
    const bool frozen = editMgr_ && track_ && editMgr_->isTrackFrozen(track_);

    QColor colour = theme.text;
    if (frozen)
        colour = QColor(120, 190, 230);
    else if (silencedBySolo || (track_ && track_->isMuted(false)))
        colour = theme.textDim;

    nameLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 11px; font-weight: bold; background: transparent; }")
            .arg(colour.name()));
}

void TrackHeaderWidget::setAutomationVisible(bool visible)
{
    if (!automationBtn_) return;
    QSignalBlocker block(automationBtn_);
    automationBtn_->setChecked(visible);
    automationVisible_ = visible;
}

void TrackHeaderWidget::setCollapsed(bool collapsed)
{
    if (collapsed_ == collapsed) return;
    collapsed_ = collapsed;

    if (collapseBtn_) {
        collapseBtn_->setText(collapsed
            ? QString::fromUtf8("\xe2\x96\xb8")
            : QString::fromUtf8("\xe2\x96\xbe"));
        collapseBtn_->setToolTip(collapsed ? "Expand track" : "Collapse track");
    }
    updateRowVisibility();
}

void TrackHeaderWidget::setIndented(bool indented)
{
    indented_ = indented;
    if (auto* l = layout())
        l->setContentsMargins(kLeftMargin + (indented ? 10 : 0), kVMargin, 4, kVMargin);
    update();
}

void TrackHeaderWidget::setTrackHeight(int h)
{
    setFixedHeight(std::max(h, minimumSizeHint().height()));
    updateRowVisibility();
}

void TrackHeaderWidget::updateRowVisibility()
{
    if (buttonRow_)
        buttonRow_->setVisible(!collapsed_);

    // Reveal the optional rows in order while they fit.
    int available = height() - 2 * kVMargin - kNameRowHeight
                    - (collapsed_ ? 0 : kRowSpacing + kButtonRowHeight);
    for (auto* row : optionalRows_) {
        const bool fits = !collapsed_ && available >= kRowSpacing + kDetailRowHeight;
        row->setVisible(fits);
        if (fits)
            available -= kRowSpacing + kDetailRowHeight;
    }
}

void TrackHeaderWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateRowVisibility();
}

QSize TrackHeaderWidget::minimumSizeHint() const
{
    int h = 2 * kVMargin + kNameRowHeight;
    if (!collapsed_)
        h += kRowSpacing + kButtonRowHeight;
    return QSize(metrics::kTrackHeaderWidth, h);
}

void TrackHeaderWidget::setSelected(bool sel)
{
    if (selected_ == sel) return;
    selected_ = sel;
    updateSelectionStyle();
}

void TrackHeaderWidget::updateSelectionStyle()
{
    update();
}

void TrackHeaderWidget::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter p(this);

    const bool frozen = editMgr_ && track_ && editMgr_->isTrackFrozen(track_);
    QColor bg = selected_ ? theme.surfaceLight : theme.surface;
    if (frozen)
        bg = QColor(bg.red() - 4, bg.green() + 2, bg.blue() + 8);
    p.fillRect(rect(), bg);

    // Colour strip (indented tracks sit inside a folder, so shift it in).
    const int stripX = indented_ ? 10 : 0;
    QColor strip = trackColour_;
    if (track_ && track_->isMuted(false))
        strip = strip.darker(170);
    p.fillRect(QRect(stripX, 0, metrics::kTrackColourStrip, height()), strip);

    if (selected_) {
        p.fillRect(QRect(stripX + metrics::kTrackColourStrip, 0, 2, height()),
                   theme.accent);
    }

    p.fillRect(QRect(0, height() - 1, width(), 1), theme.separator);
    p.fillRect(QRect(width() - 1, 0, 1, height()), theme.separator);
}

void TrackHeaderWidget::mousePressEvent(QMouseEvent* event)
{
    emit trackSelected(track_);
    QWidget::mousePressEvent(event);
}

void TrackHeaderWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!track_ || !editMgr_) return;

    QMenu menu(this);
    menu.setAccessibleName("Track Context Menu");

    menu.addAction("Rename...", this, [this]() { startRenameEdit(); });
    auto* colourMenu = menu.addMenu("Track Colour");
    colourMenu->setAccessibleName("Track Colour");
    const auto& palette = ThemeManager::trackPalette();
    for (int slot = 0; slot < int(palette.size()); ++slot) {
        QPixmap swatch(14, 14);
        swatch.fill(palette[size_t(slot)]);
        auto* mgr = editMgr_;
        const auto trackId = track_->itemID;
        // Queued: the colour change rebuilds the headers, including this one.
        colourMenu->addAction(QIcon(swatch), QString(), mgr, [mgr, trackId, slot]() {
            QTimer::singleShot(0, mgr, [mgr, trackId, slot]() {
                for (auto* t : mgr->getAudioTracks())
                    if (t->itemID == trackId) { mgr->setTrackColourSlot(*t, slot); break; }
            });
        });
    }
    menu.addSeparator();

    auto* split = menu.addAction("Split Piano Hands (AI)...", this, [this]() {
        emit splitHandsRequested(track_);
    });
    split->setToolTip("Use AI to split a piano recording into right-hand and left-hand tracks");
    split->setEnabled(editMgr_->isMidiTrack(track_) && !editMgr_->isTrackFrozen(track_));
    menu.exec(event->globalPos());
}

void TrackHeaderWidget::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
}

void TrackHeaderWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}

bool TrackHeaderWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == panKnob_ && event->type() == QEvent::MouseButtonDblClick) {
        panKnob_->setValue(0);
        return true;
    }

    if (obj == nameLabel_ && event->type() == QEvent::MouseButtonDblClick && track_) {
        startRenameEdit();
        return true;
    }

    if (obj == gripHandle_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragStartPos_ = me->globalPosition().toPoint();
                draggingToReorder_ = false;
                gripHandle_->setCursor(Qt::ClosedHandCursor);
            }
            return true;
        }
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->buttons() & Qt::LeftButton) {
                if (!draggingToReorder_) {
                    int dist = (me->globalPosition().toPoint() - dragStartPos_).manhattanLength();
                    if (dist < 6) return true;
                    draggingToReorder_ = true;
                    emit dragStarted(this);
                }
                emit dragMoved(this, me->globalPosition().toPoint().y());
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            gripHandle_->setCursor(Qt::OpenHandCursor);
            if (draggingToReorder_) {
                draggingToReorder_ = false;
                emit dragFinished(this);
            }
            return true;
        }
        if (event->type() == QEvent::Paint) {
            QPainter p(gripHandle_);
            p.setRenderHint(QPainter::Antialiasing);
            QColor dotColor = ThemeManager::instance().current().textDim;
            int dotR = 1;
            int cols = 2, rows = 3;
            int spacingX = 4, spacingY = 5;
            int totalW = (cols - 1) * spacingX + dotR * 2;
            int totalH = (rows - 1) * spacingY + dotR * 2;
            int startX = (gripHandle_->width() - totalW) / 2;
            int startY = (gripHandle_->height() - totalH) / 2;
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(dotColor);
                    p.drawEllipse(QPointF(startX + c * spacingX + dotR,
                                          startY + r * spacingY + dotR), dotR, dotR);
                }
            }
            return true;
        }
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
    edit->setGeometry(QRect(nameLabel_->mapTo(this, QPoint(0, 0)), nameLabel_->size()));
    edit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto& theme = ThemeManager::instance().current();
    edit->setStyleSheet(
        QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                "font-size: 11px; font-weight: bold; padding: 0px 3px; border-radius: 3px; }")
            .arg(theme.background.name(), theme.text.name(), theme.accent.name()));
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

    bool isPlaying = editMgr_->transport().isPlaying();
    double currentSecs = editMgr_->transport().getPosition().inSeconds();
    bool playheadMoved = std::abs(currentSecs - lastTrackedPlayheadSecs_) > 0.001;
    bool shouldTrack = isPlaying || playheadMoved;
    lastTrackedPlayheadSecs_ = currentSecs;

    if (!shouldTrack) return;

    auto mode = track_->automationMode.get();
    bool isWriting = isPlaying && (mode == te::AutomationMode::write);

    for (auto* plugin : track_->pluginList.getPlugins()) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) {
            if (volumeSlider_ && !isWriting) {
                float dbVal = te::volumeFaderPositionToDB(vp->volParam->getCurrentValue());
                double norm = (dbVal + 60.0) / 66.0;
                QSignalBlocker block(volumeSlider_);
                volumeSlider_->setValue(static_cast<int>(std::clamp(norm, 0.0, 1.0) * 100.0));
            }
            if (panKnob_ && !isWriting) {
                QSignalBlocker block(panKnob_);
                panKnob_->setValue(int(std::lround(vp->panParam->getCurrentValue() * 100.0)));
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
                "border-radius: 3px; font-weight: bold; font-size: 10px; padding: 0px; }"
                "QPushButton:hover { background: %5; color: %7; }"
                "QPushButton:checked { background: %4; color: #111; border-color: %6; }"
                "QPushButton:disabled { color: %3; }")
            .arg(theme.background.name(), theme.textDim.name(),
                 theme.border.name(), activeColor.name(),
                 theme.surfaceLight.name(),
                 activeColor.darker(130).name(), theme.text.name()));
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

    const QColor freezeColor(70, 170, 220);
    if (frozen) {
        freezeBtn_->setToolTip("Unfreeze track (restore live processing)");
        freezeBtn_->setStyleSheet(
            QString("QPushButton { background: %1; color: #111; border: 1px solid %1; "
                    "border-radius: 3px; padding: 0px; }"
                    "QPushButton:hover { background: %2; }")
                .arg(freezeColor.name(), freezeColor.lighter(120).name()));
    } else {
        freezeBtn_->setToolTip("Freeze track (render effects to audio)");
        applyToggleStyle(freezeBtn_, freezeColor);
    }
    refreshNameStyle();
    update();
}

void TrackHeaderWidget::populateInputCombo()
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
    } else if (deviceName.startsWith("midi:")) {
        editMgr_->assignMidiInputToTrack(*track_,
            juce::String(deviceName.mid(5).toStdString()));
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
        QToolTip::showText(armBtn_->mapToGlobal(QPoint(0, -30)),
                           "Select an input source first",
                           armBtn_, {}, 2500);
        return;
    }

    editMgr_->setTrackRecordEnabled(*track_, armed);
}

} // namespace OpenDaw
