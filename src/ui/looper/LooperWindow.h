#pragma once

#include "engine/LooperEngine.h"
#include <QWidget>
#include <QTimer>
#include <vector>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QGridLayout;

namespace OpenDaw {

class EditManager;

// One looper pad: a round button with a ring showing where the loop is.
class LooperPadWidget : public QWidget {
    Q_OBJECT
public:
    LooperPadWidget(LooperEngine& engine, int index, QWidget* parent = nullptr);
    QSize sizeHint() const override { return { 120, 120 }; }
    QSize minimumSizeHint() const override { return { 72, 72 }; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    LooperEngine& engine_;
    const int index_;
};

// A level bar for the looper's input.
class LooperInputMeter : public QWidget {
public:
    explicit LooperInputMeter(QWidget* parent = nullptr);
    void setLevel(float peak);
    QSize sizeHint() const override { return { 90, 10 }; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    float level_ = 0.0f;
};

// The looper: pads, their settings, and the transport while performing.
// A tool window over the main window; the pads keep running when it's hidden.
class LooperWindow : public QWidget {
    Q_OBJECT
public:
    LooperWindow(EditManager& editMgr, QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

signals:
    void visibilityChanged(bool visible);

private:
    void rebuildPads();
    void refreshInputs();
    void syncControls();
    void tick();

    EditManager& editMgr_;
    LooperEngine& engine_;
    QComboBox* inputCombo_ = nullptr;
    QCheckBox* monitorCheck_ = nullptr;
    LooperInputMeter* meter_ = nullptr;
    QComboBox* quantizeCombo_ = nullptr;
    QComboBox* lengthCombo_ = nullptr;
    QCheckBox* tempoCheck_ = nullptr;
    QCheckBox* countInCheck_ = nullptr;
    QCheckBox* clickCheck_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* playBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QGridLayout* grid_ = nullptr;
    std::vector<LooperPadWidget*> pads_;
    QTimer timer_;
};

} // namespace OpenDaw
