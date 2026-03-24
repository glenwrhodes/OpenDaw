#pragma once

#include "engine/EditManager.h"
#include "ui/controls/LevelMeter.h"
#include "ui/controls/RotaryKnob.h"
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QFrame>
#include <QTimer>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace OpenDaw {

class TrackHeaderWidget : public QWidget {
    Q_OBJECT

public:
    TrackHeaderWidget(te::AudioTrack* track, EditManager* editMgr,
                      QWidget* parent = nullptr);
    ~TrackHeaderWidget() override;

    te::AudioTrack* track() const { return track_; }
    void setTrackHeight(int h);
    void setSelected(bool sel);
    void setAutomationVisible(bool visible);
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return collapsed_; }
    void setIndented(bool indented);
    void refresh();
    bool isSelected() const { return selected_; }
    QSize minimumSizeHint() const override;

signals:
    void trackSelected(te::AudioTrack* track);
    void effectInsertRequested(te::AudioTrack* track);
    void instrumentSelectRequested(te::AudioTrack* track);
    void automationToggled(te::AudioTrack* track, bool visible);
    void collapseToggled(te::AudioTrack* track, bool collapsed);
    void dragStarted(TrackHeaderWidget* header);
    void dragMoved(TrackHeaderWidget* header, int globalY);
    void dragFinished(TrackHeaderWidget* header);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void updateMeter();
    void updateSelectionStyle();
    void applyToggleStyle(QPushButton* btn, const QColor& activeColor);
    void updateMonoButtonVisual(bool mono);
    void updateFreezeButtonVisual(bool frozen);
    void populateInputCombo();
    void onInputComboChanged(int index);
    void populateOutputCombo();
    void onOutputComboChanged(int index);
    void onArmToggled(bool armed);
    void startRenameEdit();

    te::AudioTrack* track_;
    EditManager* editMgr_;
    bool selected_ = false;
    bool collapsed_ = false;

    QComboBox* inputCombo_ = nullptr;
    QComboBox* outputCombo_ = nullptr;
    QLabel* nameLabel_;
    QPushButton* instrumentBtn_ = nullptr;
    QPushButton* muteBtn_;
    QPushButton* soloBtn_;
    QPushButton* armBtn_;
    QPushButton* monoBtn_ = nullptr;
    QPushButton* freezeBtn_ = nullptr;
    QSlider* volumeSlider_;
    RotaryKnob* panKnob_;
    LevelMeter* meter_;
    QPushButton* collapseBtn_ = nullptr;
    QPushButton* automationBtn_ = nullptr;
    QFrame* rowSeparator_ = nullptr;
    QTimer meterTimer_;
    bool automationVisible_ = false;
    QWidget* gripHandle_ = nullptr;
    QPoint dragStartPos_;
    bool draggingToReorder_ = false;
    te::LevelMeasurer::Client meterClient_;
    te::LevelMeterPlugin* levelMeterPlugin_ = nullptr;
    double lastTrackedPlayheadSecs_ = -1.0;
};

} // namespace OpenDaw
