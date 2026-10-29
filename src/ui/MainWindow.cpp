#include "MainWindow.h"
#include "ui/effects/VstSelectorDialog.h"
#include "ui/effects/PluginEditorWindow.h"
#include "ui/pianoroll/PianoRollEditor.h"
#include "ui/audioclip/AudioClipEditor.h"
#include "ui/sheetmusic/SheetMusicView.h"
#include "ui/dialogs/ExportDialog.h"
#include "ui/dialogs/AudioSettingsDialog.h"
#include "ui/video/VideoPlayerWidget.h"
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
#include <QTabBar>
#include <QActionGroup>
#include <QPainter>
#include <QFontMetrics>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>

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
    qDebug() << "[MainWindow] style applied";

    // Transport bar at the top
    transportBar_ = new TransportBar(&editMgr_, this);
    setMenuWidget(nullptr);

    // Timeline as central widget
    timelineView_ = new TimelineView(&editMgr_, this);
    qDebug() << "[MainWindow] TimelineView created";
    setCentralWidget(timelineView_);
    qDebug() << "[MainWindow] central widget set";

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
            this, &MainWindow::dropStaleEditorClips);
    connect(timelineView_, &TimelineView::splitHandsRequested,
            this, &MainWindow::onSplitPianoHands);
    // Removing tracks (or an AI batch) deletes clips without an editChanged.
    connect(&editMgr_, &EditManager::tracksChanged,
            this, &MainWindow::dropStaleEditorClips);

    // VST editor windows keep their plugin alive; close them before the
    // plugin's Edit goes away or once the plugin is removed from it.
    connect(&editMgr_, &EditManager::aboutToChangeEdit,
            this, []() { PluginEditorWindow::closeAll(); });
    connect(&editMgr_, &EditManager::tracksChanged,
            this, []() { PluginEditorWindow::closeOrphaned(); });
    connect(&editMgr_, &EditManager::editChanged,
            this, []() { PluginEditorWindow::closeOrphaned(); });

    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    qDebug() << "[MainWindow] creating docks";
    createDocks();
    qDebug() << "[MainWindow] docks done";
    qDebug() << "[MainWindow] creating menus";
    createMenus();
    qDebug() << "[MainWindow] menus done";
    qDebug() << "[MainWindow] creating toolbar";
    createToolBar();
    qDebug() << "[MainWindow] toolbar done";
    createStatusBar();

    resizeDocks({mixerDock_}, {350}, Qt::Vertical);

    // saveState()/restoreState() match docks and toolbars by object name.
    mixerDock_->setObjectName("MixerDock");
    pianoRollDock_->setObjectName("PianoRollDock");
    audioClipDock_->setObjectName("AudioClipDock");
    routingDock_->setObjectName("RoutingDock");
    sheetMusicDock_->setObjectName("SheetMusicDock");
    videoPlayerDock_->setObjectName("VideoPlayerDock");
    browserDock_->setObjectName("BrowserDock");
    effectsDock_->setObjectName("EffectsDock");
    aiDock_->setObjectName("AiDock");
    transportToolBar_->setObjectName("TransportToolBar");
    mainToolBar_->setObjectName("MainToolBar");
    restoreWindowLayout();

    connect(&editMgr_, &EditManager::editChanged, this, &MainWindow::updateWindowTitle);
    connect(&editMgr_, &EditManager::editChanged, this, [this]() {
        auto* edit = editMgr_.edit();
        if (!edit) return;
        juce::File vf = edit->getVideoFile();
        QString vpath = vf.existsAsFile()
            ? QString::fromStdString(vf.getFullPathName().toStdString())
            : QString();

        if (timelineView_ && timelineView_->videoLane()) {
            auto* lane = timelineView_->videoLane();
            if (!vpath.isEmpty() && lane->videoFilePath() != vpath)
                lane->loadVideo(vpath);
        }

        if (videoPlayer_)
            videoPlayer_->setVideoFile(vpath);
    });
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

