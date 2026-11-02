#pragma once

#include "engine/AudioEngine.h"
#include <QPoint>
#include <functional>

namespace OpenDaw {

class EditManager;

// The instrument picker for a MIDI track: the built-in sounds by family (and
// drum kits), General MIDI mode for the built-in instrument, a VST instrument,
// or none. `chooseVst` opens the VST instrument list; `chooseSurge` puts the
// bundled Surge XT synth on the track (null if it isn't shipped).
void showInstrumentMenu(EditManager& editMgr, te::AudioTrack* track,
                        const std::function<void()>& chooseVst,
                        const std::function<void()>& chooseSurge, const QPoint& globalPos);

} // namespace OpenDaw
