#include "MainWindow.h"
#include "ui/effects/VstSelectorDialog.h"
#include "ui/effects/PluginEditorWindow.h"
#include "ui/pianoroll/PianoRollEditor.h"
#include "utils/ThemeManager.h"
#include <QFileDialog>
#include <QAction>
#include <QLabel>
#include <QKeySequence>
#include <QStyle>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>

namespace freedaw {

MainWindow::MainWindow(FreeDawApplication& app, QWidget* parent)
    : QMainWindow(parent), app_(app), editMgr_(app.editManager())
{
    setWindowTitle("FreeDaw");
    setAccessibleName("FreeDaw Main Window");
    resize(1280, 800);
    setMinimumSize(900, 600);

    applyGlobalStyle();

    // Transport bar at the top
    transportBar_ = new TransportBar(&editMgr_, this);
    setMenuWidget(nullptr);

    // Timeline as central widget
    timelineView_ = new TimelineView(&editMgr_, this);
    setCentralWidget(timelineView_);

    // Connect snap mode from transport to timeline
    connect(transportBar_, &TransportBar::snapModeRequested,
            this, [this](int mode) {
                timelineView_->snapper().setMode(static_cast<SnapMode>(mode));
            });

    connect(timelineView_, &TimelineView::instrumentSelectRequested,
            this, &MainWindow::onInstrumentSelectRequested);

    connect(timelineView_, &TimelineView::trackSelected,
            this, [this](te::AudioTrack* track) {
                if (effectChain_) {
                    effectChain_->setTrack(track);
                }
                if (mixerView_) {
                    mixerView_->setSelectedTrack(track);
                }
            });

    createDocks();
    createMenus();
    createToolBar();
    createStatusBar();
}

MainWindow::~MainWindow() = default;

void MainWindow::applyGlobalStyle()
{
    auto& theme = ThemeManager::instance().current();

    QString globalSS = QString(
        "QMainWindow { background: %1; }"
        "QWidget { background: %1; color: %3; }"
        "QMenuBar { background: %2; color: %3; border-bottom: 1px solid %4; }"
        "QMenuBar::item { background: transparent; }"
        "QMenuBar::item:selected { background: %5; }"
        "QMenu { background: %2; color: %3; border: 1px solid %4; }"
        "QMenu::item:selected { background: %5; }"
        "QToolBar { background: %2; border: none; spacing: 4px; }"
        "QToolButton { background: transparent; color: %3; border: 1px solid transparent; "
        "  border-radius: 3px; padding: 3px 6px; }"
        "QToolButton:hover { background: %6; border: 1px solid %4; }"
        "QToolButton:pressed { background: %5; }"
        "QDockWidget { background: %1; color: %3; font-size: 11px; }"
        "QDockWidget::title { background: %2; padding: 4px; border: 1px solid %4; }"
        "QDockWidget > QWidget { background: %1; }"
        "QStatusBar { background: %2; color: %7; border-top: 1px solid %4; }"
        "QScrollArea { background: %1; border: none; }"
        "QScrollArea > QWidget > QWidget { background: %1; }"
        "QScrollBar:horizontal { background: %1; height: 10px; border: none; }"
        "QScrollBar::handle:horizontal { background: %4; border-radius: 4px; min-width: 20px; }"
        "QScrollBar:vertical { background: %1; width: 10px; border: none; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: %1; }"
        "QSpinBox, QDoubleSpinBox { background: %1; color: %3; border: 1px solid %4; "
        "  border-radius: 2px; padding: 2px 6px; min-height: 18px; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button { width: 16px; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { width: 16px; }"
        "QComboBox { background: %1; color: %3; border: 1px solid %4; "
        "  border-radius: 2px; padding: 2px 6px; min-height: 18px; }"
        "QComboBox::drop-down { border: none; width: 18px; }"
        "QComboBox QAbstractItemView { background: %2; color: %3; selection-background-color: %5; }"
        "QLabel { background: transparent; }"
        "QFrame { background: %1; }"
        "QHeaderView { background: %2; color: %3; border: none; }"
        "QHeaderView::section { background: %2; color: %3; border: 1px solid %4; padding: 2px 4px; }"
        "QGraphicsView { background: %1; border: none; }"
        "QSplitter::handle { background: %4; }"
        "QPushButton { background: %2; color: %3; border: 1px solid %4; "
        "  border-radius: 3px; padding: 3px 8px; }"
        "QPushButton:hover { background: %6; }"
        "QPushButton:pressed { background: %5; }"
        "QLineEdit { background: %1; color: %3; border: 1px solid %4; "
        "  border-radius: 2px; padding: 2px; }"
    ).arg(
        theme.background.name(),  // %1
        theme.surface.name(),     // %2
        theme.text.name(),        // %3
        theme.border.name(),      // %4
        theme.accent.name(),      // %5
        theme.surfaceLight.name(),// %6
        theme.textDim.name()      // %7
    );

    setStyleSheet(globalSS);
}

void MainWindow::createMenus()
{
    auto* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    // File menu
    auto* fileMenu = menuBar->addMenu("&File");

    fileMenu->addAction("&New Project", QKeySequence::New, this,
        &MainWindow::onNewProject);

    fileMenu->addAction("&Open Project...", QKeySequence::Open, this,
        &MainWindow::onOpenProject);

    fileMenu->addAction("&Save", QKeySequence::Save, this,
        &MainWindow::onSaveProject);

    fileMenu->addAction("Save &As...", QKeySequence("Ctrl+Shift+S"), this,
        &MainWindow::onSaveProjectAs);

    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", QKeySequence::Quit, this, &QMainWindow::close);

    // Edit menu
    auto* editMenu = menuBar->addMenu("&Edit");

    editMenu->addAction("Add Audio &Track", QKeySequence("Ctrl+T"), this,
        &MainWindow::onAddTrack);

    editMenu->addAction("Add &MIDI Track", QKeySequence("Ctrl+Shift+T"), this,
        &MainWindow::onAddMidiTrack);

    editMenu->addAction("&Remove Selected Track", this,
        &MainWindow::onRemoveTrack);

    editMenu->addSeparator();
    editMenu->addAction("Scan &VST Plugins...", this,
        &MainWindow::onScanVstPlugins);

    splitClipAction_ = new QAction(style()->standardIcon(QStyle::SP_ArrowRight),
                                   "Split Clip", this);
    splitClipAction_->setShortcut(QKeySequence(Qt::Key_S));
    splitClipAction_->setShortcutContext(Qt::WindowShortcut);
    splitClipAction_->setToolTip("Split selected clip at playhead (S)");
    connect(splitClipAction_, &QAction::triggered,
            this, &MainWindow::onSplitClipAtPlayhead);
    addAction(splitClipAction_);
    editMenu->addAction(splitClipAction_);

    // View menu
    auto* viewMenu = menuBar->addMenu("&View");

    viewMenu->addAction("Zoom &In", QKeySequence("Ctrl+="), timelineView_,
        &TimelineView::zoomIn);
    viewMenu->addAction("Zoom &Out", QKeySequence("Ctrl+-"), timelineView_,
        &TimelineView::zoomOut);
    viewMenu->addSeparator();
    viewMenu->addAction("Toggle &Mixer", this, [this]() {
        mixerDock_->setVisible(!mixerDock_->isVisible());
    });
    viewMenu->addAction("Toggle &Browser", this, [this]() {
        browserDock_->setVisible(!browserDock_->isVisible());
    });
    viewMenu->addAction("Toggle &Effects", this, [this]() {
        effectsDock_->setVisible(!effectsDock_->isVisible());
    });

    // Transport menu
    auto* transportMenu = menuBar->addMenu("&Transport");

    transportMenu->addAction("&Play / Pause", QKeySequence("Space"), this, [this]() {
        auto& t = editMgr_.transport();
        if (t.isPlaying()) t.stop(false, false);
        else t.play(false);
    });

    transportMenu->addAction("&Stop", this, [this]() {
        editMgr_.transport().stop(false, false);
        editMgr_.transport().setPosition(tracktion::TimePosition::fromSeconds(0));
    });

    transportMenu->addAction("&Record", QKeySequence("R"), this, [this]() {
        editMgr_.transport().record(false);
    });
}

void MainWindow::createToolBar()
{
    mainToolBar_ = addToolBar("Main");
    mainToolBar_->setAccessibleName("Main Toolbar");
    mainToolBar_->setMovable(false);
    mainToolBar_->setFloatable(false);
    mainToolBar_->setIconSize(QSize(20, 20));

    mainToolBar_->addWidget(transportBar_);
    if (splitClipAction_) {
        mainToolBar_->addAction(splitClipAction_);
        if (auto* splitButton = mainToolBar_->widgetForAction(splitClipAction_))
            splitButton->setAccessibleName("Split Clip");
    }
}

void MainWindow::createDocks()
{
    // Mixer dock (bottom)
    mixerDock_ = new QDockWidget("Mixer", this);
    mixerDock_->setAccessibleName("Mixer Dock");
    mixerView_ = new MixerView(&editMgr_, mixerDock_);
    mixerDock_->setWidget(mixerView_);
    addDockWidget(Qt::BottomDockWidgetArea, mixerDock_);

    connect(mixerView_, &MixerView::effectInsertRequested,
            this, &MainWindow::onEffectInsertRequested);

    connect(mixerView_, &MixerView::instrumentSelectRequested,
            this, &MainWindow::onInstrumentSelectRequested);

    connect(mixerView_, &MixerView::trackSelected,
            this, [this](te::AudioTrack* track) {
                if (timelineView_)
                    timelineView_->setSelectedTrack(track);
                if (effectChain_)
                    effectChain_->setTrack(track);
            });

    // Piano Roll dock (bottom, tabbed with mixer)
    pianoRollDock_ = new QDockWidget("Piano Roll", this);
    pianoRollDock_->setAccessibleName("Piano Roll Dock");
    pianoRoll_ = new PianoRollEditor(pianoRollDock_);
    pianoRollDock_->setWidget(pianoRoll_);
    addDockWidget(Qt::BottomDockWidgetArea, pianoRollDock_);

    connect(pianoRoll_, &PianoRollEditor::notesChanged,
            timelineView_, &TimelineView::rebuildClips);

    connect(&editMgr_, &EditManager::editChanged, this, [this]() {
        if (pianoRoll_)
            pianoRoll_->setClip(nullptr);
    });
    tabifyDockWidget(mixerDock_, pianoRollDock_);
    mixerDock_->raise();

    connect(&editMgr_, &EditManager::midiClipDoubleClicked,
            this, &MainWindow::onMidiClipDoubleClicked);

    // File browser dock (right, collapsible)
    browserDock_ = new QDockWidget("Browser", this);
    browserDock_->setAccessibleName("File Browser Dock");
    fileBrowser_ = new FileBrowserPanel(browserDock_);
    browserDock_->setWidget(fileBrowser_);
    addDockWidget(Qt::RightDockWidgetArea, browserDock_);

    // Effects dock (right, tabbed with browser)
    effectsDock_ = new QDockWidget("Effects", this);
    effectsDock_->setAccessibleName("Effects Dock");
    effectChain_ = new EffectChainWidget(&editMgr_, effectsDock_);
    effectsDock_->setWidget(effectChain_);
    addDockWidget(Qt::RightDockWidgetArea, effectsDock_);

    tabifyDockWidget(browserDock_, effectsDock_);
    browserDock_->raise();
}

void MainWindow::createStatusBar()
{
    auto* status = statusBar();
    auto& theme = ThemeManager::instance().current();

    auto* sampleRateLabel = new QLabel("44100 Hz", this);
    sampleRateLabel->setAccessibleName("Sample Rate");
    sampleRateLabel->setStyleSheet(
        QString("color: %1; font-size: 10px; padding: 0 8px;")
            .arg(theme.textDim.name()));

    auto* bufferLabel = new QLabel("512 samples", this);
    bufferLabel->setAccessibleName("Buffer Size");
    bufferLabel->setStyleSheet(sampleRateLabel->styleSheet());

    auto* cpuLabel = new QLabel("CPU: 0%", this);
    cpuLabel->setAccessibleName("CPU Usage");
    cpuLabel->setStyleSheet(sampleRateLabel->styleSheet());

    status->addPermanentWidget(sampleRateLabel);
    status->addPermanentWidget(bufferLabel);
    status->addPermanentWidget(cpuLabel);

    status->showMessage("Ready");
}

void MainWindow::onNewProject()
{
    editMgr_.newEdit();
}

void MainWindow::onOpenProject()
{
    QSettings settings;
    QString startDir = settings.value("paths/lastFileDialogDir", QDir::homePath()).toString();
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = QDir::homePath();

    QString path = QFileDialog::getOpenFileName(
        this, "Open Project", startDir,
        "Tracktion Edit (*.tracktionedit);;All Files (*)");
    if (path.isEmpty()) return;

    settings.setValue("paths/lastFileDialogDir", QFileInfo(path).absolutePath());

    juce::File file(path.toStdString());
    editMgr_.loadEdit(file);
}

void MainWindow::onSaveProject()
{
    if (editMgr_.currentFile() == juce::File()) {
        onSaveProjectAs();
        return;
    }
    editMgr_.saveEdit();
}

void MainWindow::onSaveProjectAs()
{
    QSettings settings;
    QString startDir = settings.value("paths/lastFileDialogDir", QDir::homePath()).toString();
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = QDir::homePath();

    QString path = QFileDialog::getSaveFileName(
        this, "Save Project", startDir,
        "Tracktion Edit (*.tracktionedit);;All Files (*)");
    if (path.isEmpty()) return;

    settings.setValue("paths/lastFileDialogDir", QFileInfo(path).absolutePath());

    juce::File file(path.toStdString());
    editMgr_.saveEditAs(file);
}

void MainWindow::onAddTrack()
{
    editMgr_.addAudioTrack();
}

void MainWindow::onRemoveTrack()
{
    auto tracks = editMgr_.getAudioTracks();
    if (tracks.size() > 1) {
        editMgr_.removeTrack(tracks.getLast());
    }
}

void MainWindow::onSplitClipAtPlayhead()
{
    if (timelineView_)
        timelineView_->splitSelectedClipsAtPlayhead();
}

void MainWindow::onAddMidiTrack()
{
    editMgr_.addMidiTrack();
}

void MainWindow::onScanVstPlugins()
{
    auto& scanner = app_.pluginScanner();
    if (scanner.isScanning()) return;

    statusBar()->showMessage("Scanning VST plugins...");
    connect(&scanner, &PluginScanner::scanFinished, this, [this]() {
        auto count = app_.pluginScanner().getPluginList().getNumTypes();
        statusBar()->showMessage(
            QString("VST scan complete - %1 plugins found").arg(count), 5000);
    });
    scanner.startScan();
}

void MainWindow::onEffectInsertRequested(te::AudioTrack* track, int)
{
    if (!track) return;
    effectChain_->setTrack(track);
    effectsDock_->setVisible(true);
    effectsDock_->raise();
}

void MainWindow::onInstrumentSelectRequested(te::AudioTrack* track)
{
    if (!track) return;

    auto& pluginList = app_.pluginScanner().getPluginList();
    VstSelectorDialog dlg(pluginList, true, this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasSelection()) {
        editMgr_.setTrackInstrument(*track, dlg.selectedPlugin());
    }
}

void MainWindow::onMidiClipDoubleClicked(te::MidiClip* clip)
{
    if (!clip) return;
    pianoRollDock_->setVisible(true);
    pianoRollDock_->raise();

    if (pianoRoll_)
        pianoRoll_->setClip(clip);
}

} // namespace freedaw