MainWindow::~MainWindow()
{
    PluginEditorWindow::closeAll();
}

void MainWindow::dropStaleEditorClips()
{
    if (pianoRoll_)
        pianoRoll_->dropStaleClips();
    if (audioClipEditor_ && audioClipEditor_->clip()
        && !editMgr_.isClipValid(audioClipEditor_->clip()))
        audioClipEditor_->setClip(nullptr, nullptr);
    if (sheetMusicView_ && sheetMusicView_->clip()
        && !editMgr_.isClipValid(sheetMusicView_->clip()))
        sheetMusicView_->setClip(nullptr, nullptr);
}

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
        saveWindowLayout();
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

        "QToolBar { background: %2; border: none; border-bottom: 1px solid %8; "
        "  spacing: 2px; padding: 2px 6px; }"
        "QToolBar::separator { background: %8; width: 1px; margin: 4px 6px; }"
        "QToolButton { background: transparent; color: %3; border: 1px solid transparent; "
        "  border-radius: 4px; padding: 4px 6px; }"
        "QToolButton:hover { background: %6; border: 1px solid %4; }"
        "QToolButton:pressed { background: %1; }"
        "QToolButton:checked { background: %9; border: 1px solid transparent; }"

        "QMainWindow::separator { background: %8; width: 3px; height: 3px; }"
        "QMainWindow::separator:hover { background: %5; }"

        "QDockWidget { background: %1; color: %3; font-size: 11px; }"
        "QDockWidget::title { background: %2; padding: 5px 8px; color: %7; "
        "  border: none; border-bottom: 1px solid %8; font-weight: bold; }"
        "QDockWidget > QWidget { background: %1; }"

        "QTabBar { background: %2; font-size: 11px; }"
        "QTabBar::tab { background: %2; color: %7; border: none; "
        "  border-top: 2px solid transparent; padding: 6px 16px; margin-right: 1px; }"
        "QTabBar::tab:selected { background: %1; color: %3; border-top: 2px solid %5; }"
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
        "QComboBox::down-arrow { image: url(" + ThemeManager::dropDownArrowPath(theme.textDim)
            + "); width: 9px; height: 6px; }"
        "QComboBox::down-arrow:disabled { image: url("
            + ThemeManager::dropDownArrowPath(theme.border) + "); }"
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
        theme.textDim.name(),     // %7
        theme.separator.name(),   // %8
        QString("rgba(%1,%2,%3,45)").arg(theme.accent.red()).arg(theme.accent.green())
            .arg(theme.accent.blue())   // %9
    );

    setStyleSheet(globalSS);
    qApp->setStyleSheet(
        QString("QToolTip { background: %1; color: %2; border: 1px solid %3; "
                "padding: 3px; font-size: 11px; }")
            .arg(theme.surfaceLight.name(), theme.text.name(), theme.border.name()));
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

    recentMenu_ = fileMenu->addMenu("Open &Recent");
    recentMenu_->setAccessibleName("Recent Projects");
    recentMenu_->setToolTipsVisible(true);
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    rebuildRecentMenu();

    fileMenu->addAction("&Save", QKeySequence::Save, this,
        &MainWindow::onSaveProject);

    fileMenu->addAction("Save &As...", QKeySequence("Ctrl+Shift+S"), this,
        &MainWindow::onSaveProjectAs);

    fileMenu->addSeparator();
    fileMenu->addAction("&Export Audio...", QKeySequence("Ctrl+Shift+E"), this,
        &MainWindow::onExportAudio);

    fileMenu->addSeparator();
    fileMenu->addAction("Import &Audio...", QKeySequence("Ctrl+I"), this,
        &MainWindow::onImportAudio);
    fileMenu->addAction("Import &MIDI...", QKeySequence("Ctrl+Shift+I"), this,
        &MainWindow::onImportMidi);
    fileMenu->addAction("Import &Video...", this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, "Import Video", {},
            "Video Files (*.mp4 *.mkv *.avi *.mov *.webm *.wmv);;All Files (*)");
        if (path.isEmpty()) return;

        if (timelineView_ && timelineView_->videoLane()) {
            if (timelineView_->videoLane()->loadVideo(path)) {
                if (!timelineView_->videoLane()->isVisible())
                    timelineView_->toggleVideoLane();
            }
        }

        if (videoPlayer_) {
            videoPlayer_->setVideoFile(path);
            if (videoPlayerDock_ && !videoPlayerDock_->isVisible()) {
                videoPlayerDock_->setVisible(true);
                videoPlayerDock_->raise();
            }
        }
    });

    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", QKeySequence::Quit, this, &QMainWindow::close);

    // Edit menu
    auto* editMenu = menuBar->addMenu("&Edit");

    auto* undoMenuAction = editMenu->addAction("&Undo\tCtrl+Z", this, [this]() { editMgr_.undo(); });
    auto* redoMenuAction = editMenu->addAction("&Redo\tCtrl+Y", this, [this]() { editMgr_.redo(); });
    connect(editMenu, &QMenu::aboutToShow, this, [this, undoMenuAction, redoMenuAction]() {
        auto* edit = editMgr_.edit();
        undoMenuAction->setEnabled(edit && edit->getUndoManager().canUndo());
        redoMenuAction->setEnabled(edit && edit->getUndoManager().canRedo());
    });
    editMenu->addSeparator();
    editMenu->addAction("&Delete Selected Clips\tDel", this, [this]() {
        if (timelineView_) timelineView_->deleteSelectedClips();
    });
    editMenu->addAction("Select &All Clips\tCtrl+A", this, [this]() {
        if (timelineView_) timelineView_->selectAllClips();
    });
    editMenu->addSeparator();

    editMenu->addAction("Add Audio &Track", QKeySequence("Ctrl+T"), this,
        &MainWindow::onAddTrack);

    editMenu->addAction("Add &MIDI Track", QKeySequence("Ctrl+Shift+T"), this,
        &MainWindow::onAddMidiTrack);

    editMenu->addAction("&Remove Selected Track", this,
        &MainWindow::onRemoveTrack);
    editMenu->addAction("Split Piano &Hands (AI)...", this, [this]() {
        onSplitPianoHands(timelineView_ ? timelineView_->selectedTrack() : nullptr);
    });

    editMenu->addSeparator();
    editMenu->addAction("&Copy Clips", QKeySequence::Copy, this, [this]() {
        if (timelineView_) timelineView_->copySelectedClips();
    });
    editMenu->addAction("Cu&t Clips", QKeySequence::Cut, this, [this]() {
        if (timelineView_) timelineView_->cutSelectedClips();
    });
    editMenu->addAction("&Paste Clips", QKeySequence::Paste, this, [this]() {
        if (timelineView_) timelineView_->pasteClips();
    });

    editMenu->addSeparator();
    addThemeMenu(editMenu);
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
    // A dock's toggleViewAction stays checked while the dock is open, even when
    // it sits behind another tab, so the ticks show what is actually open.
    auto addDockToggle = [this, viewMenu](QDockWidget* dock, const QString& text) {
        auto* action = dock->toggleViewAction();
        action->setText(text);
        connect(action, &QAction::triggered, dock, [dock](bool shown) {
            if (shown) dock->raise();
        });
        viewMenu->addAction(action);
        return action;
    };
    addDockToggle(mixerDock_, "&Mixer");
    addDockToggle(pianoRollDock_, "&Piano Roll");
    addDockToggle(audioClipDock_, "Audio &Clip Editor");
    addDockToggle(routingDock_, "&Routing");
    addDockToggle(sheetMusicDock_, "&Sheet Music");
    addDockToggle(videoPlayerDock_, "Video &Player");
    viewMenu->addSeparator();
    addDockToggle(browserDock_, "&Browser");
    addDockToggle(effectsDock_, "&Effects");
    auto* aiToggle = addDockToggle(aiDock_, "&AI Assistant");
    connect(aiToggle, &QAction::triggered, this, [this](bool shown) {
        if (shown) aiChatWidget_->focusInput();
    });
    viewMenu->addSeparator();
    auto* markerLaneAction = viewMenu->addAction("Marker/&Tempo Lane", this, [this]() {
        if (timelineView_) timelineView_->toggleMarkerTempoLane();
    });
    markerLaneAction->setCheckable(true);
    auto* videoLaneAction = viewMenu->addAction("&Video Track", this, [this]() {
        if (timelineView_) timelineView_->toggleVideoLane();
    });
    videoLaneAction->setCheckable(true);
    connect(viewMenu, &QMenu::aboutToShow, this, [this, markerLaneAction, videoLaneAction]() {
        markerLaneAction->setChecked(timelineView_ && timelineView_->isMarkerTempoLaneVisible());
        videoLaneAction->setChecked(timelineView_ && timelineView_->isVideoLaneVisible());
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
    {
        auto* markerTempoToggle = new QAction(miIcon(icons::mi::ShowChart), "Toggle Marker/Tempo Lane", this);
        markerTempoToggle->setToolTip("Toggle Marker/Tempo Lane");
        markerTempoToggle->setCheckable(true);
        connect(markerTempoToggle, &QAction::toggled, this, [this](bool) {
            if (timelineView_) timelineView_->toggleMarkerTempoLane();
        });
        mainToolBar_->addAction(markerTempoToggle);
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
    qDebug() << "[MainWindow] creating mixer dock";
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
    qDebug() << "[MainWindow] mixer dock done";

    qDebug() << "[MainWindow] creating piano roll dock";
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
                if (pianoRoll_ && pianoRoll_->isShowingClip(clip))
                    pianoRoll_->refresh();
            });

    connect(&editMgr_, &EditManager::midiClipSelected,
            this, [this](te::MidiClip* clip) {
                if (pianoRollDock_ && pianoRollDock_->isVisible() && pianoRoll_ && clip)
                    pianoRoll_->setClip(clip, &editMgr_);
            });
    qDebug() << "[MainWindow] piano roll dock done";

    qDebug() << "[MainWindow] creating audio clip dock";
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
    qDebug() << "[MainWindow] audio clip dock done";

    qDebug() << "[MainWindow] creating routing dock";
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
    qDebug() << "[MainWindow] routing dock done";

    qDebug() << "[MainWindow] creating sheet music dock";
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
    qDebug() << "[MainWindow] sheet music dock done";

    // Video Player dock (bottom, tabbed with others)
    videoPlayerDock_ = new QDockWidget("Video", this);
    videoPlayerDock_->setAccessibleName("Video Player Dock");
    videoPlayer_ = new VideoPlayerWidget(&editMgr_, videoPlayerDock_);
    videoPlayerDock_->setWidget(videoPlayer_);
    addDockWidget(Qt::BottomDockWidgetArea, videoPlayerDock_);
    qDebug() << "[MainWindow] video player dock done";

    tabifyDockWidget(mixerDock_, pianoRollDock_);
    tabifyDockWidget(pianoRollDock_, audioClipDock_);
    tabifyDockWidget(audioClipDock_, routingDock_);
    tabifyDockWidget(routingDock_, sheetMusicDock_);
    tabifyDockWidget(sheetMusicDock_, videoPlayerDock_);
    mixerDock_->raise();

    connect(&editMgr_, &EditManager::midiClipDoubleClicked,
            this, &MainWindow::onMidiClipDoubleClicked);

    connect(&editMgr_, &EditManager::audioClipDoubleClicked,
            this, &MainWindow::onAudioClipDoubleClicked);

    qDebug() << "[MainWindow] creating browser dock";
    // File browser dock (right, collapsible)
    browserDock_ = new QDockWidget("Browser", this);
    browserDock_->setAccessibleName("File Browser Dock");
    fileBrowser_ = new FileBrowserPanel(
        app_.audioEngine().engine().getDeviceManager().deviceManager, browserDock_);
    browserDock_->setWidget(fileBrowser_);
    addDockWidget(Qt::RightDockWidgetArea, browserDock_);
    qDebug() << "[MainWindow] browser dock done";

    qDebug() << "[MainWindow] creating effects dock";
    // Effects dock (right, tabbed with browser)
    effectsDock_ = new QDockWidget("Effects", this);
    effectsDock_->setAccessibleName("Effects Dock");
    effectChain_ = new EffectChainWidget(&editMgr_, effectsDock_);
    effectChain_->setPluginList(&app_.pluginScanner().getPluginList());
    effectsDock_->setWidget(effectChain_);
    addDockWidget(Qt::RightDockWidgetArea, effectsDock_);
    qDebug() << "[MainWindow] effects dock done";

    qDebug() << "[MainWindow] creating AI dock";
    // AI assistant dock (right, tabbed with browser and effects)
    aiDock_ = new QDockWidget("AI", this);
    aiDock_->setAccessibleName("AI Assistant Dock");
    aiChatWidget_ = new AiChatWidget(&editMgr_, &app_.audioEngine(),
                                     &app_.pluginScanner(), aiDock_);
    aiDock_->setWidget(aiChatWidget_);
    addDockWidget(Qt::RightDockWidgetArea, aiDock_);
    qDebug() << "[MainWindow] AI dock done";

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

    loadFile(path);
}

