#include "FileBrowserPanel.h"
#include "utils/ThemeManager.h"
#include <QDir>
#include <QStandardPaths>
#include <QHeaderView>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QSettings>
#include <QHBoxLayout>
#include <QFileInfo>

namespace OpenDaw {

FileBrowserPanel::FileBrowserPanel(juce::AudioDeviceManager& deviceManager,
                                   QWidget* parent)
    : QWidget(parent)
    , deviceManager_(deviceManager)
{
    setAccessibleName("File Browser");
    auto& theme = ThemeManager::instance().current();

    formatManager_.registerBasicFormats();

    setAutoFillBackground(true);
    QPalette pal;
    pal.setColor(QPalette::Window, theme.background);
    setPalette(pal);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(6, 6, 6, 6);
    layout_->setSpacing(5);

    // Quick location combo
    locationCombo_ = new QComboBox(this);
    locationCombo_->setAccessibleName("Quick Location");
    locationCombo_->addItem("Desktop",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    locationCombo_->addItem("Music",
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    locationCombo_->addItem("Documents",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    locationCombo_->addItem("Home",
        QDir::homePath());

    connect(locationCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                navigateTo(locationCombo_->itemData(idx).toString());
            });
    layout_->addWidget(locationCombo_);

    // Path display
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setAccessibleName("Current Path");
    pathEdit_->setReadOnly(true);
    pathEdit_->setStyleSheet(
        QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; font-size: 10px; padding: 3px 6px; }")
            .arg(theme.background.name(), theme.textDim.name(), theme.border.name()));
    layout_->addWidget(pathEdit_);

    // Tree view with reduced indentation for deep hierarchies
    model_ = new QFileSystemModel(this);
    model_->setNameFilters({"*.wav", "*.mp3", "*.flac", "*.ogg", "*.aiff", "*.aif", "*.mid", "*.midi"});
    model_->setNameFilterDisables(false);

    treeView_ = new QTreeView(this);
    treeView_->setAccessibleName("File Browser Tree");
    treeView_->setModel(model_);
    treeView_->setIndentation(10);
    treeView_->setDragEnabled(true);
    treeView_->setDragDropMode(QAbstractItemView::DragOnly);
    treeView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    treeView_->setHeaderHidden(false);
    treeView_->hideColumn(1);
    treeView_->hideColumn(2);
    treeView_->hideColumn(3);
    treeView_->header()->setStretchLastSection(true);

    treeView_->setStyleSheet(
        QString("QTreeView { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; font-size: 11px; }"
                "QTreeView::item { padding: 3px 0; }"
                "QTreeView::item:selected { background: %4; border-radius: 3px; }"
                "QTreeView::item:hover:!selected { background: %5; border-radius: 3px; }"
                "QHeaderView::section { background: %6; color: %2; "
                "border: none; border-bottom: 1px solid %3; padding: 4px 6px; "
                "font-weight: bold; font-size: 10px; }")
            .arg(theme.background.name(), theme.text.name(), theme.border.name(),
                 theme.accent.name(), theme.surfaceLight.name(), theme.surface.name()));

    connect(treeView_, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        QString path = model_->filePath(idx);
        if (!model_->isDir(idx))
            emit fileDoubleClicked(path);
    });

    layout_->addWidget(treeView_, 1);

    setupPreviewBar();

    // Restore last browser location
    QSettings settings;
    QString lastDir = settings.value("paths/lastBrowserDir").toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (lastDir.isEmpty() || !QDir(lastDir).exists())
            lastDir = QDir::homePath();
    }
    navigateTo(lastDir);

    setupDragSupport();

    // Wire selection changes to load preview
    connect(treeView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() {
        auto indexes = treeView_->selectionModel()->selectedIndexes();
        if (indexes.isEmpty()) return;
        QString path = model_->filePath(indexes.first());
        if (!model_->isDir(indexes.first()) && isAudioFile(path))
            loadPreview(path);
    });

