#include "PianoRollEditor.h"
#include "ChannelColors.h"
#include "engine/EditManager.h"
#include "utils/IconFont.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QPushButton>
#include <QCheckBox>
#include <QFrame>
#include <QLineEdit>
#include <QInputDialog>
#include <QEvent>
#include <QSignalBlocker>
#include <algorithm>

namespace freedaw {

QPushButton* PianoRollEditor::makeIconButton(QWidget* parent, const QFont& font,
                                              const QChar& glyph, const QString& tooltip,
                                              const QString& accessible, int size)
{
    auto& theme = ThemeManager::instance().current();
    auto* btn = new QPushButton(parent);
    btn->setAccessibleName(accessible);
    btn->setFont(font);
    btn->setText(QString(glyph));
    btn->setToolTip(tooltip);
    btn->setFixedSize(size, size);
    btn->setStyleSheet(QString(
        "QPushButton { min-width: %1px; min-height: %1px; font-size: 14px; }"
        "QPushButton:hover { background: %2; }")
        .arg(size).arg(theme.surfaceLight.name()));
    return btn;
}

PianoRollEditor::PianoRollEditor(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Piano Roll Editor");
    auto& theme = ThemeManager::instance().current();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(40);
    toolbar->setAutoFillBackground(true);
    QPalette tbPal;
    tbPal.setColor(QPalette::Window, theme.surface);
    toolbar->setPalette(tbPal);

    auto* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(6, 4, 6, 4);
    tbLayout->setSpacing(4);

    clipNameLabel_ = new QLabel("No clip selected", toolbar);
    clipNameLabel_->setAccessibleName("Clip Name");
    clipNameLabel_->setStyleSheet(
        QString("color: %1; font-weight: bold; font-size: 11px;")
            .arg(theme.text.name()));
    tbLayout->addWidget(clipNameLabel_);

    tbLayout->addStretch();

    const int iconSize = 16;
    const int btnSize = 26;
    const auto faFont = icons::fontAudio(iconSize);

    const QString modeButtonStyle = QString(
        "QPushButton { min-width: 26px; min-height: 26px; font-size: 14px; }"
        "QPushButton:checked { background: %1; border-color: %2; }"
        "QPushButton:hover { background: %3; }")
        .arg(theme.surfaceLight.name(), theme.accent.name(), theme.surfaceLight.name());

    editModeBtn_ = new QPushButton(toolbar);
    editModeBtn_->setAccessibleName("Edit Mode");
    editModeBtn_->setCheckable(true);
    editModeBtn_->setChecked(true);
    editModeBtn_->setFont(faFont);
    editModeBtn_->setText(QString(icons::fa::Pointer));
    editModeBtn_->setToolTip("Edit mode (select & move)");
    editModeBtn_->setFixedSize(btnSize, btnSize);
    editModeBtn_->setStyleSheet(modeButtonStyle);
    tbLayout->addWidget(editModeBtn_);

    drawModeBtn_ = new QPushButton(toolbar);
    drawModeBtn_->setAccessibleName("Draw Mode");
    drawModeBtn_->setCheckable(true);
    drawModeBtn_->setChecked(false);
    drawModeBtn_->setFont(faFont);
    drawModeBtn_->setText(QString(icons::fa::Pen));
    drawModeBtn_->setToolTip("Draw mode (pencil)");
    drawModeBtn_->setFixedSize(btnSize, btnSize);
    drawModeBtn_->setStyleSheet(modeButtonStyle);
    tbLayout->addWidget(drawModeBtn_);

    connect(drawModeBtn_, &QPushButton::toggled,
            this, &PianoRollEditor::onDrawModeToggled);
    connect(editModeBtn_, &QPushButton::toggled,
            this, &PianoRollEditor::onEditModeToggled);

    // Separator
    auto* sep1 = new QFrame(toolbar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet(QString("color: %1;").arg(theme.surfaceLight.name()));
    tbLayout->addWidget(sep1);

    // Clipboard icon buttons
    auto* cutBtn = makeIconButton(toolbar, faFont, icons::fa::Scissors,
                                   "Cut (Ctrl+X)", "Cut", btnSize);
    tbLayout->addWidget(cutBtn);
    auto* copyBtn = makeIconButton(toolbar, faFont, icons::fa::Copy,
                                    "Copy (Ctrl+C)", "Copy", btnSize);
    tbLayout->addWidget(copyBtn);
    auto* pasteBtn = makeIconButton(toolbar, faFont, icons::fa::Paste,
                                     "Paste (Ctrl+V)", "Paste", btnSize);
    tbLayout->addWidget(pasteBtn);
    auto* dupBtn = makeIconButton(toolbar, faFont, icons::fa::Duplicate,
                                   "Duplicate (Ctrl+D)", "Duplicate", btnSize);
    tbLayout->addWidget(dupBtn);

    auto* sep2 = new QFrame(toolbar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet(QString("color: %1;").arg(theme.surfaceLight.name()));
    tbLayout->addWidget(sep2);

    // Transform icon buttons
    auto* quantizeBtn = makeIconButton(toolbar, faFont, icons::fa::Timeselect,
                                        "Quantize... (Ctrl+Q)", "Quantize", btnSize);
    tbLayout->addWidget(quantizeBtn);

    auto* legatoBtn = makeIconButton(toolbar, faFont, icons::fa::HExpand,
                                      "Legato (Ctrl+L)", "Legato", btnSize);
    tbLayout->addWidget(legatoBtn);

    auto* reverseBtn = makeIconButton(toolbar, faFont, icons::fa::Shuffle,
                                       "Reverse", "Reverse", btnSize);
    tbLayout->addWidget(reverseBtn);

    auto* transpUpBtn = makeIconButton(toolbar, faFont, icons::fa::ArrowsVert,
                                        "Transpose... (dialog)", "Transpose", btnSize);
    tbLayout->addWidget(transpUpBtn);

    auto* humanizeBtn = makeIconButton(toolbar, faFont, icons::fa::Drumpad,
                                        "Humanize...", "Humanize", btnSize);
    tbLayout->addWidget(humanizeBtn);

    auto* swingBtn = makeIconButton(toolbar, faFont, icons::fa::Metronome,
                                     "Swing...", "Swing", btnSize);
    tbLayout->addWidget(swingBtn);

    auto* sep3 = new QFrame(toolbar);
    sep3->setFrameShape(QFrame::VLine);
    sep3->setStyleSheet(QString("color: %1;").arg(theme.surfaceLight.name()));
    tbLayout->addWidget(sep3);

    // Musical typing + step record toggle buttons
    const QString toggleStyle = QString(
        "QPushButton { min-width: 26px; min-height: 26px; font-size: 14px; }"
        "QPushButton:checked { background: %1; border-color: %2; }"
        "QPushButton:hover { background: %3; }")
        .arg(theme.accent.darker(120).name(), theme.accent.name(), theme.surfaceLight.name());

    musicalTypingBtn_ = new QPushButton(toolbar);
    musicalTypingBtn_->setAccessibleName("Musical Typing");
    musicalTypingBtn_->setCheckable(true);
    musicalTypingBtn_->setFont(faFont);
    musicalTypingBtn_->setText(QString(icons::fa::Keyboard));
    musicalTypingBtn_->setToolTip("Musical Typing (AWSEDFTGYHUJK)");
    musicalTypingBtn_->setFixedSize(btnSize, btnSize);
    musicalTypingBtn_->setStyleSheet(toggleStyle);
    tbLayout->addWidget(musicalTypingBtn_);

    stepRecordBtn_ = new QPushButton(toolbar);
    stepRecordBtn_->setAccessibleName("Step Record");
    stepRecordBtn_->setCheckable(true);
    stepRecordBtn_->setFont(faFont);
    stepRecordBtn_->setText(QString(icons::fa::PunchIn));
    stepRecordBtn_->setToolTip("Step Record");
    stepRecordBtn_->setFixedSize(btnSize, btnSize);
    stepRecordBtn_->setStyleSheet(toggleStyle);
    tbLayout->addWidget(stepRecordBtn_);

    auto* sep4 = new QFrame(toolbar);
    sep4->setFrameShape(QFrame::VLine);
    sep4->setStyleSheet(QString("color: %1;").arg(theme.surfaceLight.name()));
    tbLayout->addWidget(sep4);

    // Snap
    auto* snapLabel = new QLabel("Snap:", toolbar);
    snapLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(theme.textDim.name()));
    tbLayout->addWidget(snapLabel);

    snapCombo_ = new QComboBox(toolbar);
    snapCombo_->setAccessibleName("Snap Mode");
    snapCombo_->addItem("Off");
    snapCombo_->addItem("1/32");
    snapCombo_->addItem("1/16");
    snapCombo_->addItem("1/8");
    snapCombo_->addItem("1/4");
    snapCombo_->addItem("Bar");
    snapCombo_->setCurrentIndex(4);
    snapCombo_->setFixedWidth(70);
    connect(snapCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PianoRollEditor::onSnapModeChanged);
    tbLayout->addWidget(snapCombo_);

    // Zoom
    auto* zoomInBtn = makeIconButton(toolbar, faFont, icons::fa::ZoomIn,
                                      "Zoom In", "Zoom In", btnSize);
    tbLayout->addWidget(zoomInBtn);
    auto* zoomOutBtn = makeIconButton(toolbar, faFont, icons::fa::ZoomOut,
                                       "Zoom Out", "Zoom Out", btnSize);
    tbLayout->addWidget(zoomOutBtn);

    mainLayout->addWidget(toolbar);

    auto* bodyWidget = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // Ruler row (spacer for keyboard width + ruler aligned with grid)
    auto* rulerRow = new QHBoxLayout();
    rulerRow->setContentsMargins(0, 0, 0, 0);
    rulerRow->setSpacing(0);

    keyboard_ = new PianoKeyboard(bodyWidget);

    auto* rulerSpacer = new QWidget(bodyWidget);
    rulerSpacer->setFixedWidth(keyboard_->sizeHint().width());
    rulerSpacer->setFixedHeight(24);
    rulerRow->addWidget(rulerSpacer);

    ruler_ = new PianoRollRuler(bodyWidget);
    rulerRow->addWidget(ruler_, 1);

    bodyLayout->addLayout(rulerRow);

    // Grid row (keyboard + note grid)
    auto* gridRow = new QHBoxLayout();
    gridRow->setContentsMargins(0, 0, 0, 0);
    gridRow->setSpacing(0);

    gridRow->addWidget(keyboard_);

    noteGrid_ = new NoteGrid(bodyWidget);
    gridRow->addWidget(noteGrid_, 1);

    bodyLayout->addLayout(gridRow, 1);

    // Velocity row
    auto* velRow = new QHBoxLayout();
    velRow->setContentsMargins(0, 0, 0, 0);
    velRow->setSpacing(0);

    auto* velHeaderPanel = new QWidget(bodyWidget);
    velHeaderPanel->setFixedWidth(keyboard_->sizeHint().width());
    velHeaderPanel->setAutoFillBackground(true);
    QPalette velHdrPal;
    velHdrPal.setColor(QPalette::Window, theme.surface);
    velHeaderPanel->setPalette(velHdrPal);

    auto* velHdrLayout = new QVBoxLayout(velHeaderPanel);
    velHdrLayout->setContentsMargins(4, 4, 4, 4);
    velHdrLayout->setSpacing(0);

    auto* velLabel = new QLabel("Vel", velHeaderPanel);
    velLabel->setAccessibleName("Velocity Lane Label");
    velLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    velLabel->setStyleSheet(QString(
        "color: %1; font-size: 9px; font-weight: bold;")
        .arg(theme.textDim.name()));
    velHdrLayout->addWidget(velLabel);
    velHdrLayout->addStretch();

    velRow->addWidget(velHeaderPanel);

    velocityLane_ = new VelocityLane(bodyWidget);
    velRow->addWidget(velocityLane_, 1);

    bodyLayout->addLayout(velRow);

    // CC lane container (collapsible)
    ccContainer_ = new QWidget(bodyWidget);
    auto* ccContainerLayout = new QVBoxLayout(ccContainer_);
    ccContainerLayout->setContentsMargins(0, 0, 0, 0);
    ccContainerLayout->setSpacing(0);

    // Separator between velocity and CC
    auto* ccSepLine = new QFrame(ccContainer_);
    ccSepLine->setFrameShape(QFrame::HLine);
    ccSepLine->setFixedHeight(1);
    ccSepLine->setStyleSheet(QString("background: %1;").arg(theme.surfaceLight.name()));
    ccContainerLayout->addWidget(ccSepLine);

    // CC lane row (header label + lane)
    auto* ccRow = new QHBoxLayout();
    ccRow->setContentsMargins(0, 0, 0, 0);
    ccRow->setSpacing(0);

    // Left header panel (replaces blank spacer)
    auto* ccHeaderPanel = new QWidget(ccContainer_);
    ccHeaderPanel->setFixedWidth(keyboard_->sizeHint().width());
    ccHeaderPanel->setAutoFillBackground(true);
    QPalette ccHdrPal;
    ccHdrPal.setColor(QPalette::Window, theme.surface);
    ccHeaderPanel->setPalette(ccHdrPal);

    auto* ccHdrLayout = new QVBoxLayout(ccHeaderPanel);
    ccHdrLayout->setContentsMargins(4, 4, 4, 4);
    ccHdrLayout->setSpacing(2);

    ccCollapseBtn_ = new QPushButton(ccHeaderPanel);
    ccCollapseBtn_->setAccessibleName("Toggle CC Lane");
    ccCollapseBtn_->setCheckable(true);
    ccCollapseBtn_->setChecked(false);
    ccCollapseBtn_->setText(QStringLiteral("\u25B6")); // right arrow = collapsed
    ccCollapseBtn_->setToolTip("Expand CC Lane");
    ccCollapseBtn_->setFixedSize(16, 16);
    ccCollapseBtn_->setStyleSheet(QString(
        "QPushButton { font-size: 8px; border: none; color: %1; padding: 0; }"
        "QPushButton:hover { color: %2; }")
        .arg(theme.textDim.name(), theme.text.name()));

    ccHeaderLabel_ = new QLabel(ccHeaderPanel);
    ccHeaderLabel_->setAccessibleName("CC Lane Label");
    ccHeaderLabel_->setWordWrap(true);
    ccHeaderLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    ccHeaderLabel_->setStyleSheet(QString(
        "color: %1; font-size: 9px; font-weight: bold;")
        .arg(theme.textDim.name()));

    auto* ccHdrTopRow = new QHBoxLayout();
    ccHdrTopRow->setContentsMargins(0, 0, 0, 0);
    ccHdrTopRow->setSpacing(2);
    ccHdrTopRow->addWidget(ccCollapseBtn_);
    ccHdrTopRow->addWidget(ccHeaderLabel_, 1);

    ccHdrLayout->addLayout(ccHdrTopRow);

    const QString ccToolStyle = QString(
        "QPushButton { font-size: 11px; border: 1px solid %1; border-radius: 2px;"
        "  min-width: 20px; min-height: 16px; padding: 0 2px; color: %2; background: transparent; }"
        "QPushButton:checked { background: %3; color: %4; }"
        "QPushButton:hover { background: %1; }")
        .arg(theme.surfaceLight.name(), theme.textDim.name(),
             theme.accent.name(), theme.text.name());

    auto* ccToolRow = new QHBoxLayout();
    ccToolRow->setContentsMargins(0, 2, 0, 0);
    ccToolRow->setSpacing(2);

    ccFreehandBtn_ = new QPushButton(QStringLiteral("\u270E"), ccHeaderPanel);
    ccFreehandBtn_->setAccessibleName("Freehand Draw");
    ccFreehandBtn_->setToolTip("Freehand draw (pencil)");
    ccFreehandBtn_->setCheckable(true);
    ccFreehandBtn_->setChecked(true);
    ccFreehandBtn_->setFixedSize(22, 18);
    ccFreehandBtn_->setVisible(false);
    ccFreehandBtn_->setStyleSheet(ccToolStyle);

    ccLineBtn_ = new QPushButton(QStringLiteral("\u2571"), ccHeaderPanel);
    ccLineBtn_->setAccessibleName("Line Draw");
    ccLineBtn_->setToolTip("Straight line draw");
    ccLineBtn_->setCheckable(true);
    ccLineBtn_->setChecked(false);
    ccLineBtn_->setFixedSize(22, 18);
    ccLineBtn_->setVisible(false);
    ccLineBtn_->setStyleSheet(ccToolStyle);

    ccToolRow->addWidget(ccFreehandBtn_);
    ccToolRow->addWidget(ccLineBtn_);
    ccToolRow->addStretch();

    ccHdrLayout->addLayout(ccToolRow);

    ccCombo_ = new QComboBox(ccHeaderPanel);
    ccCombo_->setAccessibleName("CC Number");
    ccCombo_->addItem("CC1", 1);
    ccCombo_->addItem("CC7", 7);
    ccCombo_->addItem("CC10", 10);
    ccCombo_->addItem("CC11", 11);
    ccCombo_->addItem("CC64", 64);
    ccCombo_->addItem("...", -1);
    ccCombo_->setCurrentIndex(0);
    ccCombo_->setFixedHeight(18);
    ccCombo_->setVisible(false);
    ccCombo_->setStyleSheet(QString(
        "QComboBox { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 2px; font-size: 8px; padding: 0 2px; }"
        "QComboBox::drop-down { width: 10px; }"
        "QComboBox QAbstractItemView { background: %1; color: %2; font-size: 8px; }")
        .arg(theme.background.name(), theme.textDim.name(), theme.border.name()));
    connect(ccCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PianoRollEditor::onCcComboChanged);
    ccHdrLayout->addWidget(ccCombo_);

    ccHdrLayout->addStretch();

    ccRow->addWidget(ccHeaderPanel);

    ccLane_ = new CcLane(ccContainer_);
    ccLane_->setFixedHeight(0);
    ccRow->addWidget(ccLane_, 1);

    ccContainerLayout->addLayout(ccRow);

    bodyLayout->addWidget(ccContainer_);

    updateCcHeaderLabel();

    // Channel panel on the right side
    channelPanel_ = new QWidget();
    channelPanel_->setAccessibleName("MIDI Channel Panel");
    channelPanel_->setAutoFillBackground(true);
    QPalette chPanelPal;
    chPanelPal.setColor(QPalette::Window, theme.surface);
    channelPanel_->setPalette(chPanelPal);
    channelPanelLayout_ = new QVBoxLayout(channelPanel_);
    channelPanelLayout_->setContentsMargins(4, 4, 4, 4);
    channelPanelLayout_->setSpacing(2);

    channelScrollArea_ = new QScrollArea(this);
    channelScrollArea_->setAccessibleName("MIDI Channel List");
    channelScrollArea_->setWidget(channelPanel_);
    channelScrollArea_->setWidgetResizable(true);
    channelScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    channelScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    channelScrollArea_->setFixedWidth(120);
    channelScrollArea_->setStyleSheet(QString(
        "QScrollArea { background: %1; border-left: 1px solid %2; }"
        "QScrollBar:vertical { width: 6px; background: %1; }"
        "QScrollBar::handle:vertical { background: %3; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(theme.surface.name(), theme.border.name(), theme.surfaceLight.name()));

    auto* bodyHBox = new QHBoxLayout();
    bodyHBox->setContentsMargins(0, 0, 0, 0);
    bodyHBox->setSpacing(0);
    bodyHBox->addWidget(bodyWidget, 1);
    bodyHBox->addWidget(channelScrollArea_);

    mainLayout->addLayout(bodyHBox, 1);

    // ── Connections ──

    connect(keyboard_, &PianoKeyboard::noteClicked, this, [this](int note) {
        noteGrid_->playNotePreview(note);
    });
    connect(keyboard_, &PianoKeyboard::noteReleased, this, [this](int note) {
        noteGrid_->stopNotePreview(note);
    });

    connect(noteGrid_, &NoteGrid::verticalScrollChanged,
            this, &PianoRollEditor::syncKeyboardScroll);
    connect(noteGrid_, &NoteGrid::horizontalScrollChanged,
            this, &PianoRollEditor::syncLaneScroll);
    connect(noteGrid_, &NoteGrid::zoomChanged,
            this, &PianoRollEditor::syncLaneScroll);
    connect(noteGrid_, &NoteGrid::notesChanged,
            this, &PianoRollEditor::onNotesChanged);
    noteGrid_->setEnsureClipCallback([this]() -> te::MidiClip* {
        return ensureClipForActiveChannel();
    });
    connect(noteGrid_, &NoteGrid::editModeRequested,
            this, [this]() { editModeBtn_->setChecked(true); });
    connect(noteGrid_, &NoteGrid::drawModeRequested,
            this, [this]() { drawModeBtn_->setChecked(true); });
    connect(velocityLane_, &VelocityLane::velocityChanged,
            this, [this]() { noteGrid_->rebuildNotes(); });
    connect(ccLane_, &CcLane::ccDataChanged,
            this, [this]() { noteGrid_->rebuildNotes(); });

    connect(ccCollapseBtn_, &QPushButton::toggled, this, [this](bool checked) {
        setCcLaneVisible(checked);
    });

    connect(ccFreehandBtn_, &QPushButton::clicked, this, [this]() {
        ccFreehandBtn_->setChecked(true);
        ccLineBtn_->setChecked(false);
        ccLane_->setDrawTool(CcLane::DrawTool::Freehand);
    });
    connect(ccLineBtn_, &QPushButton::clicked, this, [this]() {
        ccLineBtn_->setChecked(true);
        ccFreehandBtn_->setChecked(false);
        ccLane_->setDrawTool(CcLane::DrawTool::Line);
    });

    // Ruler sync
    connect(noteGrid_, &NoteGrid::horizontalScrollChanged, this, [this](int value) {
        ruler_->setScrollX(static_cast<double>(value));
    });
    connect(noteGrid_, &NoteGrid::zoomChanged, this, [this]() {
        ruler_->setPixelsPerBeat(noteGrid_->pixelsPerBeat());
    });
    connect(noteGrid_, &NoteGrid::typingCursorMoved, this, [this](double beat) {
        ruler_->setCursorBeat(beat);
    });
    connect(ruler_, &PianoRollRuler::cursorPositionClicked, this, [this](double beat) {
        noteGrid_->setTypingCursorBeat(beat);
    });
    connect(ruler_, &PianoRollRuler::cursorPositionDragged, this, [this](double beat) {
        noteGrid_->setTypingCursorBeat(beat);
    });
    ruler_->setPixelsPerBeat(noteGrid_->pixelsPerBeat());
    ruler_->setSnapFunction([this](double beat) { return noteGrid_->snapper().snapBeat(beat); });

    ccLane_->setSnapFunction([this](double beat) {
        return noteGrid_->snapper().snapBeat(beat);
    });

    // Musical typing / step record sync
    connect(musicalTypingBtn_, &QPushButton::toggled, this, [this](bool checked) {
        noteGrid_->setMusicalTypingEnabled(checked);
    });
    connect(stepRecordBtn_, &QPushButton::toggled, this, [this](bool checked) {
        noteGrid_->setStepRecordEnabled(checked);
    });
    connect(noteGrid_, &NoteGrid::musicalTypingToggled, this, [this](bool enabled) {
        musicalTypingBtn_->setChecked(enabled);
    });
    connect(noteGrid_, &NoteGrid::stepRecordToggled, this, [this](bool enabled) {
        stepRecordBtn_->setChecked(enabled);
    });

    // Clipboard buttons
    connect(cutBtn, &QPushButton::clicked, this, [this]() { noteGrid_->cutSelectedNotes(); });
    connect(copyBtn, &QPushButton::clicked, this, [this]() { noteGrid_->copySelectedNotes(); });
    connect(pasteBtn, &QPushButton::clicked, this, [this]() { noteGrid_->pasteNotes(0.0); });
    connect(dupBtn, &QPushButton::clicked, this, [this]() { noteGrid_->duplicateSelectedNotes(); });

    // Transform buttons
    connect(quantizeBtn, &QPushButton::clicked, this, [this]() { noteGrid_->showQuantizeDialog(); });
    connect(legatoBtn, &QPushButton::clicked, this, [this]() { noteGrid_->legatoSelectedNotes(); });
    connect(reverseBtn, &QPushButton::clicked, this, [this]() { noteGrid_->reverseSelectedNotes(); });
    connect(transpUpBtn, &QPushButton::clicked, this, [this]() { noteGrid_->showTransposeDialog(); });
    connect(humanizeBtn, &QPushButton::clicked, this, [this]() { noteGrid_->showHumanizeDialog(); });
    connect(swingBtn, &QPushButton::clicked, this, [this]() { noteGrid_->showSwingDialog(); });

    // Zoom buttons
    connect(zoomInBtn, &QPushButton::clicked, this, [this]() {
        noteGrid_->setPixelsPerBeat(noteGrid_->pixelsPerBeat() * 1.3);
    });
    connect(zoomOutBtn, &QPushButton::clicked, this, [this]() {
        noteGrid_->setPixelsPerBeat(noteGrid_->pixelsPerBeat() / 1.3);
    });

    onSnapModeChanged(4);
    noteGrid_->setEditMode(NoteGrid::EditMode::Edit);
}

void PianoRollEditor::setClip(te::MidiClip* clip, EditManager* editMgr)
{
    clip_ = clip;
    editMgr_ = editMgr;
    track_ = nullptr;
    linkedClips_.clear();
    hiddenChannels_.clear();

    if (clip) {
        track_ = clip->getAudioTrack();
        activeChannel_ = clip->getMidiChannel().getChannelNumber();
        if (activeChannel_ < 1) activeChannel_ = 1;

        gatherLinkedClips();

        clipNameLabel_->setText(
            QString::fromStdString(clip->getName().toStdString()));
    } else {
        clipNameLabel_->setText("No clip selected");
    }

    passClipsToChildren();
    rebuildChannelPanel();
}

void PianoRollEditor::gatherLinkedClips()
{
    linkedClips_.clear();
    if (!clip_ || !track_) {
        if (clip_)
            linkedClips_.push_back(clip_);
        return;
    }

    if (editMgr_) {
        linkedClips_ = editMgr_->getLinkedMidiClips(track_, clip_);
    }

    if (linkedClips_.empty())
        linkedClips_.push_back(clip_);
}

void PianoRollEditor::passClipsToChildren()
{
    double ppb = noteGrid_->pixelsPerBeat();

    if (linkedClips_.size() <= 1) {
        noteGrid_->setClip(clip_);
    } else {
        noteGrid_->setClips(clip_, linkedClips_);
    }

    velocityLane_->setClip(clip_);
    velocityLane_->setPixelsPerBeat(ppb);

    ccLane_->setClip(clip_);
    ccLane_->setPixelsPerBeat(ppb);
}

void PianoRollEditor::setActiveChannel(int channelNumber)
{
    activeChannel_ = channelNumber;

    te::MidiClip* newPrimary = nullptr;
    for (auto* mc : linkedClips_) {
        if (mc->getMidiChannel().getChannelNumber() == channelNumber) {
            newPrimary = mc;
            break;
        }
    }

    if (newPrimary) {
        clip_ = newPrimary;
        noteGrid_->setPrimaryClip(newPrimary);
        velocityLane_->setClip(newPrimary);
        ccLane_->setClip(newPrimary);
    }

    rebuildChannelPanel();
}

te::MidiClip* PianoRollEditor::ensureClipForActiveChannel()
{
    for (auto* mc : linkedClips_) {
        if (mc->getMidiChannel().getChannelNumber() == activeChannel_)
            return mc;
    }

    if (!editMgr_ || !track_ || linkedClips_.empty()) return nullptr;

    auto* refClip = linkedClips_.front();
    auto* newClip = editMgr_->addLinkedMidiChannel(*track_, *refClip, activeChannel_);
    if (!newClip) return nullptr;

    gatherLinkedClips();
    clip_ = newClip;
    noteGrid_->setClips(newClip, linkedClips_);
    velocityLane_->setClip(newClip);
    ccLane_->setClip(newClip);

    return newClip;
}

void PianoRollEditor::refresh()
{
    if (!clip_) return;
    noteGrid_->rebuildNotes();
    velocityLane_->setPixelsPerBeat(noteGrid_->pixelsPerBeat());
    velocityLane_->refresh();
    velocityLane_->repaint();
    ccLane_->setPixelsPerBeat(noteGrid_->pixelsPerBeat());
    ccLane_->refresh();
}

void PianoRollEditor::syncKeyboardScroll()
{
    int vScroll = noteGrid_->verticalScrollBar()->value();
    keyboard_->setScrollOffset(vScroll);
}

void PianoRollEditor::syncLaneScroll()
{
    int hScroll = noteGrid_->horizontalScrollBar()->value();
    velocityLane_->setScrollOffset(hScroll);
    velocityLane_->setPixelsPerBeat(noteGrid_->pixelsPerBeat());
    ccLane_->setScrollOffset(hScroll);
    ccLane_->setPixelsPerBeat(noteGrid_->pixelsPerBeat());
}

void PianoRollEditor::onSnapModeChanged(int index)
{
    static constexpr SnapMode mapping[] = {
        SnapMode::Off, SnapMode::EighthBeat, SnapMode::QuarterBeat,
        SnapMode::HalfBeat, SnapMode::Beat, SnapMode::Bar
    };
    if (index >= 0 && index < 6)
        noteGrid_->snapper().setMode(mapping[index]);
}

void PianoRollEditor::onDrawModeToggled(bool checked)
{
    if (!checked) {
        if (!editModeBtn_->isChecked())
            editModeBtn_->setChecked(true);
        return;
    }

    if (editModeBtn_->isChecked())
        editModeBtn_->setChecked(false);
    noteGrid_->setEditMode(NoteGrid::EditMode::Draw);
}

void PianoRollEditor::onEditModeToggled(bool checked)
{
    if (!checked) {
        if (!drawModeBtn_->isChecked())
            drawModeBtn_->setChecked(true);
        return;
    }

    if (drawModeBtn_->isChecked())
        drawModeBtn_->setChecked(false);
    noteGrid_->setEditMode(NoteGrid::EditMode::Edit);
}

void PianoRollEditor::onNotesChanged()
{
    velocityLane_->setPixelsPerBeat(noteGrid_->pixelsPerBeat());
    velocityLane_->setScrollOffset(noteGrid_->horizontalScrollBar()->value());
    velocityLane_->refresh();
    velocityLane_->repaint();
    ccLane_->setPixelsPerBeat(noteGrid_->pixelsPerBeat());
    ccLane_->setScrollOffset(noteGrid_->horizontalScrollBar()->value());
    ccLane_->refresh();
    emit notesChanged();
}

void PianoRollEditor::onChannelComboChanged(int /*index*/)
{
}

void PianoRollEditor::onCcComboChanged(int index)
{
    if (index < 0) return;
    int ccNum = ccCombo_->itemData(index).toInt();

    if (ccNum < 0) {
        bool ok = false;
        int custom = QInputDialog::getInt(
            this, "CC Number", "Enter CC number (0-127):",
            1, 0, 127, 1, &ok);
        if (ok) {
            ccNum = custom;
            ccCombo_->setItemText(index, QStringLiteral("CC%1").arg(ccNum));
            ccCombo_->setItemData(index, ccNum);
        } else {
            ccCombo_->blockSignals(true);
            ccCombo_->setCurrentIndex(0);
            ccCombo_->blockSignals(false);
            ccNum = ccCombo_->itemData(0).toInt();
        }
    }

    ccLane_->setCcNumber(ccNum);
    updateCcHeaderLabel();
}

void PianoRollEditor::setCcLaneVisible(bool visible)
{
    if (visible) {
        ccLane_->setFixedHeight(80);
        ccLane_->show();
    } else {
        ccLane_->setFixedHeight(0);
    }
    ccCollapseBtn_->setText(visible ? QStringLiteral("\u25BC") : QStringLiteral("\u25B6"));
    ccCollapseBtn_->setToolTip(visible ? "Collapse CC Lane" : "Expand CC Lane");
    ccFreehandBtn_->setVisible(visible);
    ccLineBtn_->setVisible(visible);
    if (ccCombo_) ccCombo_->setVisible(visible);
}

void PianoRollEditor::updateCcHeaderLabel()
{
    if (ccHeaderLabel_)
        ccHeaderLabel_->setText(ccDisplayName());
}

QString PianoRollEditor::ccDisplayName() const
{
    if (!ccCombo_) return QStringLiteral("CC1");
    int idx = ccCombo_->currentIndex();
    if (idx < 0) return QStringLiteral("CC1");
    int ccNum = ccCombo_->itemData(idx).toInt();
    if (ccNum < 0) return QStringLiteral("CC");

    switch (ccNum) {
    case 1:  return QStringLiteral("CC1\nMod");
    case 7:  return QStringLiteral("CC7\nVol");
    case 10: return QStringLiteral("CC10\nPan");
    case 11: return QStringLiteral("CC11\nExpr");
    case 64: return QStringLiteral("CC64\nSust");
    default: return QStringLiteral("CC%1").arg(ccNum);
    }
}

bool PianoRollEditor::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(obj);
        if (widget) {
            QVariant chVar = widget->property("channelNumber");
            if (chVar.isValid()) {
                setActiveChannel(chVar.toInt());
                return true;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* widget = qobject_cast<QWidget*>(obj);
        if (widget && editMgr_ && track_ && !linkedClips_.empty()) {
            QVariant chVar = widget->property("channelNumber");
            if (chVar.isValid()) {
                int ch = chVar.toInt();
                te::MidiClip* targetClip = nullptr;
                for (auto* mc : linkedClips_) {
                    if (mc->getMidiChannel().getChannelNumber() == ch) {
                        targetClip = mc;
                        break;
                    }
                }
                if (!targetClip) {
                    auto* refClip = linkedClips_.front();
                    targetClip = editMgr_->addLinkedMidiChannel(*track_, *refClip, ch);
                    if (targetClip) gatherLinkedClips();
                }
                if (targetClip) {
                    auto rawVal = targetClip->state.getProperty(
                        juce::Identifier("channelDisplayName"));
                    QString current = rawVal.isVoid()
                        ? QString()
                        : QString::fromStdString(rawVal.toString().toStdString());

                    bool ok = false;
                    QString newName = QInputDialog::getText(
                        this, "Rename Channel",
                        QString("Name for channel %1:").arg(ch),
                        QLineEdit::Normal, current, &ok);
                    if (ok && !newName.trimmed().isEmpty()) {
                        editMgr_->setChannelName(*targetClip, newName.trimmed());
                        passClipsToChildren();
                        rebuildChannelPanel();
                    }
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void PianoRollEditor::rebuildChannelPanel()
{
    if (!channelPanel_) return;

    QLayoutItem* child;
    while ((child = channelPanelLayout_->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    auto& theme = ThemeManager::instance().current();

    auto* titleRow = new QWidget(channelPanel_);
    auto* titleRowLayout = new QHBoxLayout(titleRow);
    titleRowLayout->setContentsMargins(2, 2, 2, 2);
    titleRowLayout->setSpacing(3);

    bool allVisible = (hiddenChannels_.size() == 0);

    auto* selectAllBtn = new QPushButton(titleRow);
    selectAllBtn->setAccessibleName("Toggle All Channels Visibility");
    selectAllBtn->setToolTip(allVisible ? "Hide all channels" : "Show all channels");
    selectAllBtn->setFixedSize(18, 14);
    selectAllBtn->setText(allVisible ? QStringLiteral("\u25CF") : QStringLiteral("\u25CB"));
    selectAllBtn->setStyleSheet(QString(
        "QPushButton { font-size: 10px; border: none; color: %1; padding: 0; background: transparent; }"
        "QPushButton:hover { color: %2; }")
        .arg(allVisible ? theme.text.name() : theme.textDim.name(),
             theme.accent.name()));
    connect(selectAllBtn, &QPushButton::clicked, this, [this, allVisible]() {
        if (allVisible) {
            for (int ch = 1; ch <= 16; ++ch) {
                hiddenChannels_.insert(ch);
                noteGrid_->setChannelVisible(ch, false);
            }
        } else {
            hiddenChannels_.clear();
            for (int ch = 1; ch <= 16; ++ch)
                noteGrid_->setChannelVisible(ch, true);
        }
        rebuildChannelPanel();
    });
    titleRowLayout->addWidget(selectAllBtn);

    auto* titleLabel = new QLabel("Channels", titleRow);
    titleLabel->setAccessibleName("Channel Panel Title");
    titleLabel->setAlignment(Qt::AlignLeft);
    titleLabel->setStyleSheet(QString(
        "color: %1; font-size: 9px; font-weight: bold;")
        .arg(theme.textDim.name()));
    titleRowLayout->addWidget(titleLabel, 1);

    channelPanelLayout_->addWidget(titleRow);

    std::map<int, te::MidiClip*> channelClipMap;
    for (auto* mc : linkedClips_) {
        int ch = mc->getMidiChannel().getChannelNumber();
        if (ch >= 1 && ch <= 16)
            channelClipMap[ch] = mc;
    }

    for (int ch = 1; ch <= 16; ++ch) {
        bool isActive = (ch == activeChannel_);
        bool isVisible = (hiddenChannels_.count(ch) == 0);
        te::MidiClip* mc = channelClipMap.count(ch) ? channelClipMap[ch] : nullptr;

        auto* row = new QWidget(channelPanel_);
        row->setAccessibleName(QString("Channel %1 Row").arg(ch));
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(2, 1, 2, 1);
        rowLayout->setSpacing(3);

        auto* eyeBtn = new QPushButton(row);
        eyeBtn->setAccessibleName(QString("Channel %1 Visibility").arg(ch));
        eyeBtn->setToolTip(isVisible ? "Hide channel" : "Show channel");
        eyeBtn->setFixedSize(18, 14);
        eyeBtn->setText(isVisible ? QStringLiteral("\u25CF") : QStringLiteral("\u25CB"));
        eyeBtn->setStyleSheet(QString(
            "QPushButton { font-size: 10px; border: none; color: %1; padding: 0; background: transparent; }"
            "QPushButton:hover { color: %2; }")
            .arg(isVisible ? theme.text.name() : theme.textDim.darker(140).name(),
                 theme.accent.name()));
        connect(eyeBtn, &QPushButton::clicked, this, [this, ch, isVisible]() {
            if (isVisible)
                hiddenChannels_.insert(ch);
            else
                hiddenChannels_.erase(ch);
            noteGrid_->setChannelVisible(ch, !isVisible);
            rebuildChannelPanel();
        });
        rowLayout->addWidget(eyeBtn);

        auto* swatch = new QLabel(row);
        swatch->setAccessibleName(QString("Channel %1 Color").arg(ch));
        swatch->setFixedSize(10, 10);
        swatch->setStyleSheet(QString("background: %1; border-radius: 2px;")
            .arg(channelColor(ch).name()));
        rowLayout->addWidget(swatch);

        QString name;
        if (mc && editMgr_)
            name = editMgr_->getChannelName(mc);
        else
            name = QString("Ch %1").arg(ch);

        auto* nameLabel = new QLabel(name, row);
        nameLabel->setAccessibleName(QString("Channel %1 Name").arg(ch));
        nameLabel->setStyleSheet(QString(
            "color: %1; font-size: 9px; %2 padding: 0 1px;")
            .arg(isActive ? theme.text.name() : theme.textDim.name(),
                 isActive ? QStringLiteral("font-weight: bold;") : QString()));
        rowLayout->addWidget(nameLabel, 1);

        QColor rowBg = isActive ? theme.surfaceLight : Qt::transparent;
        row->setStyleSheet(QString(
            "QWidget { background: %1; border-radius: 2px; }")
            .arg(rowBg.name()));

        row->setCursor(Qt::PointingHandCursor);
        row->installEventFilter(this);
        row->setProperty("channelNumber", ch);

        channelPanelLayout_->addWidget(row);
    }

    channelPanelLayout_->addStretch();
}

} // namespace freedaw
