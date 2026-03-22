#pragma once

#include "engine/EditManager.h"
#include "ui/controls/VolumeFader.h"
#include "ui/controls/RotaryKnob.h"
#include "ui/controls/LevelMeter.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QPixmap>
#include <tracktion_engine/tracktion_engine.h>

namespace OpenDaw {

class ChannelStrip : public QWidget {
    Q_OBJECT

public:
    ChannelStrip(te::AudioTrack* track, EditManager* editMgr,
                 QWidget* parent = nullptr);
    ~ChannelStrip() override;

    static ChannelStrip* createMasterStrip(EditManager* editMgr,
                                           QWidget* parent = nullptr);

    QSize sizeHint() const override { return {92, 400}; }

    te::AudioTrack* track() const { return track_; }
    void refresh();
    void setSelected(bool selected);
    bool isSelected() const { return selected_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void instrumentSelectRequested(te::AudioTrack* track);

private:
    void setupUI();
    void setupMasterUI();
    void updateMeter();
    void reconnectLevelMeterSource();
    void updateSelectionStyle();
    void applyToggleStyle(QPushButton* btn, const QColor& activeColor);
    void updateMonoButtonVisual(bool mono);
    void updateAutoModeButton();
    void updateVolumeLabel();
    void startVolumeEdit();
    void populateInputCombo();
    void onInputComboChanged(int index);
    void populateOutputCombo();
    void onOutputComboChanged(int index);
    void onArmToggled(bool armed);
    void onMidiChannelChanged(int index);
    void startRenameEdit();

    te::AudioTrack* track_ = nullptr;
    uint64_t trackId_ = 0;
    EditManager* editMgr_;
    bool isMaster_ = false;
    bool selected_ = false;

    QComboBox* inputCombo_ = nullptr;
    QComboBox* outputCombo_ = nullptr;
    QComboBox* midiChannelCombo_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QPushButton* instrumentBtn_ = nullptr;
    VolumeFader* fader_ = nullptr;
    RotaryKnob* panKnob_ = nullptr;
    LevelMeter* meter_ = nullptr;
    QPushButton* muteBtn_ = nullptr;
    QPushButton* soloBtn_ = nullptr;
    QPushButton* armBtn_ = nullptr;
    QPushButton* monoBtn_ = nullptr;
    QPushButton* autoModeBtn_ = nullptr;
    QLabel* volumeLabel_ = nullptr;
    QLabel* frozenLabel_ = nullptr;

    QTimer meterTimer_;
    te::LevelMeasurer::Client meterClient_;
    te::LevelMeterPlugin* levelMeterPlugin_ = nullptr;
    double lastTrackedPlayheadSecs_ = -1.0;

    QPixmap stripBgPixmap_;
    QPixmap stripBgScaled_;
};

} // namespace OpenDaw