void MainWindow::loadFile(const QString& path)
{
    QFileInfo fi(path);
    if (!fi.exists() || fi.suffix().toLower() != "tracktionedit")
        return;

    juce::File file(juce::String(fi.absoluteFilePath().toUtf8().constData()));
    const bool loaded = editMgr_.loadEdit(file);
    if (aiChatWidget_) aiChatWidget_->loadChatSessions();
    updateWindowTitle();

    QSettings settings;
    settings.setValue("paths/lastFileDialogDir", fi.absolutePath());

    if (loaded) {
        addRecentProject(fi.absoluteFilePath());
    } else {
        QMessageBox::warning(this, "Open Project",
            QString("Could not open \"%1\".").arg(fi.fileName()));
    }
}

// ── Themes ──────────────────────────────────────────────────────────────────

void MainWindow::addThemeMenu(QMenu* parent)
{
    auto* themeMenu = parent->addMenu("&Theme");
    themeMenu->setAccessibleName("Theme");
    auto* group = new QActionGroup(themeMenu);
    group->setExclusive(true);

    const QString currentName = ThemeManager::instance().current().name;
    for (const auto& theme : ThemeManager::builtInThemes()) {
        // A strip of the theme's own colours, so the list previews itself.
        QPixmap swatch(30, 14);
        swatch.fill(theme.background);
        {
            QPainter p(&swatch);
            p.fillRect(QRect(0, 0, 10, 14), theme.surface);
            p.fillRect(QRect(10, 0, 10, 14), theme.accent);
            p.fillRect(QRect(20, 0, 10, 14),
                       theme.trackColours.empty() ? theme.text : theme.trackColours[1]);
            p.setPen(theme.separator);
            p.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
        }
        auto* action = themeMenu->addAction(QIcon(swatch), theme.name);
        action->setCheckable(true);
        action->setChecked(theme.name == currentName);
        group->addAction(action);

        const QString name = theme.name;
        connect(action, &QAction::triggered, this, [this, name]() {
            if (name == ThemeManager::instance().current().name) return;
            ThemeManager::instance().setCurrentByName(name);
            ThemeManager::saveChoice(name);
            // Every widget bakes its colours in when it is built, so rebuild the
            // window. Queued: this action belongs to the window being replaced.
            auto* app = &app_;
            QTimer::singleShot(0, app, [app]() { app->rebuildMainWindow(); });
        });
    }
}

