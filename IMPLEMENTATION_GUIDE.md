# FreeDaw v1 Implementation Guide

> Actionable task list for contributors. Each task includes what to build, where to start in the code, which Tracktion Engine APIs to use, what new files to create, and acceptance criteria. See [V1_FEATURE_SET_ROADMAP.md](V1_FEATURE_SET_ROADMAP.md) for the full feature matrix and priority rationale.

## How to use this guide

- Tasks are grouped by feature area and ordered by priority within each group.
- Priority tags: **[MUST]** = required for v1 ship, **[SHOULD]** = strong v1 usability, **[STRETCH]** = post-v1.
- When adding new source files, add the `.h`/`.cpp` pair to `FREEDAW_SOURCES` in `CMakeLists.txt` (lines 57-109) in the matching directory group.
- All code lives in the `freedaw` namespace. JUCE types are qualified with `juce::` or the `te::` alias.
- After any code change, verify the build passes (see `CLAUDE.md` for build commands).

---

## 1. Data Safety and Project Reliability

### 1.1 ~~Unsaved-changes prompt on close~~ **[MUST]** -- DONE

**What:** Warn the user before losing work when closing the window or quitting.

**Where to start:**
- `src/ui/MainWindow.h` / `MainWindow.cpp` -- no `closeEvent` override exists today.
- The `onNewProject()` method in `MainWindow.cpp` (line ~652) already has a save/discard/cancel prompt using `QMessageBox` that can be extracted as a reusable helper.

**Implementation:**
1. Override `void MainWindow::closeEvent(QCloseEvent* event)`.
2. Check `editMgr_.edit()->getUndoManager().canUndo()` to detect unsaved changes.
3. Show a save/discard/cancel dialog (reuse the pattern from `onNewProject`).
4. On Save: call `onSaveProject()`, then `event->accept()`.
5. On Discard: `event->accept()`.
6. On Cancel: `event->ignore()`.
7. Also guard `onOpenProject()` and `File > Quit` with the same check.

**Acceptance criteria:**
- Closing the window with unsaved changes shows a prompt.
- Choosing Cancel keeps the window open.
- Choosing Save persists the project before closing.

---

### 1.2 ~~Periodic autosave~~ **[MUST]** -- DONE

**What:** Automatically save a recovery snapshot at a regular interval so work is not lost on crash.

**Where to start:**
- `src/engine/EditManager.h` / `.cpp` -- add autosave logic here since it owns the `te::Edit`.
- Tracktion's `te::EditFileOperations` can `save()` to a specific file.

**New files:** None required; add to `EditManager`.

**Implementation:**
1. Add a `QTimer autosaveTimer_` to `EditManager`, firing every 60 seconds (configurable).
2. On timer fire, save a copy to `%APPDATA%/FreeDaw/autosave/<project-hash>.tracktionedit`.
3. Write a small `.json` sidecar with `{ "originalPath": "...", "timestamp": "..." }`.
4. On clean exit (after successful save or discard), delete the autosave file.
5. Start the timer after `loadEdit()` or `newEdit()`.

**Acceptance criteria:**
- An autosave file appears in `%APPDATA%/FreeDaw/autosave/` within 60 seconds of editing.
- Clean exit removes the autosave.
- Autosave does not block the UI (use `QTimer` on the main thread; `EditFileOperations::save` is fast for XML).

---

### 1.3 ~~Crash recovery on startup~~ **[MUST]** -- DONE

**What:** On launch, detect leftover autosave files and offer to restore them.

**Where to start:**
- `src/app/FreeDawApplication.cpp` -- add recovery check after engine init, before `MainWindow` is shown.
- Read the autosave directory created in task 1.2.

**New files:** `src/ui/dialogs/RecoveryDialog.h` / `.cpp` (or inline in `FreeDawApplication`).

**Implementation:**
1. On startup, scan `%APPDATA%/FreeDaw/autosave/` for `.tracktionedit` files.
2. If found, show a dialog: "FreeDaw found an unsaved session from [timestamp]. Restore it?"
3. Restore: call `editMgr_.loadEdit(autosaveFile)`, then set `currentFile_` to the original path from the sidecar (or leave as untitled).
4. Discard: delete the autosave file and proceed normally.

**Acceptance criteria:**
- After a simulated crash (kill the process), relaunch offers recovery.
- Restoring loads the autosaved state.
- Discarding starts with a fresh project.

---

### 1.4 ~~Window title with project name~~ **[SHOULD]** -- DONE

**What:** Show the current project filename in the title bar, with a dirty indicator.

**Where to start:** `src/ui/MainWindow.cpp` -- `setWindowTitle("FreeDaw")` is currently hardcoded.

**Implementation:**
1. After every `loadEdit`, `saveEdit`, `newEdit`, update the title: `"FreeDaw - [filename]"` or `"FreeDaw - Untitled"`.
2. When `editChanged` fires and `canUndo()` is true, prepend `"* "` to indicate unsaved changes.

---

### 1.5 Recent projects list **[SHOULD]**

**What:** File > Open Recent submenu with the last 10 opened projects.

**Where to start:** `src/ui/MainWindow.cpp` -- the File menu is built in `createMenus()`.

**Implementation:**
1. Use `QSettings` to store a `QStringList` of recent paths under `"recentProjects"`.
2. Build a `QMenu` submenu under File with one action per recent path.
3. Update the list on every `loadEdit` / `saveEditAs`.

---

