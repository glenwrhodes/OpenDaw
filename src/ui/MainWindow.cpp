#include "MainWindow.h"
#include "ui/effects/VstSelectorDialog.h"
#include "ui/effects/PluginEditorWindow.h"
#include "ui/pianoroll/PianoRollEditor.h"
#include "ui/audioclip/AudioClipEditor.h"
#include "ui/sheetmusic/SheetMusicView.h"
#include "ui/dialogs/ExportDialog.h"
#include "ui/dialogs/AudioSettingsDialog.h"
#include "ui/SplashScreen.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include <QFileDialog>
#include <QAction>
#include <QLabel>
#include <QKeySequence>
#include <QStyle>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QShortcutEvent>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace OpenDaw {

bool ShortcutFilter::eventFilter(QObject* /*obj*/, QEvent* event)
{
    if (event->type() != QEvent::Shortcut) return false;

    auto* se = static_cast<QShortcutEvent*>(event);
    auto seq = se->key();
    if (seq.count() != 1) return false;

    auto combo = seq[0];
    auto mods = combo.keyboardModifiers();
    if (mods != Qt::NoModifier && mods != Qt::ShiftModifier) return false;

    auto* focus = QApplication::focusWidget();
    if (qobject_cast<QLineEdit*>(focus) ||
        qobject_cast<QTextEdit*>(focus) ||
        qobject_cast<QPlainTextEdit*>(focus)) {
        return true;
    }
    if (auto* cb = qobject_cast<QComboBox*>(focus))
        if (cb->isEditable()) return true;
    return false;
}

MainWindow::MainWindow(OpenDawApplication& app, QWidget* parent)
    : QMainWindow(parent), app_(app), editMgr_(app.editManager())
{
    auto* shortcutFilter = new ShortcutFilter(this);
    QApplication::instance()->installEventFilter(shortcutFilter);

    setWindowTitle("OpenDaw");
    setAccessibleName("OpenDaw Main Window");
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

    // Connect loop toggle from transport bar to timeline view
    connect(transportBar_, &TransportBar::loopToggled,
            timelineView_, &TimelineView::onLoopToggled);

    connect(timelineView_, &TimelineView::instrumentSelectRequested,
            this, &MainWindow::onInstrumentSelectRequested);

    connect(timelineView_, &TimelineView::trackSelected,
            this, [this](te::AudioTrack* track) {
                if (effectChain_)
                    effectChain_->setTrack(track);
                if (mixerView_)
                    mixerView_->setSelectedTrack(track);
            });

    connect(timelineView_, &TimelineView::selectedClipsDeleted,
            this, [this]() {
                if (pianoRoll_)
                    pianoRoll_->setClip(nullptr, nullptr);
                if (audioClipEditor_)
                    audioClipEditor_->setClip(nullptr, nullptr);
            });

    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    createDocks();
    createMenus();
    createToolBar();
    createStatusBar();

    resizeDocks({mixerDock_}, {350}, Qt::Vertical);

    connect(&editMgr_, &EditManager::editChanged, this, &MainWindow::updateWindowTitle);
    updateWindowTitle();

    aiQuickPrompt_ = new AiQuickPrompt(this);
    connect(aiQuickPrompt_, &AiQuickPrompt::promptSubmitted,
            this, [this](const QString& text) {
                if (aiChatWidget_) {
                    aiChatWidget_->submitPrompt(text);
                    aiDock_->setVisible(true);
                    aiDock_->raise();
                }
            });
}

MainWindow::~MainWindow() = default;

