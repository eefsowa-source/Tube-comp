#include "TubeSaturator.h"

void TubeSaturator::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    adaaShapers.assign (spec.numChannels, ADAATanhShaper {});
    tiltFilter.prepare (spec);
    setBrightness (0.5f);
}

void TubeSaturator::reset()
{
    for (auto& shaper : adaaShapers)
        shaper.reset();
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
    // Asymmetric soft clip: blends a tanh (odd, 3rd-harmonic-heavy) curve with
    // a quadratic-biased curve (even, 2nd-harmonic-heavy). harmonicRatio sweeps
    // between the two to approximate the Manley (2nd-dominant) vs Fairchild
    // (elevated 2nd/3rd mix) targets from the psychoacoustic guide.
    const float odd = std::tanh (x);
    const float even = x - 0.3f * x * std::abs (x);
    return juce::jmap (harmonicRatio, odd, even);
}