## 2. Audio Device and Performance

### 2.1 ~~Audio settings dialog~~ **[MUST]** -- DONE

**What:** Let users choose audio device, sample rate, and buffer size.

**Where to start:**
- `src/engine/AudioEngine.h` / `.cpp` -- `deviceManager()` returns `te::DeviceManager&`, which wraps `juce::AudioDeviceManager`.
- `AudioEngine::setDefaultAudioDevice()` currently calls `dm.initialise(2, 2)` with no options.

**New files:** `src/ui/dialogs/AudioSettingsDialog.h` / `.cpp`.

**Implementation:**
1. Create a `QDialog` with combo boxes for:
   - Output device (from `audioEngine.getAvailableOutputDevices()`)
   - Input device (from `audioEngine.getAvailableInputDevices()`)
   - Sample rate (query `juce::AudioIODevice::getAvailableSampleRates()`)
   - Buffer size (query `juce::AudioIODevice::getAvailableBufferSizes()`)
2. On accept, call `juce::AudioDeviceManager::setAudioDeviceSetup(setup, true)` via the JUCE device manager (accessible through `te::DeviceManager::deviceManager`).
3. Persist the selected device config with `QSettings`.
4. Add a menu action: Edit > Audio Settings (or a toolbar gear icon).

**Tracktion Engine note:** `te::DeviceManager` wraps `juce::AudioDeviceManager`. Access the JUCE device manager for low-level device setup. Tracktion will pick up the new device automatically.

**Acceptance criteria:**
- User can switch audio devices without restarting.
- Selected device persists across sessions.

---

### 2.2 ~~Live status bar~~ **[MUST]** -- DONE

**What:** Replace the static `"44100 Hz"`, `"512 samples"`, and `"CPU: 0%"` labels with live values.

**Where to start:** `src/ui/MainWindow.cpp`, `createStatusBar()` (line ~626).

**Implementation:**
1. Store pointers to the three `QLabel` widgets as member variables.
2. Add a `QTimer` (1-second interval) that reads:
   - Sample rate: `juce::AudioIODevice::getCurrentSampleRate()`
   - Buffer size: `juce::AudioIODevice::getCurrentBufferSizeSamples()`
   - CPU: `te::Engine::getDeviceManager().getCpuUsage()` (returns 0.0-1.0)
3. Update the labels on each tick.

**Acceptance criteria:**
- Status bar reflects actual device settings.
- CPU meter moves during playback.

---

## 3. Automation Envelopes

### 3.1 ~~Automation lane UI on timeline~~ **[MUST]** -- DONE

**What:** Expandable automation lanes below each track in the timeline, showing parameter envelopes that can be edited with point add/move/delete, freehand draw, and curve shape editing.

**Files created:**
- `src/ui/timeline/AutomationLaneItem.h/.cpp` -- `QGraphicsItem` that draws the envelope curve (sampled from engine for pixel-perfect accuracy), handles point creation, freehand drawing, and curve segment bending.
- `src/ui/timeline/AutomationPointItem.h/.cpp` -- Draggable diamond-shaped point handles with snap, shift-constrain, value tooltips, and right-click curve shape presets.
- `src/ui/timeline/AutomationLaneHeader.h/.cpp` -- Header widget with parameter dropdown (Volume, Pan, all plugin params), bypass toggle, and close button.
- `src/ui/timeline/EnvelopeUtils.h` -- Shared coordinate/path/hit-test utilities designed for reuse by future CC lane.

**Files modified:**
- `TimelineView.h/.cpp` -- Variable-height `TrackLayoutInfo` layout system, automation lane management, resize handles, playhead-driven lane updates.
- `TrackHeaderWidget.h/.cpp` -- "Auto" disclosure button, `setAutomationVisible()` for state sync.
- `EditManager.h/.cpp` -- `getAutomatableParamsForTrack()`, `getVolumeParam()`, `getPanParam()` helpers.
- `CMakeLists.txt` -- Added 7 new source files.

**Features implemented:**
- Single automation lane per track with parameter switcher dropdown.
- Double-click to add points; drag to move with grid snap and shift-constrain.
- Freehand draw mode with automatic simplification.
- Ctrl+drag curve segments to bend (ease in/out); right-click presets (Linear, Ease In/Out, Sharp, Step).
- Discrete/boolean parameter support (step rendering, state snapping).
- Resizable lane height (50-120px) via drag handle.
- Curve line with drop shadow; playback position dot that follows the curve.
- Value labels on lane edges (dB for volume, L/R for pan, % for generic).
- Keyboard shortcut: `A` to toggle automation lane on selected track.
- Automation data persists in `.tracktionedit` files automatically.

---

### 3.2 ~~Automation read/write/touch/latch modes on mixer~~ **[SHOULD]** -- DONE

**What:** Per-track automation mode button on each channel strip (including master) with four modes: Read, Touch, Latch, Write.

**Implementation:**
- `ChannelStrip.cpp` -- Cycle button (READ → TOUCH → LATCH → WRITE → READ) sets per-track `track->automationMode` and global `AutomationRecordManager` flags.
- Touch mode: records only while actively moving a control, reverts to reading on release.
- Latch mode: starts recording on first touch, holds last value until stop.
- Write mode: records all params from playback start (most destructive).
- Master channel strip automation supported via `edit->getMasterTrack()->automationMode`.
- Volume faders, pan knobs, and effect parameter knobs visually follow automation during playback (30fps polling).
- Scrubbing (moving playhead while stopped) updates controls to show automation value at that position.
- Controls only tracked in non-WRITE modes to avoid fighting with user input.
- Pan automation recording fixed: uses `panParam->setParameter()` instead of `pan.setValue()`.

