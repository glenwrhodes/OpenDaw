#pragma once

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QTimer>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace OpenDaw {

class FileBrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit FileBrowserPanel(juce::AudioDeviceManager& deviceManager,
                              QWidget* parent = nullptr);
    ~FileBrowserPanel() override;

    QSize sizeHint() const override { return {220, 400}; }

signals:
    void fileDoubleClicked(const QString& filePath);

private:
    void navigateTo(const QString& path);
    void setupDragSupport();
    void setupPreviewBar();
    void loadPreview(const QString& filePath);
    void onPlayPauseClicked();
    void onStopClicked();
    void updatePreviewUI();
    bool isAudioFile(const QString& path) const;
    QString formatTime(double seconds) const;

    QVBoxLayout* layout_;
    QComboBox* locationCombo_;
    QLineEdit* pathEdit_;
    QTreeView* treeView_;
    QFileSystemModel* model_;

    // Preview controls
    QWidget* previewBar_;
    QPushButton* playPauseBtn_;
    QPushButton* stopBtn_;
    QSlider* seekSlider_;
    QLabel* timeLabel_;
    QLabel* fileNameLabel_;
    QTimer* uiTimer_;
    bool seekSliderPressed_ = false;

    // JUCE audio preview
    juce::AudioDeviceManager& deviceManager_;
    juce::AudioFormatManager formatManager_;
    juce::AudioTransportSource transportSource_;
    juce::AudioSourcePlayer sourcePlayer_;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;
    QString currentPreviewFile_;
};

} // namespace OpenDaw
