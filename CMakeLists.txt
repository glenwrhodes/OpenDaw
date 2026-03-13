cmake_minimum_required(VERSION 3.22)
project(FreeDaw VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Qt6 ──────────────────────────────────────────────────────────────────────
set(CMAKE_PREFIX_PATH "c:/qt/6.10.2/msvc2022_64" CACHE STRING "Qt prefix path")
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# ── JUCE ─────────────────────────────────────────────────────────────────────
set(JUCE_MODULES_ONLY OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/JUCE)

# ── Tracktion Engine (detects JUCE automatically) ────────────────────────────
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/tracktion_engine)

# ── Source files ─────────────────────────────────────────────────────────────
set(FREEDAW_SOURCES
    src/main.cpp
    src/app/FreeDawApplication.h    src/app/FreeDawApplication.cpp
    src/app/JuceQtBridge.h          src/app/JuceQtBridge.cpp
    src/engine/AudioEngine.h        src/engine/AudioEngine.cpp
    src/engine/EditManager.h        src/engine/EditManager.cpp
    src/engine/PluginScanner.h      src/engine/PluginScanner.cpp
    src/ui/MainWindow.h             src/ui/MainWindow.cpp
    src/ui/timeline/TimelineView.h  src/ui/timeline/TimelineView.cpp
    src/ui/timeline/TimeRuler.h     src/ui/timeline/TimeRuler.cpp
    src/ui/timeline/TrackLane.h     src/ui/timeline/TrackLane.cpp
    src/ui/timeline/ClipItem.h      src/ui/timeline/ClipItem.cpp
    src/ui/timeline/GridSnapper.h   src/ui/timeline/GridSnapper.cpp
    src/ui/timeline/TrackHeaderWidget.h src/ui/timeline/TrackHeaderWidget.cpp
    src/ui/mixer/MixerView.h        src/ui/mixer/MixerView.cpp
    src/ui/mixer/ChannelStrip.h     src/ui/mixer/ChannelStrip.cpp
    src/ui/transport/TransportBar.h src/ui/transport/TransportBar.cpp
    src/ui/controls/RotaryKnob.h    src/ui/controls/RotaryKnob.cpp
    src/ui/controls/VolumeFader.h   src/ui/controls/VolumeFader.cpp
    src/ui/controls/LevelMeter.h    src/ui/controls/LevelMeter.cpp
    src/ui/controls/WaveformWidget.h src/ui/controls/WaveformWidget.cpp
    src/ui/effects/EffectChainWidget.h   src/ui/effects/EffectChainWidget.cpp
    src/ui/effects/EffectSelectorDialog.h src/ui/effects/EffectSelectorDialog.cpp
    src/ui/effects/VstSelectorDialog.h    src/ui/effects/VstSelectorDialog.cpp
    src/ui/effects/PluginEditorWindow.h   src/ui/effects/PluginEditorWindow.cpp
    src/ui/pianoroll/PianoRollEditor.h    src/ui/pianoroll/PianoRollEditor.cpp
    src/ui/pianoroll/PianoKeyboard.h      src/ui/pianoroll/PianoKeyboard.cpp
    src/ui/pianoroll/NoteGrid.h           src/ui/pianoroll/NoteGrid.cpp
    src/ui/pianoroll/NoteItem.h           src/ui/pianoroll/NoteItem.cpp
    src/ui/pianoroll/VelocityLane.h       src/ui/pianoroll/VelocityLane.cpp
    src/ui/browser/FileBrowserPanel.h    src/ui/browser/FileBrowserPanel.cpp
    src/ui/SplashScreen.h           src/ui/SplashScreen.cpp
    src/utils/WaveformCache.h       src/utils/WaveformCache.cpp
    src/utils/ThemeManager.h        src/utils/ThemeManager.cpp
)

# ── Create the FreeDaw JUCE GUI App target ───────────────────────────────────
juce_add_gui_app(FreeDaw
    PRODUCT_NAME "FreeDaw"
    COMPANY_NAME "FreeDaw"
    BUNDLE_ID "com.freedaw.app"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "FreeDaw needs microphone access for audio recording")

juce_generate_juce_header(FreeDaw)

target_sources(FreeDaw PRIVATE ${FREEDAW_SOURCES})
qt_add_resources(FreeDaw "freedaw_resources" PREFIX "/" FILES resources/splash.png)
target_include_directories(FreeDaw PRIVATE src)

target_compile_definitions(FreeDaw PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    DONT_SET_USING_JUCE_NAMESPACE=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(FreeDaw PRIVATE
    Qt6::Widgets
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_events
    juce::juce_recommended_warning_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(FreeDaw PRIVATE -Wno-unknown-pragmas)
endif()

if(MSVC)
    target_compile_options(FreeDaw PRIVATE /EHsc /bigobj)
endif()

if(WIN32)
    target_link_libraries(FreeDaw PRIVATE winmm)
endif()