---

## 4. MIDI CC Lanes and Channel Handling

### 4.1 ~~CC lane editor in piano roll~~ **[MUST]** -- DONE

**What:** Automation-grade CC lane panel below the velocity lane in the piano roll, with full point editing.

**Files created:**
- `src/ui/pianoroll/CcLane.h/.cpp` -- QWidget with cached point model, diamond-handle painting, step-curve rendering via `EnvelopeUtils`, freehand draw, individual and bulk point editing.

**Files modified:**
- `PianoRollEditor.h/.cpp` -- CC number combo box (CC1/7/10/11/64 + arbitrary via "Other..."), CC row layout below velocity, scroll/zoom/clip sync, snap function pass-through.
- `CMakeLists.txt` -- Added CcLane source files.

**Features implemented:**
- CC number selector combo box in toolbar with preset CCs and custom entry.
- Step-curve rendering with translucent fill using `EnvelopeUtils::buildEnvelopePath(discrete=true)`.
- Diamond point handles at each CC event (8px normal, 10px hovered), styled like `AutomationPointItem`.
- Double-click to add a point at any position.
- Click-drag on empty space for freehand CC drawing with range-overwrite and interval simplification.
- Alt+drag (or Line tool) for straight-line CC drawing -- linear ramp between start and end points.
- Freehand/Line draw tool toggle buttons in the CC header panel for users who prefer clicking over modifier keys.
- Point drag with grid snap and shift-constrain (horizontal or vertical lock).
- Value tooltip during drag (e.g. "CC1: 87").
- Ctrl+drag rubber-band multi-select; Shift+click for additive selection toggle.
- Bulk move: drag any selected point to move entire selection as a group.
- Delete key removes selected points; right-click context menu for single/multi delete and "Add Point Here".
- Ctrl+A selects all; Escape clears selection.
- Horizontal reference lines at 25%/50%/75% with value labels.
- Collapsible CC lane (collapsed by default) with disclosure arrow; "Vel" label on velocity lane header.
- Selected CC name displayed in the left header panel alongside the collapse toggle and draw tool buttons.
- Horizontal separator between velocity lane and CC lane.
- Undo/redo for all operations via `edit->getUndoManager()`.
- CC data persists in `.tracktionedit` files automatically (Tracktion ValueTree serialization).

---

### 4.2 MIDI channel selection **[MUST]**

**What:** Let users choose which MIDI channel (1-16) a track or clip sends on.

**Where to start:**
- `src/ui/pianoroll/PianoRollEditor.cpp` -- add a channel combo to the toolbar.
- `src/ui/mixer/ChannelStrip.cpp` -- add a channel selector for MIDI tracks.

**Tracktion Engine API:**
- `te::MidiClip` has `setMidiChannel(te::MidiChannel)` and `getMidiChannel()`.
- `te::MidiChannel` is constructed from 1-16.

**Implementation:**
1. Add a `QComboBox` with channels 1-16 in `PianoRollEditor`.
2. On change, call `clip->setMidiChannel(te::MidiChannel(ch))`.
3. Optionally add the same control in `ChannelStrip` for MIDI tracks.

**Acceptance criteria:**
- User can set MIDI channel per clip.
- Playback sends notes on the selected channel.

---

### 4.3 Quantize strength and swing **[SHOULD]**

**What:** Enhance the existing quantize with strength (0-100%) and swing parameters.

**Where to start:** `src/ui/pianoroll/NoteGrid.cpp`, `quantizeNotes()`.

**Tracktion Engine API:**
- `te::MidiNote` has `getStartBeat()`, `setStartBeat(beat, undoManager)`.
- Quantize is currently manual: snap each note's start to the nearest grid line.

**Implementation:**
1. Add strength slider: interpolate between original position and grid position by the strength factor.
2. Add swing slider: offset every other grid line by a swing amount (0-100% of a grid division).
3. Add controls to the `PianoRollEditor` toolbar.

---

### 4.4 Transpose selected notes **[SHOULD]**

**What:** Shift selected notes up or down by semitones.

**Where to start:** `src/ui/pianoroll/NoteGrid.cpp` -- add `transposeSelectedNotes(int semitones)`.

**Implementation:**
1. Iterate selected `NoteItem` objects, call `note->setNoteNumber(note->getNoteNumber() + semitones, undoManager)`.
2. Add menu items in the piano roll context menu: Transpose Up (+1), Transpose Down (-1), Transpose Octave Up (+12), Transpose Octave Down (-12).
3. Keyboard shortcuts: Up/Down arrows for semitone, Shift+Up/Down for octave.

---

## 5. Transport and Recording

### 5.1 Loop in/out region UI **[MUST]**

**What:** Let users set the loop start and end points visually on the timeline.

**Where to start:**
- `src/ui/timeline/TimeRuler.cpp` -- the ruler already handles click-to-seek. Add loop region overlay here.
- `src/ui/transport/TransportBar.cpp` -- loop toggle exists (line ~212); extend it to display/set loop range.

**Tracktion Engine API:**
- `te::TransportControl` has `loopPoint1` and `loopPoint2` properties (type `juce::CachedValue<double>`).
- Setting these defines the loop range when `looping` is enabled.

