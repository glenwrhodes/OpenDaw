# FreeDaw

A free, open-source Digital Audio Workstation (DAW) for Windows, built with **Qt 6** for the UI and **Tracktion Engine** (JUCE) for the audio backend. FreeDaw aims to be powerful yet simple enough for non-engineers -- drag in samples, press play, add reverb with one click.

---

## Features (MVP)

### Timeline / Arrangement

- Multi-track audio arrangement with horizontal scrolling
- Drag-and-drop audio files (`.wav`, `.mp3`, `.flac`, `.ogg`, `.aiff`) from the built-in file browser onto any track
- Waveform display on clips
- Move clips by dragging -- snaps to the grid horizontally and locks to track lanes vertically
- Move clips between tracks by dragging up/down
- Grid snapping with 5 modes: Off, 1/4 Beat, 1/2 Beat, Beat, Bar
- Horizontal and vertical zoom (Ctrl+= / Ctrl+-)
- Animated playhead cursor that tracks playback in real time

### Transport

- Play, Stop, Record, and Loop buttons
- Click the time ruler to jump the playhead (snaps to grid)
- Click and drag the ruler to scrub smoothly without snapping
- BPM control (20-300 BPM)
- Time signature control (numerator / denominator)
- Dual position display: elapsed time (mm:ss.ms) and bars.beats.ticks

### Track Headers (left panel)

- Per-track controls: name, Mute (M), Solo (S), Record Arm (R)
- Horizontal volume slider and pan knob per track
- Real-time level meters (green/yellow/red) that react to playback audio
- Vertically synchronized with the timeline scroll

### Mixer (bottom panel)

- Channel strip per track with: vertical volume fader, pan knob, Mute/Solo/Record Arm, level meter
- Master channel strip on the right
- Two FX insert slots per track (quick-add Reverb, EQ, Compressor)
- Always left-aligned, horizontally scrollable

### Built-in Effects

Add any of these to a track with one click from the Effects panel or mixer FX slots:

| Effect | Description |
|--------|-------------|
| Reverb | Room size, damping, wet/dry |
| EQ | 4-band equalizer with per-band gain |
| Compressor | Threshold, ratio, attack, release |
| Delay | Delay time, feedback, mix |
| Chorus | Rate, depth, mix |
| Phaser | Rate, depth, feedback |
| Low Pass Filter | Cutoff frequency |
| Pitch Shift | Semitone shift |

### Effects Panel (right panel, tabbed)

- Select a track to see and edit its effect chain
- Add effects via the "+ Add Effect" button and dialog
- Per-effect bypass and remove controls
- Rotary knobs for up to 4 parameters per effect

### File Browser (right panel, tabbed)

- Browse your file system filtered to audio files
- Quick-jump locations: Desktop, Music, Documents, Home
- Drag files directly from the browser onto timeline tracks

### Project Management

- File > New Project -- start fresh
- File > Open Project -- load a `.tracktionedit` file
- File > Save / Save As -- save your arrangement

---

## Prerequisites

You need the following installed on Windows:

