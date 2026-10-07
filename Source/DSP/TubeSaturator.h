#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "AADAWaveshaper.h"

/**
    Program-dependent tube saturation stage.

    Models the psychoacoustic targets from the JUCE nonlinear-emulation guide:
    - harmonicRatio biases the waveshaper toward a softer 2nd-harmonic-dominant
      curve (Manley-style) or a harder 2nd/3rd mix (Fairchild-style).
    - drive sets the input gain into the nonlinearity (progressive saturation).
    - brightness trims a post-saturation tilt filter to track spectral centroid.
*/
class TubeSaturator
{
public:
    /** Output trim that level-matches this path to the WDF triode path, measured
        with Tools/sound_baseline.cpp. The two models differ in both static gain
        and compression, so one constant cannot match every operating point: this
        value keeps the normal working region (drive plus input at or below 0 dB)
        inside 0.5 dB, and the residual under combined heavy drive is reported by
        the baseline tool. */
    static constexpr float modelMatchGainDb = 8.0f;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setDrive (float driveDb) noexcept { drive = juce::Decibels::decibelsToGain (driveDb); }
    void setHarmonicRatio (float ratio01) noexcept { harmonicRatio = ratio01; }
    void setBrightness (float brightness01) noexcept;
    void setUseADAA (bool shouldUseADAA) noexcept { useADAA = shouldUseADAA; }

    template <typename ProcessContext>
    void process (const ProcessContext& context) noexcept
    {
        auto&& outputBlock = context.getOutputBlock();
        auto&& inputBlock = context.getInputBlock();

        for (size_t channel = 0; channel < outputBlock.getNumChannels(); ++channel)
        {
            auto* in = inputBlock.getChannelPointer (channel);
            auto* out = outputBlock.getChannelPointer (channel);

            for (size_t i = 0; i < outputBlock.getNumSamples(); ++i)
            {
                const float x = in[i] * drive;
                const float shaped = modelMatchGain
                                       * ((useADAA && channel < oddShapers.size())
                                            ? juce::jmap (harmonicRatio,
                                                          oddShapers[channel].processSample (x),
                                                          evenShapers[channel].processSample (x))
                                            : shape (x));

                // The asymmetric branch carries a level-dependent DC offset, which
                // a one-pole high pass removes the way a coupling capacitor would.
                // A one-pole stays stable at any rate: a biquad at 20 Hz against an
                // 8x rate of 384 kHz puts a pole outside the unit circle once its
                // coefficients are stored as float, which lets the stage diverge.
                const float dc = shaped - dcPrevInput[channel] + dcCoeff * dcPrevOutput[channel];
                dcPrevInput[channel] = shaped;
                dcPrevOutput[channel] = dc;
                out[i] = dc;
            }
        }

        processTilt (outputBlock);
    }

private:
    void applyTiltGain (float gainDb) noexcept;
    void processTilt (juce::dsp::AudioBlock<float> block) noexcept;
    float shape (float x) const noexcept;

    float drive = 1.0f;
    float harmonicRatio = 0.5f;
    float modelMatchGain = 1.0f;
    bool useADAA = false;
    // ADAA is linear in the shaped function, so blending the two anti-aliased
    // branches keeps the harmonic-balance control intact with ADAA engaged.
    std::vector<ADAATanhShaper> oddShapers;
    std::vector<ADAAAsymTanhShaper> evenShapers;

    float dcCoeff = 0.0f;
    std::vector<float> dcPrevInput;
    std::vector<float> dcPrevOutput;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> tiltFilter;
    // ±3 dB shelf gain. Ramped over the same 20 ms as the input/output/mix
    // gains; the coefficient object itself is allocated once in prepare().
    juce::LinearSmoothedValue<float> brightnessDb { 0.0f };
    double sampleRate = 44100.0;
};
