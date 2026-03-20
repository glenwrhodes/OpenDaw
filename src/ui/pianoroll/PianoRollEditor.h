#pragma once

#include "PianoKeyboard.h"
#include "NoteGrid.h"
#include "VelocityLane.h"
#include "CcLane.h"
#include "PianoRollRuler.h"
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSplitter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>
#include <set>

namespace te = tracktion::engine;

namespace freedaw {

class EditManager;

class PianoRollEditor : public QWidget {
    Q_OBJECT

public:
    explicit PianoRollEditor(QWidget* parent = nullptr);

    void setClip(te::MidiClip* clip, EditManager* editMgr = nullptr);
    void refresh();
    te::MidiClip* clip() const { return clip_; }

signals:
    void notesChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QPushButton* makeIconButton(QWidget* parent, const QFont& font,
                                const QChar& glyph, const QString& tooltip,
                                const QString& accessible, int size = 24);

    void syncKeyboardScroll();
    void syncLaneScroll();
    void onSnapModeChanged(int index);
    void onDrawModeToggled(bool checked);
    void onEditModeToggled(bool checked);
    void onNotesChanged();
    void onCcComboChanged(int index);
    void onChannelComboChanged(int index);
    void setCcLaneVisible(bool visible);
    void updateCcHeaderLabel();
    QString ccDisplayName() const;

    void gatherLinkedClips();
    void rebuildChannelPanel();
    void setActiveChannel(int channelNumber);
    te::MidiClip* ensureClipForActiveChannel();
    void passClipsToChildren();

    te::MidiClip* clip_ = nullptr;
    EditManager* editMgr_ = nullptr;
    te::AudioTrack* track_ = nullptr;
    std::vector<te::MidiClip*> linkedClips_;
    int activeChannel_ = 1;
    std::set<int> hiddenChannels_;

    QLabel* clipNameLabel_;
    QPushButton* drawModeBtn_ = nullptr;
    QPushButton* editModeBtn_ = nullptr;
    QPushButton* musicalTypingBtn_ = nullptr;
    QPushButton* stepRecordBtn_ = nullptr;
    QComboBox* snapCombo_;
    QComboBox* ccCombo_ = nullptr;
    PianoRollRuler* ruler_;
    PianoKeyboard* keyboard_;
    NoteGrid* noteGrid_;
    VelocityLane* velocityLane_;
    CcLane* ccLane_ = nullptr;

    QScrollArea* channelScrollArea_ = nullptr;
    QWidget* channelPanel_ = nullptr;
    QVBoxLayout* channelPanelLayout_ = nullptr;

    QWidget* ccContainer_ = nullptr;
    QLabel* ccHeaderLabel_ = nullptr;
    QPushButton* ccCollapseBtn_ = nullptr;
    QPushButton* ccFreehandBtn_ = nullptr;
    QPushButton* ccLineBtn_ = nullptr;
};

} // namespace freedaw