void MainWindow::prepareForRebuild()
{
    saveWindowLayout();
    if (routingView_) routingView_->flushNodePositions();
    if (aiChatWidget_) aiChatWidget_->saveChatSessions();
}

void MainWindow::finishRebuild()
{
    if (aiChatWidget_) aiChatWidget_->loadChatSessions();
    statusBar()->showMessage(
        QString("Theme: %1").arg(ThemeManager::instance().current().name), 4000);
}

// ── Recent projects ─────────────────────────────────────────────────────────

static const char* kRecentProjectsKey = "recentProjects";
static const int kMaxRecentProjects = 10;

void MainWindow::addRecentProject(const QString& path)
{
    const QString full = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QSettings settings;
    QStringList recent = settings.value(kRecentProjectsKey).toStringList();
    recent.removeIf([&](const QString& p) { return p.compare(full, Qt::CaseInsensitive) == 0; });
    recent.prepend(full);
    while (recent.size() > kMaxRecentProjects)
        recent.removeLast();
    settings.setValue(kRecentProjectsKey, recent);
}

void MainWindow::rebuildRecentMenu()
{
    if (!recentMenu_) return;
    recentMenu_->clear();

    const QStringList recent = QSettings().value(kRecentProjectsKey).toStringList();
    if (recent.isEmpty()) {
        recentMenu_->addAction("No recent projects")->setEnabled(false);
        return;
    }

    const QFontMetrics fm(recentMenu_->font());
    for (int i = 0; i < recent.size(); ++i) {
        const QString& path = recent[i];
        QFileInfo fi(path);
        QString name = fi.completeBaseName();
        name.replace('&', "&&");
        QString folder = fm.elidedText(QDir::toNativeSeparators(fi.absolutePath()),
                                       Qt::ElideMiddle, 360);
        folder.replace('&', "&&");
        QString text = (i < 9 ? QString("&%1  ").arg(i + 1) : QString("    "))
                       + name + "    " + folder;
        auto* action = recentMenu_->addAction(text, this, [this, path]() {
            openRecentProject(path);
        });
        action->setToolTip(path);
        if (!fi.exists())
            action->setText(text + "  (missing)");
    }
    recentMenu_->addSeparator();
    recentMenu_->addAction("&Clear Recent Projects", this, []() {
        QSettings().remove(kRecentProjectsKey);
    });
}

