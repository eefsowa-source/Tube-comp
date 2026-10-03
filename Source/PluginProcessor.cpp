#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SnappingBoolParameter.h"

#include <cmath>

namespace
{
/** Vari-mu coupling depth: volts of grid-bias shift per dB of gain reduction.
    With TriodeStage's 1.8 V ceiling this reaches full depth near 12 dB GR, where
    the tube's second harmonic is strongest and the compressor is working hard. */
constexpr double grCouplingVoltsPerDb = 0.15;

struct FactoryPreset
{
    const char* name;
    std::array<float, 21> values;
};

// Values follow the parameter order in allParameterIDs below. Keeping every
// parameter in each preset makes program changes deterministic in every host.
constexpr std::array<FactoryPreset, 7> factoryPresets {{
    { "Default Vari-Mu", { 0.0f, 0.0f, 6.0f, 0.50f, 0.50f, 2.0f, 1.00f, 1.0f,
                            -18.0f, 4.0f, 10.0f, 100.0f, 6.0f, 0.0f, 0.0f, 5.0f, 100.0f, 0.50f, 1.0f, 0.0f, 1.0f } },
    { "Gentle Glue",     { 0.0f, 0.5f, 4.0f, 0.58f, 0.48f, 2.0f, 1.00f, 1.0f,
                            -12.0f, 2.0f, 30.0f, 300.0f, 12.0f, 0.0f, 0.0f, 2.0f, 120.0f, 0.42f, 1.0f, 0.0f, 1.0f } },
    { "Vocal Leveler",   { 1.0f, 0.0f, 5.5f, 0.66f, 0.56f, 2.0f, 1.00f, 1.0f,
                            -20.0f, 3.0f, 8.0f, 180.0f, 9.0f, 0.0f, 0.0f, 4.0f, 90.0f, 0.55f, 1.0f, 0.0f, 1.0f } },
    { "Drum Control",    { 0.0f, 1.0f, 7.0f, 0.42f, 0.58f, 2.0f, 1.00f, 1.0f,
                            -16.0f, 6.0f, 3.0f, 80.0f, 4.0f, 0.0f, 0.0f, 8.0f, 140.0f, 0.62f, 1.0f, 0.0f, 1.0f } },
    { "Bass Weight",     { 0.0f, -0.5f, 8.0f, 0.72f, 0.34f, 2.0f, 1.00f, 1.0f,
                            -15.0f, 3.0f, 20.0f, 240.0f, 10.0f, 0.0f, 0.0f, 3.0f, 160.0f, 0.68f, 1.0f, 0.0f, 1.0f } },
    { "Tube Color",      { -1.0f, -1.0f, 14.0f, 0.78f, 0.44f, 3.0f, 1.00f, 1.0f,
                            -8.0f, 1.5f, 45.0f, 400.0f, 16.0f, 0.0f, 0.0f, 1.0f, 80.0f, 0.76f, 1.0f, 0.0f, 1.0f } },
    { "Parallel Lift",   { 2.0f, 0.0f, 7.5f, 0.60f, 0.54f, 2.0f, 0.45f, 1.0f,
                            -24.0f, 8.0f, 5.0f, 120.0f, 5.0f, 0.0f, 0.0f, 6.0f, 110.0f, 0.58f, 1.0f, 0.0f, 1.0f } }
}};

const std::array<juce::String, 21> allParameterIDs {{
    ParamIDs::inputGain, ParamIDs::outputGain, ParamIDs::drive, ParamIDs::harmonics,
    ParamIDs::brightness, ParamIDs::oversample, ParamIDs::mix, ParamIDs::circuitModel,
    ParamIDs::threshold, ParamIDs::ratio, ParamIDs::attack, ParamIDs::release,
    ParamIDs::knee, ParamIDs::bypass, ParamIDs::antiAlias, ParamIDs::lookAhead,
    ParamIDs::sidechainHPF, ParamIDs::biasDrive, ParamIDs::topology,
    ParamIDs::timeConstant, ParamIDs::linkMode
}};

constexpr const char* currentProgramProperty = "factoryProgram";
}

TubeCompAudioProcessor::TubeCompAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

TubeCompAudioProcessor::~TubeCompAudioProcessor() = default;

int TubeCompAudioProcessor::getNumPrograms()
{
    return static_cast<int> (factoryPresets.size());
}

int TubeCompAudioProcessor::getCurrentProgram()
{
    return currentProgram.load();
}

void TubeCompAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    const auto& preset = factoryPresets[static_cast<size_t> (index)];
    for (size_t i = 0; i < allParameterIDs.size(); ++i)
    {
        if (auto* parameter = apvts.getParameter (allParameterIDs[i]))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (preset.values[i]));
    }

    currentProgram.store (index);
}

const juce::String TubeCompAudioProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return {};

    return factoryPresets[static_cast<size_t> (index)].name;
}

void TubeCompAudioProcessor::changeProgramName (int, const juce::String&)
{
    // Factory program names are intentionally immutable.
}

juce::AudioProcessorValueTreeState::ParameterLayout TubeCompAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::inputGain, "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::outputGain, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::drive, "Drive",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::harmonics, "Harmonic Balance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::brightness, "Brightness",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::oversample, "Oversampling",
        juce::StringArray { "1x", "2x", "4x", "8x" }, 2));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::mix, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::circuitModel, "Circuit Model",
        juce::StringArray { "Fast", "Tube (WDF)" }, 1));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::threshold, "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::ratio, "Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f, 0.4f), 4.0f,
        juce::AudioParameterFloatAttributes().withLabel (":1")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::attack, "Attack",
        juce::NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.4f), 10.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::release, "Release",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.1f, 0.4f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::knee, "Knee",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // SnappingBoolParameter rather than AudioParameterBool: the JUCE class keeps
    // non-boolean values, which makes the saved state and the restored value
    // disagree (pluginval: "Bypass not restored on setStateInformation").
    params.push_back (std::make_unique<SnappingBoolParameter> (
        ParamIDs::bypass, "Bypass", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::antiAlias, "Anti-Aliasing",
        juce::StringArray { "Oversampling", "ADAA" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::lookAhead, "Look-Ahead",
        juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f, 0.6f), 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::sidechainHPF, "Sidechain HPF",
        juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.5f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::biasDrive, "Bias Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::topology, "Detector Mode",
        juce::StringArray { "Feedforward", "Feedback" }, 1));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::timeConstant, "Time Constant",
        juce::StringArray { "Custom", "1", "2", "3", "4", "5", "6" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::linkMode, "Link",
        juce::StringArray { "Left/Right", "Linked", "Lat/Ver" }, 1));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::transformer, "Transformer",
        juce::StringArray { "Off", "On" }, 0));

    return { params.begin(), params.end() };
}

void TubeCompAudioProcessor::updateOversamplingIfNeeded (int newFactorChoice)
{
    if (newFactorChoice == currentOversamplingChoice)
        return;

    if (! juce::isPositiveAndBelow (newFactorChoice, static_cast<int> (oversamplingStages.size())))
        return;

    currentOversamplingChoice = newFactorChoice;
    oversampling = oversamplingStages[static_cast<size_t> (newFactorChoice)].get();
}

float TubeCompAudioProcessor::getInputLevelDb (int channel) const noexcept
{
    return (channel >= 0 && channel < 2)
        ? inputMeterLevelDb[static_cast<size_t> (channel)].load (std::memory_order_relaxed)
        : -100.0f;
}

float TubeCompAudioProcessor::getOutputLevelDb (int channel) const noexcept
{
    return (channel >= 0 && channel < 2)
        ? outputMeterLevelDb[static_cast<size_t> (channel)].load (std::memory_order_relaxed)
        : -100.0f;
}

void TubeCompAudioProcessor::updateMeterTap (const juce::AudioBuffer<float>& buffer,
                                              std::array<float, 2>& envelope,
                                              std::array<std::atomic<float>, 2>& publishedLevels) noexcept
{
    const int samples = buffer.getNumSamples();
    if (samples <= 0)
        return;

    // Peak detector with a 300 ms fall time. Measuring one block peak is cheap
    // and preserves transients that a 30 Hz UI poll could otherwise miss.
    const float release = static_cast<float> (std::exp (-static_cast<double> (samples)
                                                         / (meterSampleRate * 0.3)));
    const int channels = juce::jmin (buffer.getNumChannels(), 2);
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto index = static_cast<size_t> (ch);
        const float peak = buffer.getMagnitude (ch, 0, samples);
        envelope[index] = juce::jmax (peak, envelope[index] * release);
        publishedLevels[index].store (juce::Decibels::gainToDecibels (envelope[index], -100.0f),
                                      std::memory_order_relaxed);
    }
}

void TubeCompAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto safeSamplesPerBlock = static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock));
    const int numChannels = juce::jmax (1, getTotalNumInputChannels());

    juce::dsp::ProcessSpec spec { sampleRate, safeSamplesPerBlock,
                                   static_cast<juce::uint32> (numChannels) };

    dryBuffer.setSize (numChannels, static_cast<int> (safeSamplesPerBlock), false, false, true);
    meterSampleRate = sampleRate;
    inputMeterEnvelope.fill (0.0f);
    outputMeterEnvelope.fill (0.0f);
    for (auto& level : inputMeterLevelDb)
        level.store (-100.0f, std::memory_order_relaxed);
    for (auto& level : outputMeterLevelDb)
        level.store (-100.0f, std::memory_order_relaxed);

    // Build the oversamplers first: the dry reference has to be long enough to
    // cover the largest look-ahead plus whichever oversampler reports the most
    // filter delay, so the dry path can be aligned with the wet path exactly.
    int maxOversamplingLatency = 0;
    for (int choice = 0; choice < static_cast<int> (oversamplingStages.size()); ++choice)
    {
        oversamplingStages[static_cast<size_t> (choice)] = std::make_unique<juce::dsp::Oversampling<float>> (
            numChannels, static_cast<size_t> (choice),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversamplingStages[static_cast<size_t> (choice)]->initProcessing (safeSamplesPerBlock);
        maxOversamplingLatency = juce::jmax (maxOversamplingLatency, static_cast<int> (
            oversamplingStages[static_cast<size_t> (choice)]->getLatencyInSamples()));
    }

    const int maxLookAheadSamples = static_cast<int> (
        std::llround (sampleRate * 0.001 * static_cast<double> (Compressor::maxLookAheadMs)));
    dryDelay.prepare (numChannels, maxLookAheadSamples + maxOversamplingLatency + 32);
    lastReportedLatency = -1;

    for (int choice = 0; choice < static_cast<int> (saturators.size()); ++choice)
    {
        auto oversampledSpec = spec;
        oversampledSpec.sampleRate = sampleRate * static_cast<double> (1 << choice);
        oversampledSpec.maximumBlockSize = safeSamplesPerBlock * (1u << choice);
        saturators[static_cast<size_t> (choice)].prepare (oversampledSpec);
        triodeStages[static_cast<size_t> (choice)].prepare (oversampledSpec);
        transformerStages[static_cast<size_t> (choice)].prepare (oversampledSpec);
    }

    oversampling = nullptr;
    currentOversamplingChoice = -1;
    lastAppliedFactor = -1;
    updateOversamplingIfNeeded (juce::jlimit (0, static_cast<int> (oversamplingStages.size()) - 1,
        static_cast<int> (apvts.getRawParameterValue (ParamIDs::oversample)->load())));

    compressor.prepare (sampleRate, numChannels, static_cast<int> (safeSamplesPerBlock));
    compressor.setLookAheadMs (apvts.getRawParameterValue (ParamIDs::lookAhead)->load());
    compressor.setFeedbackMode (apvts.getRawParameterValue (ParamIDs::topology)->load() > 0.5f);

    // Report latency as soon as the host prepares, so delay compensation is
    // correct before the first block; processBlock keeps it current afterwards.
    int initialLatency = static_cast<int> (compressor.getLookAheadSamples());
    if (oversampling != nullptr)
        initialLatency += static_cast<int> (oversampling->getLatencyInSamples());
    lastReportedLatency = initialLatency;
    setLatencySamples (initialLatency);

    // Smoothed gains must start at the current parameter values -- a default
    // zero-initialised smoother would fade the whole plugin in over its ramp.
    inputGainSmoothed.reset (sampleRate, 0.02);
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamIDs::inputGain)->load()));
    outputGainSmoothed.reset (sampleRate, 0.02);
    outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamIDs::outputGain)->load()));
    mixSmoothed.reset (sampleRate, 0.02);
    mixSmoothed.setCurrentAndTargetValue (apvts.getRawParameterValue (ParamIDs::mix)->load());

    lastBrightness = lastHarmonics = lastDrive = lastBiasDrive = -1.0f;
}

void TubeCompAudioProcessor::releaseResources()
{
    oversampling = nullptr;
    for (auto& stage : oversamplingStages)
        stage.reset();
}

