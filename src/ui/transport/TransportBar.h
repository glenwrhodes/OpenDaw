#pragma once

#include "engine/EditManager.h"
#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTimer>
#include <QComboBox>

namespace OpenDaw {

class TransportBar : public QWidget {
    Q_OBJECT

public:
    explicit TransportBar(EditManager* editMgr, QWidget* parent = nullptr);

    QSize sizeHint() const override { return {800, 48}; }

signals:
    void snapModeRequested(int mode);
    void loopToggled(bool enabled);

private slots:
    void onPlay();
    void onStop();
    void onRecord();
    void onLoop();
    void onMetronome();
    void onCountIn();
    void onCountInModeChanged(int index);
    void onPanic();
    void onEngineToggle();
    void onBpmChanged(double bpm);
    void onTimeSigNumChanged(int num);
    void updatePosition();

private:
    void applyButtonStyle(QPushButton* btn, const QColor& activeColor);
    void updateEngineButtonStyle();
    void syncCountInToEngine();

    EditManager* editMgr_;

    QPushButton* rtzBtn_;
    QPushButton* playBtn_;
    QPushButton* stopBtn_;
    QPushButton* recordBtn_;
    QPushButton* loopBtn_;
    QPushButton* metronomeBtn_;
    QPushButton* countInBtn_;
    QComboBox*   countInCombo_;
    QPushButton* panicBtn_;
    QPushButton* engineBtn_;
    QLabel*      positionLabel_;
    QLabel*      beatLabel_;
    QDoubleSpinBox* bpmSpin_;
    QSpinBox*    timeSigNumSpin_;
    QComboBox*   timeSigDenCombo_;
    QComboBox*   snapCombo_;

    QTimer positionTimer_;
    bool isRecording_ = false;
    bool& deferredLoopActivation_;   // lives in EditManager, so it survives a window rebuild
};

} // namespace OpenDaw
