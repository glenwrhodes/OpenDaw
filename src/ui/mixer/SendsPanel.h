#pragma once

#include "engine/EditManager.h"
#include <QFrame>
#include <QPointer>

class QVBoxLayout;
class QMenu;

namespace OpenDaw {

// Popup listing a track's aux sends: a level slider, pre/post-fader and remove
// for each, plus a button to add a send to a bus (or to a new effects bus).
class SendsPanel : public QFrame {
    Q_OBJECT

public:
    static void showFor(EditManager* editMgr, te::AudioTrack* track,
                        const QPoint& globalPos, QWidget* parent);

    // Menu entries for sending `track` to each eligible bus, plus new effects buses.
    static void fillAddSendMenu(QMenu* menu, EditManager* editMgr, te::AudioTrack* track);

private:
    SendsPanel(EditManager* editMgr, te::AudioTrack* track, QWidget* parent);
    void rebuild();

    EditManager* editMgr_;
    te::AudioTrack* track_;
    uint64_t trackId_;
    QVBoxLayout* rowsLayout_ = nullptr;
};

} // namespace OpenDaw
