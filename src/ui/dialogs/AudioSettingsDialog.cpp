#include "AudioSettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QMessageBox>

namespace OpenDaw {

AudioSettingsDialog::AudioSettingsDialog(AudioEngine& engine, QWidget* parent)
    : QDialog(parent), engine_(engine)
{
    setWindowTitle("Audio Settings");
    setAccessibleName("Audio Settings Dialog");
    setMinimumWidth(420);

    auto* mainLayout = new QVBoxLayout(this);

    auto* group = new QGroupBox("Audio Device", this);
    group->setAccessibleName("Audio Device Settings");
    auto* form = new QFormLayout(group);

    deviceTypeCombo_ = new QComboBox(this);
    deviceTypeCombo_->setAccessibleName("Audio device type");
    form->addRow("Device Type:", deviceTypeCombo_);

    outputCombo_ = new QComboBox(this);
    outputCombo_->setAccessibleName("Output device");
    form->addRow("Output Device:", outputCombo_);

    sampleRateCombo_ = new QComboBox(this);
    sampleRateCombo_->setAccessibleName("Sample rate");
    form->addRow("Sample Rate:", sampleRateCombo_);

    bufferSizeCombo_ = new QComboBox(this);
    bufferSizeCombo_->setAccessibleName("Buffer size");
    form->addRow("Buffer Size:", bufferSizeCombo_);

    mainLayout->addWidget(group);

    driverNote_ = new QLabel(this);
    driverNote_->setAccessibleName("Driver information note");
    driverNote_->setWordWrap(true);
    driverNote_->setStyleSheet("font-size: 10px; color: gray; padding: 4px 8px;");
    mainLayout->addWidget(driverNote_);

    auto* recGroup = new QGroupBox("Recording", this);
    recGroup->setAccessibleName("Recording Settings");
    auto* recForm = new QFormLayout(recGroup);
    QSettings settings;

    monitorCombo_ = new QComboBox(this);
    monitorCombo_->setAccessibleName("Input monitoring");
    monitorCombo_->addItem("Off", 0);
    monitorCombo_->addItem("Auto (while the track is armed)", 1);
    monitorCombo_->addItem("Always", 2);
    monitorCombo_->setToolTip("When you hear an input through its track. Use Off when "
                              "you monitor through your interface's direct monitoring.");
    monitorCombo_->setCurrentIndex(
        std::max(0, monitorCombo_->findData(settings.value("audio/monitorMode", 1).toInt())));
    recForm->addRow("Input Monitoring:", monitorCombo_);

    recordOffsetSpin_ = new QSpinBox(this);
    recordOffsetSpin_->setAccessibleName("Recording offset in milliseconds");
    recordOffsetSpin_->setRange(-500, 500);
    recordOffsetSpin_->setSuffix(" ms");
    recordOffsetSpin_->setToolTip("Shifts new recordings to line up with what you played "
                                  "along to. Added to the driver's reported latency; raise it "
                                  "if recordings land late, lower it if they land early.");
    recordOffsetSpin_->setValue(settings.value("audio/recordOffsetMs", 0).toInt());
    recForm->addRow("Recording Offset:", recordOffsetSpin_);

    mainLayout->addWidget(recGroup);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setAccessibleName("Cancel audio settings");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    auto* applyBtn = new QPushButton("Apply", this);
    applyBtn->setAccessibleName("Apply audio settings");
    applyBtn->setDefault(true);
    connect(applyBtn, &QPushButton::clicked, this, [this]() {
        applySettings();
        accept();
    });
    btnLayout->addWidget(applyBtn);

    mainLayout->addLayout(btnLayout);

    connect(deviceTypeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (deviceTypeCombo_->currentIndex() < 0) return;
        auto& adm = engine_.deviceManager().deviceManager;
        auto& types = adm.getAvailableDeviceTypes();
        int idx = deviceTypeCombo_->currentIndex();
        if (idx >= 0 && idx < types.size()) {
            adm.setCurrentAudioDeviceType(types[idx]->getTypeName(), true);
            populateOutputDevices();
            updateDriverNote();
        }
    });

    connect(outputCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        if (outputCombo_->currentIndex() < 0) return;
        applyOutputDevice();
    });

    populateDeviceTypes();
}

void AudioSettingsDialog::populateDeviceTypes()
{
    deviceTypeCombo_->blockSignals(true);
    deviceTypeCombo_->clear();

    auto& adm = engine_.deviceManager().deviceManager;
    auto& types = adm.getAvailableDeviceTypes();
    auto* currentType = adm.getCurrentDeviceTypeObject();

    int selectedIdx = 0;
    for (int i = 0; i < types.size(); ++i) {
        auto name = types[i]->getTypeName();
        deviceTypeCombo_->addItem(QString::fromStdString(name.toStdString()));
        if (currentType && currentType->getTypeName() == name)
            selectedIdx = i;
    }
    deviceTypeCombo_->setCurrentIndex(selectedIdx);
    deviceTypeCombo_->blockSignals(false);

    populateOutputDevices();
    updateDriverNote();
}

void AudioSettingsDialog::populateOutputDevices()
{
    outputCombo_->blockSignals(true);
    outputCombo_->clear();

    auto& adm = engine_.deviceManager().deviceManager;
    auto* type = adm.getCurrentDeviceTypeObject();
    if (!type) {
        outputCombo_->blockSignals(false);
        return;
    }

    auto names = type->getDeviceNames(false);
    auto* currentDevice = adm.getCurrentAudioDevice();
    juce::String currentName;
    if (currentDevice)
        currentName = currentDevice->getName();

    int selectedIdx = 0;
    for (int i = 0; i < names.size(); ++i) {
        outputCombo_->addItem(QString::fromStdString(names[i].toStdString()));
        if (names[i] == currentName)
            selectedIdx = i;
    }
    outputCombo_->setCurrentIndex(selectedIdx);
    outputCombo_->blockSignals(false);

    populateSampleRates();
    populateBufferSizes();
}

