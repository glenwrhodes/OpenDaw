#pragma once

#include "engine/EditManager.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace OpenDaw {

class FolderHeaderWidget : public QWidget {
    Q_OBJECT

public:
    FolderHeaderWidget(int folderId, EditManager* editMgr,
                       QWidget* parent = nullptr);

    int folderId() const { return folderId_; }
    bool isCollapsed() const { return collapsed_; }
    void setCollapsed(bool collapsed);
    void setDropHighlight(bool highlighted);
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;

signals:
    void collapseToggled(int folderId, bool collapsed);
    void dragStarted(FolderHeaderWidget* header);
    void dragMoved(FolderHeaderWidget* header, int globalY);
    void dragFinished(FolderHeaderWidget* header);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void startRenameEdit();
    void toggleMuteAll();
    void toggleSoloAll();

    int folderId_;
    EditManager* editMgr_;
    bool collapsed_ = false;
    bool highlighted_ = false;

    QPushButton* collapseBtn_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QPushButton* muteAllBtn_ = nullptr;
    QPushButton* soloAllBtn_ = nullptr;
    QWidget* gripHandle_ = nullptr;
    QPoint dragStartPos_;
    bool draggingToReorder_ = false;
};

} // namespace OpenDaw
