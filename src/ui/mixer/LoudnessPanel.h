#pragma once

#include "engine/EditManager.h"
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>

namespace OpenDaw {

// Loudness of what OpenDaw plays, beside the master strip: integrated,
// short-term and momentary LUFS, loudness range and true peak, against a
// delivery target.
class LoudnessPanel : public QWidget {
    Q_OBJECT

public:
    explicit LoudnessPanel(EditManager* editMgr, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    class Bar;
    void poll();
    void resetMeasurement();
    double target() const;

    EditManager* editMgr_;
    QTimer timer_;
    bool wasPlaying_ = false;

    QLabel* integrated_ = nullptr;
    QLabel* verdict_ = nullptr;
    QLabel* shortTerm_ = nullptr;
    QLabel* momentary_ = nullptr;
    QLabel* range_ = nullptr;
    QLabel* truePeak_ = nullptr;
    QLabel* duration_ = nullptr;
    Bar* bar_ = nullptr;
    QComboBox* targetCombo_ = nullptr;
    QCheckBox* autoReset_ = nullptr;
};

} // namespace OpenDaw