    // Connect the JUCE source player to the device manager
    sourcePlayer_.setSource(&transportSource_);
    deviceManager_.addAudioCallback(&sourcePlayer_);
}

FileBrowserPanel::~FileBrowserPanel()
{
    deviceManager_.removeAudioCallback(&sourcePlayer_);
    transportSource_.setSource(nullptr);
    sourcePlayer_.setSource(nullptr);
    readerSource_.reset();
}

void FileBrowserPanel::setupPreviewBar()
{
    auto& theme = ThemeManager::instance().current();

    previewBar_ = new QWidget(this);
    previewBar_->setAccessibleName("Audio Preview");
    previewBar_->setObjectName("previewBar");
    previewBar_->setStyleSheet(
        QString("QWidget#previewBar { background: %1; border: 1px solid %2; "
                "border-radius: 4px; }")
            .arg(theme.surface.name(), theme.border.name()));

    auto* previewLayout = new QVBoxLayout(previewBar_);
    previewLayout->setContentsMargins(8, 6, 8, 6);
    previewLayout->setSpacing(4);

    fileNameLabel_ = new QLabel(previewBar_);
    fileNameLabel_->setAccessibleName("Preview File Name");
    fileNameLabel_->setText("No file selected");
    fileNameLabel_->setStyleSheet(
        QString("color: %1; font-size: 10px;").arg(theme.textDim.name()));
    fileNameLabel_->setWordWrap(false);
    fileNameLabel_->setTextFormat(Qt::PlainText);
    previewLayout->addWidget(fileNameLabel_);

    auto* controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(4);

    QString btnStyle = QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 3px; font-size: 14px; padding: 2px 6px; min-width: 28px; }"
        "QPushButton:hover { background: %4; }"
        "QPushButton:disabled { color: %5; }")
        .arg(theme.surface.name(), theme.text.name(), theme.border.name(),
             theme.surfaceLight.name(), theme.textDim.name());

    playPauseBtn_ = new QPushButton("\u25B6", previewBar_);
    playPauseBtn_->setAccessibleName("Play or Pause Preview");
    playPauseBtn_->setToolTip("Play / Pause");
    playPauseBtn_->setStyleSheet(btnStyle);
    playPauseBtn_->setEnabled(false);
    controlsRow->addWidget(playPauseBtn_);

    stopBtn_ = new QPushButton("\u25A0", previewBar_);
    stopBtn_->setAccessibleName("Stop Preview");
    stopBtn_->setToolTip("Stop");
    stopBtn_->setStyleSheet(btnStyle);
    stopBtn_->setEnabled(false);
    controlsRow->addWidget(stopBtn_);

    seekSlider_ = new QSlider(Qt::Horizontal, previewBar_);
    seekSlider_->setAccessibleName("Preview Seek Position");
    seekSlider_->setRange(0, 1000);
    seekSlider_->setValue(0);
    seekSlider_->setStyleSheet(
        QString("QSlider::groove:horizontal { background: %1; height: 4px; border-radius: 2px; }"
                "QSlider::handle:horizontal { background: %2; width: 10px; margin: -3px 0; border-radius: 5px; }"
                "QSlider::sub-page:horizontal { background: %3; border-radius: 2px; }")
            .arg(theme.surfaceLight.name(), theme.accent.name(), theme.accent.name()));
    controlsRow->addWidget(seekSlider_, 1);

    timeLabel_ = new QLabel("0:00 / 0:00", previewBar_);
    timeLabel_->setAccessibleName("Preview Time");
    timeLabel_->setStyleSheet(
        QString("color: %1; font-size: 9px;").arg(theme.textDim.name()));
    timeLabel_->setMinimumWidth(70);
    controlsRow->addWidget(timeLabel_);

    previewLayout->addLayout(controlsRow);
    layout_->addWidget(previewBar_);

    connect(playPauseBtn_, &QPushButton::clicked, this, &FileBrowserPanel::onPlayPauseClicked);
    connect(stopBtn_, &QPushButton::clicked, this, &FileBrowserPanel::onStopClicked);

    connect(seekSlider_, &QSlider::sliderPressed, this, [this]() { seekSliderPressed_ = true; });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this]() {
        seekSliderPressed_ = false;
        double length = transportSource_.getLengthInSeconds();
        if (length > 0.0) {
            double ratio = seekSlider_->value() / 1000.0;
            transportSource_.setPosition(ratio * length);
        }
    });

    // Poll transport position for UI updates
    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(50);
    connect(uiTimer_, &QTimer::timeout, this, &FileBrowserPanel::updatePreviewUI);
    uiTimer_->start();
}

