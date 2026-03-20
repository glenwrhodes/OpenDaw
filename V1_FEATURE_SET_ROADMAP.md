# FreeDaw Version 1 Feature Set (Hobbyist Complete)

This document defines the feature set needed for a completed FreeDaw v1, based on current code in `src/`.

## 1) Feature Matrix: Implemented vs Missing


| Area                  | Implemented (code-backed)                                                                                       | Missing / Partial (v1 gaps)                                                                                               |
| --------------------- | --------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| Project lifecycle     | New/open/save/save-as, autosave, crash recovery, unsaved-changes prompt, window title with dirty indicator       | ~~No autosave, crash/session recovery, or close-with-unsaved-changes prompt~~ **DONE**                                    |
| Transport             | Play/stop/record/loop toggle, BPM, time signature in `src/ui/transport/TransportBar.cpp`                        | No loop in/out range UI, no count-in/pre-roll, no punch in/out                                                            |
| Timeline editing      | Drag/resize/split/delete clips and drag-drop import in `src/ui/timeline/TimelineView.cpp` and `ClipItem.cpp`; automation lanes with envelope editing in `AutomationLaneItem.cpp` | No marker lane, no comping/take lanes                                                                |
| Audio editing         | Trim/fade/normalize/reverse/silence in `src/ui/audioclip/AudioClipEditor.cpp` and `AudioWaveformView.cpp`       | No integrated time-stretch workflow for clip warping across timeline                                                      |
| MIDI note editing     | Note draw/select/move/resize, quantize, velocity lane in `src/ui/pianoroll/NoteGrid.cpp` and `VelocityLane.cpp`; CC lane editor with point editing in `src/ui/pianoroll/CcLane.cpp` | ~~No CC lanes~~, no MIDI channel chooser, no pitch-bend/aftertouch editing                                                    |
| Mixer and routing     | Channel strip controls + routing graph in `src/ui/mixer/ChannelStrip.cpp` and `src/ui/routing/RoutingView.cpp`; per-track automation modes (Read/Touch/Latch/Write) with fader/knob visual tracking | ~~No automation controls surfaced from mixer for writing/reading envelopes~~ **DONE**                                                  |
| Plugins and FX        | VST3 scan/cache in `src/engine/PluginScanner.cpp`, effect chain and plugin editor UI in `src/ui/effects/`       | No plugin blacklist UI, no custom scan path UI, VST3-only scan                                                            |
| Export/render         | Mix export, freeze, bounce in `src/engine/EditManager.cpp`; export dialog in `src/ui/dialogs/ExportDialog.cpp`  | Export is WAV-only, no stem/per-track export workflow                                                                     |
| Device/performance UX | Audio settings dialog, live status bar, device persistence in `src/engine/AudioEngine.cpp`                       | ~~No audio settings panel; status bar shows static values~~ **DONE**                                                      |
| Editing tools UX      | Toolbar icons for pointer/pen/eraser in `src/ui/MainWindow.cpp`                                                 | Tool actions are not wired to actual edit modes                                                                           |
| File browser ingest   | File browser emits `fileDoubleClicked` in `src/ui/browser/FileBrowserPanel.cpp`                                 | `fileDoubleClicked` is not connected; double-click import path is missing                                                 |


## 2) Priority for v1

### Must-have before v1 release

1. **Automation envelopes (core DAW expectation)** -- **DONE**
  - ~~Track automation lanes for volume, pan, and plugin parameters.~~
  - ~~Point add/move/delete, freehand draw, and curve shape editing on timeline.~~
  - ~~Read/Touch/Latch/Write modes per track with fader/knob visual tracking.~~
  - ~~Master channel automation support.~~
2. **MIDI CC + channel fundamentals**
  - ~~CC lane editor (minimum CC1, CC7, CC10, CC11 + arbitrary CC picker).~~ **DONE**
  - Per-track or per-clip MIDI channel assignment (1-16).
3. **Recording ergonomics baseline**
  - Loop in/out range handles in transport/timeline.
  - Metronome click while playing/recording.
  - Optional count-in before record starts.
4. **Data safety and continuity** -- **DONE**
  - ~~Unsaved-changes prompt on app close/project switch.~~
  - ~~Periodic autosave snapshots.~~
  - ~~Startup recovery flow after abnormal exit.~~
5. **Device and latency control** -- **DONE**
  - ~~Audio settings dialog for input/output device, sample rate, buffer size.~~
  - ~~Live status bar values for sample rate, buffer, and CPU.~~

### Should-have for strong v1 usability

1. **Tool mode completeness**
  - Wire pointer/pen/eraser actions to concrete timeline and piano-roll behaviors.
2. **Export maturity**
  - Stem export (all tracks or selected tracks), in addition to full-mix export.
  - Optional FLAC export alongside WAV.
3. **Plugin management reliability**
  - Failed-plugin list/blacklist visibility.
  - Manual rescan controls and custom plugin search paths.
4. **Basic MIDI quality-of-life**
  - Quantize strength and swing.
  - Transpose selected notes and optional timing/velocity humanize.

### Post-v1 / stretch

- Comping and take lanes.
- Loop recording with take stacking.
- Tempo map and timeline markers.
- Pitch bend and aftertouch lane editing.

## 3) Version 1 Exit Criteria (Ship Checklist)

FreeDaw v1 is complete when all items below are true:

### Recording and transport

- User can set loop start/end visually and record repeated passes against that loop.
- User can enable metronome and optional count-in and hear click in recording workflow.
- ~~User can configure audio device and latency without leaving the app.~~ **DONE**

### MIDI creation workflow

- ~~User can edit notes and velocity (already present) and also edit CC data in lanes.~~ **DONE**
- User can select MIDI channel behavior per track or clip and get predictable playback/output.

### Mixing and automation

- ~~User can automate at least volume, pan, mute, and one plugin parameter with visible envelopes.~~ **DONE**
- ~~Automation points persist after save/reload and render correctly in playback/export.~~ **DONE**

### Reliability and data protection

- ~~User is warned before losing unsaved changes on close/open/new.~~ **DONE**
- ~~Autosave captures recoverable snapshots during active editing.~~ **DONE**
- ~~App offers restoration after crash and can restore a usable recent state.~~ **DONE**

### Export and delivery

- User can export full mix to WAV with selectable sample rate/bit depth.
- User can export stems (per-track files) without destructive project changes.

## Suggested Execution Order

1. ~~Data safety (`unsaved prompt` + `autosave` + `recovery`) and audio settings panel.~~ **DONE**
2. ~~Automation lanes (engine model + timeline UI + persistence) and mixer automation modes.~~ **DONE**
3. MIDI CC lanes and channel assignment.
4. Loop in/out UI, metronome, and count-in.
5. Stem export and plugin-management polish.
6. Tool mode wiring and remaining usability upgrades.