**Implementation:**
1. Add two draggable handles on the `TimeRuler` for loop start and loop end.
2. Draw a shaded overlay between the two handles.
3. On drag, update `transport.loopPoint1` and `transport.loopPoint2` (these are beat positions converted via the tempo sequence).
4. Alternatively, allow setting loop points from the transport bar with numeric entry.
5. Keyboard shortcuts: `I` to set loop-in at playhead, `O` to set loop-out at playhead.

**Acceptance criteria:**
- User can drag loop start/end on the ruler.
- Loop playback respects the set range.
- Loop region is visually distinct on the timeline.

---

### 5.2 Metronome / click track **[MUST]**

**What:** Audible click during playback and recording.

**Where to start:**
- `src/ui/transport/TransportBar.cpp` -- add a metronome toggle button.
- `src/engine/AudioEngine.h` / `.cpp` -- enable the click in Tracktion Engine.

**Tracktion Engine API:**
- `te::Edit` has `clickTrackEnabled` (a `juce::CachedValue<bool>`).
- Setting `edit->clickTrackEnabled = true` enables the built-in click.
- Volume/panning of the click can be adjusted via `edit->getClickTrackVolume()` / `setClickTrackVolume()`.

**Implementation:**
1. Add a metronome button to `TransportBar` (checkable toggle).
2. On toggle: `editMgr_->edit()->clickTrackEnabled = checked`.
3. Persist the preference in `QSettings`.
4. Optional: add click volume knob in a settings popover.

**Acceptance criteria:**
- Metronome audible during playback when enabled.
- Metronome audible during recording when enabled.
- Toggle state persists across sessions.

---

### 5.3 Count-in before recording **[MUST]**

**What:** Play a configurable number of bars of metronome click before recording starts.

**Where to start:** `src/ui/transport/TransportBar.cpp`, `onRecord()` (line ~197).

**Tracktion Engine API:**
- `te::TransportControl::setRecordingPreCount(int numBarsCountIn)` or manual approach: start playback N bars before the punch-in point, then begin recording.
- Tracktion Engine supports a pre-count internally.

**Implementation:**
1. Add a "Count-in" toggle (and optionally a bar count spinner) in `TransportBar`.
2. Before calling `transport.record(false)`, if count-in is enabled, set the appropriate pre-count on the transport or manually offset the start position.
3. Ensure the metronome is audible during the count-in bars.

---

### 5.4 Punch in/out recording **[STRETCH]**

**What:** Record only within a defined time range.

**Tracktion Engine API:**
- `te::TransportControl` supports punch regions via the loop region when `looping` is enabled during recording.

---

## 6. Export and Rendering

### 6.1 Stem export **[SHOULD]**

**What:** Export each track as a separate audio file (stems).

**Where to start:**
- `src/engine/EditManager.cpp`, `exportMix()` (line ~738) -- uses `te::Renderer::Parameters` with `tracksToDo` bitmask.
- `src/ui/dialogs/ExportDialog.cpp` -- extend with a "Stems" option.

**Implementation:**
1. Add a checkbox or mode toggle in `ExportDialog`: "Full Mix" vs "Stems (one file per track)".
2. In stem mode, iterate `getAudioTracks()` and for each track:
   - Set `params.tracksToDo` to include only that track.
   - Set `params.destFile` to `<outputDir>/<trackName>.wav`.
   - Run `Renderer::RenderTask`.
3. Show a combined progress bar across all tracks.

**Acceptance criteria:**
- Each track is exported as a separate WAV file.
- Stem files are properly named by track name.
- Effects on each track are rendered into the stem.

---

### 6.2 FLAC export option **[SHOULD]**

**What:** Offer FLAC as an export format alongside WAV.

**Where to start:** `src/ui/dialogs/ExportDialog.cpp` -- add a format combo box.

**Implementation:**
1. Add format selector: WAV, FLAC.
2. When FLAC is selected, use `juce::FlacAudioFormat` for the output writer.
3. Adjust the file dialog filter accordingly.

---

## 7. Plugin Management

### 7.1 Custom VST scan paths **[SHOULD]**

**What:** Let users add custom directories to the VST3 scan.

**Where to start:** `src/engine/PluginScanner.cpp`, `doScan()` (line 12) -- currently only uses `vst3.getDefaultLocationsToSearch()`.

**Implementation:**
1. Add `QSettings`-backed list of custom paths.
2. In `doScan()`, merge custom paths with the default paths.
3. Add a "Plugin Paths" section in the audio settings dialog (or a dedicated dialog) with add/remove path controls.

---

### 7.2 Plugin blacklist visibility **[SHOULD]**

**What:** Show plugins that crashed during scan and let users retry or permanently exclude them.

**Where to start:** `src/engine/PluginScanner.cpp` -- crash-during-scan plugins are silently skipped (line 24-27).

**Implementation:**
1. Track failed plugins in a `QStringList failedPlugins_`.
2. After scan, expose the list via a signal or getter.
3. Add a "Failed Plugins" tab in the plugin scanner UI showing the list with "Retry" and "Blacklist" actions.

---

## 8. Editing Tools and UX

### 8.1 Wire pointer/pen/eraser tool modes **[SHOULD]**

**What:** Make the toolbar pointer, pen, and eraser buttons actually change editing behavior.

