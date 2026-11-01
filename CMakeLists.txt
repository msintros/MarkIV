cmake_minimum_required(VERSION 3.22)

project(MarkIVPreamp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(MARKIV_BUILD_PLUGIN "Build the JUCE plugin (downloads JUCE)" ON)
option(MARKIV_BUILD_TESTS "Build the DSP test programs" ON)

# ---------------------------------------------------------------------------
# Circuit model: plain C++, no JUCE dependency.
add_library(markiv_dsp STATIC
    source/dsp/DKSolver.cpp
    source/dsp/MarkIVPreamp.cpp
    source/dsp/GraphicEQ.cpp)
target_include_directories(markiv_dsp PUBLIC source/dsp)
set_target_properties(markiv_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(markiv_dsp PRIVATE -O3)
endif()

if(MARKIV_BUILD_TESTS)
    enable_testing()
    add_executable(markiv_tests tests/model_tests.cpp)
    target_link_libraries(markiv_tests PRIVATE markiv_dsp)
    add_test(NAME model_tests COMMAND markiv_tests)

    add_executable(markiv_render tests/render.cpp)
    target_link_libraries(markiv_render PRIVATE markiv_dsp)

    add_executable(markiv_response tests/response_test.cpp)
    target_link_libraries(markiv_response PRIVATE markiv_dsp)
endif()

# ---------------------------------------------------------------------------
# Plugin.
if(MARKIV_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    set(MARKIV_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND MARKIV_FORMATS AU)
    endif()

    juce_add_plugin(MarkIVPreamp
        COMPANY_NAME "msintros"
        PLUGIN_MANUFACTURER_CODE Msin
        PLUGIN_CODE Mk4p
        FORMATS ${MARKIV_FORMATS}
        PRODUCT_NAME "Mark IV Preamp"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Distortion
        AU_MAIN_TYPE kAudioUnitType_Effect)

    target_sources(MarkIVPreamp PRIVATE
        source/plugin/PluginProcessor.cpp
        source/plugin/PluginEditor.cpp
        source/plugin/CabSim.cpp)

    target_compile_definitions(MarkIVPreamp PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(MarkIVPreamp
        PRIVATE
            markiv_dsp
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(MARKIV_BUILD_TESTS)
        # Headless smoke test of the plugin processor (no GUI).
        # It links the plugin's shared-code library, which already contains the
        # compiled JUCE modules, and borrows that target's include paths and defines.
        add_executable(MarkIVPluginSmoke tests/plugin_smoke.cpp)
        target_include_directories(MarkIVPluginSmoke PRIVATE $<TARGET_PROPERTY:MarkIVPreamp,INCLUDE_DIRECTORIES>)
        target_compile_definitions(MarkIVPluginSmoke PRIVATE $<TARGET_PROPERTY:MarkIVPreamp,COMPILE_DEFINITIONS>)
        target_link_libraries(MarkIVPluginSmoke PRIVATE MarkIVPreamp)
    endif()
endif()
