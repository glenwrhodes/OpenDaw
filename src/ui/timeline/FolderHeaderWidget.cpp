#include "FolderHeaderWidget.h"
#include "utils/ThemeManager.h"
#include "utils/IconFont.h"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPainter>
#include <QMenu>

namespace OpenDaw {

FolderHeaderWidget::FolderHeaderWidget(int folderId, EditManager* editMgr,
                                       QWidget* parent)
    : QWidget(parent), folderId_(folderId), editMgr_(editMgr)
{
    setAccessibleName("Folder Header");
    setObjectName("folderHeaderWidget");
    setAttribute(Qt::WA_StyledBackground, true);
    auto& theme = ThemeManager::instance().current();

    collapsed_ = editMgr_->isFolderCollapsed(folderId_);

    setFixedWidth(140);
    setFixedHeight(28);
    setAutoFillBackground(true);
    QPalette pal;
    QColor folderBg = theme.surface.lighter(115);
    pal.setColor(QPalette::Window, folderBg);
    setPalette(pal);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(3);

    collapseBtn_ = new QPushButton(this);
    collapseBtn_->setAccessibleName("Collapse Folder");
    collapseBtn_->setFixedSize(16, 20);
    collapseBtn_->setText(collapsed_ ? QString::fromUtf8("\xe2\x96\xb8")
                                     : QString::fromUtf8("\xe2\x96\xbe"));
    collapseBtn_->setStyleSheet(
        QString("QPushButton { background: transparent; color: %1; border: none; "
                "font-size: 11px; padding: 0; }"
                "QPushButton:hover { color: %2; }")
            .arg(theme.textDim.name(), theme.text.name()));
    connect(collapseBtn_, &QPushButton::clicked, this, [this]() {
        setCollapsed(!collapsed_);
        editMgr_->setFolderCollapsed(folderId_, collapsed_);
        emit collapseToggled(folderId_, collapsed_);
    });
    layout->addWidget(collapseBtn_);

    gripHandle_ = new QWidget(this);
    gripHandle_->setFixedSize(12, 20);
    gripHandle_->setCursor(Qt::OpenHandCursor);
    gripHandle_->setToolTip("Drag to reorder folder");
    gripHandle_->setAccessibleName("Folder Reorder Grip");
    gripHandle_->installEventFilter(this);
    layout->addWidget(gripHandle_);

    QString folderName = editMgr_->getFolderName(folderId_);
    QColor folderAccent(255, 185, 50);

    nameLabel_ = new QLabel(folderName, this);
    nameLabel_->setAccessibleName("Folder Name");
    nameLabel_->setToolTip("Double-click to rename");
    nameLabel_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    nameLabel_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 10px; font-weight: bold; "
                "padding: 0 2px; }")
            .arg(folderAccent.name()));
    nameLabel_->installEventFilter(this);
    layout->addWidget(nameLabel_, 1);

    muteAllBtn_ = new QPushButton(this);
    muteAllBtn_->setAccessibleName("Mute All Tracks in Folder");
    muteAllBtn_->setFixedSize(20, 18);
    muteAllBtn_->setFont(icons::fontAudio(9));
    muteAllBtn_->setText(QString(icons::fa::Mute));
    muteAllBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 3px; font-size: 8px; padding: 0; }"
                "QPushButton:hover { background: %4; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.muteButton.name()));
    connect(muteAllBtn_, &QPushButton::clicked, this, &FolderHeaderWidget::toggleMuteAll);
    layout->addWidget(muteAllBtn_);

    soloAllBtn_ = new QPushButton(this);
    soloAllBtn_->setAccessibleName("Solo All Tracks in Folder");
    soloAllBtn_->setFixedSize(20, 18);
    soloAllBtn_->setFont(icons::fontAudio(9));
    soloAllBtn_->setText(QString(icons::fa::Solo));
    soloAllBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 3px; font-size: 8px; padding: 0; }"
                "QPushButton:hover { background: %4; }")
            .arg(theme.surface.name(), theme.textDim.name(),
                 theme.border.name(), theme.soloButton.name()));
    connect(soloAllBtn_, &QPushButton::clicked, this, &FolderHeaderWidget::toggleSoloAll);
    layout->addWidget(soloAllBtn_);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu;
        menu.setAccessibleName("Folder Context Menu");
        menu.addAction("Rename Folder...", this, &FolderHeaderWidget::startRenameEdit);
        menu.addAction("Delete Folder", [this]() {
            editMgr_->removeFolder(folderId_);
        });
        menu.exec(mapToGlobal(pos));
    });
}