**Where to start:**
- `src/ui/MainWindow.cpp` (lines 400-410) -- the actions exist but have no `connect()`.
- `src/ui/timeline/TimelineView.h` / `.cpp` -- needs an `EditTool` enum and mode-dependent behavior in mouse handlers.
- `src/ui/pianoroll/NoteGrid.cpp` -- already has Edit/Draw modes internally.

**Implementation:**
1. Define an enum: `enum class EditTool { Pointer, Pen, Eraser }`.
2. Add `setEditTool(EditTool)` to `TimelineView` and `NoteGrid`.
3. Connect the toolbar actions in `MainWindow` to set the tool on both views.
4. **Pointer mode** (default): select, move, resize clips/notes.
5. **Pen mode**: click to create new clips (audio: empty placeholder; MIDI: empty MIDI clip) or notes.
6. **Eraser mode**: click to delete clips or notes.
7. Change cursor to match the active tool.
8. Add keyboard shortcuts: `V` = Pointer, `P` = Pen, `E` = Eraser.

**Acceptance criteria:**
- Switching tools changes cursor and click behavior in both timeline and piano roll.
- Eraser deletes on click.
- Pen creates on click.

---

### 8.2 Connect file browser double-click **[SHOULD]**

**What:** Double-clicking a file in the browser panel should import it to the selected track or create a new track.

**Where to start:**
- `src/ui/browser/FileBrowserPanel.cpp` (line 84) -- emits `fileDoubleClicked(path)` but it's not connected.
- `src/ui/MainWindow.cpp` -- `fileBrowser_` is created at line ~601.

**Implementation:**
1. In `MainWindow::createDocks()` (or nearby), add:
   ```cpp
   connect(fileBrowser_, &FileBrowserPanel::fileDoubleClicked,
           this, &MainWindow::onFileBrowserDoubleClicked);
   ```
2. In the handler, detect file type (audio vs MIDI by extension).
3. If a track is selected, import to that track at the playhead position.
4. If no track is selected, create a new track of the appropriate type and import.

---

### 8.3 Undo/redo button state **[SHOULD]**

**What:** Disable the undo/redo toolbar buttons when there is nothing to undo or redo.

**Where to start:** `src/ui/MainWindow.cpp` (lines 352-367) -- actions are always enabled.

**Implementation:**
1. Store the undo/redo `QAction*` as member variables.
2. Connect to `EditManager::editChanged` to re-evaluate:
   ```cpp
   undoAction_->setEnabled(editMgr_.edit()->getUndoManager().canUndo());
   redoAction_->setEnabled(editMgr_.edit()->getUndoManager().canRedo());
   ```

---

### 8.4 Emit `transportStateChanged` signal **[SHOULD]**

**What:** The `transportStateChanged` signal is declared on `EditManager` (line 128 of `EditManager.h`) but never emitted. UI cannot react to transport changes from the engine side.

**Where to start:** `src/engine/EditManager.cpp`.

**Implementation:**
1. Register a `te::TransportControl::Listener` on the edit's transport.
2. In the listener callbacks (`playbackStarted`, `playbackStopped`, etc.), emit `transportStateChanged()`.
3. Connect UI elements (play/record button checked state, position display) to this signal.

---

## 9. Timeline Editing Fundamentals

### 9.1 Clip copy / paste / cut on timeline **[MUST]**

**What:** Ctrl+C / Ctrl+V / Ctrl+X for clips on the timeline. Currently only the audio waveform editor supports clipboard operations.

**Where to start:**
- `src/ui/timeline/TimelineView.cpp` -- `keyPressEvent` handles Delete and Escape but not Ctrl+C/V/X.
- `src/ui/timeline/ClipItem.cpp` -- clips are `QGraphicsItem` with selection support, but no clipboard logic.

**Implementation:**
1. Add a clipboard model: store a list of serialized clip descriptions (track-relative position, clip type, source file or MIDI data).
2. **Copy (Ctrl+C):** serialize selected `ClipItem` data into the clipboard model.
3. **Cut (Ctrl+X):** copy then delete the selected clips.
4. **Paste (Ctrl+V):** create new clips at the playhead position on the same (or selected) track from the clipboard model.
5. For audio clips, pasting re-references the same source file. For MIDI clips, duplicate the `te::MidiList` data.
6. Add context menu entries "Copy", "Cut", "Paste" to the clip right-click menu.

**Acceptance criteria:**
- Ctrl+C/V/X works for single and multi-selected clips.
- Pasted clips appear at the playhead.
- Undo reverses paste and cut operations.

---

### 9.2 Arrow key nudge for clips and notes **[MUST]**

**What:** Arrow keys nudge selected clips/notes by the current grid interval.

**Where to start:**
- `src/ui/timeline/TimelineView.cpp` -- `keyPressEvent`.
- `src/ui/pianoroll/NoteGrid.cpp` -- `keyPressEvent`.

**Implementation:**
1. **Timeline:** Left/Right arrows move selected clips by `gridSnapper_.gridIntervalBeats()`. Up/Down moves clips between adjacent tracks.
2. **Piano roll:** Left/Right nudge note start by grid interval. Up/Down transpose by 1 semitone. Shift+Up/Down transpose by octave.
3. All operations go through `edit->getUndoManager()`.

**Acceptance criteria:**
- Arrow keys move clips/notes by snap grid amount.
- Shift modifier increases step size (octave for notes, bar for clips).

---

### 9.3 Home / End navigation **[SHOULD]**

**What:** Home jumps playhead to project start; End jumps to the end of the last clip.

