# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**FreeDaw** — a free, open-source DAW for Windows. Qt 6 handles all GUI; Tracktion Engine (built on JUCE) handles all audio. The binary is produced at `build\FreeDaw_artefacts\Debug\FreeDaw.exe`.

## Build Commands

All commands must run from a **Visual Studio Developer Command Prompt** (or wrap with `vcvarsall.bat`):

**Configure:**
```powershell
cmd /c "`"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat`" x64 && c:\qt\Tools\CMake_64\bin\cmake.exe -B build -G Ninja -DCMAKE_PREFIX_PATH=c:/qt/6.10.2/msvc2022_64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_MAKE_PROGRAM=c:/qt/Tools/Ninja/ninja.exe"
```

**Build:**
```powershell
cmd /c "`"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat`" x64 && c:\qt\Tools\CMake_64\bin\cmake.exe --build build"
```

**Run:**
```powershell
$env:PATH = "c:\qt\6.10.2\msvc2022_64\bin;" + $env:PATH
.\build\FreeDaw_artefacts\Debug\FreeDaw.exe
```

First build takes 2-4 minutes (compiling JUCE/Tracktion). Incremental rebuilds take ~10-15 seconds. There is no test suite.

## Architecture

### Framework Bridge

Two frameworks coexist via `JuceQtBridge` (`src/app/JuceQtBridge.h`): a `QTimer` fires every 10ms to pump JUCE's `MessageManager` from inside Qt's event loop. JUCE's audio processing runs on its own real-time thread, independent of both GUI event loops. This pump must be running before any Tracktion Engine calls.

### Ownership Chain

```
main.cpp
  └─ FreeDawApplication          owns all singletons
       ├─ JuceQtBridge           JUCE message pump
       ├─ AudioEngine            wraps te::Engine
       ├─ EditManager            wraps te::Edit (the project document)
       ├─ PluginScanner          VST scan (stub)
       └─ MainWindow             QMainWindow with dock layout
```

`FreeDawApplication` is the single place to access engine/edit globals — never create additional `te::Engine` instances.

### Tracktion Engine Integration

- Namespace alias: `namespace te = tracktion::engine;` (defined in `AudioEngine.h`, used throughout)
- The `te::Edit` object is the project data model (tracks, clips, tempo, effects). It is owned by `EditManager`.
- `EditManager` exposes Qt signals (`editChanged`, `tracksChanged`, `transportStateChanged`) so Qt widgets can react to engine-side changes.
- Audio clips in Tracktion are positioned by **beat**, not seconds. `addAudioClipToTrack` takes a `double startBeat`.
- Level metering: each track needs a `te::LevelMeterPlugin` inserted into its plugin chain. `EditManager::ensureLevelMetersOnAllTracks()` handles this. `ChannelStrip` polls `te::LevelMeasurer::Client` via a `QTimer`.
- The project file format is `.tracktionedit` (Tracktion's native XML format).

### UI Layout

`MainWindow` uses Qt dock widgets:
- **Central widget**: `TimelineView` (arrangement)
- **Bottom dock**: `MixerView` (channel strips)
- **Right dock** (tabbed): `EffectChainWidget` + `FileBrowserPanel`
- **Top toolbar**: `TransportBar`

### Timeline Coordinates

`TimelineView` uses `QGraphicsScene`/`QGraphicsView` for the clip area. The coordinate system is pixels, converted to/from beats via `pixelsPerBeat_` (default 40.0 px/beat). `GridSnapper` handles snap-to-grid logic for all drag operations.

### Naming & Namespace

All project code lives in the `freedaw` namespace. The `DONT_SET_USING_JUCE_NAMESPACE=1` compile definition prevents JUCE from polluting the global namespace — always qualify JUCE types with `juce::` or use the `te::` alias.

## Key Files

| File | Purpose |
|------|---------|
| `src/app/FreeDawApplication.h` | Top-level app object; entry point for accessing engine/edit |
| `src/engine/EditManager.h` | Project document API; Qt signals for track/transport changes |
| `src/engine/AudioEngine.h` | Wraps `te::Engine`; device management |
| `src/app/JuceQtBridge.h` | QTimer-based JUCE message pump |
| `src/ui/timeline/GridSnapper.h` | Beat/pixel conversion and snap logic |
| `CMakeLists.txt` | Single build file; all sources listed here |

## Submodules

- `libs/JUCE` — pinned to commit `7c89e11` (required by Tracktion Engine)
- `libs/tracktion_engine` — audio engine

After cloning without `--recurse-submodules`, run:
```powershell
git submodule update --init --depth 1
cd libs/JUCE && git checkout 7c89e11f6b7316c369f3d3f22227c60e816e738b
```
