cmake_minimum_required(VERSION 3.22)

project(OniriqueDoubleBox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary, and the oldest macOS the JUCE 9 runtime supports.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE is looked for next to this file first, then in the PitchBox checkout
# beside this one, so the plugins of the range can share a single copy.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(DOUBLEBOX_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
else()
    set(DOUBLEBOX_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../PitchBox/JUCE")
endif()

set(DOUBLEBOX_JUCE_DIR "${DOUBLEBOX_JUCE_DEFAULT}" CACHE PATH "Path to JUCE")

if(NOT EXISTS "${DOUBLEBOX_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${DOUBLEBOX_JUCE_DIR}.\n"
        "Run:  git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git")
endif()

add_subdirectory("${DOUBLEBOX_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The DSP, built on its own so the offline tests can link it without dragging
# in JUCE. Everything that decides how the plugin sounds lives here.
add_library(doublebox_core STATIC
    Source/dsp/PeriodTracker.cpp
    Source/dsp/Doubler.cpp)

target_include_directories(doublebox_core PUBLIC Source)
set_target_properties(doublebox_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # MSVC's <cmath> only declares M_PI when asked. PUBLIC so the tests get it too.
    target_compile_definitions(doublebox_core PUBLIC _USE_MATH_DEFINES)
    target_compile_options(doublebox_core PRIVATE /W4)
else()
    target_compile_options(doublebox_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The plugin.
set(DOUBLEBOX_FORMATS VST3)

if(APPLE)
    list(APPEND DOUBLEBOX_FORMATS AU)
endif()

# The system VST3 folder on Windows is under Program Files, which a normal build
# cannot write to, so installing after the build is opt-in there.
if(WIN32)
    option(DOUBLEBOX_COPY_AFTER_BUILD "Install the plugin after building" OFF)
else()
    option(DOUBLEBOX_COPY_AFTER_BUILD "Install the plugin after building" ON)
endif()

juce_add_plugin(OniriqueDoubleBox
    COMPANY_NAME "Onirique"
    BUNDLE_ID "com.onirique.doublebox"
    PLUGIN_MANUFACTURER_CODE Onrq
    PLUGIN_CODE Dbbx
    FORMATS ${DOUBLEBOX_FORMATS}
    PRODUCT_NAME "DoubleBox"
    # What hosts list it as. The file on disk keeps the short name.
    PLUGIN_NAME "ONIRIQUE DoubleBox"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    # For typing a value, and a preset's name.
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Delay Modulation
    COPY_PLUGIN_AFTER_BUILD ${DOUBLEBOX_COPY_AFTER_BUILD})

# The Onirique sigil, as it appears in the header of the site.
juce_add_binary_data(DoubleBoxAssets SOURCES Resources/sigil.png)

target_sources(OniriqueDoubleBox PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/EqDisplay.cpp
    Source/ui/PresetBrowser.cpp
    Source/ui/StageDisplay.cpp
    Source/ui/Theme.cpp)

target_compile_definitions(OniriqueDoubleBox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# The descriptions are in French: the sources are UTF-8, and MSVC has to be told.
if(MSVC)
    target_compile_options(OniriqueDoubleBox PRIVATE /utf-8)
endif()

# The ONIRIQUE licence: the file ONIRIQUE CENTRAL writes is checked, and the editor
# shows the activation page. See License/README.md.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueDoubleBox)

target_link_libraries(OniriqueDoubleBox
    PRIVATE
        doublebox_core
        DoubleBoxAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test: the whole audio path without a host.
enable_testing()

add_executable(test_doubler tests/test_doubler.cpp)
target_link_libraries(test_doubler PRIVATE doublebox_core)
target_include_directories(test_doubler PRIVATE tests)
add_test(NAME doubler COMMAND test_doubler)

# A development tool, not part of the product: renders the editor to a PNG
# without a host. Built only when asked for by name.
add_executable(doublebox_snapshot EXCLUDE_FROM_ALL tools/snapshot.cpp)

target_include_directories(doublebox_snapshot PRIVATE
    tests
    $<TARGET_PROPERTY:OniriqueDoubleBox,INCLUDE_DIRECTORIES>)

target_compile_definitions(doublebox_snapshot PRIVATE
    $<TARGET_PROPERTY:OniriqueDoubleBox,COMPILE_DEFINITIONS>)

target_link_libraries(doublebox_snapshot PRIVATE OniriqueDoubleBox doublebox_core DoubleBoxAssets)

# Another development tool: the doubler run over a real recording, each
# setting written out as a file to be listened to.
add_executable(doublebox_voicecheck EXCLUDE_FROM_ALL tools/voicecheck.cpp)
target_link_libraries(doublebox_voicecheck PRIVATE doublebox_core)
target_include_directories(doublebox_voicecheck PRIVATE tests)
