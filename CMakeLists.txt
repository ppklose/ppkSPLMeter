cmake_minimum_required(VERSION 3.22)
project(SPLMeter VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Fetch JUCE 8.0.7 (matches the Projucer version used for the macOS build)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(SPLMeter
    VERSION                     1.0.0
    PLUGIN_MANUFACTURER_CODE    Manu
    PLUGIN_CODE                 Splm
    FORMATS                     Standalone VST3
    PRODUCT_NAME                "SPLMeter"
    COMPANY_NAME                "Philipp Paul Klose Audio Engineering"
    BUNDLE_ID                   com.PhilippPaulKloseAudioEngineering.SPLMeter
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

juce_generate_juce_header(SPLMeter)

target_sources(SPLMeter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/MeterComponent.cpp
    Source/LogComponent.cpp
)

target_include_directories(SPLMeter PRIVATE Source)

target_compile_definitions(SPLMeter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(SPLMeter
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