| Tool | Version | Notes |
|------|---------|-------|
| **Visual Studio 2022** | Community or higher | Provides the MSVC C++ compiler |
| **Qt 6.8+** | MSVC 2022 64-bit kit | Install via [Qt Online Installer](https://www.qt.io/download-qt-installer) or `aqtinstall` |
| **CMake** | 3.22+ | Bundled with Qt at `c:\qt\Tools\CMake_64\` |
| **Ninja** | 1.10+ | Bundled with Qt at `c:\qt\Tools\Ninja\` |
| **Git** | 2.x | For cloning and submodule management |

### Installing Qt MSVC kit via command line (optional)

If you only have the MinGW Qt kit, you can add the MSVC kit without the Qt Maintenance Tool:

```powershell
pip install aqtinstall
aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 --outputdir c:\qt
```

---

## Building

All commands are for **PowerShell on Windows**.

### 1. Clone the repository with submodules

```powershell
git clone --recurse-submodules https://github.com/your-username/AudioMixer.git
cd AudioMixer
```

If you already cloned without submodules:

```powershell
git submodule update --init --depth 1
```

### 2. Check out the correct JUCE version

Tracktion Engine requires a specific JUCE commit. After cloning:

```powershell
cd libs/JUCE
git fetch origin 7c89e11f6b7316c369f3d3f22227c60e816e738b
git checkout 7c89e11f6b7316c369f3d3f22227c60e816e738b
cd ../..
```

### 3. Configure with CMake

This must be run from a Visual Studio Developer Command Prompt (or use the `vcvarsall.bat` wrapper below):

```powershell
cmd /c "`"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat`" x64 && c:\qt\Tools\CMake_64\bin\cmake.exe -B build -G Ninja -DCMAKE_PREFIX_PATH=c:/qt/6.10.2/msvc2022_64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_MAKE_PROGRAM=c:/qt/Tools/Ninja/ninja.exe"
```

Adjust paths if your Qt or Visual Studio installation differs.

### 4. Build

```powershell
cmd /c "`"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat`" x64 && c:\qt\Tools\CMake_64\bin\cmake.exe --build build"
```

The first build takes 2-4 minutes (compiling JUCE and Tracktion Engine). Subsequent rebuilds are fast (10-15 seconds).

### 5. Run

```powershell
# Add Qt DLLs to PATH for runtime
$env:PATH = "c:\qt\6.10.2\msvc2022_64\bin;" + $env:PATH

# Launch
.\build\FreeDaw_artefacts\Debug\FreeDaw.exe
```

Or deploy Qt DLLs alongside the executable:

```powershell
c:\qt\6.10.2\msvc2022_64\bin\windeployqt.exe .\build\FreeDaw_artefacts\Debug\FreeDaw.exe
.\build\FreeDaw_artefacts\Debug\FreeDaw.exe
```

---

## How to Use FreeDaw

### Getting started

1. **Launch FreeDaw.** The window opens with one empty audio track, a transport bar at the top, track headers on the left, the timeline in the center, the mixer at the bottom, and the file browser / effects panel tabbed on the right.

2. **Set your tempo.** In the transport bar, adjust the BPM spinner (default 120). Change the time signature with the two spinners next to it (default 4/4).

3. **Set your snap mode.** Use the "Snap" dropdown in the transport bar to choose grid resolution: Beat (default), Bar, 1/2 Beat, 1/4 Beat, or Off.

### Adding audio to the timeline

1. In the **Browser** tab (right panel), navigate to a folder containing audio files. Use the dropdown at the top for quick access to Desktop, Music, Documents, or Home.

2. **Drag an audio file** from the browser and **drop it onto a track lane** in the timeline. The clip will snap to the grid at the drop position.

3. The clip appears with its waveform. You can **drag it** to reposition -- it snaps horizontally to the grid and vertically to track lanes.

4. To **add more tracks**, go to Edit > Add Audio Track (Ctrl+T).

### Playback

- Click **Play** (or press Space) to start playback from the current playhead position.
- Click **Stop** to stop and return the playhead to the beginning.
- **Click on the time ruler** (the bar/beat numbers at the top) to jump the playhead to that position (snapped to grid).
- **Click and drag on the ruler** to scrub smoothly through the timeline.
- Toggle **Loop** to loop playback over the current region.

### Mixing

Each track has controls in two places:

**Track headers (left of timeline):**
- **M** -- Mute the track
- **S** -- Solo the track (only this track plays)
- **R** -- Arm for recording
- **Volume slider** -- horizontal, adjusts track volume
- **Pan knob** -- drag up/down to pan left/right
- **Level meter** -- shows real-time audio level (green = good, yellow = hot, red = clipping)

**Mixer panel (bottom):**
- Same controls in a traditional vertical channel strip layout
- **Volume fader** -- vertical, drag up/down
- **Pan knob** -- rotary control
- **FX 1 / FX 2** dropdowns -- quick-add Reverb, EQ, or Compressor
- **Master strip** on the far right -- controls overall output level

### Adding effects

**Quick method:** In the mixer, use the FX 1 or FX 2 dropdown on any track's channel strip and select Reverb, EQ, or Compressor.

**Full method:**
1. Click on an FX dropdown in the mixer to open the **Effects** tab in the right panel.
2. Click **+ Add Effect** to open the effect selector dialog.
3. Choose from 8 built-in effects and click OK (or double-click).
4. The effect appears with parameter knobs. Adjust to taste.
5. Click **Byp** to bypass an effect, or **X** to remove it.

### Saving and loading

- **File > Save As** (Ctrl+Shift+S) -- save your project as a `.tracktionedit` file.
- **File > Save** (Ctrl+S) -- save to the current file.
- **File > Open** (Ctrl+O) -- load a previously saved project.
- **File > New** (Ctrl+N) -- start a fresh empty project.

### Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| Space | Play / Pause |
| R | Record |
| Ctrl+T | Add audio track |
| Ctrl+N | New project |
| Ctrl+O | Open project |
| Ctrl+S | Save project |
| Ctrl+Shift+S | Save As |
| Ctrl+= | Zoom in (timeline) |
| Ctrl+- | Zoom out (timeline) |

---

## Project Structure

```
AudioMixer/
  CMakeLists.txt                          Root build configuration
  README.md                               This file
  libs/
    JUCE/                                 JUCE framework (git submodule)
    tracktion_engine/                     Tracktion Engine (git submodule)
  src/
    main.cpp                              Entry point, JUCE-Qt bridge
    app/
      FreeDawApplication.h/cpp            Application lifecycle
      JuceQtBridge.h/cpp                  QTimer-based JUCE message pump
    engine/
      AudioEngine.h/cpp                   Wraps tracktion::engine::Engine
      EditManager.h/cpp                   Manages the current Edit (project)
      PluginScanner.h/cpp                 VST plugin scanning (stub)
    ui/
      MainWindow.h/cpp                    Main window with menus, docks, toolbar
      timeline/
        TimelineView.h/cpp                Arrangement view with track headers
        TimeRuler.h/cpp                   Beat/bar ruler with click and drag
        TrackHeaderWidget.h/cpp           Per-track controls and level meter
        TrackLane.h/cpp                   Track lane data model
        ClipItem.h/cpp                    Audio clip with snapped dragging
        GridSnapper.h/cpp                 Snap-to-grid logic
      mixer/
        MixerView.h/cpp                   Horizontal mixer panel
        ChannelStrip.h/cpp                Per-track mixer strip
      transport/
        TransportBar.h/cpp                Play/Stop/Record/Loop/BPM controls
      controls/
        RotaryKnob.h/cpp                  Custom painted rotary knob
        VolumeFader.h/cpp                 Custom painted vertical fader
        LevelMeter.h/cpp                  Animated green/yellow/red level meter
        WaveformWidget.h/cpp              Waveform rendering widget
      effects/
        EffectChainWidget.h/cpp           Per-track effect chain editor
        EffectSelectorDialog.h/cpp        Effect picker dialog
      browser/
        FileBrowserPanel.h/cpp            File system browser with drag support
    utils/
      WaveformCache.h/cpp                 Audio file waveform thumbnail cache
      ThemeManager.h/cpp                  Dark theme color management
```

## Architecture

FreeDaw bridges two frameworks:

- **Qt 6 Widgets** handles all GUI rendering, layout, and user interaction
- **Tracktion Engine** (built on JUCE) handles all audio: playback, recording, effects processing, plugin hosting, and the project data model

A `QTimer` pumps JUCE's `MessageManager` every 10ms from Qt's event loop, allowing both frameworks to coexist. Tracktion Engine's audio processing runs on its own real-time thread, independent of either GUI event loop.

---

## License

This project uses **Tracktion Engine** (GPLv3) and **JUCE** (AGPLv3).
FreeDaw is licensed under **GPLv3**.
