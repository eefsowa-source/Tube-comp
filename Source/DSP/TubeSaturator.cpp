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
    setBrightness (0.5f);
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
    // High-shelf tilt: brightness01 in [0, 1], 0.5 = flat.
    const float gainDb = juce::jmap (brightness01, 0.0f, 1.0f, -3.0f, 3.0f);
    *tiltFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, 3000.0f, 0.707f, juce::Decibels::decibelsToGain (gainDb));
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
