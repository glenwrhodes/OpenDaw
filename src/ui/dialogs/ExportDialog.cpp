#include "ExportDialog.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QDir>
#include <QSettings>
#include <QFileInfo>

namespace OpenDaw {

ExportDialog::ExportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Export Audio");
    setAccessibleName("Export Audio Dialog");
    setMinimumWidth(480);

    auto& theme = ThemeManager::instance().current();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    auto* titleLabel = new QLabel("Export Audio", this);
    titleLabel->setAccessibleName("Export Dialog Title");
    titleLabel->setStyleSheet(
        QString("font-size: 14px; font-weight: bold; color: %1;")
            .arg(theme.text.name()));
    mainLayout->addWidget(titleLabel);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    formatCombo_ = new QComboBox(this);
    formatCombo_->setAccessibleName("Export Format");
    formatCombo_->addItem("WAV (Uncompressed)", static_cast<int>(ExportFormat::WAV));
    formatCombo_->addItem("FLAC (Lossless)", static_cast<int>(ExportFormat::FLAC));
    formatCombo_->addItem("OGG Vorbis (Lossy)", static_cast<int>(ExportFormat::OGG));
    formatCombo_->addItem("MP3 (Lossy)", static_cast<int>(ExportFormat::MP3));
    form->addRow("Format:", formatCombo_);

    auto* pathRow = new QHBoxLayout();
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setAccessibleName("Export File Path");
    pathEdit_->setPlaceholderText("Select output file...");
    pathEdit_->setReadOnly(false);
    pathRow->addWidget(pathEdit_, 1);

