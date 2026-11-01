#include "ExportDialog.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QDir>
#include <QSettings>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QStandardItemModel>

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

    whatCombo_ = new QComboBox(this);
    whatCombo_->setAccessibleName("What to Export");
    whatCombo_->addItem("Mix (one file)", int(StemMode::None));
    whatCombo_->addItem("Stems: a file per track", int(StemMode::Tracks));
    whatCombo_->addItem("Stems: a file per bus", int(StemMode::Groups));
    whatCombo_->setToolTip(
        "Stems are each track's (or each bus's) share of the mix, including the buses and "
        "effect returns it goes through, so they add up to the mix. With \"per bus\", tracks "
        "routed into a bus share one file and tracks going straight to Master get their own.");
    form->addRow("Export:", whatCombo_);

    rangeCombo_ = new QComboBox(this);
    rangeCombo_->setAccessibleName("Export Range");
    rangeCombo_->addItem("Whole song", 0);
    rangeCombo_->addItem("Loop / punch range", 1);
    rangeCombo_->addItem("Time selection", 2);
    form->addRow("Range:", rangeCombo_);

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

    pathLabel_ = new QLabel("Output File:", this);
    form->addRow(pathLabel_, pathRow);

    patternLabel_ = new QLabel("File Names:", this);
    patternEdit_ = new QLineEdit(this);
    patternEdit_->setAccessibleName("Stem File Name Pattern");
    patternEdit_->setToolTip("How stem files are named: {project} is the project's name, "
                             "{name} the track or bus, {n} its number");
    form->addRow(patternLabel_, patternEdit_);

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

    ditherCheck_ = new QCheckBox("Dither", this);
    ditherCheck_->setAccessibleName("Dither 16-bit Output");
    ditherCheck_->setToolTip("Adds a trace of noise when reducing to 16 bits, so quiet passages "
                             "and fades don't turn into distortion");
    form->addRow("", ditherCheck_);

    masterFxCheck_ = new QCheckBox("Include Master effects", this);
    masterFxCheck_->setAccessibleName("Include Master Effects");
    masterFxCheck_->setToolTip("Run the export through Master's effects and fader. Usually on "
                               "for a mix and off for stems, which the mastering engineer "
                               "processes again.");
    form->addRow("", masterFxCheck_);

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
        exportBtn_->setEnabled(!exporting_ && !text.trimmed().isEmpty());
    });

    connect(formatCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onFormatChanged);
    connect(bitDepthCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateDitherVisibility(); });
    connect(whatCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onWhatChanged(); });

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
    ditherCheck_->setChecked(qs.value("export/dither", true).toBool());
    patternEdit_->setText(qs.value("export/stemPattern", "{project} - {name}").toString());
    {
        QSignalBlocker block(whatCombo_);
        const int whatIdx = whatCombo_->findData(qs.value("export/what", 0));
        whatCombo_->setCurrentIndex(whatIdx >= 0 ? whatIdx : 0);
    }
    setRanges({}, {});

    onFormatChanged(formatCombo_->currentIndex());
    onWhatChanged();
}

bool ExportDialog::exportingStems() const
{
    return static_cast<StemMode>(whatCombo_->currentData().toInt()) != StemMode::None;
}

void ExportDialog::onWhatChanged()
{
    const bool stems = exportingStems();
    pathLabel_->setText(stems ? "Folder:" : "Output File:");
    pathEdit_->setPlaceholderText(stems ? "Select a folder for the stems..." : "Select output file...");
    patternLabel_->setVisible(stems);
    patternEdit_->setVisible(stems);
    // Master processing belongs on the mix; stems usually go without it.
    masterFxCheck_->setChecked(!stems);
    // The path is a file for a mix and a folder for stems.
    QString path = pathEdit_->text().trimmed();
    if (!path.isEmpty()) {
        QFileInfo fi(path);
        if (stems && !fi.suffix().isEmpty())
            pathEdit_->setText(fi.absolutePath());
        else if (!stems && fi.suffix().isEmpty())
            pathEdit_->setText(QDir(path).filePath("mix.wav"));
        updatePathExtension();
    }
}

