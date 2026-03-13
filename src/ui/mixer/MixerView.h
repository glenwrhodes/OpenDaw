#pragma once

#include "ChannelStrip.h"
#include "engine/EditManager.h"
#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <vector>

namespace freedaw {

class MixerView : public QWidget {
    Q_OBJECT

public:
    explicit MixerView(EditManager* editMgr, QWidget* parent = nullptr);

    QSize sizeHint() const override { return {800, 320}; }

public slots:
    void rebuildStrips();

signals:
    void effectInsertRequested(te::AudioTrack* track, int slotIndex);
    void instrumentSelectRequested(te::AudioTrack* track);

private:
    EditManager* editMgr_;
    QScrollArea* scrollArea_;
    QWidget* stripContainer_;
    QHBoxLayout* stripLayout_;
    ChannelStrip* masterStrip_ = nullptr;
    std::vector<ChannelStrip*> strips_;
};

} // namespace freedaw