void AudioSettingsDialog::applyOutputDevice()
{
    auto& adm = engine_.deviceManager().deviceManager;
    auto* type = adm.getCurrentDeviceTypeObject();
    if (!type) return;

    auto outputNames = type->getDeviceNames(false);
    int idx = outputCombo_->currentIndex();
    if (idx < 0 || idx >= outputNames.size()) return;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    adm.getAudioDeviceSetup(setup);
    setup.outputDeviceName = outputNames[idx];
    adm.setAudioDeviceSetup(setup, true);

    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    populateSampleRates();
    populateBufferSizes();
}

void AudioSettingsDialog::populateSampleRates()
{
    sampleRateCombo_->blockSignals(true);
    sampleRateCombo_->clear();

    auto& adm = engine_.deviceManager().deviceManager;
    auto* device = adm.getCurrentAudioDevice();
    if (!device) {
        sampleRateCombo_->blockSignals(false);
        return;
    }

    auto rates = device->getAvailableSampleRates();
    double currentRate = device->getCurrentSampleRate();

    int selectedIdx = 0;
    for (int i = 0; i < rates.size(); ++i) {
        sampleRateCombo_->addItem(QString("%1 Hz").arg(static_cast<int>(rates[i])),
                                   QVariant(rates[i]));
        if (std::abs(rates[i] - currentRate) < 1.0)
            selectedIdx = i;
    }
    sampleRateCombo_->setCurrentIndex(selectedIdx);
    sampleRateCombo_->blockSignals(false);
}

void AudioSettingsDialog::populateBufferSizes()
{
    bufferSizeCombo_->blockSignals(true);
    bufferSizeCombo_->clear();

    auto& adm = engine_.deviceManager().deviceManager;
    auto* device = adm.getCurrentAudioDevice();
    if (!device) {
        bufferSizeCombo_->blockSignals(false);
        return;
    }

    auto sizes = device->getAvailableBufferSizes();
    int currentSize = device->getCurrentBufferSizeSamples();
    double sr = device->getCurrentSampleRate();
    if (sr <= 0.0) sr = 44100.0;

    int selectedIdx = 0;
    for (int i = 0; i < sizes.size(); ++i) {
        double latencyMs = (sizes[i] / sr) * 1000.0;
        bufferSizeCombo_->addItem(
            QString("%1 samples (%2 ms)").arg(sizes[i]).arg(latencyMs, 0, 'f', 1),
            QVariant(sizes[i]));
        if (sizes[i] == currentSize)
            selectedIdx = i;
    }
    bufferSizeCombo_->setCurrentIndex(selectedIdx);
    bufferSizeCombo_->blockSignals(false);
}

void AudioSettingsDialog::updateDriverNote()
{
    auto& adm = engine_.deviceManager().deviceManager;
    auto* type = adm.getCurrentDeviceTypeObject();
    if (!type) {
        driverNote_->clear();
        return;
    }

    auto typeName = QString::fromStdString(type->getTypeName().toStdString());
    if (typeName.contains("WASAPI", Qt::CaseInsensitive)) {
        driverNote_->setText(
            "Windows Audio (WASAPI) uses the sample rate configured in Windows "
            "Sound Settings. For more sample rate options, switch to an ASIO driver "
            "or change the rate in Windows Settings > Sound > Device Properties.");
    } else if (typeName.contains("ASIO", Qt::CaseInsensitive)) {
        driverNote_->setText(
            "ASIO drivers provide low-latency access with flexible sample rate "
            "and buffer size options.");
    } else if (typeName.contains("DirectSound", Qt::CaseInsensitive)) {
        driverNote_->setText(
            "DirectSound has higher latency than WASAPI or ASIO. "
            "Consider switching to WASAPI or an ASIO driver for better performance.");
    } else {
        driverNote_->clear();
    }
}

void AudioSettingsDialog::applySettings()
{
    {
        QSettings settings;
        settings.setValue("audio/monitorMode", monitorCombo_->currentData().toInt());
        settings.setValue("audio/recordOffsetMs", recordOffsetSpin_->value());
    }
    engine_.applyInputSettings();

    auto& adm = engine_.deviceManager().deviceManager;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    adm.getAudioDeviceSetup(setup);

    if (sampleRateCombo_->currentIndex() >= 0)
        setup.sampleRate = sampleRateCombo_->currentData().toDouble();

    if (bufferSizeCombo_->currentIndex() >= 0)
        setup.bufferSize = bufferSizeCombo_->currentData().toInt();

    auto error = adm.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) {
        QMessageBox::warning(this, "Audio Settings Error",
            QString("Failed to apply audio settings:\n%1")
                .arg(QString::fromStdString(error.toStdString())));
        return;
    }

    QSettings settings;
    settings.setValue("audio/deviceType", deviceTypeCombo_->currentText());
    settings.setValue("audio/outputDevice", outputCombo_->currentText());
    settings.setValue("audio/sampleRate", sampleRateCombo_->currentData().toDouble());
    settings.setValue("audio/bufferSize", bufferSizeCombo_->currentData().toInt());
}

} // namespace OpenDaw
