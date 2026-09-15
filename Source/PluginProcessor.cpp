#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct FactoryPreset
{
    const char* name;
    std::array<float, 18> values;
};

// Values follow the parameter order in allParameterIDs below. Keeping every
// parameter in each preset makes program changes deterministic in every host.
constexpr std::array<FactoryPreset, 7> factoryPresets {{
    { "Default Vari-Mu", { 0.0f, 0.0f, 6.0f, 0.50f, 0.50f, 2.0f, 1.00f, 1.0f,
                            -18.0f, 4.0f, 10.0f, 100.0f, 6.0f, 0.0f, 0.0f, 5.0f, 100.0f, 0.50f } },
    { "Gentle Glue",     { 0.0f, 0.5f, 4.0f, 0.58f, 0.48f, 2.0f, 1.00f, 1.0f,
                            -12.0f, 2.0f, 30.0f, 300.0f, 12.0f, 0.0f, 0.0f, 2.0f, 120.0f, 0.42f } },
    { "Vocal Leveler",   { 1.0f, 0.0f, 5.5f, 0.66f, 0.56f, 2.0f, 1.00f, 1.0f,
                            -20.0f, 3.0f, 8.0f, 180.0f, 9.0f, 0.0f, 0.0f, 4.0f, 90.0f, 0.55f } },
    { "Drum Control",    { 0.0f, 1.0f, 7.0f, 0.42f, 0.58f, 2.0f, 1.00f, 1.0f,
                            -16.0f, 6.0f, 3.0f, 80.0f, 4.0f, 0.0f, 0.0f, 8.0f, 140.0f, 0.62f } },
    { "Bass Weight",     { 0.0f, -0.5f, 8.0f, 0.72f, 0.34f, 2.0f, 1.00f, 1.0f,
                            -15.0f, 3.0f, 20.0f, 240.0f, 10.0f, 0.0f, 0.0f, 3.0f, 160.0f, 0.68f } },
    { "Tube Color",      { -1.0f, -1.0f, 14.0f, 0.78f, 0.44f, 3.0f, 1.00f, 1.0f,
                            -8.0f, 1.5f, 45.0f, 400.0f, 16.0f, 0.0f, 0.0f, 1.0f, 80.0f, 0.76f } },
    { "Parallel Lift",   { 2.0f, 0.0f, 7.5f, 0.60f, 0.54f, 2.0f, 0.45f, 1.0f,
                            -24.0f, 8.0f, 5.0f, 120.0f, 5.0f, 0.0f, 0.0f, 6.0f, 110.0f, 0.58f } }
}};