bool TubeCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void TubeCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // The input tap lives before input gain, detection, latency alignment, or
    // bypass so its 0 VU calibration always describes the signal arriving at
    // the plug-in rather than a processing-dependent level.
    updateMeterTap (buffer, inputMeterEnvelope, inputMeterLevelDb);

    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamIDs::inputGain)->load()));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamIDs::outputGain)->load()));
    mixSmoothed.setTargetValue (apvts.getRawParameterValue (ParamIDs::mix)->load());

    // Quality, look-ahead and topology define the latency; apply them first so
    // the reported value and the dry alignment never lag a block behind.
    const int totalLatency = syncLatencyWithParameters();

    const int numQualitySteps = static_cast<int> (oversamplingStages.size());
    const int activeQuality = juce::jlimit (0, numQualitySteps - 1, currentOversamplingChoice);
    const auto activeFactor = static_cast<size_t> (activeQuality);

    // A quality change swaps to a stage that still holds the tone parameters
    // from the last time it was active, so force a full re-apply on the switch.
    if (activeQuality != lastAppliedFactor)
    {
        lastAppliedFactor = activeQuality;
        lastDrive = lastHarmonics = lastBrightness = lastBiasDrive = -1.0f;
    }

    const float driveDb = apvts.getRawParameterValue (ParamIDs::drive)->load();
    const float harmonicsVal = apvts.getRawParameterValue (ParamIDs::harmonics)->load();
    const float brightnessVal = apvts.getRawParameterValue (ParamIDs::brightness)->load();
    const bool useADAA = static_cast<int> (apvts.getRawParameterValue (ParamIDs::antiAlias)->load()) == 1;

    const float biasDriveVal = apvts.getRawParameterValue (ParamIDs::biasDrive)->load();
    if (std::abs (driveDb - lastDrive) > 1.0e-6f) { saturators[activeFactor].setDrive (driveDb); triodeStages[activeFactor].setDrive (driveDb); lastDrive = driveDb; }
    if (std::abs (harmonicsVal - lastHarmonics) > 1.0e-6f) { saturators[activeFactor].setHarmonicRatio (harmonicsVal); triodeStages[activeFactor].setHarmonicRatio (harmonicsVal); lastHarmonics = harmonicsVal; }
    if (std::abs (brightnessVal - lastBrightness) > 1.0e-6f) { saturators[activeFactor].setBrightness (brightnessVal); triodeStages[activeFactor].setBrightness (brightnessVal); lastBrightness = brightnessVal; }
    if (std::abs (biasDriveVal - lastBiasDrive) > 1.0e-6f) { triodeStages[activeFactor].setBiasDrive (biasDriveVal); lastBiasDrive = biasDriveVal; }
    saturators[activeFactor].setUseADAA (useADAA);

    const bool useTriode = static_cast<int> (apvts.getRawParameterValue (ParamIDs::circuitModel)->load()) == 1;
    const bool useTransformer = apvts.getRawParameterValue (ParamIDs::transformer)->load() > 0.5f;

    compressor.setThresholdDb (apvts.getRawParameterValue (ParamIDs::threshold)->load());
    compressor.setRatio (apvts.getRawParameterValue (ParamIDs::ratio)->load());
    const int tcChoice = static_cast<int> (apvts.getRawParameterValue (ParamIDs::timeConstant)->load());
    compressor.setTimeConstant (tcChoice);
    // A Time Constant preset owns attack/release; the manual knobs only apply
    // in Custom mode. Applying them every block would overwrite the TC values
    // setTimeConstant() just installed.
    if (tcChoice == 0)
    {
        compressor.setAttackMs (apvts.getRawParameterValue (ParamIDs::attack)->load());
        compressor.setReleaseMs (apvts.getRawParameterValue (ParamIDs::release)->load());
    }
    compressor.setLinkMode (static_cast<int> (apvts.getRawParameterValue (ParamIDs::linkMode)->load()));
    compressor.setKneeDb (apvts.getRawParameterValue (ParamIDs::knee)->load());
    compressor.setSidechainHPFHz (apvts.getRawParameterValue (ParamIDs::sidechainHPF)->load());

    jassert (dryBuffer.getNumChannels() >= numChannels && dryBuffer.getNumSamples() >= numSamples);

    // Capture the dry reference once, delayed by the wet path latency, so the
    // mix blend and bypass both stay aligned with the processed signal.
    dryDelay.setDelaySamples (totalLatency);
    dryDelay.process (buffer, dryBuffer, numSamples);

    if (apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f)
    {
        for (int ch = 0; ch < juce::jmin (numChannels, dryBuffer.getNumChannels()); ++ch)
            buffer.copyFrom (ch, 0, dryBuffer, ch, 0, numSamples);
        updateMeterTap (buffer, outputMeterEnvelope, outputMeterLevelDb);
        return;
    }

    // Input gain applies to the wet path only; the reference above is the raw
    // input, which is what a Mix blend should interpolate against.
    for (int i = 0; i < numSamples; ++i)
    {
        const float gain = inputGainSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= gain;
    }

    compressor.process (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    auto oversampledBlock = oversampling->processSamplesUp (block);

    juce::dsp::ProcessContextReplacing<float> context (oversampledBlock);

    if (useTriode)
    {
        // Vari-mu coupling: the compressor's gain reduction drives the tube's
        // grid colder, so the harmonic character grows with GR instead of
        // staying fixed. It follows the per-sample GR trace (held across each
        // oversampling period), not a per-block value, so the result does not
        // depend on the host's block size. Only the triode path has an
        // operating point to move; the Fast waveshaper has no bias.
        auto& triode = triodeStages[activeFactor];
        triode.setControlBiasSource (compressor.getGainReductionTrace(),
                                     compressor.getGainReductionTraceLength(),
                                     static_cast<size_t> (1) << activeFactor,
                                     grCouplingVoltsPerDb);
        triode.process (context);
        triode.clearControlBiasSource();
    }
    else
    {
        saturators[activeFactor].process (context);
    }

    // Output transformer, still inside the oversampled domain so the iron's
    // nonlinearity is filtered by the same decimation path. Sits after the tone
    // stage because the real chain has the output iron last: its low-end bloom
    // and HF rolloff act on whatever harmonics the tube just produced.
    if (useTransformer)
        transformerStages[activeFactor].process (context);

    oversampling->processSamplesDown (block);

    for (int i = 0; i < numSamples; ++i)
    {
        const float outGain = outputGainSmoothed.getNextValue();
        const float mix = mixSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float wet = buffer.getSample (ch, i) * outGain;
            const float dry = dryBuffer.getSample (ch, i);
            buffer.setSample (ch, i, juce::jmap (mix, dry, wet));
        }
    }

    // Out mode represents the actual plug-in result, including output gain,
    // saturation and dry/wet blend, not the compressor's internal buffer.
    updateMeterTap (buffer, outputMeterEnvelope, outputMeterLevelDb);
}