void FolderHeaderWidget::setCollapsed(bool collapsed)
{
    collapsed_ = collapsed;
    if (collapseBtn_)
        collapseBtn_->setText(collapsed
            ? QString::fromUtf8("\xe2\x96\xb8")
            : QString::fromUtf8("\xe2\x96\xbe"));
}

void FolderHeaderWidget::setDropHighlight(bool highlighted)
{
    if (highlighted_ == highlighted) return;
    highlighted_ = highlighted;
    update();
}

void FolderHeaderWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    if (highlighted_) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(255, 200, 60, 60));
        p.setPen(QPen(QColor(255, 185, 50), 2));
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

void FolderHeaderWidget::refresh()
{
    if (nameLabel_)
        nameLabel_->setText(editMgr_->getFolderName(folderId_));
    collapsed_ = editMgr_->isFolderCollapsed(folderId_);
    if (collapseBtn_)
        collapseBtn_->setText(collapsed_
            ? QString::fromUtf8("\xe2\x96\xb8")
            : QString::fromUtf8("\xe2\x96\xbe"));
}

void FolderHeaderWidget::toggleMuteAll()
{
    if (!editMgr_) return;
    auto tracks = editMgr_->getAudioTracks();
    bool anyUnmuted = false;
    for (auto* t : tracks) {
        if (editMgr_->getTrackFolderId(t) == folderId_ && !t->isMuted(false)) {
            anyUnmuted = true;
            break;
        }
    }
    for (auto* t : tracks) {
        if (editMgr_->getTrackFolderId(t) == folderId_)
            t->setMute(anyUnmuted);
    }
    emit editMgr_->tracksChanged();
}

void FolderHeaderWidget::toggleSoloAll()
{
    if (!editMgr_) return;
    auto tracks = editMgr_->getAudioTracks();
    bool anySoloed = false;
    for (auto* t : tracks) {
        if (editMgr_->getTrackFolderId(t) == folderId_ && t->isSolo(false)) {
            anySoloed = true;
            break;
        }
    }
    for (auto* t : tracks) {
        if (editMgr_->getTrackFolderId(t) == folderId_)
            t->setSolo(!anySoloed);
    }
    emit editMgr_->tracksChanged();
}

void FolderHeaderWidget::startRenameEdit()
{
    auto* edit = new QLineEdit(this);
    edit->setAccessibleName("Rename Folder");
    edit->setText(editMgr_->getFolderName(folderId_));
    edit->selectAll();
    edit->setGeometry(nameLabel_->geometry());
    edit->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    edit->setStyleSheet(
        "QLineEdit { background: #222; color: #eee; border: 1px solid #888; "
        "font-size: 10px; font-weight: bold; padding: 1px 2px; border-radius: 2px; }");
    edit->show();
    edit->setFocus();
    nameLabel_->hide();

    int fid = folderId_;
    auto* mgr = editMgr_;
    auto* nameLbl = nameLabel_;

    connect(edit, &QLineEdit::editingFinished, edit, [edit, fid, mgr, nameLbl]() {
        QString newName = edit->text().trimmed();
        edit->hide();
        edit->deleteLater();
        if (!newName.isEmpty()) {
            QTimer::singleShot(0, mgr, [fid, mgr, newName]() {
                mgr->renameFolder(fid, newName);
            });
        } else if (nameLbl) {
            nameLbl->show();
        }
    });
}

bool FolderHeaderWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == nameLabel_ && event->type() == QEvent::MouseButtonDblClick) {
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
            QColor dotColor(180, 160, 80);
            int dotR = 1, cols = 2, rows = 3, spacingX = 4, spacingY = 5;
            int totalW = (cols - 1) * spacingX + dotR * 2;
            int totalH = (rows - 1) * spacingY + dotR * 2;
            int startX = (gripHandle_->width() - totalW) / 2;
            int startY = (gripHandle_->height() - totalH) / 2;
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(dotColor);
                    p.drawEllipse(QPointF(startX + c * spacingX + dotR,
                                          startY + r * spacingY + dotR), dotR, dotR);
                }
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}

} // namespace OpenDaw