bool MainWindow::maybeSaveBeforeAction()
{
    if (!editMgr_.edit() || !editMgr_.hasUnsavedChanges()) {
        editMgr_.clearAutosave();
        return true;
    }

    auto answer = QMessageBox::question(
        this, "Unsaved Changes",
        "Save changes to the current project?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        onSaveProject();
    else
        editMgr_.clearAutosave();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSaveBeforeAction()) {
        if (aiChatWidget_) aiChatWidget_->saveChatSessions();
        PluginEditorWindow::closeAll();
        editMgr_.stopAutosave();
        editMgr_.clearAutosave();
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::updateWindowTitle()
{
    QString title = "OpenDaw";
    auto currentFile = editMgr_.currentFile();
    if (currentFile != juce::File()) {
        auto name = QString::fromStdString(currentFile.getFileNameWithoutExtension().toStdString());
        title = QString("OpenDaw - %1").arg(name);
    } else {
        title = "OpenDaw - Untitled";
    }

    if (editMgr_.hasUnsavedChanges())
        title = "* " + title;

    setWindowTitle(title);
}

void MainWindow::updateStatusBar()
{
    auto& dm = app_.audioEngine().deviceManager();
    auto* device = dm.deviceManager.getCurrentAudioDevice();

    if (device) {
        double sr = device->getCurrentSampleRate();
        int buf = device->getCurrentBufferSizeSamples();
        sampleRateLabel_->setText(QString("%1 Hz").arg(static_cast<int>(sr)));
        bufferLabel_->setText(QString("%1 samples").arg(buf));
    }

    int cpuPercent = static_cast<int>(dm.getCpuUsage() * 100.0f);
    cpuLabel_->setText(QString("CPU: %1%").arg(cpuPercent));
}

void MainWindow::applyGlobalStyle()
{
    auto& theme = ThemeManager::instance().current();

    QString hoverBorder = QColor(theme.border).lighter(140).name();

    QString globalSS = QString(
        "QMainWindow { background: %1; }"
        "QWidget { background: %1; color: %3; }"

        "QMenuBar { background: %2; color: %3; border-bottom: 1px solid %4; }"
        "QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 4px; }"
        "QMenuBar::item:selected { background: %5; }"
        "QMenu { background: %2; color: %3; border: 1px solid %4; border-radius: 4px; "
        "  padding: 4px 0; }"
        "QMenu::item { padding: 5px 24px; }"
        "QMenu::item:selected { background: %5; border-radius: 3px; margin: 0 4px; }"
        "QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }"

        "QToolBar { background: %2; border: none; spacing: 6px; }"
        "QToolButton { background: transparent; color: %3; border: 1px solid transparent; "
        "  border-radius: 5px; padding: 4px 8px; }"
        "QToolButton:hover { background: %6; border: 1px solid %4; }"
        "QToolButton:pressed { background: %5; }"

        "QDockWidget { background: %1; color: %3; font-size: 11px; }"
        "QDockWidget::title { background: %2; padding: 6px 8px; "
        "  border: none; border-bottom: 1px solid %4; font-weight: bold; }"
        "QDockWidget > QWidget { background: %1; }"

        "QTabBar { background: %1; }"
        "QTabBar::tab { background: %2; color: %7; border: 1px solid %4; "
        "  border-bottom: none; border-top-left-radius: 5px; border-top-right-radius: 5px; "
        "  padding: 5px 14px; margin-right: 2px; font-size: 11px; }"
        "QTabBar::tab:selected { background: %1; color: %3; "
        "  border-bottom: 2px solid %5; font-weight: bold; }"
        "QTabBar::tab:hover:!selected { background: %6; color: %3; }"

        "QStatusBar { background: %2; color: %7; border-top: 1px solid %4; }"

        "QScrollArea { background: %1; border: none; }"
        "QScrollArea > QWidget > QWidget { background: %1; }"
        "QScrollBar:horizontal { background: %1; height: 10px; border: none; }"
        "QScrollBar::handle:horizontal { background: %4; border-radius: 5px; min-width: 24px; }"
        "QScrollBar::handle:horizontal:hover { background: %6; }"
        "QScrollBar:vertical { background: %1; width: 10px; border: none; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 5px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: %6; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: %1; }"

        "QSpinBox, QDoubleSpinBox { background: %1; color: %3; border: 1px solid %4; "
        "  border-radius: 4px; padding: 3px 6px; min-height: 20px; }"
        "QSpinBox:hover, QDoubleSpinBox:hover { border-color: " + hoverBorder + "; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button { width: 16px; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { width: 16px; }"

        "QComboBox { background: %1; color: %3; border: 1px solid %4; "
        "  border-radius: 4px; padding: 3px 8px; min-height: 20px; }"
        "QComboBox:hover { border-color: " + hoverBorder + "; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background: %2; color: %3; "
        "  selection-background-color: %5; border: 1px solid %4; border-radius: 4px; }"

        "QLabel { background: transparent; }"
        "QFrame { background: %1; }"

        "QHeaderView { background: %2; color: %3; border: none; }"
        "QHeaderView::section { background: %2; color: %3; border: 1px solid %4; "
        "  padding: 3px 6px; }"

        "QGraphicsView { background: %1; border: none; }"
        "QSplitter::handle { background: %4; }"

        "QPushButton { background: %2; color: %3; border: 1px solid %4; "
        "  border-radius: 5px; padding: 4px 10px; }"
        "QPushButton:hover { background: %6; border-color: " + hoverBorder + "; }"
        "QPushButton:pressed { background: %5; }"

        "QLineEdit { background: %1; color: %3; border: 1px solid %4; "
        "  border-radius: 4px; padding: 3px 4px; }"
        "QLineEdit:focus { border-color: %5; }"
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
    fileMenu->addAction("&Export Audio...", QKeySequence("Ctrl+Shift+E"), this,
        &MainWindow::onExportAudio);

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
    editMenu->addAction("AI &Preferences...", this, [this]() {
        if (aiChatWidget_)
            aiChatWidget_->openSettings();
    });
    editMenu->addSeparator();
    editMenu->addAction("&Audio Settings...", this, [this]() {
        AudioSettingsDialog dlg(app_.audioEngine(), this);
        dlg.exec();
        updateStatusBar();
    });
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
    viewMenu->addAction("Toggle &Piano Roll", this, [this]() {
        pianoRollDock_->setVisible(!pianoRollDock_->isVisible());
        if (pianoRollDock_->isVisible()) pianoRollDock_->raise();
    });
    viewMenu->addAction("Toggle &Browser", this, [this]() {
        browserDock_->setVisible(!browserDock_->isVisible());
    });
    viewMenu->addAction("Toggle &Effects", this, [this]() {
        effectsDock_->setVisible(!effectsDock_->isVisible());
    });
    viewMenu->addAction("Toggle Audio &Clip Editor", this, [this]() {
        audioClipDock_->setVisible(!audioClipDock_->isVisible());
        if (audioClipDock_->isVisible()) audioClipDock_->raise();
    });
    viewMenu->addAction("Toggle &Routing", this, [this]() {
        routingDock_->setVisible(!routingDock_->isVisible());
        if (routingDock_->isVisible()) routingDock_->raise();
    });
    viewMenu->addAction("Toggle &Sheet Music", this, [this]() {
        sheetMusicDock_->setVisible(!sheetMusicDock_->isVisible());
        if (sheetMusicDock_->isVisible()) sheetMusicDock_->raise();
    });
    viewMenu->addAction("Toggle &AI Assistant", this, [this]() {
        aiDock_->setVisible(!aiDock_->isVisible());
        if (aiDock_->isVisible()) {
            aiDock_->raise();
            aiChatWidget_->focusInput();
        }
    });
    viewMenu->addSeparator();
    auto* quickPromptAction = viewMenu->addAction("AI &Quick Prompt",
        QKeySequence("Ctrl+Shift+Space"), this, [this]() {
            if (aiQuickPrompt_)
                aiQuickPrompt_->showCentered();
        });
    quickPromptAction->setShortcutContext(Qt::WindowShortcut);
    addAction(quickPromptAction);

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

    transportMenu->addSeparator();
    auto* panicAction = transportMenu->addAction("MIDI &Panic",
        QKeySequence("Ctrl+Shift+P"), this, [this]() {
            editMgr_.midiPanic();
        });
    panicAction->setShortcutContext(Qt::WindowShortcut);
    addAction(panicAction);

    // Help menu
    auto* helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("&About OpenDaw", this, [this]() {
        auto* about = new SplashScreen(false, this);
        about->setAttribute(Qt::WA_DeleteOnClose);
        about->finish();
        about->show();
    });
}

void MainWindow::createToolBar()
{
    auto& theme = ThemeManager::instance().current();
    const QColor ic = theme.text;
    const QColor icAccent = theme.accentLight;
    const int sz = 18;
    const QFont faFont = icons::fontAudio(sz);
    const QFont miFont = icons::materialIcons(sz);

    auto faIcon = [&](const QChar& g) { return icons::glyphIcon(faFont, g, ic, sz); };
    auto miIcon = [&](const QChar& g) { return icons::glyphIcon(miFont, g, ic, sz); };
    auto faIconAccent = [&](const QChar& g) { return icons::glyphIcon(faFont, g, icAccent, sz); };

    transportToolBar_ = addToolBar("Transport");
    transportToolBar_->setAccessibleName("Transport Toolbar");
    transportToolBar_->setMovable(false);
    transportToolBar_->setFloatable(false);
    transportToolBar_->setIconSize(QSize(sz, sz));
    transportToolBar_->addWidget(transportBar_);

    addToolBarBreak(Qt::TopToolBarArea);

    mainToolBar_ = addToolBar("Tools");
    mainToolBar_->setAccessibleName("Tools Toolbar");
    mainToolBar_->setMovable(false);
    mainToolBar_->setFloatable(false);
    mainToolBar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mainToolBar_->setIconSize(QSize(sz, sz));

    auto* newProjectAction = mainToolBar_->addAction(
        miIcon(icons::mi::InsertDriveFile), "New Project",
        this, &MainWindow::onNewProject);
    newProjectAction->setToolTip("New Project");

    auto* openProjectAction = mainToolBar_->addAction(
        faIcon(icons::fa::Open), "Open Project",
        this, &MainWindow::onOpenProject);
    openProjectAction->setToolTip("Open Project");

    auto* saveProjectAction = mainToolBar_->addAction(
        faIcon(icons::fa::Save), "Save Project",
        this, &MainWindow::onSaveProject);
    saveProjectAction->setToolTip("Save Project");

    mainToolBar_->addSeparator();

    auto* undoAction = mainToolBar_->addAction(
        faIcon(icons::fa::Undo), "Undo",
        this, [this]() { editMgr_.undo(); });
    undoAction->setToolTip("Undo (Ctrl+Z)");
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setShortcutContext(Qt::WindowShortcut);
    addAction(undoAction);

    auto* redoAction = mainToolBar_->addAction(
        faIcon(icons::fa::Redo), "Redo",
        this, [this]() { editMgr_.redo(); });
    redoAction->setToolTip("Redo (Ctrl+Y)");
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setShortcutContext(Qt::WindowShortcut);
    addAction(redoAction);

    mainToolBar_->addSeparator();

    auto* addAudioTrackAction = mainToolBar_->addAction(
        faIconAccent(icons::fa::Waveform), "Add Audio Track",
        this, &MainWindow::onAddTrack);
    addAudioTrackAction->setToolTip("Add Audio Track");

    auto* addMidiTrackAction = mainToolBar_->addAction(
        faIconAccent(icons::fa::Keyboard), "Add MIDI Track",
        this, &MainWindow::onAddMidiTrack);
    addMidiTrackAction->setToolTip("Add MIDI Track");

    auto* removeTrackAction = mainToolBar_->addAction(
        miIcon(icons::mi::Delete), "Remove Track",
        this, &MainWindow::onRemoveTrack);
    removeTrackAction->setToolTip("Remove Track");

    mainToolBar_->addSeparator();

    if (splitClipAction_) {
        splitClipAction_->setIcon(faIcon(icons::fa::Scissors));
        splitClipAction_->setText("Split Clip");
        mainToolBar_->addAction(splitClipAction_);
    }

    auto* pointerAction = mainToolBar_->addAction(
        faIcon(icons::fa::Pointer), "Pointer Tool");
    pointerAction->setToolTip("Pointer Tool");

    auto* penAction = mainToolBar_->addAction(
        faIcon(icons::fa::Pen), "Pen Tool");
    penAction->setToolTip("Pen Tool");

    auto* eraserAction = mainToolBar_->addAction(
        faIcon(icons::fa::Eraser), "Eraser Tool");
    eraserAction->setToolTip("Eraser Tool");

    mainToolBar_->addSeparator();

    auto* zoomInAction = mainToolBar_->addAction(
        faIcon(icons::fa::ZoomIn), "Zoom In",
        timelineView_, &TimelineView::zoomIn);
    zoomInAction->setToolTip("Zoom In");

    auto* zoomOutAction = mainToolBar_->addAction(
        faIcon(icons::fa::ZoomOut), "Zoom Out",
        timelineView_, &TimelineView::zoomOut);
    zoomOutAction->setToolTip("Zoom Out");

    mainToolBar_->addSeparator();

    auto* scanPluginsAction = mainToolBar_->addAction(
        miIcon(icons::mi::Refresh), "Scan VST Plugins",
        this, &MainWindow::onScanVstPlugins);
    scanPluginsAction->setToolTip("Scan VST Plugins");

    mainToolBar_->addSeparator();

    if (mixerDock_) {
        auto* mixerToggle = mixerDock_->toggleViewAction();
        mixerToggle->setIcon(faIcon(icons::fa::Speaker));
        mixerToggle->setToolTip("Toggle Mixer");
        mainToolBar_->addAction(mixerToggle);
    }
    if (browserDock_) {
        auto* browserToggle = browserDock_->toggleViewAction();
        browserToggle->setIcon(miIcon(icons::mi::Folder));
        browserToggle->setToolTip("Toggle Browser");
        mainToolBar_->addAction(browserToggle);
    }
    if (effectsDock_) {
        auto* effectsToggle = effectsDock_->toggleViewAction();
        effectsToggle->setIcon(miIcon(icons::mi::Tune));
        effectsToggle->setToolTip("Toggle Effects");
        mainToolBar_->addAction(effectsToggle);
    }
    if (pianoRollDock_) {
        auto* pianoRollToggle = pianoRollDock_->toggleViewAction();
        pianoRollToggle->setIcon(faIcon(icons::fa::Keyboard));
        pianoRollToggle->setToolTip("Toggle Piano Roll");
        mainToolBar_->addAction(pianoRollToggle);
    }
    if (audioClipDock_) {
        auto* audioClipToggle = audioClipDock_->toggleViewAction();
        audioClipToggle->setIcon(faIcon(icons::fa::Waveform));
        audioClipToggle->setToolTip("Toggle Audio Clip Editor");
        mainToolBar_->addAction(audioClipToggle);
    }
    if (routingDock_) {
        auto* routingToggle = routingDock_->toggleViewAction();
        routingToggle->setIcon(miIcon(icons::mi::Settings));
        routingToggle->setToolTip("Toggle Routing");
        mainToolBar_->addAction(routingToggle);
    }
    if (sheetMusicDock_) {
        auto* sheetMusicToggle = sheetMusicDock_->toggleViewAction();
        sheetMusicToggle->setIcon(miIcon(icons::mi::LibraryMusic));
        sheetMusicToggle->setToolTip("Toggle Sheet Music");
        mainToolBar_->addAction(sheetMusicToggle);
    }
    if (aiDock_) {
        auto* aiToggle = aiDock_->toggleViewAction();
        aiToggle->setIcon(miIcon(icons::mi::Chat));
        aiToggle->setToolTip("Toggle AI Assistant");
        mainToolBar_->addAction(aiToggle);
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

    connect(mixerView_, &MixerView::masterSelected,
            this, [this]() {
                if (timelineView_)
                    timelineView_->clearTrackSelection();
                if (effectChain_) {
                    effectChain_->setMasterMode();
                    effectsDock_->setVisible(true);
                    effectsDock_->raise();
                }
            });

    // Piano Roll dock (bottom, tabbed with mixer)
    pianoRollDock_ = new QDockWidget("Piano Roll", this);
    pianoRollDock_->setAccessibleName("Piano Roll Dock");
    pianoRoll_ = new PianoRollEditor(pianoRollDock_);
    pianoRollDock_->setWidget(pianoRoll_);
    addDockWidget(Qt::BottomDockWidgetArea, pianoRollDock_);

    connect(pianoRoll_, &PianoRollEditor::notesChanged,
            timelineView_, &TimelineView::rebuildClips);

    connect(pianoRoll_, &PianoRollEditor::notesChanged,
            this, [this]() {
                if (sheetMusicView_ && sheetMusicView_->clip())
                    sheetMusicView_->refresh();
            });

    connect(&editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
        if (pianoRoll_)
            pianoRoll_->setClip(nullptr, nullptr);
    });

    connect(&editMgr_, &EditManager::editChanged, this, [this]() {
        if (pianoRoll_ && pianoRoll_->clip()) {
            if (editMgr_.isClipValid(pianoRoll_->clip()))
                pianoRoll_->refresh();
            else
                pianoRoll_->setClip(nullptr, nullptr);
        }
    });

    connect(&editMgr_, &EditManager::midiClipModified,
            this, [this](te::MidiClip* clip) {
                if (pianoRoll_ && pianoRoll_->clip() == clip)
                    pianoRoll_->refresh();
            });

    connect(&editMgr_, &EditManager::midiClipSelected,
            this, [this](te::MidiClip* clip) {
                if (pianoRollDock_ && pianoRollDock_->isVisible() && pianoRoll_ && clip)
                    pianoRoll_->setClip(clip, &editMgr_);
            });

    // Audio Clip Editor dock (bottom, tabbed with mixer and piano roll)
    audioClipDock_ = new QDockWidget("Audio Clip", this);
    audioClipDock_->setAccessibleName("Audio Clip Editor Dock");
    audioClipEditor_ = new AudioClipEditor(audioClipDock_);
    audioClipDock_->setWidget(audioClipEditor_);
    addDockWidget(Qt::BottomDockWidgetArea, audioClipDock_);

    connect(audioClipEditor_, &AudioClipEditor::clipModified,
            timelineView_, &TimelineView::rebuildClips);

    connect(&editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
        if (audioClipEditor_)
            audioClipEditor_->setClip(nullptr, nullptr);
    });

    connect(&editMgr_, &EditManager::editChanged, this, [this]() {
        if (audioClipEditor_ && audioClipEditor_->clip()) {
            if (editMgr_.isClipValid(audioClipEditor_->clip()))
                audioClipEditor_->refresh();
            else
                audioClipEditor_->setClip(nullptr, nullptr);
        }
    });

    connect(&editMgr_, &EditManager::audioClipModified,
            this, [this](te::WaveAudioClip* clip) {
                if (audioClipEditor_ && audioClipEditor_->clip() == clip)
                    audioClipEditor_->refresh();
            });

    connect(&editMgr_, &EditManager::audioClipSelected,
            this, [this](te::WaveAudioClip* clip) {
                if (audioClipDock_ && audioClipDock_->isVisible() && audioClipEditor_ && clip)
                    audioClipEditor_->setClip(clip, &editMgr_);
            });

    // Routing view dock (bottom, tabbed with mixer and piano roll)
    routingDock_ = new QDockWidget("Routing", this);
    routingDock_->setAccessibleName("Routing Dock");
    routingView_ = new RoutingView(&editMgr_, routingDock_);
    routingDock_->setWidget(routingView_);
    addDockWidget(Qt::BottomDockWidgetArea, routingDock_);

    connect(routingView_, &RoutingView::trackSelected,
            this, [this](te::AudioTrack* track) {
                if (timelineView_)
                    timelineView_->setSelectedTrack(track);
                if (effectChain_)
                    effectChain_->setTrack(track);
                if (mixerView_)
                    mixerView_->setSelectedTrack(track);
                if (effectsDock_) {
                    effectsDock_->setVisible(true);
                    effectsDock_->raise();
                }
            });

    connect(routingView_, &RoutingView::masterSelected,
            this, [this]() {
                if (timelineView_)
                    timelineView_->clearTrackSelection();
                if (effectChain_) {
                    effectChain_->setMasterMode();
                    effectsDock_->setVisible(true);
                    effectsDock_->raise();
                }
                if (mixerView_)
                    mixerView_->setMasterSelected();
            });

    // Sheet Music dock (bottom, tabbed after routing)
    sheetMusicDock_ = new QDockWidget("Sheet Music", this);
    sheetMusicDock_->setAccessibleName("Sheet Music Dock");
    sheetMusicView_ = new SheetMusicView(sheetMusicDock_);
    sheetMusicDock_->setWidget(sheetMusicView_);
    addDockWidget(Qt::BottomDockWidgetArea, sheetMusicDock_);

    connect(&editMgr_, &EditManager::aboutToChangeEdit, this, [this]() {
        if (sheetMusicView_)
            sheetMusicView_->setClip(nullptr, nullptr);
    });

    connect(&editMgr_, &EditManager::editChanged, this, [this]() {
        if (sheetMusicView_ && sheetMusicView_->clip()) {
            if (editMgr_.isClipValid(sheetMusicView_->clip()))
                sheetMusicView_->refresh();
            else
                sheetMusicView_->setClip(nullptr, nullptr);
        }
    });

    connect(&editMgr_, &EditManager::midiClipModified,
            this, [this](te::MidiClip* clip) {
                if (sheetMusicView_ && sheetMusicView_->clip() == clip)
                    sheetMusicView_->refresh();
            });

    connect(&editMgr_, &EditManager::midiClipSelected,
            this, [this](te::MidiClip* clip) {
                if (sheetMusicView_ && clip)
                    sheetMusicView_->setClip(clip, &editMgr_);
            });

    tabifyDockWidget(mixerDock_, pianoRollDock_);
    tabifyDockWidget(pianoRollDock_, audioClipDock_);
    tabifyDockWidget(audioClipDock_, routingDock_);
    tabifyDockWidget(routingDock_, sheetMusicDock_);
    mixerDock_->raise();

    connect(&editMgr_, &EditManager::midiClipDoubleClicked,
            this, &MainWindow::onMidiClipDoubleClicked);

    connect(&editMgr_, &EditManager::audioClipDoubleClicked,
            this, &MainWindow::onAudioClipDoubleClicked);

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
    effectChain_->setPluginList(&app_.pluginScanner().getPluginList());
    effectsDock_->setWidget(effectChain_);
    addDockWidget(Qt::RightDockWidgetArea, effectsDock_);

    // AI assistant dock (right, tabbed with browser and effects)
    aiDock_ = new QDockWidget("AI", this);
    aiDock_->setAccessibleName("AI Assistant Dock");
    aiChatWidget_ = new AiChatWidget(&editMgr_, &app_.audioEngine(),
                                     &app_.pluginScanner(), aiDock_);
    aiDock_->setWidget(aiChatWidget_);
    addDockWidget(Qt::RightDockWidgetArea, aiDock_);

    tabifyDockWidget(browserDock_, effectsDock_);
    tabifyDockWidget(effectsDock_, aiDock_);
    browserDock_->raise();
}

void MainWindow::createStatusBar()
{
    auto* status = statusBar();
    auto& theme = ThemeManager::instance().current();

    auto labelStyle = QString("color: %1; font-size: 10px; padding: 0 8px;")
                          .arg(theme.textDim.name());

    sampleRateLabel_ = new QLabel("-- Hz", this);
    sampleRateLabel_->setAccessibleName("Sample Rate");
    sampleRateLabel_->setStyleSheet(labelStyle);

    bufferLabel_ = new QLabel("-- samples", this);
    bufferLabel_->setAccessibleName("Buffer Size");
    bufferLabel_->setStyleSheet(labelStyle);

    cpuLabel_ = new QLabel("CPU: 0%", this);
    cpuLabel_->setAccessibleName("CPU Usage");
    cpuLabel_->setStyleSheet(labelStyle);

    status->addPermanentWidget(sampleRateLabel_);
    status->addPermanentWidget(bufferLabel_);
    status->addPermanentWidget(cpuLabel_);

    status->showMessage("Ready");

    statusBarTimer_ = new QTimer(this);
    connect(statusBarTimer_, &QTimer::timeout, this, &MainWindow::updateStatusBar);
    statusBarTimer_->start(1000);
    updateStatusBar();
}

void MainWindow::onNewProject()
{
    if (!maybeSaveBeforeAction()) return;
    if (aiChatWidget_) aiChatWidget_->saveChatSessions();
    editMgr_.newEdit();
    if (aiChatWidget_) aiChatWidget_->loadChatSessions();
    updateWindowTitle();
}

void MainWindow::onOpenProject()
{
    if (!maybeSaveBeforeAction()) return;

    QSettings settings;
    QString startDir = settings.value("paths/lastFileDialogDir", QDir::homePath()).toString();
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = QDir::homePath();

    QString path = QFileDialog::getOpenFileName(
        this, "Open Project", startDir,
        "Tracktion Edit (*.tracktionedit);;All Files (*)");
    if (path.isEmpty()) return;

    settings.setValue("paths/lastFileDialogDir", QFileInfo(path).absolutePath());

    juce::File file(juce::String(path.toUtf8().constData()));
    editMgr_.loadEdit(file);
    if (aiChatWidget_) aiChatWidget_->loadChatSessions();
    updateWindowTitle();
}

void MainWindow::loadFile(const QString& path)
{
    QFileInfo fi(path);
    if (!fi.exists() || fi.suffix().toLower() != "tracktionedit")
        return;

    juce::File file(juce::String(fi.absoluteFilePath().toUtf8().constData()));
    editMgr_.loadEdit(file);
    if (aiChatWidget_) aiChatWidget_->loadChatSessions();
    updateWindowTitle();

    QSettings settings;
    settings.setValue("paths/lastFileDialogDir", fi.absolutePath());
}

void MainWindow::onSaveProject()
{
    if (editMgr_.currentFile() == juce::File()) {
        onSaveProjectAs();
        return;
    }
    if (routingView_) routingView_->flushNodePositions();
    editMgr_.saveEdit();
    if (aiChatWidget_) aiChatWidget_->saveChatSessions();
    updateWindowTitle();
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

    if (routingView_) routingView_->flushNodePositions();
    juce::File file(juce::String(path.toUtf8().constData()));
    editMgr_.saveEditAs(file);
    if (aiChatWidget_) aiChatWidget_->saveChatSessions();
    updateWindowTitle();
}

void MainWindow::onExportAudio()
{
    ExportDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto settings = dlg.settings();
    if (settings.destFile == juce::File()) {
        QMessageBox::warning(this, "Export Audio",
                             "Please select an output file.");
        return;
    }

    ExportDialog progressDlg(this);
    progressDlg.setWindowTitle("Exporting...");
    progressDlg.setExporting(true);
    progressDlg.setModal(true);
    progressDlg.show();

    bool success = false;
    auto future = QtConcurrent::run([this, settings, &progressDlg, &success]() {
        success = editMgr_.exportMix(settings, [&progressDlg](float p) {
            QMetaObject::invokeMethod(&progressDlg, [&progressDlg, p]() {
                progressDlg.setProgress(p);
            }, Qt::QueuedConnection);
        });
    });

    while (!future.isFinished()) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    progressDlg.hide();

    if (success) {
        auto fileName = QString::fromStdString(
            settings.destFile.getFileName().toStdString());
        QMessageBox::information(this, "Export Complete",
            QString("Mix exported successfully to:\n%1").arg(fileName));
        statusBar()->showMessage("Export complete", 5000);
    } else {
        QMessageBox::critical(this, "Export Failed",
            "Failed to export the mix. Check the output path and try again.");
    }
}

void MainWindow::onAddTrack()
{
    editMgr_.addAudioTrack();
}

void MainWindow::onRemoveTrack()
{
    auto tracks = editMgr_.getAudioTracks();
    if (tracks.size() <= 1) return;

    te::Track* target = nullptr;
    if (timelineView_)
        target = timelineView_->selectedTrack();
    if (!target)
        target = tracks.getLast();

    auto answer = QMessageBox::question(
        this, "Remove Track",
        QString("Remove the selected track?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        editMgr_.removeTrack(target);
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
        pianoRoll_->setClip(clip, &editMgr_);
}

void MainWindow::onAudioClipDoubleClicked(te::WaveAudioClip* clip)
{
    if (!clip) return;
    audioClipDock_->setVisible(true);
    audioClipDock_->raise();

    if (audioClipEditor_)
        audioClipEditor_->setClip(clip, &editMgr_);
}

} // namespace OpenDaw
