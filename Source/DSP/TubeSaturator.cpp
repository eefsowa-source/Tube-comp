#include "TubeSaturator.h"

#include <algorithm>

void TubeSaturator::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    modelMatchGain = juce::Decibels::decibelsToGain (modelMatchGainDb);
    oddShapers.assign (spec.numChannels, ADAATanhShaper {});
    evenShapers.assign (spec.numChannels, ADAAAsymTanhShaper {});
    for (auto& shaper : evenShapers)
        shaper.setBias (asymBranchBias);

    constexpr double dcCutoffHz = 20.0;
    dcCoeff = static_cast<float> (std::exp (-2.0 * juce::MathConstants<double>::pi * dcCutoffHz / spec.sampleRate));
    dcPrevInput.assign (spec.numChannels, 0.0f);
    dcPrevOutput.assign (spec.numChannels, 0.0f);

    tiltFilter.prepare (spec);
    brightnessDb.reset (spec.sampleRate, 0.02);
    brightnessDb.setCurrentAndTargetValue (0.0f);
    applyTiltGain (0.0f);
}

void TubeSaturator::reset()
{
    for (auto& shaper : oddShapers)
        shaper.reset();
    for (auto& shaper : evenShapers)
        shaper.reset();
    std::fill (dcPrevInput.begin(), dcPrevInput.end(), 0.0f);
    std::fill (dcPrevOutput.begin(), dcPrevOutput.end(), 0.0f);
    tiltFilter.reset();
}

void TubeSaturator::setBrightness (float brightness01) noexcept
{
    // 0.5 is a flat shelf. The gain ramps; coefficients are rewritten in place.
    const float gainDb = juce::jmap (juce::jlimit (0.0f, 1.0f, brightness01), 0.0f, 1.0f, -3.0f, 3.0f);
    brightnessDb.setTargetValue (gainDb);
}

void TubeSaturator::applyTiltGain (float gainDb) noexcept
{
    if (tiltFilter.state == nullptr || sampleRate <= 0.0)
        return;

    *tiltFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
        sampleRate, 3000.0f, 0.707f, juce::Decibels::decibelsToGain (gainDb));
}

void TubeSaturator::processTilt (juce::dsp::AudioBlock<float> block) noexcept
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    // While the shelf is moving, update the shared coefficients once per sample
    // and filter that sample. A whole-block process() snapshots the biquad
    // coefficients up front, which would turn the ramp back into a step.
    if (! brightnessDb.isSmoothing())
    {
        tiltFilter.process (juce::dsp::ProcessContextReplacing<float> (block));
        return;
    }

    for (size_t i = 0; i < numSamples; ++i)
    {
        applyTiltGain (brightnessDb.getNextValue());
        auto sampleBlock = block.getSubBlock (i, 1);
        juce::dsp::ProcessContextReplacing<float> context (sampleBlock);
        tiltFilter.process (context);
    }
}

float TubeSaturator::shape (float x) const noexcept
{
    // Asymmetric soft clip: blends a tanh (odd, 3rd-harmonic-heavy) curve with a
    // grid-biased tanh (even, 2nd-harmonic-heavy). harmonicRatio sweeps between
    // the two to approximate the Manley (2nd-dominant) vs Fairchild (elevated
    // 2nd/3rd mix) targets from the psychoacoustic guide. Both branches stay
    // inside [-1, 1], so the stage output is bounded at any drive.
    const float odd = std::tanh (x);
    const float even = biasedTanh (x, asymBranchBias);
    return juce::jmap (harmonicRatio, odd, even);
}
