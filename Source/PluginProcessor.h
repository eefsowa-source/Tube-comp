#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/TubeSaturator.h"
#include "DSP/TriodeStage.h"
#include "DSP/Compressor.h"
#include "DSP/DryDelayLine.h"

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
    static const juce::String topology    { "topology" }; // 0=feedforward, 1=feedback vari-mu
    static const juce::String timeConstant { "timeConstant" }; // 0=custom, 1..6=Fairchild timing
    static const juce::String linkMode    { "linkMode" }; // 0=L/R, 1=linked, 2=Lat/Ver
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
    // One saturator/triode pair per oversampling factor, each prepared at that
    // factor's effective sample rate. Switching quality at runtime then only
    // swaps the active pair -- no allocations, no stale filter coefficients.
    std::array<TubeSaturator, 4> saturators;
    std::array<TriodeStage, 4> triodeStages;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 4> oversamplingStages;
    juce::dsp::Oversampling<float>* oversampling = nullptr;
    int currentOversamplingChoice = -1;
    // Index of the stage the tone parameters were last pushed into; a change of
    // quality step forces a full re-apply so the newly active stage is current.
    int lastAppliedFactor = -1;
    // Unprocessed reference, delayed to match the wet path (compressor
    // look-ahead + oversampling latency). Used for the Mix blend and for bypass,
    // so both stay time-aligned with the processed signal.
    DryDelayLine dryDelay;
    juce::AudioBuffer<float> dryBuffer;
    int lastReportedLatency = -1;

    juce::LinearSmoothedValue<float> inputGainSmoothed, outputGainSmoothed, mixSmoothed;
    float lastBrightness = -1.0f, lastHarmonics = -1.0f, lastDrive = -1.0f, lastBiasDrive = -1.0f;
    std::atomic<int> currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TubeCompAudioProcessor)
};