void ExportDialog::setRanges(tracktion::TimeRange loopRange, tracktion::TimeRange selection)
{
    loopRange_ = loopRange;
    selectionRange_ = selection;
    auto describe = [](tracktion::TimeRange r) {
        auto mmss = [](double s) {
            return QString("%1:%2").arg(int(s) / 60).arg(s - 60 * (int(s) / 60), 4, 'f', 1, QChar('0'));
        };
        return QString(" (%1 to %2)").arg(mmss(r.getStart().inSeconds()), mmss(r.getEnd().inSeconds()));
    };
    auto* model = qobject_cast<QStandardItemModel*>(rangeCombo_->model());
    const bool hasLoop = loopRange.getLength().inSeconds() > 0.0;
    const bool hasSel = selection.getLength().inSeconds() > 0.0;
    rangeCombo_->setItemText(1, "Loop / punch range" + (hasLoop ? describe(loopRange) : QString()));
    rangeCombo_->setItemText(2, "Time selection" + (hasSel ? describe(selection) : QString()));
    if (model) {
        model->item(1)->setEnabled(hasLoop);
        model->item(2)->setEnabled(hasSel);
    }
    // A fresh time selection is most likely what to export.
    rangeCombo_->setCurrentIndex(hasSel ? 2 : 0);
}

void ExportDialog::updateDitherVisibility()
{
    const auto fmt = static_cast<ExportFormat>(formatCombo_->currentData().toInt());
    const bool pcm = fmt == ExportFormat::WAV || fmt == ExportFormat::FLAC;
    ditherCheck_->setVisible(pcm && bitDepthCombo_->currentData().toInt() == 16);
}

void ExportDialog::setStatus(const QString& text)
{
    statusPrefix_ = text;
    statusLabel_->setText(text);
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
    updateDitherVisibility();
}

void ExportDialog::updatePathExtension()
{
    QString path = pathEdit_->text().trimmed();
    if (path.isEmpty() || exportingStems()) return;

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
    s.stems = static_cast<StemMode>(whatCombo_->currentData().toInt());
    s.stemPattern = patternEdit_->text().trimmed().isEmpty() ? QString("{project} - {name}")
                                                              : patternEdit_->text().trimmed();
    s.dither = ditherCheck_->isChecked();
    s.useMasterPlugins = masterFxCheck_->isChecked();
    switch (rangeCombo_->currentData().toInt()) {
        case 1: s.range = loopRange_; break;
        case 2: s.range = selectionRange_; break;
        default: break;
    }

    QSettings qs;
    qs.setValue("export/what", int(s.stems));
    qs.setValue("export/dither", s.dither);
    qs.setValue("export/stemPattern", s.stemPattern);
    qs.setValue("export/format", static_cast<int>(s.format));
    qs.setValue("export/sampleRate", s.sampleRate);
    qs.setValue("export/bitDepth", s.bitDepth);
    qs.setValue("export/normalize", s.normalize);
    qs.setValue("export/oggQuality", s.oggQuality);
    qs.setValue("export/mp3Bitrate", s.mp3Bitrate);

    return s;
}

void ExportDialog::reject()
{
    if (!exporting_)
        QDialog::reject();
}

void ExportDialog::setExporting(bool exporting)
{
    exporting_ = exporting;
    cancelBtn_->setEnabled(!exporting);
    exportBtn_->setEnabled(!exporting);
    browseBtn_->setEnabled(!exporting);
    pathEdit_->setEnabled(!exporting);
    formatCombo_->setEnabled(!exporting);
    sampleRateCombo_->setEnabled(!exporting);
    bitDepthCombo_->setEnabled(!exporting);
    oggQualityCombo_->setEnabled(!exporting);
    mp3BitrateCombo_->setEnabled(!exporting);
    normalizeCheck_->setEnabled(!exporting);
    whatCombo_->setEnabled(!exporting);
    rangeCombo_->setEnabled(!exporting);
    patternEdit_->setEnabled(!exporting);
    ditherCheck_->setEnabled(!exporting);
    masterFxCheck_->setEnabled(!exporting);
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
    statusLabel_->setText(QString("%1 %2%").arg(statusPrefix_.isEmpty() ? QString("Exporting...")
                                                                         : statusPrefix_)
                              .arg(pct));
}

void ExportDialog::onBrowse()
{
    QSettings settings;
    QString startDir = settings.value("paths/lastExportDir",
                                      QDir::homePath()).toString();
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = QDir::homePath();

    if (exportingStems()) {
        const QString dir = QFileDialog::getExistingDirectory(this, "Folder for the Stems", startDir);
        if (dir.isEmpty()) return;
        settings.setValue("paths/lastExportDir", dir);
        pathEdit_->setText(dir);
        return;
    }

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
