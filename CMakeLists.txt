cmake_minimum_required(VERSION 3.22)
project(FreeDaw VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Version from git tag ─────────────────────────────────────────────────────
# Runs early so the parsed version is available for JUCE's exe metadata.
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE FREEDAW_GIT_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_DESCRIBE_RESULT)
    if(NOT GIT_DESCRIBE_RESULT EQUAL 0)
        set(FREEDAW_GIT_RAW "dev")
    endif()
else()
    set(FREEDAW_GIT_RAW "dev")
endif()

# Strip leading "v" for display (v1.0.003 -> 1.0.003)
string(REGEX REPLACE "^v" "" FREEDAW_DISPLAY_VERSION "${FREEDAW_GIT_RAW}")

# Extract numeric major.minor.patch for Windows exe metadata
string(REGEX MATCH "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ver_match "${FREEDAW_GIT_RAW}")
if(_ver_match)
    math(EXPR _major "${CMAKE_MATCH_1}")
    math(EXPR _minor "${CMAKE_MATCH_2}")
    math(EXPR _patch "${CMAKE_MATCH_3}")
    set(FREEDAW_NUMERIC_VERSION "${_major}.${_minor}.${_patch}")
else()
    set(FREEDAW_NUMERIC_VERSION "0.0.0")
endif()

message(STATUS "FreeDaw display version : ${FREEDAW_DISPLAY_VERSION}")
message(STATUS "FreeDaw numeric version : ${FREEDAW_NUMERIC_VERSION}")

# ── Qt6 ──────────────────────────────────────────────────────────────────────
set(CMAKE_PREFIX_PATH "c:/qt/6.10.2/msvc2022_64" CACHE STRING "Qt prefix path")
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)
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
    src/ui/timeline/EnvelopeUtils.h
    src/ui/timeline/AutomationLaneItem.h  src/ui/timeline/AutomationLaneItem.cpp
    src/ui/timeline/AutomationPointItem.h src/ui/timeline/AutomationPointItem.cpp
    src/ui/timeline/AutomationLaneHeader.h src/ui/timeline/AutomationLaneHeader.cpp
    src/ui/mixer/MixerView.h        src/ui/mixer/MixerView.cpp
    src/ui/mixer/ChannelStrip.h     src/ui/mixer/ChannelStrip.cpp
    src/ui/transport/TransportBar.h src/ui/transport/TransportBar.cpp
    src/ui/controls/RotaryKnob.h    src/ui/controls/RotaryKnob.cpp
    src/ui/controls/VolumeFader.h   src/ui/controls/VolumeFader.cpp
    src/ui/controls/LevelMeter.h    src/ui/controls/LevelMeter.cpp
    src/ui/controls/WaveformWidget.h src/ui/controls/WaveformWidget.cpp
    src/ui/dialogs/ExportDialog.h        src/ui/dialogs/ExportDialog.cpp
    src/ui/dialogs/RecoveryDialog.h      src/ui/dialogs/RecoveryDialog.cpp
    src/ui/dialogs/AudioSettingsDialog.h src/ui/dialogs/AudioSettingsDialog.cpp
    src/ui/effects/EffectChainWidget.h   src/ui/effects/EffectChainWidget.cpp
    src/ui/effects/DelayEffectWidget.h   src/ui/effects/DelayEffectWidget.cpp
    src/ui/effects/EffectSelectorDialog.h src/ui/effects/EffectSelectorDialog.cpp
    src/ui/effects/VstSelectorDialog.h    src/ui/effects/VstSelectorDialog.cpp
    src/ui/effects/PluginEditorWindow.h   src/ui/effects/PluginEditorWindow.cpp
    src/ui/audioclip/WaveformSelection.h
    src/ui/audioclip/AudioFileUndoManager.h  src/ui/audioclip/AudioFileUndoManager.cpp
    src/ui/audioclip/AudioFileOperations.h   src/ui/audioclip/AudioFileOperations.cpp
    src/ui/audioclip/AudioClipEditor.h       src/ui/audioclip/AudioClipEditor.cpp
    src/ui/audioclip/AudioWaveformView.h     src/ui/audioclip/AudioWaveformView.cpp
    src/ui/pianoroll/PianoRollEditor.h    src/ui/pianoroll/PianoRollEditor.cpp
    src/ui/pianoroll/PianoKeyboard.h      src/ui/pianoroll/PianoKeyboard.cpp
    src/ui/pianoroll/NoteGrid.h           src/ui/pianoroll/NoteGrid.cpp
    src/ui/pianoroll/NoteItem.h           src/ui/pianoroll/NoteItem.cpp
    src/ui/pianoroll/VelocityLane.h       src/ui/pianoroll/VelocityLane.cpp
    src/ui/pianoroll/CcLane.h             src/ui/pianoroll/CcLane.cpp
    src/ui/pianoroll/ChannelColors.h
    src/ui/pianoroll/PianoRollRuler.h     src/ui/pianoroll/PianoRollRuler.cpp
    src/ui/routing/RoutingView.h          src/ui/routing/RoutingView.cpp
    src/ui/routing/RoutingNode.h          src/ui/routing/RoutingNode.cpp
    src/ui/routing/CableItem.h            src/ui/routing/CableItem.cpp
    src/ui/browser/FileBrowserPanel.h    src/ui/browser/FileBrowserPanel.cpp
    src/ui/SplashScreen.h           src/ui/SplashScreen.cpp
    src/utils/WaveformCache.h       src/utils/WaveformCache.cpp
    src/utils/ThemeManager.h        src/utils/ThemeManager.cpp
    src/utils/IconFont.h
    src/ai/AiTypes.h
    src/ai/AiAudioAnalysis.h      src/ai/AiAudioAnalysis.cpp
    src/ai/AiToolDefs.h            src/ai/AiToolDefs.cpp
    src/ai/AiToolExecutor.h        src/ai/AiToolExecutor.cpp
    src/ai/AiService.h             src/ai/AiService.cpp
    src/ai/AiChatWidget.h          src/ai/AiChatWidget.cpp
    src/ai/AiQuickPrompt.h         src/ai/AiQuickPrompt.cpp
)

# ── Create the FreeDaw JUCE GUI App target ───────────────────────────────────
juce_add_gui_app(FreeDaw
    VERSION "${FREEDAW_NUMERIC_VERSION}"
    PRODUCT_NAME "FreeDaw"
    COMPANY_NAME "FreeDaw"
    BUNDLE_ID "com.freedaw.app"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "FreeDaw needs microphone access for audio recording")

juce_generate_juce_header(FreeDaw)

target_sources(FreeDaw PRIVATE ${FREEDAW_SOURCES})
qt_add_resources(FreeDaw "freedaw_resources" PREFIX "/" BASE resources FILES
    resources/splash.png
    resources/fontaudio.ttf
    resources/MaterialSymbolsOutlined.ttf)
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
    Qt6::Network
    Qt6::Concurrent
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

target_compile_definitions(FreeDaw PRIVATE
    FREEDAW_VERSION="${FREEDAW_DISPLAY_VERSION}")

if(WIN32)
    target_link_libraries(FreeDaw PRIVATE winmm)
endif()
