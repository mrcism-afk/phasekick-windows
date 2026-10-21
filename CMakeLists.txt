cmake_minimum_required(VERSION 3.22)
project(Phasekick VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(Phasekick
 COMPANY_NAME "Phasekick Audio"
 BUNDLE_ID "audio.phasekick.synth"
 PLUGIN_MANUFACTURER_CODE Phks
 PLUGIN_CODE Phk1
 FORMATS VST3 Standalone
 PRODUCT_NAME "Phasekick"
 IS_SYNTH TRUE
 NEEDS_MIDI_INPUT TRUE
 NEEDS_MIDI_OUTPUT FALSE
 IS_MIDI_EFFECT FALSE
 EDITOR_WANTS_KEYBOARD_FOCUS FALSE
 VST3_CATEGORIES Instrument Synth
 COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(Phasekick PRIVATE src/JucePlugin.cpp)
target_compile_definitions(Phasekick PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Phasekick PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