void MainWindow::openRecentProject(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        auto answer = QMessageBox::question(this, "Open Recent",
            QString("\"%1\" no longer exists.\n\nRemove it from the recent projects list?")
                .arg(QDir::toNativeSeparators(path)));
        if (answer == QMessageBox::Yes) {
            QSettings settings;
            QStringList recent = settings.value(kRecentProjectsKey).toStringList();
            recent.removeIf([&](const QString& p) { return p.compare(path, Qt::CaseInsensitive) == 0; });
            settings.setValue(kRecentProjectsKey, recent);
        }
        return;
    }
    if (!maybeSaveBeforeAction()) return;
    loadFile(path);
}

// ── Import ──────────────────────────────────────────────────────────────────

static double playheadBeat(EditManager& editMgr)
{
    auto* edit = editMgr.edit();
    if (!edit) return 0.0;
    return std::max(0.0, edit->tempoSequence.toBeats(editMgr.transport().getPosition()).inBeats());
}

static QString importStartDir()
{
    QSettings settings;
    QString dir = settings.value("paths/lastImportDir").toString();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = settings.value("paths/lastFileDialogDir", QDir::homePath()).toString();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::homePath();
    return dir;
}

void MainWindow::onImportAudio()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Import Audio", importStartDir(),
        "Audio Files (*.wav *.aif *.aiff *.flac *.ogg *.mp3);;All Files (*)");
    if (paths.isEmpty() || !editMgr_.edit()) return;
    QSettings().setValue("paths/lastImportDir", QFileInfo(paths.first()).absolutePath());

    // One file goes on the selected audio track; several get a track each.
    te::AudioTrack* selected = timelineView_ ? timelineView_->selectedTrack() : nullptr;
    const bool useSelected = paths.size() == 1 && selected && !editMgr_.isMidiTrack(selected)
                             && !editMgr_.isBusTrack(selected);
    const double beat = playheadBeat(editMgr_);

    for (const auto& path : paths) {
        te::AudioTrack* track = useSelected ? selected : editMgr_.addAudioTrack();
        if (!track) break;
        if (!useSelected)
            track->setName(juce::String(QFileInfo(path).completeBaseName().toUtf8().constData()));
        editMgr_.addAudioClipToTrack(*track, juce::File(juce::String(path.toUtf8().constData())), beat);
    }
    if (timelineView_) timelineView_->rebuildClips();
    statusBar()->showMessage(QString("Imported %1 audio file%2 at the playhead")
                                 .arg(paths.size()).arg(paths.size() == 1 ? "" : "s"), 5000);
}

