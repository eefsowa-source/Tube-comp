cmake_minimum_required(VERSION 3.22)

project(eonchild VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(/Users/sungha/JUCE JUCE)

include(FetchContent)
FetchContent_Declare(melatonin_inspector
    GIT_REPOSITORY https://github.com/sudara/melatonin_inspector.git
    GIT_TAG 9c483f865854d83fe9873e1c20e3fd772f695f80
    # juce_add_module() derives the module name from the directory name, so
    # the default "<name>-src" checkout would fail the module-header lookup.
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/melatonin_inspector
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(melatonin_inspector)

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
target_link_libraries(eonchild PUBLIC "$<$<CONFIG:Debug>:melatonin_inspector>")
target_compile_definitions(eonchild PUBLIC "$<$<CONFIG:Debug>:MELATONIN_INSPECTOR_ENABLED=1>")

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

add_executable(eonchild_CompressorFeedbackTest
    Tests/CompressorFeedbackTest.cpp
    Source/DSP/Compressor.cpp)

target_link_libraries(eonchild_CompressorFeedbackTest PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_CompressorFeedbackTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_executable(eonchild_TriodeBiasProgramTest
    Tests/TriodeBiasProgramTest.cpp
    Source/DSP/TriodeStage.cpp)

target_link_libraries(eonchild_TriodeBiasProgramTest PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_TriodeBiasProgramTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_executable(eonchild_KorenSlopeTest
    Tests/KorenSlopeTest.cpp)

target_link_libraries(eonchild_KorenSlopeTest PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_KorenSlopeTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_executable(eonchild_DryDelayLineTest
    Tests/DryDelayLineTest.cpp)

target_link_libraries(eonchild_DryDelayLineTest PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_DryDelayLineTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_executable(eonchild_SaturatorTest
    Tests/SaturatorTest.cpp
    Source/DSP/TubeSaturator.cpp)

target_link_libraries(eonchild_SaturatorTest PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
target_compile_definitions(eonchild_SaturatorTest PRIVATE JUCE_STANDALONE_APPLICATION=1)

# End-to-end processor check: links the plugin's own shared code so it exercises
# the real processBlock latency reporting, dry alignment, and bypass path.
add_executable(eonchild_ProcessorTest
    Tests/ProcessorTest.cpp)

target_link_libraries(eonchild_ProcessorTest PRIVATE
    eonchild
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Measurement harness, not a pass/fail test: prints the tone/aliasing/cost baseline
# for every quality, model and anti-aliasing setting.
add_executable(eonchild_SoundBaseline
    Tools/sound_baseline.cpp)

target_link_libraries(eonchild_SoundBaseline PRIVATE
    eonchild
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Deterministic render + sample-exact comparison tool, used to prove that a DSP
# change is a no-op before any listening or cost measurement.
add_executable(eonchild_NullRender
    Tools/null_render.cpp)

target_link_libraries(eonchild_NullRender PRIVATE
    eonchild
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Offline editor snapshot tool: paints the real editor to a PNG so faceplate
# review does not require a DAW session. Not registered as a CTest case --
# it produces an artifact rather than a pass/fail verdict.
add_executable(eonchild_EditorSnapshot
    Tools/editor_snapshot.cpp)

target_link_libraries(eonchild_EditorSnapshot PRIVATE
    eonchild
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)

enable_testing()
add_test(NAME dsp_sanity COMMAND eonchild_DspTest)
add_test(NAME compressor_sanity COMMAND eonchild_CompressorTest)
add_test(NAME compressor_program COMMAND eonchild_CompressorProgramTest)
add_test(NAME compressor_feedback COMMAND eonchild_CompressorFeedbackTest)
add_test(NAME triode_bias_program COMMAND eonchild_TriodeBiasProgramTest)
add_test(NAME dry_delay_line COMMAND eonchild_DryDelayLineTest)
add_test(NAME saturator_shape COMMAND eonchild_SaturatorTest)
add_test(NAME processor_latency COMMAND eonchild_ProcessorTest)
add_test(NAME koren_slope COMMAND eonchild_KorenSlopeTest)