const std::array<juce::String, 18> allParameterIDs {{
    ParamIDs::inputGain, ParamIDs::outputGain, ParamIDs::drive, ParamIDs::harmonics,
    ParamIDs::brightness, ParamIDs::oversample, ParamIDs::mix, ParamIDs::circuitModel,
    ParamIDs::threshold, ParamIDs::ratio, ParamIDs::attack, ParamIDs::release,
    ParamIDs::knee, ParamIDs::bypass, ParamIDs::antiAlias, ParamIDs::lookAhead,
    ParamIDs::sidechainHPF, ParamIDs::biasDrive
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

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamIDs::bypass, "Bypass", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::antiAlias, "Anti-Aliasing",
        juce::StringArray { "Oversampling", "ADAA" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::lookAhead, "Look-Ahead",
        juce::NormalisableRange<float> (0.1f, 20.0f, 0.1f, 0.6f), 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::sidechainHPF, "Sidechain HPF",
        juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.5f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::biasDrive, "Bias Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    return { params.begin(), params.end() };
}

void TubeCompAudioProcessor::updateOversamplingIfNeeded (int newFactorChoice)
{
    if (newFactorChoice == currentOversamplingChoice)
        return;

    currentOversamplingChoice = newFactorChoice;
    if (newFactorChoice >= 0 && newFactorChoice < static_cast<int> (oversamplingStages.size()))
    {
        oversampling = oversamplingStages[static_cast<size_t> (newFactorChoice)].get();
        setLatencySamples (static_cast<int> (oversampling->getLatencyInSamples()));
    }
}

void TubeCompAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlock),
                                   static_cast<juce::uint32> (getTotalNumInputChannels()) };
    dryBuffer.setSize (getTotalNumInputChannels(), samplesPerBlock, false, false, true);

    for (int choice = 0; choice < static_cast<int> (oversamplingStages.size()); ++choice)
    {
        oversamplingStages[static_cast<size_t> (choice)] = std::make_unique<juce::dsp::Oversampling<float>> (
            getTotalNumInputChannels(), static_cast<size_t> (choice),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversamplingStages[static_cast<size_t> (choice)]->initProcessing (static_cast<size_t> (samplesPerBlock));
    }

    currentOversamplingChoice = -1;
    const int choice = static_cast<int> (apvts.getRawParameterValue (ParamIDs::oversample)->load());
    updateOversamplingIfNeeded (choice);

    juce::dsp::ProcessSpec oversampledSpec = spec;
    oversampledSpec.sampleRate = sampleRate * static_cast<double> (1 << choice);
    saturator.prepare (oversampledSpec);
    triodeStage.prepare (oversampledSpec);
    compressor.prepare (sampleRate, getTotalNumInputChannels());

    inputGainSmoothed.reset (sampleRate, 0.02);
    outputGainSmoothed.reset (sampleRate, 0.02);
    mixSmoothed.reset (sampleRate, 0.02);
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

    if (apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f)
        return;

    const int choice = static_cast<int> (apvts.getRawParameterValue (ParamIDs::oversample)->load());
    updateOversamplingIfNeeded (choice);

    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamIDs::inputGain)->load()));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (ParamIDs::outputGain)->load()));
    mixSmoothed.setTargetValue (apvts.getRawParameterValue (ParamIDs::mix)->load());

    const float driveDb = apvts.getRawParameterValue (ParamIDs::drive)->load();
    const float harmonicsVal = apvts.getRawParameterValue (ParamIDs::harmonics)->load();
    const float brightnessVal = apvts.getRawParameterValue (ParamIDs::brightness)->load();
    const bool useADAA = static_cast<int> (apvts.getRawParameterValue (ParamIDs::antiAlias)->load()) == 1;

    const float biasDriveVal = apvts.getRawParameterValue (ParamIDs::biasDrive)->load();
    if (std::abs (driveDb - lastDrive) > 1.0e-6f) { saturator.setDrive (driveDb); triodeStage.setDrive (driveDb); lastDrive = driveDb; }
    if (std::abs (harmonicsVal - lastHarmonics) > 1.0e-6f) { saturator.setHarmonicRatio (harmonicsVal); triodeStage.setHarmonicRatio (harmonicsVal); lastHarmonics = harmonicsVal; }
    if (std::abs (brightnessVal - lastBrightness) > 1.0e-6f) { saturator.setBrightness (brightnessVal); triodeStage.setBrightness (brightnessVal); lastBrightness = brightnessVal; }
    if (std::abs (biasDriveVal - lastBiasDrive) > 1.0e-6f) { triodeStage.setBiasDrive (biasDriveVal); lastBiasDrive = biasDriveVal; }
    saturator.setUseADAA (useADAA);

    const bool useTriode = static_cast<int> (apvts.getRawParameterValue (ParamIDs::circuitModel)->load()) == 1;

    compressor.setThresholdDb (apvts.getRawParameterValue (ParamIDs::threshold)->load());
    compressor.setRatio (apvts.getRawParameterValue (ParamIDs::ratio)->load());
    compressor.setAttackMs (apvts.getRawParameterValue (ParamIDs::attack)->load());
    compressor.setReleaseMs (apvts.getRawParameterValue (ParamIDs::release)->load());
    compressor.setKneeDb (apvts.getRawParameterValue (ParamIDs::knee)->load());
    compressor.setLookAheadMs (apvts.getRawParameterValue (ParamIDs::lookAhead)->load());
    compressor.setSidechainHPFHz (apvts.getRawParameterValue (ParamIDs::sidechainHPF)->load());

    jassert (dryBuffer.getNumChannels() >= buffer.getNumChannels()
             && dryBuffer.getNumSamples() >= buffer.getNumSamples());
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float gain = inputGainSmoothed.getNextValue();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, sample, buffer.getSample (ch, sample) * gain);
    }

    compressor.process (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    auto oversampledBlock = oversampling->processSamplesUp (block);

    juce::dsp::ProcessContextReplacing<float> context (oversampledBlock);

    if (useTriode)
        triodeStage.process (context);
    else
        saturator.process (context);

    oversampling->processSamplesDown (block);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float outGain = outputGainSmoothed.getNextValue();
        const float mix = mixSmoothed.getNextValue();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float wet = buffer.getSample (ch, sample) * outGain;
            const float dry = dryBuffer.getSample (ch, sample);
            buffer.setSample (ch, sample, juce::jmap (mix, dry, wet));
        }
    }
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
