#pragma once

#include "engine/EditManager.h"
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>

namespace OpenDaw {

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportDialog(QWidget* parent = nullptr);

    ExportSettings settings() const;

    // The loop/punch range and the timeline's time selection, offered as
    // export ranges when they aren't empty.
    void setRanges(tracktion::TimeRange loopRange, tracktion::TimeRange selection);

    void setExporting(bool exporting);
    void setProgress(float progress);
    void setStatus(const QString& text);

    // The export can't be cancelled mid-render; hiding the dialog would leave
    // the UI free to change the Edit the worker thread is rendering.
    void reject() override;

private:
    void onBrowse();
    void onFormatChanged(int index);
    void onWhatChanged();
    void updatePathExtension();
    void updateDitherVisibility();
    bool exportingStems() const;

    QComboBox* whatCombo_ = nullptr;
    QComboBox* rangeCombo_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* patternLabel_ = nullptr;
    QLineEdit* patternEdit_ = nullptr;
    QCheckBox* masterFxCheck_ = nullptr;
    QCheckBox* ditherCheck_ = nullptr;
    tracktion::TimeRange loopRange_;
    tracktion::TimeRange selectionRange_;
    QComboBox* formatCombo_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
    QPushButton* browseBtn_ = nullptr;
    QComboBox* sampleRateCombo_ = nullptr;
    QComboBox* bitDepthCombo_ = nullptr;
    QLabel* bitDepthLabel_ = nullptr;
    QComboBox* oggQualityCombo_ = nullptr;
    QLabel* oggQualityLabel_ = nullptr;
    QComboBox* mp3BitrateCombo_ = nullptr;
    QLabel* mp3BitrateLabel_ = nullptr;
    QCheckBox* normalizeCheck_ = nullptr;
    QPushButton* exportBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    bool exporting_ = false;
    QString statusPrefix_;
};

} // namespace OpenDaw
