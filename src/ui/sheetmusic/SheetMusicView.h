#pragma once

#include "ScoreScene.h"
#include <QWidget>
#include <QGraphicsView>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QToolBar>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace OpenDaw {

class EditManager;

class SheetMusicView : public QWidget {
    Q_OBJECT

public:
    explicit SheetMusicView(QWidget* parent = nullptr);

    void setClip(te::MidiClip* clip, EditManager* editMgr);
    void refresh();
    te::MidiClip* clip() const { return clip_; }

private:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void buildToolbar();
    void buildEditToolbar();
    void rebuildScore();
    void applyKeyToSelectedNote(int qtKey);
    void onZoomChanged(int value);
    void onPrint();
    void onExportPdf();
    void onGeneratePhrasing();
    void onClearPhrasing();
    void onNoteChanged();
    void onArticulationToggled(double beat, int midiNote, StaffKind staff, int artType);
    void onSpellingOverride(double beat, int midiNote, Accidental forced);

    void saveAnnotationsToClip();
    void loadAnnotationsFromClip();

    te::MidiClip* clip_ = nullptr;
    EditManager* editMgr_ = nullptr;
    int keySig_ = 0;
    bool preferFlats_ = false;

    std::vector<PhraseMarking> phrases_;
    std::vector<ArticulationMarking> articulations_;
    std::vector<SpellingOverride> spellingOverrides_;

    QToolBar* toolbar_ = nullptr;
    QToolBar* editToolbar_ = nullptr;
    QPushButton* preferFlatsBtn_ = nullptr;
    QLabel* clipNameLabel_ = nullptr;
    QComboBox* keySigCombo_ = nullptr;
    QSlider* zoomSlider_ = nullptr;
    QPushButton* printBtn_ = nullptr;
    QPushButton* pdfBtn_ = nullptr;
    QPushButton* phrasingBtn_ = nullptr;
    QPushButton* clearPhrasingBtn_ = nullptr;
    QGraphicsView* view_ = nullptr;
    ScoreScene* scene_ = nullptr;

    QNetworkAccessManager* nam_ = nullptr;
};

} // namespace OpenDaw
