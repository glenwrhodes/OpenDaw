#pragma once

#include "engine/EditManager.h"
#include "ui/controls/LevelMeter.h"
#include "ui/controls/RotaryKnob.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace freedaw {

class TrackHeaderWidget : public QWidget {
    Q_OBJECT

public:
    TrackHeaderWidget(te::AudioTrack* track, EditManager* editMgr,
                      QWidget* parent = nullptr);
    ~TrackHeaderWidget() override;

    te::AudioTrack* track() const { return track_; }
    void setTrackHeight(int h);

signals:
    void effectInsertRequested(te::AudioTrack* track);
    void instrumentSelectRequested(te::AudioTrack* track);

private:
    void updateMeter();
    void applyToggleStyle(QPushButton* btn, const QColor& activeColor);

    te::AudioTrack* track_;
    EditManager* editMgr_;

    QLabel* nameLabel_;
    QPushButton* instrumentBtn_ = nullptr;
    QPushButton* muteBtn_;
    QPushButton* soloBtn_;
    QPushButton* armBtn_;
    QSlider* volumeSlider_;
    RotaryKnob* panKnob_;
    LevelMeter* meter_;
    QTimer meterTimer_;

    te::LevelMeasurer::Client meterClient_;
    te::LevelMeterPlugin* levelMeterPlugin_ = nullptr;
};

} // namespace freedaw
