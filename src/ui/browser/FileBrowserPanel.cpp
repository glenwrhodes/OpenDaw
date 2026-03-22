#include "FileBrowserPanel.h"
#include "utils/ThemeManager.h"
#include <QDir>
#include <QStandardPaths>
#include <QHeaderView>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QSettings>

namespace OpenDaw {

FileBrowserPanel::FileBrowserPanel(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("File Browser");
    auto& theme = ThemeManager::instance().current();

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

    // Tree view
    model_ = new QFileSystemModel(this);
    model_->setNameFilters({"*.wav", "*.mp3", "*.flac", "*.ogg", "*.aiff", "*.aif", "*.mid", "*.midi"});
    model_->setNameFilterDisables(false);

    treeView_ = new QTreeView(this);
    treeView_->setAccessibleName("File Browser Tree");
    treeView_->setModel(model_);
    treeView_->setDragEnabled(true);
    treeView_->setDragDropMode(QAbstractItemView::DragOnly);
    treeView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    treeView_->setHeaderHidden(false);
    treeView_->hideColumn(1);  // Size
    treeView_->hideColumn(2);  // Type
    treeView_->hideColumn(3);  // Date
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

    // Restore last browser location if available, otherwise fall back to Music.
    QSettings settings;
    QString lastDir = settings.value("paths/lastBrowserDir").toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (lastDir.isEmpty() || !QDir(lastDir).exists())
            lastDir = QDir::homePath();
    }
    navigateTo(lastDir);

    setupDragSupport();
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

} // namespace OpenDaw