int TubeCompAudioProcessor::syncLatencyWithParameters()
{
    const int numQualitySteps = static_cast<int> (oversamplingStages.size());
    const int requestedQuality = static_cast<int> (apvts.getRawParameterValue (ParamIDs::oversample)->load());
    updateOversamplingIfNeeded (juce::jlimit (0, numQualitySteps - 1, requestedQuality));

    compressor.setLookAheadMs (apvts.getRawParameterValue (ParamIDs::lookAhead)->load());
    compressor.setFeedbackMode (apvts.getRawParameterValue (ParamIDs::topology)->load() > 0.5f);

    // Latency = compressor look-ahead + oversampling filter delay.
    int totalLatency = static_cast<int> (compressor.getLookAheadSamples());
    if (oversampling != nullptr)
        totalLatency += static_cast<int> (oversampling->getLatencyInSamples());

    if (totalLatency != lastReportedLatency)
    {
        lastReportedLatency = totalLatency;
        setLatencySamples (totalLatency);
    }

    return totalLatency;
}

void TubeCompAudioProcessor::renderDelayedDry (juce::AudioBuffer<float>& buffer, int delaySamples) noexcept
{
    const int numSamples = buffer.getNumSamples();
    jassert (dryBuffer.getNumSamples() >= numSamples);

    dryDelay.setDelaySamples (delaySamples);
    dryDelay.process (buffer, dryBuffer, numSamples);

    for (int ch = 0; ch < juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels()); ++ch)
        buffer.copyFrom (ch, 0, dryBuffer, ch, 0, numSamples);
}

void TubeCompAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Same taps and the same dry delay line as processBlock(): alternating
    // active and host-bypassed blocks stays one continuous, aligned signal.
    updateMeterTap (buffer, inputMeterEnvelope, inputMeterLevelDb);
    renderDelayedDry (buffer, syncLatencyWithParameters());
    updateMeterTap (buffer, outputMeterEnvelope, outputMeterLevelDb);
}

juce::AudioProcessorParameter* TubeCompAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

juce::AudioProcessorEditor* TubeCompAudioProcessor::createEditor()
{
    return new TubeCompAudioProcessorEditor (*this);
}

void TubeCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        state.setProperty (currentProgramProperty, currentProgram.load(), nullptr);
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }
}

void TubeCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto restoredState = juce::ValueTree::fromXml (*xml);
            const int restoredProgram = static_cast<int> (restoredState.getProperty (currentProgramProperty, 0));
            currentProgram.store (juce::jlimit (0, getNumPrograms() - 1, restoredProgram));
            restoredState.removeProperty (currentProgramProperty, nullptr);
            apvts.replaceState (restoredState);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TubeCompAudioProcessor();
}
