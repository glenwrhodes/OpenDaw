#include "InstrumentMenu.h"
#include "engine/EditManager.h"
#include "engine/instruments/BundledPlugins.h"
#include "engine/instruments/SoundFontInstrument.h"
#include <QMenu>
#include <map>

namespace OpenDaw {

namespace {
// Menu text shows a lone & as a shortcut marker.
QString menuText(const QString& s) { return QString(s).replace('&', "&&"); }
}

void showInstrumentMenu(EditManager& editMgr, te::AudioTrack* track,
                        const std::function<void()>& chooseVst,
                        const std::function<void()>& chooseSurge, const QPoint& globalPos)
{
    if (!track) return;
    auto* current = editMgr.getTrackInstrument(track);
    auto* builtIn = dynamic_cast<SoundFontPlugin*>(current);

    QMenu menu;
    menu.setAccessibleName("Instrument Menu");
    menu.setToolTipsVisible(true);

    auto& lib = SoundFontLibrary::instance();
    if (lib.isAvailable()) {
        auto* heading = menu.addAction("Built-in Sounds");
        heading->setEnabled(false);

        std::map<QString, QMenu*> families;
        for (const auto& category : SoundFontLibrary::categories())
            families[category] = menu.addMenu(menuText(category));

        QString lastCategory;
        bool inVariations = false;
        for (const auto& preset : lib.presets()) {
            auto* sub = families[preset.category];
            if (!sub) continue;
            if (preset.category != lastCategory) { lastCategory = preset.category; inVariations = false; }
            // General MIDI sounds first, then a separator and the variations.
            if (!inVariations && preset.bank != 0 && !preset.isDrumKit()) {
                sub->addSeparator();
                inVariations = true;
            }
            auto* a = sub->addAction(menuText(preset.name));
            a->setCheckable(true);
            a->setChecked(builtIn && builtIn->bank() == preset.bank && builtIn->program() == preset.program);
            const int bank = preset.bank, program = preset.program;
            QObject::connect(a, &QAction::triggered, &menu, [&editMgr, track, bank, program]() {
                editMgr.setTrackBuiltInInstrument(*track, bank, program);
            });
        }

        if (builtIn) {
            menu.addSeparator();
            auto* gm = menu.addAction("Follow MIDI Program Changes");
            gm->setCheckable(true);
            gm->setChecked(builtIn->followsProgramChanges());
            gm->setToolTip("Play like a General MIDI module: program changes in the MIDI pick\n"
                           "each channel's sound, and channel 10 plays drums.\n"
                           "For imported General MIDI files.");
            QObject::connect(gm, &QAction::triggered, &menu, [&editMgr, builtIn](bool on) {
                editMgr.edit()->getUndoManager().beginNewTransaction("Follow Program Changes");
                builtIn->setFollowsProgramChanges(on);
                editMgr.notifyEdited();
            });
        }
    } else {
        auto* missing = menu.addAction("Built-in sounds not found");
        missing->setEnabled(false);
        missing->setToolTip(QString("Expected GeneralUser-GS.sf2 in a soundfonts folder beside the program."));
    }

    if (chooseSurge) {
        menu.addSeparator();
        auto* surge = menu.addAction("Surge XT Synth", [chooseSurge]() { chooseSurge(); });
        surge->setCheckable(true);
        surge->setChecked(BundledPlugins::isSurgeXt(current));
        surge->setToolTip("Surge XT: a free, open-source synthesizer with hundreds of\n"
                          "presets. Choose a preset in its window (click the patch name).");
    }

    menu.addSeparator();
    menu.addAction("VST Instrument...", [chooseVst]() { if (chooseVst) chooseVst(); });
    auto* none = menu.addAction("No Instrument", [&editMgr, track]() {
        editMgr.edit()->getUndoManager().beginNewTransaction("Remove Instrument");
        editMgr.removeTrackInstrument(*track);
        editMgr.notifyEdited();
        emit editMgr.tracksChanged();
    });
    none->setEnabled(current != nullptr);

    menu.exec(globalPos);
}

} // namespace OpenDaw
