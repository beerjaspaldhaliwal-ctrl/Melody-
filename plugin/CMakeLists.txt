cmake_minimum_required(VERSION 3.22)

project(Melodyverse VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()
# Static MSVC runtime so the plugin loads on machines without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MELODYVERSE_BUILD_PLUGIN "Build the JUCE VST3/Standalone plugin" ON)
option(MELODYVERSE_BUILD_TESTS "Build the core unit tests" ON)

# ---------------------------------------------------------------------------
# Core engine: pure C++17, no dependencies
# ---------------------------------------------------------------------------
add_library(melodyverse_core STATIC
    core/Theory.cpp
    core/Styles.cpp
    core/Generator.cpp
    core/Analysis.cpp
    core/Synth.cpp
    core/MidiFile.cpp
    core/Vocal.cpp
    core/MelodyLab.cpp)
target_include_directories(melodyverse_core PUBLIC core)
if(MSVC)
    target_compile_options(melodyverse_core PRIVATE /utf-8 /W3)
else()
    target_compile_options(melodyverse_core PRIVATE -Wall -Wextra)
endif()

if(MELODYVERSE_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE melodyverse_core)
    if(MSVC)
        target_compile_options(core_tests PRIVATE /utf-8)
    endif()
    add_test(NAME core_tests COMMAND core_tests)
endif()

# ---------------------------------------------------------------------------
# JUCE plugin
# ---------------------------------------------------------------------------
if(MELODYVERSE_BUILD_PLUGIN)
    set(JUCE_DIR "" CACHE PATH "Path to a JUCE checkout (leave empty to download JUCE 8.0.4)")
    if(JUCE_DIR)
        add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(MELODYVERSE_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND MELODYVERSE_FORMATS AU)
    endif()

    juce_add_plugin(Melodyverse
        COMPANY_NAME "Melodyverse"
        COMPANY_WEBSITE "https://github.com/beerjaspaldhaliwal-ctrl/Melody-"
        BUNDLE_ID com.melodyverse.plugin
        PLUGIN_MANUFACTURER_CODE Mlvs
        PLUGIN_CODE Mlv1
        PRODUCT_NAME "Melodyverse"
        VERSION ${PROJECT_VERSION}
        FORMATS ${MELODYVERSE_FORMATS}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Generator
        AU_MAIN_TYPE kAudioUnitType_MusicDevice)

    target_sources(Melodyverse PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/PianoRoll.cpp)

    target_compile_definitions(Melodyverse PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Melodyverse
        PRIVATE
            melodyverse_core
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(Melodyverse PRIVATE /utf-8)
    endif()
endif()

if(MELODYVERSE_BUILD_TESTS)
    add_executable(analyze_wav tests/analyze_wav.cpp)
    target_link_libraries(analyze_wav PRIVATE melodyverse_core)
endif()

if(MELODYVERSE_BUILD_PLUGIN AND MELODYVERSE_BUILD_TESTS)
    juce_add_console_app(plugin_tests PRODUCT_NAME "MelodyversePluginTests")
    target_sources(plugin_tests PRIVATE
        tests/plugin_tests.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/PianoRoll.cpp)
    target_compile_definitions(plugin_tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_STANDALONE_APPLICATION=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(plugin_tests PRIVATE
        melodyverse_core
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    if(MSVC)
        target_compile_options(plugin_tests PRIVATE /utf-8)
    endif()
endif()
