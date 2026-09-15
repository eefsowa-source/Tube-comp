#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/TubeSaturator.h"
#include "DSP/TriodeStage.h"
#include "DSP/Compressor.h"

namespace ParamIDs
{
    static const juce::String inputGain   { "inputGain" };
    static const juce::String outputGain  { "outputGain" };
    static const juce::String drive       { "drive" };
    static const juce::String harmonics   { "harmonics" };   // 2nd vs 3rd harmonic balance
    static const juce::String brightness  { "brightness" };  // spectral centroid tilt
    static const juce::String oversample  { "oversample" };  // quality choice: 1x/2x/4x/8x
    static const juce::String mix         { "mix" };
    static const juce::String circuitModel { "circuitModel" }; // Fast waveshaper vs WDF triode
    static const juce::String threshold   { "threshold" };
    static const juce::String ratio       { "ratio" };
    static const juce::String attack      { "attack" };
    static const juce::String release     { "release" };
    static const juce::String knee        { "knee" };
    static const juce::String bypass      { "bypass" };
    static const juce::String antiAlias   { "antiAlias" }; // 0=Oversampling, 1=ADAA
    static const juce::String lookAhead   { "lookAhead" };
    static const juce::String sidechainHPF { "sidechainHPF" };
    static const juce::String biasDrive   { "biasDrive" };
}

class TubeCompAudioProcessor : public juce::AudioProcessor
{
public:
    TubeCompAudioProcessor();
    ~TubeCompAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI metering accessors -- safe to poll from the message thread.
    float getGainReductionDb() const noexcept { return compressor.getCurrentGainReductionDb(); }
    float getChannelLevelDb (int channel) const noexcept { return compressor.getChannelLevelDb (channel); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateOversamplingIfNeeded (int newFactorChoice);

    Compressor compressor;
    TubeSaturator saturator;
    TriodeStage triodeStage;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 4> oversamplingStages;
    juce::dsp::Oversampling<float>* oversampling = nullptr;
    int currentOversamplingChoice = -1;
    juce::AudioBuffer<float> dryBuffer;

    juce::LinearSmoothedValue<float> inputGainSmoothed, outputGainSmoothed, mixSmoothed;
    float lastBrightness = -1.0f, lastHarmonics = -1.0f, lastDrive = -1.0f, lastBiasDrive = -1.0f;
    std::atomic<int> currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TubeCompAudioProcessor)
};