void FileBrowserPanel::navigateTo(const QString& path)
{
    if (path.isEmpty() || !QDir(path).exists())
        return;

    model_->setRootPath(path);
    treeView_->setRootIndex(model_->index(path));
    pathEdit_->setText(path);
    QSettings().setValue("paths/lastBrowserDir", path);
}

void FileBrowserPanel::setupDragSupport()
{
    treeView_->setDragEnabled(true);
}

void FileBrowserPanel::loadPreview(const QString& filePath)
{
    if (filePath == currentPreviewFile_)
        return;

    transportSource_.stop();
    transportSource_.setSource(nullptr);
    readerSource_.reset();

    currentPreviewFile_ = filePath;
    QFileInfo fi(filePath);
    fileNameLabel_->setText(fi.fileName());
    fileNameLabel_->setToolTip(filePath);

    juce::File juceFile(filePath.toStdString());
    auto* reader = formatManager_.createReaderFor(juceFile);
    if (!reader) {
        playPauseBtn_->setEnabled(false);
        stopBtn_->setEnabled(false);
        fileNameLabel_->setText(fi.fileName() + " (unsupported)");
        return;
    }

    readerSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    transportSource_.setSource(readerSource_.get(), 0, nullptr,
                               reader->sampleRate);

    playPauseBtn_->setEnabled(true);
    stopBtn_->setEnabled(true);
    seekSlider_->setValue(0);
}

void FileBrowserPanel::onPlayPauseClicked()
{
    if (transportSource_.isPlaying()) {
        transportSource_.stop();
    } else {
        if (!readerSource_) return;
        if (transportSource_.getCurrentPosition() >= transportSource_.getLengthInSeconds())
            transportSource_.setPosition(0.0);
        transportSource_.start();
    }
}

void FileBrowserPanel::onStopClicked()
{
    transportSource_.stop();
    transportSource_.setPosition(0.0);
}

void FileBrowserPanel::updatePreviewUI()
{
    bool playing = transportSource_.isPlaying();
    double pos = transportSource_.getCurrentPosition();
    double length = transportSource_.getLengthInSeconds();

    playPauseBtn_->setText(playing ? "\u23F8" : "\u25B6");

    if (length > 0.0 && !seekSliderPressed_) {
        int sliderPos = static_cast<int>((pos / length) * 1000.0);
        seekSlider_->setValue(sliderPos);
    }

    timeLabel_->setText(QString("%1 / %2").arg(formatTime(pos), formatTime(length)));

    // Auto-stop at end
    if (!playing && readerSource_ && pos >= length && length > 0.0) {
        transportSource_.setPosition(0.0);
        seekSlider_->setValue(0);
    }
}

bool FileBrowserPanel::isAudioFile(const QString& path) const
{
    static const QStringList exts = {"wav", "mp3", "flac", "ogg", "aiff", "aif"};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QString FileBrowserPanel::formatTime(double seconds) const
{
    if (seconds < 0.0) seconds = 0.0;
    int totalSec = static_cast<int>(seconds);
    int min = totalSec / 60;
    int sec = totalSec % 60;
    return QString("%1:%2").arg(min).arg(sec, 2, 10, QChar('0'));
}

} // namespace OpenDaw
