#include "LoudnessPanel.h"
#include "engine/LoudnessMeter.h"
#include "utils/ThemeManager.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QSettings>
#include <cmath>

namespace OpenDaw {

namespace {
constexpr double kBarFloor = -40.0;   // LUFS at the bar's left end

QString lufsText(double v)
{
    return v <= -70.0 ? QString::fromUtf8("–") : QString::number(v, 'f', 1);
}
}

// Momentary loudness against the target, -40 to 0 LUFS.
class LoudnessPanel::Bar : public QWidget {
public:
    explicit Bar(QWidget* parent) : QWidget(parent) { setFixedHeight(12); }
    void set(double momentary, double shortTerm, double target)
    {
        momentary_ = momentary;
        shortTerm_ = shortTerm;
        target_ = target;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        auto& theme = ThemeManager::instance().current();
        QPainter p(this);
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(theme.border, 1.0));
        p.setBrush(theme.controlTrack);
        p.drawRoundedRect(r, 2, 2);

        auto xFor = [&](double lufs) {
            return r.left() + r.width() * std::clamp((lufs - kBarFloor) / -kBarFloor, 0.0, 1.0);
        };
        if (momentary_ > kBarFloor) {
            const QColor fill = momentary_ > target_ + 1.0 ? theme.meterRed
                              : momentary_ > target_ - 1.0 ? theme.meterYellow
                                                           : theme.meterGreen;
            p.fillRect(QRectF(r.left() + 1, r.top() + 2, xFor(momentary_) - r.left() - 1,
                              r.height() - 4), fill);
        }
        if (shortTerm_ > kBarFloor) {
            p.setPen(QPen(theme.text, 2.0));
            const double x = xFor(shortTerm_);
            p.drawLine(QPointF(x, r.top() + 1), QPointF(x, r.bottom() - 1));
        }
        p.setPen(QPen(theme.accentLight, 1.0, Qt::DashLine));
        const double tx = xFor(target_);
        p.drawLine(QPointF(tx, r.top()), QPointF(tx, r.bottom()));
    }

private:
    double momentary_ = LoudnessMeter::kSilence;
    double shortTerm_ = LoudnessMeter::kSilence;
    double target_ = -14.0;
};

LoudnessPanel::LoudnessPanel(EditManager* editMgr, QWidget* parent)
    : QWidget(parent), editMgr_(editMgr)
{
    setAccessibleName("Loudness Meter");
    setFixedWidth(158);
    auto& theme = ThemeManager::instance().current();
    setStyleSheet(QString("QLabel { color: %1; background: transparent; }"
                          "QLabel[role=\"caption\"] { color: %2; font-size: 9px; font-weight: bold; "
                          "  letter-spacing: 1px; }"
                          "QLabel[role=\"value\"] { font-family: 'Consolas', 'Cascadia Mono', monospace; "
                          "  font-size: 12px; }")
                      .arg(theme.text.name(), theme.textDim.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    auto caption = [&](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setProperty("role", "caption");
        return l;
    };
    auto value = [&](const QString& name) {
        auto* l = new QLabel(QString::fromUtf8("–"), this);
        l->setProperty("role", "value");
        l->setAccessibleName(name);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };

    layout->addWidget(caption("LOUDNESS"));

    integrated_ = new QLabel(QString::fromUtf8("–"), this);
    integrated_->setAccessibleName("Integrated loudness");
    integrated_->setToolTip("Integrated loudness: the whole of what has played since the "
                            "last reset, gated as in EBU R128 / ITU-R BS.1770");
    integrated_->setStyleSheet(QString("QLabel { color: %1; font-family: 'Consolas', 'Cascadia Mono', "
                                       "monospace; font-size: 26px; font-weight: bold; }")
                                   .arg(theme.lcdText.name()));
    layout->addWidget(integrated_);
    auto* unit = caption("LUFS INTEGRATED");
    layout->addWidget(unit);

    verdict_ = new QLabel(this);
    verdict_->setAccessibleName("Loudness against target");
    verdict_->setWordWrap(true);
    verdict_->setStyleSheet("QLabel { font-size: 10px; }");
    layout->addWidget(verdict_);

    bar_ = new Bar(this);
    bar_->setToolTip("Momentary loudness (bar), short-term (white line) and the target "
                     "(dashed), from -40 to 0 LUFS");
    layout->addWidget(bar_);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(2);
    shortTerm_ = value("Short-term loudness");
    momentary_ = value("Momentary loudness");
    range_ = value("Loudness range");
    truePeak_ = value("True peak");
    duration_ = value("Measured time");
    const struct { const char* text; QLabel* label; const char* tip; } rows[] = {
        { "SHORT-TERM", shortTerm_, "Short-term loudness (last 3 seconds), LUFS" },
        { "MOMENTARY", momentary_, "Momentary loudness (last 400 ms), LUFS" },
        { "RANGE", range_, "Loudness range (LRA), LU: how much the loudness varies" },
        { "TRUE PEAK", truePeak_, "Highest true peak (4x oversampled) since the reset, dBTP. "
                                  "Keep it below -1 dBTP for streaming." },
        { "MEASURED", duration_, "How much audio the integrated value covers" },
    };
    int row = 0;
    for (const auto& r : rows) {
        auto* c = caption(r.text);
        c->setToolTip(r.tip);
        r.label->setToolTip(r.tip);
        grid->addWidget(c, row, 0);
        grid->addWidget(r.label, row, 1);
        ++row;
    }
    layout->addLayout(grid);
    layout->addSpacing(4);

    layout->addWidget(caption("TARGET"));
    targetCombo_ = new QComboBox(this);
    targetCombo_->setAccessibleName("Loudness target");
    targetCombo_->addItem("Streaming  -14", -14.0);
    targetCombo_->addItem("Apple Music  -16", -16.0);
    targetCombo_->addItem("Podcast  -16", -16.0);
    targetCombo_->addItem("Broadcast  -23", -23.0);
    targetCombo_->addItem("Film / TV  -27", -27.0);
    targetCombo_->addItem("Loud master  -9", -9.0);
    targetCombo_->setToolTip("Loudness the finished mix should reach on its platform, in LUFS");
    QSettings settings;
    targetCombo_->setCurrentIndex(std::clamp(settings.value("loudness/target", 0).toInt(), 0,
                                             targetCombo_->count() - 1));
    connect(targetCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        QSettings().setValue("loudness/target", idx);
        poll();
    });
    layout->addWidget(targetCombo_);

    autoReset_ = new QCheckBox("Reset on play", this);
    autoReset_->setAccessibleName("Reset loudness when playback starts");
    autoReset_->setToolTip("Start a new measurement each time playback starts, so the "
                           "integrated value covers one pass");
    autoReset_->setChecked(settings.value("loudness/resetOnPlay", true).toBool());
    connect(autoReset_, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue("loudness/resetOnPlay", on);
    });
    layout->addWidget(autoReset_);

    auto* resetBtn = new QPushButton("Reset", this);
    resetBtn->setAccessibleName("Reset loudness measurement");
    connect(resetBtn, &QPushButton::clicked, this, &LoudnessPanel::resetMeasurement);
    layout->addWidget(resetBtn);
    layout->addStretch();

    connect(&timer_, &QTimer::timeout, this, &LoudnessPanel::poll);
}

void LoudnessPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    timer_.start(100);
}

void LoudnessPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    timer_.stop();
}

double LoudnessPanel::target() const
{
    return targetCombo_->currentData().toDouble();
}

void LoudnessPanel::resetMeasurement()
{
    if (auto* tap = editMgr_->audioEngine().loudnessTap())
        tap->requestReset();
    poll();
}

void LoudnessPanel::poll()
{
    auto* tap = editMgr_->audioEngine().loudnessTap();
    if (!tap || !editMgr_->edit()) return;

    const bool playing = editMgr_->transport().isPlaying();
    if (playing && !wasPlaying_ && autoReset_->isChecked())
        tap->requestReset();
    wasPlaying_ = playing;

    const auto r = tap->readings();
    auto& theme = ThemeManager::instance().current();

    integrated_->setText(lufsText(r.integrated));
    shortTerm_->setText(lufsText(r.shortTerm));
    momentary_->setText(lufsText(r.momentary));
    range_->setText(r.seconds >= 3.0 ? QString::number(r.loudnessRange, 'f', 1) + " LU"
                                     : QString::fromUtf8("–"));
    truePeak_->setText(r.truePeakDb <= -70.0 ? QString::fromUtf8("–")
                                             : QString::number(r.truePeakDb, 'f', 1));
    truePeak_->setStyleSheet(r.truePeakDb > -1.0 ? QString("QLabel { color: %1; font-weight: bold; }")
                                                       .arg(theme.meterRed.name())
                                                 : QString());
    const int secs = int(r.seconds);
    duration_->setText(QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0')));
    bar_->set(r.momentary, r.shortTerm, target());

    if (r.integrated <= -70.0) {
        verdict_->setText(QString("Play the song to measure it against %1 LUFS")
                              .arg(target(), 0, 'f', 0));
        verdict_->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }")
                                    .arg(theme.textDim.name()));
        return;
    }
    const double diff = r.integrated - target();
    QString text;
    QColor colour;
    if (std::abs(diff) <= 1.0) {
        text = "On target";
        colour = theme.meterGreen;
    } else if (diff > 0) {
        text = QString("%1 LU louder than the target").arg(diff, 0, 'f', 1);
        colour = theme.meterYellow;
    } else {
        text = QString("%1 LU quieter than the target").arg(-diff, 0, 'f', 1);
        colour = theme.meterYellow;
    }
    verdict_->setText(text);
    verdict_->setStyleSheet(QString("QLabel { color: %1; font-size: 10px; }").arg(colour.name()));
}

} // namespace OpenDaw
