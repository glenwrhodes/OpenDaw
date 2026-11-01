#pragma once

#include "engine/AudioEngine.h"
#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>

namespace OpenDaw {

class AudioSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AudioSettingsDialog(AudioEngine& engine, QWidget* parent = nullptr);

private:
    void populateDeviceTypes();
    void populateOutputDevices();
    void populateSampleRates();
    void populateBufferSizes();
    void applyOutputDevice();
    void applySettings();
    void updateDriverNote();

    AudioEngine& engine_;

    QComboBox* deviceTypeCombo_  = nullptr;
    QComboBox* outputCombo_      = nullptr;
    QComboBox* sampleRateCombo_  = nullptr;
    QComboBox* bufferSizeCombo_  = nullptr;
    QLabel*    driverNote_       = nullptr;
    QComboBox* monitorCombo_     = nullptr;
    QSpinBox*  recordOffsetSpin_ = nullptr;
};

} // namespace OpenDaw
