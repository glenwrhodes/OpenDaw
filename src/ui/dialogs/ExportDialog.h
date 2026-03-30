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

    void setExporting(bool exporting);
    void setProgress(float progress);

private:
    void onBrowse();
    void onFormatChanged(int index);
    void updatePathExtension();

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
};

} // namespace OpenDaw
