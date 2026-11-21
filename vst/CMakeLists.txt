cmake_minimum_required(VERSION 3.22)

project(ResoniqueVST VERSION 0.2.0 LANGUAGES C CXX)

# Link the MSVC runtime statically so Resonique.exe and Resonique.vst3 run on PCs
# without the Visual C++ Redistributable installed (family convention).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if RESONIQUE_JUCE_DIR is given, otherwise fetch it.
# build.bat passes a sibling project's download (E-Piano, Eight Ball, ...) when one exists.
# ---------------------------------------------------------------------------
set(RESONIQUE_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")
set(RESONIQUE_JUCE_TAG "8.0.17-security" CACHE STRING "JUCE git tag to fetch")

if(RESONIQUE_JUCE_DIR AND EXISTS "${RESONIQUE_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${RESONIQUE_JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${RESONIQUE_JUCE_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin: VST3 + Standalone
# ---------------------------------------------------------------------------
juce_add_plugin(ResoniqueVST
    COMPANY_NAME            "Resonique"
    PRODUCT_NAME            "Resonique"
    ICON_BIG                "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    PLUGIN_MANUFACTURER_CODE Rsnq
    PLUGIN_CODE             Rsq1
    FORMATS                 VST3 Standalone
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES         Instrument Synth Piano
    NEEDS_WEBVIEW2          TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# The UI (HTML/CSS/JS/SVG) and the factory presets are compiled into the binary.
file(GLOB_RECURSE RESONIQUE_UI_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.svg")
juce_add_binary_data(ResoniqueUI SOURCES ${RESONIQUE_UI_FILES} presets/factory_presets.json)

# The DSP core is JUCE-free (Source/dsp, Source/instruments, PianoEngine.h): plain C++20.
# The per-sample kernels are compiled twice (baseline and AVX2) and selected at run time.
set(RESONIQUE_KERNEL_SOURCES
    Source/dsp/Kernels.cpp
    Source/dsp/Kernels_sse.cpp
    Source/dsp/Kernels_avx2.cpp)
if(MSVC)
    set_source_files_properties(Source/dsp/Kernels_avx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    set_source_files_properties(Source/dsp/Kernels_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
endif()
# On other architectures (arm64 Android, where VST gHost builds the plugin natively) Kernels_avx2.cpp
# is a second baseline build; cpuSupportsAvx2() is false there, so it is never selected.

set(RESONIQUE_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp
    Source/TableStore.cpp
    ${RESONIQUE_KERNEL_SOURCES})

target_sources(ResoniqueVST PRIVATE ${RESONIQUE_PLUGIN_SOURCES})
target_include_directories(ResoniqueVST PRIVATE Source)

target_compile_definitions(ResoniqueVST PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    # the modal banks are plain float loops; let the compiler vectorise them
    target_compile_options(ResoniqueVST PRIVATE /fp:fast /utf-8)
endif()

target_link_libraries(ResoniqueVST
    PRIVATE
        ResoniqueUI
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Console tests + engine tools:  --target ResoniqueTests
#   ResoniqueTests.exe                    DSP, acoustic, preset and state tests
#   ResoniqueTests.exe --bench            CPU benchmark
#   ResoniqueTests.exe --render <dir>     diagnostic WAVs + spectrum CSVs
#   ResoniqueTests.exe --host-check <Resonique.vst3>
# ---------------------------------------------------------------------------
juce_add_console_app(ResoniqueTests PRODUCT_NAME "ResoniqueTests")
target_sources(ResoniqueTests PRIVATE
    tests/TestMain.cpp tests/DSPTests.cpp tests/AcousticTests.cpp tests/PerformanceTests.cpp
    tests/PresetTests.cpp tests/HostCheck.cpp tests/EngineTools.cpp tests/CompareTool.cpp tests/InstrumentTests.cpp tests/RobustnessTests.cpp
    Source/PresetManager.cpp Source/TableStore.cpp ${RESONIQUE_KERNEL_SOURCES})
target_include_directories(ResoniqueTests PRIVATE Source)
target_compile_definitions(ResoniqueTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1)
if(MSVC)
    target_compile_options(ResoniqueTests PRIVATE /fp:fast /utf-8)
endif()
target_link_libraries(ResoniqueTests PRIVATE ResoniqueUI juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