**Where to start:** `src/ui/MainWindow.cpp` -- add keyboard shortcuts alongside the existing Space/R bindings.

**Implementation:**
1. Home: `transport.setPosition(TimePosition::fromSeconds(0))`.
2. End: find the latest clip end beat, convert to time, set position.

---

### 9.4 Timeline marquee / range selection **[SHOULD]**

**What:** Rubber-band drag on the timeline background to select multiple clips by area.

**Where to start:** `src/ui/timeline/TimelineView.cpp` -- the `QGraphicsView` drag mode is currently `NoDrag` (line ~227). Background drags on MIDI tracks create MIDI clips, which conflicts.

**Implementation:**
1. In Pointer tool mode, enable `QGraphicsView::RubberBandDrag` for the scene.
2. Reserve background drag for clip creation only in Pen tool mode.
3. After rubber-band completes, all `ClipItem` objects within the rect are selected.

---

### 9.5 Select All Clips **[SHOULD]**

**What:** Ctrl+A selects all clips on the timeline (and all notes in the piano roll, which already works).

**Where to start:** `src/ui/timeline/TimelineView.cpp` -- `keyPressEvent`.

---

### 9.6 Zoom to fit on timeline **[SHOULD]**

**What:** A "Zoom to Fit" action that adjusts horizontal and vertical zoom so all clips are visible.

**Where to start:** `src/ui/timeline/TimelineView.cpp` -- `zoomIn` / `zoomOut` exist. The routing view already has `zoomToFit()` which can serve as a pattern.

**Implementation:**
1. Calculate the bounding rect of all clips in the scene.
2. Set `pixelsPerBeat_` and scroll position so the view fits all content.
3. Add a menu action and shortcut (Ctrl+0 or Ctrl+Shift+F).

---

## 10. Track Management

### 10.1 Track reordering **[SHOULD]**

**What:** Let users drag tracks to reorder them. Currently tracks are fixed by index with no move API.

**Where to start:**
- `src/engine/EditManager.h` -- no `moveTrack` method exists.
- `src/ui/timeline/TimelineView.cpp` -- track headers are laid out in a `QVBoxLayout`.

**Tracktion Engine API:**
- `te::Edit` stores tracks in an ordered list. Use `edit->moveTrack(track, insertIndex)` or re-insert via track list manipulation.

**Implementation:**
1. Add `EditManager::moveTrack(te::Track* track, int newIndex)`.
2. Add drag handles to `TrackHeaderWidget` (or make the entire header draggable).
3. On drop, call `moveTrack` and emit `tracksChanged()` to rebuild the UI.

---

### 10.2 Track color assignment **[SHOULD]**

**What:** Let users assign a color to each track, used in the header, clips, and mixer strip.

**Where to start:**
- `src/ui/timeline/TrackLane.h` -- already has `setTrackColor()` but it is not used by the main UI.
- `src/ui/timeline/TrackHeaderWidget.cpp` -- badge colors are currently hardcoded from the theme.

**Implementation:**
1. Store color in track properties (Tracktion Edit XML supports custom properties via `state`).
2. Add a color swatch button to `TrackHeaderWidget` that opens a `QColorDialog`.
3. Propagate the color to `ClipItem` background, `ChannelStrip` header, and `TrackLane`.

---

## 11. Recording Workflow

### 11.1 Input monitoring (hear-through) **[MUST]**

**What:** Let armed tracks pass input audio to the output so the user can hear themselves while not recording.

**Where to start:**
- `src/engine/EditManager.cpp` -- `setTrackRecordEnabled()` arms the track but does not enable monitoring.
- Tracktion Engine example code has `EngineHelpers::enableInputMonitoring()` and `isInputMonitoringEnabled()`.

**Tracktion Engine API:**
- `te::InputDeviceInstance` has `setMonitorMode()` (e.g. `InputDeviceInstance::MonitorMode::on`).
- Access via `edit->getAllInputDevices()` or the track's assigned input device.

**Implementation:**
1. When a track is armed, also enable input monitoring on its assigned input device.
2. Add a monitor toggle button to `ChannelStrip` and `TrackHeaderWidget` (speaker icon).
3. When un-armed or monitoring is toggled off, disable monitoring.

**Acceptance criteria:**
- User hears live input through the track's effects chain and output when monitoring is on.
- Monitoring works independently of record state.

---

### 11.2 MIDI note audition in piano roll **[SHOULD]**

**What:** Clicking or drawing a note in the piano roll plays a short preview of that note through the track's instrument.

**Where to start:** `src/ui/pianoroll/NoteGrid.cpp` -- no audition logic exists.

**Tracktion Engine API:**
- Send a short MIDI note-on/off to the track's `te::MidiInputDevice` or directly to the instrument plugin.
- `te::ExternalPlugin::sendMidiMessage(juce::MidiMessage)` can inject MIDI.

**Implementation:**
1. On note click (in edit mode) or note draw commit, send a `juce::MidiMessage::noteOn(channel, noteNum, velocity)` to the track's instrument.
2. Schedule a note-off after ~200ms via `QTimer::singleShot`.

---

## 12. File Browser Enhancements

### 12.1 Audio preview / audition in file browser **[SHOULD]**

**What:** Preview audio files before importing them. A `previewBtn_` is already declared in `FileBrowserPanel.h` (line 33) but never instantiated or connected.

**Where to start:** `src/ui/browser/FileBrowserPanel.h` / `.cpp`.