void MainWindow::onImportMidi()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Import MIDI", importStartDir(),
        "MIDI Files (*.mid *.midi);;All Files (*)");
    if (path.isEmpty() || !editMgr_.edit()) return;
    QSettings().setValue("paths/lastImportDir", QFileInfo(path).absolutePath());

    te::AudioTrack* track = timelineView_ ? timelineView_->selectedTrack() : nullptr;
    if (!track || !editMgr_.isMidiTrack(track)) {
        track = editMgr_.addMidiTrack();
        if (!track) return;
        track->setName(juce::String(QFileInfo(path).completeBaseName().toUtf8().constData()));
    }
    if (!editMgr_.importMidiFileToTrack(*track, juce::File(juce::String(path.toUtf8().constData())),
                                        playheadBeat(editMgr_))) {
        QMessageBox::warning(this, "Import MIDI",
            QString("Could not read \"%1\" as a MIDI file.").arg(QFileInfo(path).fileName()));
        return;
    }
    if (timelineView_) timelineView_->rebuildClips();
    statusBar()->showMessage("Imported MIDI at the playhead", 5000);
}

// ── Window layout ───────────────────────────────────────────────────────────

// Bump when docks or toolbars are added/renamed so a stale layout is ignored.
static const int kWindowStateVersion = 1;

