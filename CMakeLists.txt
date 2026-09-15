cmake_minimum_required(VERSION 3.22)

project(eonchild VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(/Users/sungha/JUCE JUCE)

juce_add_plugin(eonchild
    COMPANY_NAME "EON"
    PLUGIN_MANUFACTURER_CODE Eonn
    PLUGIN_CODE Eonc
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "EON-Vari mu DSP"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

juce_generate_juce_header(eonchild)

target_sources(eonchild
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DSP/TubeSaturator.cpp
        Source/DSP/TriodeStage.cpp
        Source/DSP/Compressor.cpp
        Source/UI/TubeCompLookAndFeel.cpp
        Source/UI/VUMeterComponent.cpp)

target_compile_definitions(eonchild
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(eonchild
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP sanity check (no DAW/GUI needed) -- feeds a stress signal through
# the WDF triode model and checks for NaN/Inf/divergence.
add_executable(eonchild_DspTest
    Tests/DspSanityTest.cpp
    Source/DSP/TriodeStage.cpp)

target_link_libraries(eonchild_DspTest PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_DspTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_executable(eonchild_CompressorTest
    Tests/CompressorSanityTest.cpp
    Source/DSP/Compressor.cpp)

target_link_libraries(eonchild_CompressorTest PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_CompressorTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_executable(eonchild_CompressorProgramTest
    Tests/CompressorProgramTest.cpp
    Source/DSP/Compressor.cpp)

target_link_libraries(eonchild_CompressorProgramTest PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_CompressorProgramTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_executable(eonchild_TriodeBiasProgramTest
    Tests/TriodeBiasProgramTest.cpp
    Source/DSP/TriodeStage.cpp)

target_link_libraries(eonchild_TriodeBiasProgramTest PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_TriodeBiasProgramTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

enable_testing()
add_test(NAME dsp_sanity COMMAND eonchild_DspTest)
add_test(NAME compressor_sanity COMMAND eonchild_CompressorTest)
add_test(NAME compressor_program COMMAND eonchild_CompressorProgramTest)
add_test(NAME triode_bias_program COMMAND eonchild_TriodeBiasProgramTest)