**Implementation:**
1. Instantiate `previewBtn_` and add it to the layout.
2. On click, use `juce::AudioTransportSource` + `juce::AudioFormatReaderSource` to play the selected file through the default output.
3. Toggle button stops playback if already playing.
4. Show a small waveform or progress indicator.

---

## 13. Crossfade and Clip Overlap

### 13.1 Crossfade between adjacent clips **[SHOULD]**

**What:** When two clips overlap on the same track, create an automatic or manual crossfade between them. Currently only an in-clip destructive crossfade exists in `AudioFileOperations`.

**Where to start:**
- `src/ui/timeline/ClipItem.cpp` -- clip drag can create overlaps but there is no crossfade handling.

**Tracktion Engine API:**
- Tracktion Engine supports clip-level fade-in and fade-out via `te::AudioClipBase::setFadeIn()` / `setFadeOut()`.
- When clips overlap, setting complementary fades on the overlapping region creates a crossfade.

**Implementation:**
1. Detect when a clip drag creates an overlap with an adjacent clip on the same track.
2. Automatically set fade-out on the earlier clip and fade-in on the later clip for the overlap duration.
3. Draw the crossfade region visually on the timeline.

---

## 14. Additional Export Capabilities

### 14.1 MIDI export **[SHOULD]**

**What:** Export MIDI tracks as `.mid` files.

**Where to start:** `src/ui/dialogs/ExportDialog.cpp` and `src/engine/EditManager.cpp`.

**Tracktion Engine API:**
- `te::MidiClip::getSequence()` provides the MIDI data.
- `juce::MidiFile` can be written from `juce::MidiMessageSequence` objects.

**Implementation:**
1. Add a "MIDI" option in the export format selector.
2. Collect all MIDI clips from the edit, convert to `juce::MidiMessageSequence`, build a `juce::MidiFile`, and write to disk.

---

### 14.2 Dithering on export **[STRETCH]**

**What:** Apply dithering when exporting to 16-bit to reduce quantization artifacts.

**Where to start:** `src/engine/EditManager.cpp`, `exportMix()`.

**Implementation:**
1. Add a dithering checkbox in `ExportDialog`, enabled when bit depth is 16.
2. Apply triangular probability density function (TPDF) dither during the render pass.

---

## 15. Visual Polish

### 15.1 Snap crosshair / visual indicator **[SHOULD]**

**What:** Show a vertical guide line during clip or note drag indicating the snapped position.

**Where to start:** `src/ui/timeline/TimelineView.cpp` and `src/ui/pianoroll/NoteGrid.cpp`.

**Implementation:**
1. Draw a thin vertical line at the snapped beat position during any drag operation.
2. Remove the line on drag end.

---

### 15.2 Clip rename **[SHOULD]**

**What:** Let users rename clips via double-click or context menu.

**Where to start:** `src/ui/timeline/ClipItem.cpp` -- clip name is drawn in `paint()` (line ~250) but there is no rename UI.

**Tracktion Engine API:** `te::Clip::setName(juce::String)`.

---

## 16. Engine Signals and Internals

### 16.1 Plugin preset save/load **[STRETCH]**

**What:** Save and load plugin presets (VST3 state blobs) from within FreeDaw.

**Tracktion Engine API:**
- `te::ExternalPlugin::getStateInformation()` / `setStateInformation()`.
- Store presets as files in `%APPDATA%/FreeDaw/presets/<pluginId>/`.

---

### 16.2 Aux send/return routing **[STRETCH]**

**What:** True aux send/return alongside the existing bus routing. Currently tracks can only route output to another track or master.

**Tracktion Engine API:**
- Tracktion Engine has `te::AuxSendPlugin` and `te::AuxReturnPlugin`.
- Insert an `AuxSendPlugin` on the source track and an `AuxReturnPlugin` on the destination bus.

---

### 16.3 Tempo map (multiple tempo points) **[STRETCH]**

**What:** Support tempo changes at specific points in the project timeline.

**Tracktion Engine API:**
- `edit->tempoSequence.insertTempo(BeatPosition, bpm, curveValue)`.
- Currently FreeDaw only modifies `getTempo(0)`.

---

## 17. Summary: New Files to Create

| File | Purpose | Status |
|------|---------|--------|
| `src/ui/dialogs/AudioSettingsDialog.h/.cpp` | Audio device selection dialog | DONE |
| `src/ui/dialogs/RecoveryDialog.h/.cpp` | Crash recovery prompt on startup | DONE |
| `src/ui/timeline/AutomationLaneItem.h/.cpp` | Automation envelope drawing and editing in timeline | DONE |
| `src/ui/timeline/AutomationPointItem.h/.cpp` | Draggable automation point handles | DONE |
| `src/ui/timeline/AutomationLaneHeader.h/.cpp` | Automation lane header with parameter selector | DONE |
| `src/ui/timeline/EnvelopeUtils.h` | Shared coordinate/path utilities for automation and CC lanes | DONE |
| `src/ui/pianoroll/CcLane.h/.cpp` | CC event drawing and editing lane in piano roll | DONE |

All other tasks modify existing files. Remember to add new `.h`/`.cpp` pairs to `FREEDAW_SOURCES` in `CMakeLists.txt`.

## 18. Complete Task Index

Quick-reference of every task sorted by priority.

### MUST (required for v1 ship)

