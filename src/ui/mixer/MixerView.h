#pragma once

#include "ChannelStrip.h"
#include "engine/EditManager.h"
#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QEvent>
#include <vector>

namespace freedaw {

class MixerView : public QWidget {
    Q_OBJECT

public:
    explicit MixerView(EditManager* editMgr, QWidget* parent = nullptr);

    QSize sizeHint() const override { return {800, 320}; }

public slots:
    void rebuildStrips();
    void setSelectedTrack(te::AudioTrack* track);

signals:
    void effectInsertRequested(te::AudioTrack* track, int slotIndex);
    void instrumentSelectRequested(te::AudioTrack* track);
    void trackSelected(te::AudioTrack* track);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    EditManager* editMgr_;
    QScrollArea* scrollArea_;
    QWidget* stripContainer_;
    QHBoxLayout* stripLayout_;
    ChannelStrip* masterStrip_ = nullptr;
    std::vector<ChannelStrip*> strips_;
    te::AudioTrack* selectedTrack_ = nullptr;
};

} // namespace freedaw
