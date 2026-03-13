#pragma once

#include "engine/EditManager.h"
#include "ui/controls/VolumeFader.h"
#include "ui/controls/RotaryKnob.h"
#include "ui/controls/LevelMeter.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <tracktion_engine/tracktion_engine.h>

namespace freedaw {

class ChannelStrip : public QWidget {
    Q_OBJECT

public:
    ChannelStrip(te::AudioTrack* track, EditManager* editMgr,
                 QWidget* parent = nullptr);
    ~ChannelStrip() override;

    static ChannelStrip* createMasterStrip(EditManager* editMgr,
                                           QWidget* parent = nullptr);

    QSize sizeHint() const override { return {88, 400}; }

    te::AudioTrack* track() const { return track_; }
    void refresh();

signals:
    void effectInsertRequested(te::AudioTrack* track, int slotIndex);
    void instrumentSelectRequested(te::AudioTrack* track);

private:
    void setupUI();
    void setupMasterUI();
    void updateMeter();
    void applyToggleStyle(QPushButton* btn, const QColor& activeColor);

    te::AudioTrack* track_ = nullptr;
    EditManager* editMgr_;
    bool isMaster_ = false;

    QLabel* nameLabel_ = nullptr;
    QPushButton* instrumentBtn_ = nullptr;
    VolumeFader* fader_ = nullptr;
    RotaryKnob* panKnob_ = nullptr;
    LevelMeter* meter_ = nullptr;
    QPushButton* muteBtn_ = nullptr;
    QPushButton* soloBtn_ = nullptr;
    QPushButton* armBtn_ = nullptr;
    QComboBox* fxSlot1_ = nullptr;
    QComboBox* fxSlot2_ = nullptr;

    QTimer meterTimer_;
    te::LevelMeasurer::Client meterClient_;
    te::LevelMeterPlugin* levelMeterPlugin_ = nullptr;
};

} // namespace freedaw
