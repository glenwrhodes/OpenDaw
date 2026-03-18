#pragma once

#include "engine/EditManager.h"
#include "ui/controls/LevelMeter.h"
#include "ui/controls/RotaryKnob.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QFrame>
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
    void setSelected(bool sel);
    void refresh();
    bool isSelected() const { return selected_; }
    QSize minimumSizeHint() const override;

signals:
    void trackSelected(te::AudioTrack* track);
    void effectInsertRequested(te::AudioTrack* track);
    void instrumentSelectRequested(te::AudioTrack* track);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void updateMeter();
    void updateSelectionStyle();
    void applyToggleStyle(QPushButton* btn, const QColor& activeColor);
    void updateMonoButtonVisual(bool mono);
    void populateInputCombo();
    void onInputComboChanged(int index);
    void populateOutputCombo();
    void onOutputComboChanged(int index);
    void onArmToggled(bool armed);

    te::AudioTrack* track_;
    EditManager* editMgr_;
    bool selected_ = false;

    QComboBox* inputCombo_ = nullptr;
    QComboBox* outputCombo_ = nullptr;
    QLabel* nameLabel_;
    QPushButton* instrumentBtn_ = nullptr;
    QPushButton* muteBtn_;
    QPushButton* soloBtn_;
    QPushButton* armBtn_;
    QPushButton* monoBtn_ = nullptr;
    QSlider* volumeSlider_;
    RotaryKnob* panKnob_;
    LevelMeter* meter_;
    QFrame* rowSeparator_ = nullptr;
    QTimer meterTimer_;

    te::LevelMeasurer::Client meterClient_;
    te::LevelMeterPlugin* levelMeterPlugin_ = nullptr;
};

} // namespace freedaw