    browseBtn_ = new QPushButton("Browse...", this);
    browseBtn_->setAccessibleName("Browse for Export Path");
    connect(browseBtn_, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    pathRow->addWidget(browseBtn_);

    form->addRow("Output File:", pathRow);

    sampleRateCombo_ = new QComboBox(this);
    sampleRateCombo_->setAccessibleName("Sample Rate");
    sampleRateCombo_->addItem("44100 Hz", 44100);
    sampleRateCombo_->addItem("48000 Hz", 48000);
    sampleRateCombo_->addItem("88200 Hz", 88200);
    sampleRateCombo_->addItem("96000 Hz", 96000);
    sampleRateCombo_->setCurrentIndex(0);
    form->addRow("Sample Rate:", sampleRateCombo_);

    bitDepthLabel_ = new QLabel("Bit Depth:", this);
    bitDepthCombo_ = new QComboBox(this);
    bitDepthCombo_->setAccessibleName("Bit Depth");
    bitDepthCombo_->addItem("16-bit", 16);
    bitDepthCombo_->addItem("24-bit", 24);
    bitDepthCombo_->addItem("32-bit (float)", 32);
    bitDepthCombo_->setCurrentIndex(1);
    form->addRow(bitDepthLabel_, bitDepthCombo_);

    oggQualityLabel_ = new QLabel("Quality:", this);
    oggQualityCombo_ = new QComboBox(this);
    oggQualityCombo_->setAccessibleName("OGG Vorbis Quality");
    for (int i = 0; i <= 10; ++i) {
        QString label;
        if (i == 0)       label = "0 (Lowest)";
        else if (i == 5)  label = "5 (Default)";
        else if (i == 10) label = "10 (Highest)";
        else              label = QString::number(i);
        oggQualityCombo_->addItem(label, i);
    }
    oggQualityCombo_->setCurrentIndex(5);
    form->addRow(oggQualityLabel_, oggQualityCombo_);
    oggQualityLabel_->setVisible(false);
    oggQualityCombo_->setVisible(false);

    mp3BitrateLabel_ = new QLabel("Bitrate:", this);
    mp3BitrateCombo_ = new QComboBox(this);
    mp3BitrateCombo_->setAccessibleName("MP3 Bitrate");
    mp3BitrateCombo_->addItem("128 kbps", 128);
    mp3BitrateCombo_->addItem("192 kbps", 192);
    mp3BitrateCombo_->addItem("256 kbps", 256);
    mp3BitrateCombo_->addItem("320 kbps", 320);
    mp3BitrateCombo_->setCurrentIndex(3);
    form->addRow(mp3BitrateLabel_, mp3BitrateCombo_);
    mp3BitrateLabel_->setVisible(false);
    mp3BitrateCombo_->setVisible(false);

    normalizeCheck_ = new QCheckBox("Normalize output", this);
    normalizeCheck_->setAccessibleName("Normalize Output");
    form->addRow("", normalizeCheck_);

    mainLayout->addLayout(form);

    progressBar_ = new QProgressBar(this);
    progressBar_->setAccessibleName("Export Progress");
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    progressBar_->setVisible(false);
    mainLayout->addWidget(progressBar_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setAccessibleName("Export Status");
    statusLabel_->setStyleSheet(
        QString("color: %1; font-size: 11px;").arg(theme.textDim.name()));
    statusLabel_->setVisible(false);
    mainLayout->addWidget(statusLabel_);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();

    cancelBtn_ = new QPushButton("Cancel", this);
    cancelBtn_->setAccessibleName("Cancel Export");
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(cancelBtn_);

    exportBtn_ = new QPushButton("Export", this);
    exportBtn_->setAccessibleName("Start Export");
    exportBtn_->setDefault(true);
    exportBtn_->setStyleSheet(
        QString("QPushButton { background: %1; color: #fff; font-weight: bold; "
                "padding: 6px 20px; border-radius: 4px; }"
                "QPushButton:hover { background: %2; }"
                "QPushButton:disabled { background: %3; color: %4; }")
            .arg(theme.accent.name(), theme.accentLight.name(),
                 theme.border.name(), theme.textDim.name()));
    exportBtn_->setEnabled(false);
    connect(exportBtn_, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(exportBtn_);

    connect(pathEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        exportBtn_->setEnabled(!text.trimmed().isEmpty());
    });

    connect(formatCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onFormatChanged);

    mainLayout->addLayout(buttonRow);

    QSettings qs;
    int fmtIdx = formatCombo_->findData(qs.value("export/format", 0));
    if (fmtIdx >= 0) formatCombo_->setCurrentIndex(fmtIdx);
    int srIdx = sampleRateCombo_->findData(qs.value("export/sampleRate", 44100));
    if (srIdx >= 0) sampleRateCombo_->setCurrentIndex(srIdx);
    int bdIdx = bitDepthCombo_->findData(qs.value("export/bitDepth", 24));
    if (bdIdx >= 0) bitDepthCombo_->setCurrentIndex(bdIdx);
    int oqIdx = oggQualityCombo_->findData(qs.value("export/oggQuality", 5));
    if (oqIdx >= 0) oggQualityCombo_->setCurrentIndex(oqIdx);
    int mbIdx = mp3BitrateCombo_->findData(qs.value("export/mp3Bitrate", 320));
    if (mbIdx >= 0) mp3BitrateCombo_->setCurrentIndex(mbIdx);
    normalizeCheck_->setChecked(qs.value("export/normalize", false).toBool());

    onFormatChanged(formatCombo_->currentIndex());
}

void ExportDialog::onFormatChanged(int)
{
    auto fmt = static_cast<ExportFormat>(formatCombo_->currentData().toInt());
    bool isOgg = (fmt == ExportFormat::OGG);
    bool isMp3 = (fmt == ExportFormat::MP3);
    bool isFlac = (fmt == ExportFormat::FLAC);

    bitDepthLabel_->setVisible(!isOgg && !isMp3);
    bitDepthCombo_->setVisible(!isOgg && !isMp3);
    oggQualityLabel_->setVisible(isOgg);
    oggQualityCombo_->setVisible(isOgg);
    mp3BitrateLabel_->setVisible(isMp3);
    mp3BitrateCombo_->setVisible(isMp3);

    if (isFlac) {
        int prevBitDepth = bitDepthCombo_->currentData().toInt();
        bitDepthCombo_->clear();
        bitDepthCombo_->addItem("16-bit", 16);
        bitDepthCombo_->addItem("24-bit", 24);
        int idx = bitDepthCombo_->findData(prevBitDepth);
        bitDepthCombo_->setCurrentIndex(idx >= 0 ? idx : 1);
    } else if (!isOgg && !isMp3) {
        int prevBitDepth = bitDepthCombo_->currentData().toInt();
        bitDepthCombo_->clear();
        bitDepthCombo_->addItem("16-bit", 16);
        bitDepthCombo_->addItem("24-bit", 24);
        bitDepthCombo_->addItem("32-bit (float)", 32);
        int idx = bitDepthCombo_->findData(prevBitDepth);
        bitDepthCombo_->setCurrentIndex(idx >= 0 ? idx : 1);
    }

    updatePathExtension();
}

void ExportDialog::updatePathExtension()
{
    QString path = pathEdit_->text().trimmed();
    if (path.isEmpty()) return;

    auto fmt = static_cast<ExportFormat>(formatCombo_->currentData().toInt());
    QString newExt;
    switch (fmt) {
        case ExportFormat::FLAC: newExt = ".flac"; break;
        case ExportFormat::OGG:  newExt = ".ogg"; break;
        case ExportFormat::MP3:  newExt = ".mp3"; break;
        default:                 newExt = ".wav"; break;
    }

    QStringList exts = {".wav", ".flac", ".ogg", ".mp3"};
    for (const auto& ext : exts) {
        if (path.endsWith(ext, Qt::CaseInsensitive)) {
            path = path.left(path.length() - ext.length()) + newExt;
            pathEdit_->setText(path);
            return;
        }
    }
}

ExportSettings ExportDialog::settings() const
{
    ExportSettings s;
    s.destFile = juce::File(juce::String(pathEdit_->text().toUtf8().constData()));
    s.sampleRate = sampleRateCombo_->currentData().toDouble();
    s.format = static_cast<ExportFormat>(formatCombo_->currentData().toInt());
    s.bitDepth = bitDepthCombo_->currentData().toInt();
    s.normalize = normalizeCheck_->isChecked();
    s.oggQuality = oggQualityCombo_->currentData().toInt();
    s.mp3Bitrate = mp3BitrateCombo_->currentData().toInt();

    QSettings qs;
    qs.setValue("export/format", static_cast<int>(s.format));
    qs.setValue("export/sampleRate", s.sampleRate);
    qs.setValue("export/bitDepth", s.bitDepth);
    qs.setValue("export/normalize", s.normalize);
    qs.setValue("export/oggQuality", s.oggQuality);
    qs.setValue("export/mp3Bitrate", s.mp3Bitrate);

    return s;
}

void ExportDialog::setExporting(bool exporting)
{
    exportBtn_->setEnabled(!exporting);
    browseBtn_->setEnabled(!exporting);
    formatCombo_->setEnabled(!exporting);
    sampleRateCombo_->setEnabled(!exporting);
    bitDepthCombo_->setEnabled(!exporting);
    oggQualityCombo_->setEnabled(!exporting);
    mp3BitrateCombo_->setEnabled(!exporting);
    normalizeCheck_->setEnabled(!exporting);
    progressBar_->setVisible(exporting);
    statusLabel_->setVisible(exporting);

    if (exporting) {
        progressBar_->setValue(0);
        statusLabel_->setText("Exporting...");
        cancelBtn_->setText("Cancel");
    } else {
        cancelBtn_->setText("Close");
    }
}

void ExportDialog::setProgress(float progress)
{
    progressBar_->setValue(static_cast<int>(progress * 1000.0f));
    int pct = static_cast<int>(progress * 100.0f);
    statusLabel_->setText(QString("Exporting... %1%").arg(pct));
}

void ExportDialog::onBrowse()
{
    QSettings settings;
    QString startDir = settings.value("paths/lastExportDir",
                                      QDir::homePath()).toString();
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = QDir::homePath();

    auto fmt = static_cast<ExportFormat>(formatCombo_->currentData().toInt());
    QString filter;
    QString defaultName;
    switch (fmt) {
        case ExportFormat::FLAC:
            filter = "FLAC Audio (*.flac)";
            defaultName = "/mix.flac";
            break;
        case ExportFormat::OGG:
            filter = "OGG Vorbis Audio (*.ogg)";
            defaultName = "/mix.ogg";
            break;
        case ExportFormat::MP3:
            filter = "MP3 Audio (*.mp3)";
            defaultName = "/mix.mp3";
            break;
        default:
            filter = "WAV Audio (*.wav)";
            defaultName = "/mix.wav";
            break;
    }

    QString path = QFileDialog::getSaveFileName(
        this, "Export Audio", startDir + defaultName, filter);
    if (path.isEmpty()) return;

    QString ext;
    switch (fmt) {
        case ExportFormat::FLAC: ext = ".flac"; break;
        case ExportFormat::OGG:  ext = ".ogg"; break;
        case ExportFormat::MP3:  ext = ".mp3"; break;
        default:                 ext = ".wav"; break;
    }
    if (!path.endsWith(ext, Qt::CaseInsensitive))
        path += ext;

    settings.setValue("paths/lastExportDir", QFileInfo(path).absolutePath());
    pathEdit_->setText(path);
}

} // namespace OpenDaw