void MainWindow::restoreWindowLayout()
{
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/state").toByteArray(), kWindowStateVersion);
    fixDockTabBars();
}

void MainWindow::saveWindowLayout()
{
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState(kWindowStateVersion));
}

void MainWindow::fixDockTabBars()
{
    // Dock tab bars elide by default ("Piano R..."); there is room for full names.
    // QMainWindow creates them lazily, so look again once the layout settles.
    auto apply = [this]() {
        for (auto* tabBar : findChildren<QTabBar*>(Qt::FindDirectChildrenOnly))
            tabBar->setElideMode(Qt::ElideNone);
    };
    apply();
    QTimer::singleShot(0, this, apply);
}

void MainWindow::onSaveProject()
{
    if (editMgr_.currentFile() == juce::File()) {
        onSaveProjectAs();
        return;
    }
    if (routingView_) routingView_->flushNodePositions();
    if (editMgr_.saveEdit())
        addRecentProject(QString::fromStdString(
            editMgr_.currentFile().getFullPathName().toStdString()));
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
    if (editMgr_.saveEditAs(file))
        addRecentProject(path);
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

void MainWindow::onSplitPianoHands(te::AudioTrack* track)
{
    const QString title = "Split Piano Hands";
    if (!track) {
        QMessageBox::information(this, title, "Select the piano track to split first.");
        return;
    }
    if (!editMgr_.isMidiTrack(track)) {
        QMessageBox::information(this, title, "Hand splitting works on MIDI tracks.");
        return;
    }

    if (!handSplitter_) {
        handSplitter_ = new AiHandSplitter(&editMgr_, this);
        connect(handSplitter_, &AiHandSplitter::progress, this, [this](int done, int total) {
            if (!handSplitProgress_) return;
            // A single request has no meaningful progress; keep the busy bar.
            handSplitProgress_->setMaximum(total > 1 ? total : 0);
            handSplitProgress_->setValue(total > 1 ? done : 0);
            if (total > 1)
                handSplitProgress_->setLabelText(
                    QString("Analysing hand movement... (%1 of %2 sections)").arg(done).arg(total));
        });
        connect(handSplitter_, &AiHandSplitter::finished,
                this, &MainWindow::onHandSplitFinished);
        connect(handSplitter_, &AiHandSplitter::failed, this, [this](const QString& message) {
            closeHandSplitProgress();
            QMessageBox::warning(this, "Split Piano Hands", message);
        });
    }
    if (handSplitter_->isRunning()) {
        QMessageBox::information(this, title, "A hand split is already running.");
        return;
    }

    // Created before start(): start() reports progress straight away.
    handSplitProgress_ = new QProgressDialog("Analysing hand movement...", "Cancel", 0, 0, this);
    handSplitProgress_->setWindowTitle(title);
    handSplitProgress_->setAccessibleName("Split Piano Hands Progress");
    handSplitProgress_->setWindowModality(Qt::WindowModal);
    handSplitProgress_->setMinimumDuration(0);
    handSplitProgress_->setAutoClose(false);
    handSplitProgress_->setAutoReset(false);
    connect(handSplitProgress_, &QProgressDialog::canceled, this, [this]() {
        if (handSplitter_) handSplitter_->cancel();
        closeHandSplitProgress();
        statusBar()->showMessage("Hand split cancelled", 4000);
    });

    QString error;
    if (!handSplitter_->start(track, error)) {
        closeHandSplitProgress();
        QMessageBox::warning(this, title, error);
        return;
    }
    if (handSplitProgress_)
        handSplitProgress_->open();
}

void MainWindow::closeHandSplitProgress()
{
    if (!handSplitProgress_) return;
    // close() would emit canceled(); detach first.
    disconnect(handSplitProgress_, nullptr, this, nullptr);
    handSplitProgress_->hide();
    handSplitProgress_->deleteLater();
    handSplitProgress_ = nullptr;
}

void MainWindow::onHandSplitFinished(const QVector<bool>& isLeftHand, int fallbackNotes)
{
    closeHandSplitProgress();
    const QString title = "Split Piano Hands";

    // The Edit must not change while an export renders it; apply the split afterwards.
    if (editMgr_.isRenderInProgress()) {
        QTimer::singleShot(250, this, [this, isLeftHand, fallbackNotes]() {
            onHandSplitFinished(isLeftHand, fallbackNotes);
        });
        return;
    }

    te::AudioTrack* track = nullptr;
    for (auto* t : editMgr_.getAudioTracks())
        if (t->itemID == handSplitter_->trackId())
            track = t;
    if (!track || !handSplitter_->matchesTrack(track)) {
        QMessageBox::warning(this, title,
            "The track changed while it was being analysed, so nothing was split. "
            "Try again.");
        return;
    }

    const int leftCount = static_cast<int>(std::count(isLeftHand.begin(), isLeftHand.end(), true));
    const int rightCount = isLeftHand.size() - leftCount;
    if (leftCount == 0 || rightCount == 0) {
        QMessageBox::information(this, title,
            QString("Every note was assigned to the %1 hand, so there is nothing to split.")
                .arg(leftCount == 0 ? "right" : "left"));
        return;
    }

    std::vector<bool> leftHand(isLeftHand.begin(), isLeftHand.end());
    if (!editMgr_.splitTrackByHands(*track, leftHand)) {
        QMessageBox::warning(this, title, "The track could not be split.");
        return;
    }

    QString message = QString("Split into %1 right-hand and %2 left-hand notes")
                          .arg(rightCount).arg(leftCount);
    if (fallbackNotes > 0)
        message += QString(" (%1 guessed without AI)").arg(fallbackNotes);
    statusBar()->showMessage(message + ". Ctrl+Z to undo.", 10000);
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
