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
                out[i] = (useADAA && channel < adaaShapers.size())
                           ? adaaShapers[channel].processSample (x)
                           : shape (x);
            }
        }

        tiltFilter.process (context);
    }

private:
    float shape (float x) const noexcept;

    float drive = 1.0f;
    float harmonicRatio = 0.5f;
    bool useADAA = false;
    std::vector<ADAATanhShaper> adaaShapers;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> tiltFilter;
    double sampleRate = 44100.0;
};
