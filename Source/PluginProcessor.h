#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/TubeSaturator.h"
#include "DSP/TriodeStage.h"
#include "DSP/TransformerStage.h"
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
    static const juce::String transformer  { "transformer" }; // 0=off, 1=linear iron model
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

    /** Host bypass for hosts that bypass without going through the parameter
        returned by getBypassParameter(): the input is delayed by the reported
        latency so delay compensation stays correct while bypassed. */
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Exposes the panel's Bypass to the host, so a host bypass drives the same
        latency-compensated path instead of JUCE's undelayed pass-through. */
    juce::AudioProcessorParameter* getBypassParameter() const override;

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
    float getChannelGainReductionDb (int channel) const noexcept { return compressor.getChannelGainReductionDb (channel); }
    float getInputLevelDb (int channel) const noexcept;
    float getOutputLevelDb (int channel) const noexcept;
    // Retained for callers built against the earlier UI API; "level" now means
    // final plugin output, after output gain and dry/wet mix.
    float getChannelLevelDb (int channel) const noexcept { return getOutputLevelDb (channel); }

    /** Vari-mu coupling state of the active triode stage, in volts (<= 0). The
        compressor's gain reduction drives this colder as it works; exposed so the
        coupling can be verified without reaching into the DSP stack. */
    float getControlBiasVolts() const noexcept
    {
        const int factor = juce::jlimit (0, static_cast<int> (triodeStages.size()) - 1,
                                         juce::jmax (0, lastAppliedFactor));
        return static_cast<float> (triodeStages[static_cast<size_t> (factor)].getControlBiasVolts());
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateOversamplingIfNeeded (int newFactorChoice);
    /** Applies the latency-defining parameters (quality, look-ahead, detector
        topology), reports a changed latency to the host and returns it. */
    int syncLatencyWithParameters();
    /** Writes the input, delayed by `delaySamples`, to `buffer` (bypass). */
    void renderDelayedDry (juce::AudioBuffer<float>& buffer, int delaySamples) noexcept;
    void updateMeterTap (const juce::AudioBuffer<float>& buffer,
                          std::array<float, 2>& envelope,
                          std::array<std::atomic<float>, 2>& publishedLevels) noexcept;

    Compressor compressor;
    // One saturator/triode pair per oversampling factor, each prepared at that
    // factor's effective sample rate. Switching quality at runtime then only
    // swaps the active pair -- no allocations, no stale filter coefficients.
    std::array<TubeSaturator, 4> saturators;
    std::array<TriodeStage, 4> triodeStages;
    std::array<TransformerStage, 4> transformerStages;
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
    // Pre-gain input and final output taps. Only the audio thread owns the
    // envelopes; the message thread only observes their dB atomics.
    std::array<float, 2> inputMeterEnvelope { 0.0f, 0.0f };
    std::array<float, 2> outputMeterEnvelope { 0.0f, 0.0f };
    std::array<std::atomic<float>, 2> inputMeterLevelDb { { -100.0f, -100.0f } };
    std::array<std::atomic<float>, 2> outputMeterLevelDb { { -100.0f, -100.0f } };
    double meterSampleRate = 44100.0;
    std::atomic<int> currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TubeCompAudioProcessor)
};