| ID | Task | Section |
|----|------|---------|
| ~~1.1~~ | ~~Unsaved-changes prompt on close~~ | ~~Data Safety~~ DONE |
| ~~1.2~~ | ~~Periodic autosave~~ | ~~Data Safety~~ DONE |
| ~~1.3~~ | ~~Crash recovery on startup~~ | ~~Data Safety~~ DONE |
| ~~2.1~~ | ~~Audio settings dialog~~ | ~~Device/Performance~~ DONE |
| ~~2.2~~ | ~~Live status bar~~ | ~~Device/Performance~~ DONE |
| ~~3.1~~ | ~~Automation lane UI on timeline~~ | ~~Automation~~ DONE |
| ~~4.1~~ | ~~CC lane editor in piano roll~~ | ~~MIDI~~ DONE |
| 4.2 | MIDI channel selection | MIDI |
| 5.1 | Loop in/out region UI | Transport |
| 5.2 | Metronome / click track | Transport |
| 5.3 | Count-in before recording | Transport |
| 9.1 | Clip copy / paste / cut on timeline | Timeline Editing |
| 9.2 | Arrow key nudge for clips and notes | Timeline Editing |
| 11.1 | Input monitoring (hear-through) | Recording |

### SHOULD (strong v1 usability)

| ID | Task | Section |
|----|------|---------|
| ~~1.4~~ | ~~Window title with project name~~ | ~~Data Safety~~ DONE |
| 1.5 | Recent projects list | Data Safety |
| ~~3.2~~ | ~~Automation read/write/touch/latch modes on mixer~~ | ~~Automation~~ DONE |
| 4.3 | Quantize strength and swing | MIDI |
| 4.4 | Transpose selected notes | MIDI |
| 6.1 | Stem export | Export |
| 6.2 | FLAC export option | Export |
| 7.1 | Custom VST scan paths | Plugins |
| 7.2 | Plugin blacklist visibility | Plugins |
| 8.1 | Wire pointer/pen/eraser tool modes | Editing Tools |
| 8.2 | Connect file browser double-click | Editing Tools |
| 8.3 | Undo/redo button state | Editing Tools |
| 8.4 | Emit transportStateChanged signal | Editing Tools |
| 9.3 | Home / End navigation | Timeline Editing |
| 9.4 | Timeline marquee / range selection | Timeline Editing |
| 9.5 | Select All Clips | Timeline Editing |
| 9.6 | Zoom to fit on timeline | Timeline Editing |
| 10.1 | Track reordering | Track Management |
| 10.2 | Track color assignment | Track Management |
| 11.2 | MIDI note audition in piano roll | Recording |
| 12.1 | Audio preview in file browser | File Browser |
| 13.1 | Crossfade between adjacent clips | Clips |
| 14.1 | MIDI export | Export |
| 15.1 | Snap crosshair / visual indicator | Visual Polish |
| 15.2 | Clip rename | Visual Polish |

### STRETCH (post-v1)

| ID | Task | Section |
|----|------|---------|
| 5.4 | Punch in/out recording | Transport |
| 14.2 | Dithering on export | Export |
| 16.1 | Plugin preset save/load | Engine |
| 16.2 | Aux send/return routing | Engine |
| 16.3 | Tempo map (multiple tempo points) | Engine |

## 19. Key Tracktion Engine API Quick Reference

| Need | API |
|------|-----|
| Transport | `edit->getTransport()` returns `te::TransportControl&` |
| Loop range | `transport.loopPoint1`, `transport.loopPoint2` (beat-based) |
| Metronome | `edit->clickTrackEnabled` (bool property) |
| Click volume | `edit->getClickTrackVolume()` / `setClickTrackVolume()` |
| Automation curve | `param->getCurve()` returns `te::AutomationCurve&` |
| Automation points | `curve.addPoint(time, value, curveValue)`, `movePoint(...)`, `removePoint(...)` |
| Automation mode | `param->setAutomationMode(readEnabled / writeEnabled)` |
| Automatable params | `plugin->getAutomatableParameters()` returns `juce::Array<te::AutomatableParameter*>` |
| Volume/Pan plugin | `te::VolumeAndPanPlugin` on every track; `volParam`, `panParam` |
| MIDI CC events | `midiList.addControllerEvent(beat, ccNum, value, undoManager)` |
| MIDI channel | `midiClip->setMidiChannel(te::MidiChannel(1..16))` |
| Input monitoring | `te::InputDeviceInstance::setMonitorMode(MonitorMode::on)` |
| Clip fades | `te::AudioClipBase::setFadeIn(length)` / `setFadeOut(length)` |
| Clip name | `te::Clip::setName(juce::String)` |
| Track move | `edit->moveTrack(track, insertIndex)` |
| Tempo sequence | `edit->tempoSequence.insertTempo(BeatPosition, bpm, curve)` |
| Aux send/return | `te::AuxSendPlugin` / `te::AuxReturnPlugin` |
| Plugin state | `te::ExternalPlugin::getStateInformation()` / `setStateInformation()` |
| Device manager | `engine.getDeviceManager()` returns `te::DeviceManager&` |
| CPU usage | `engine.getDeviceManager().getCpuUsage()` (0.0-1.0) |
| Render/export | `te::Renderer::Parameters` with `tracksToDo`, `time`, `destFile` |
| MIDI file write | `juce::MidiFile` + `juce::MidiMessageSequence` |
| Undo | `edit->getUndoManager()` returns `juce::UndoManager&` |
