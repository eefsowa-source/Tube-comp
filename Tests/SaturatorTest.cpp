#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstdio>
#include "../Source/DSP/TubeSaturator.h"

namespace
{
const float modelMatchGain = juce::Decibels::decibelsToGain (TubeSaturator::modelMatchGainDb);

struct Metrics
{
    float maxAbs = 0.0f;
    double rms = 0.0;
    double h2 = 0.0;
    double h3 = 0.0;
    bool finite = true;
};

Metrics renderAt (double rate, float frequencyHz, float driveDb, float harmonicRatio,
                  bool useADAA, float amplitude)
{
    const double sampleRate = rate;
    const int numSamples = static_cast<int> (sampleRate);

    TubeSaturator saturator;
    saturator.prepare ({ sampleRate, 512, 1 });
    saturator.setDrive (driveDb);
    saturator.setHarmonicRatio (harmonicRatio);
    saturator.setBrightness (0.5f);
    saturator.setUseADAA (useADAA);

    juce::AudioBuffer<float> buffer (1, numSamples);
    auto* data = buffer.getWritePointer (0);
    for (int i = 0; i < numSamples; ++i)
        data[i] = amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequencyHz * i / sampleRate);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    saturator.process (context);

    Metrics result;
    double sumSquares = 0.0;
    double h2Sin = 0.0, h2Cos = 0.0, h3Sin = 0.0, h3Cos = 0.0;
    int count = 0;

    for (int i = numSamples / 2; i < numSamples; ++i)
    {
        const double x = data[i];
        result.finite = result.finite && std::isfinite (x);
        result.maxAbs = juce::jmax (result.maxAbs, static_cast<float> (std::abs (x)));
        sumSquares += x * x;

        const double phase = 2.0 * juce::MathConstants<double>::pi * frequencyHz * i / sampleRate;
        h2Sin += x * std::sin (2.0 * phase);
        h2Cos += x * std::cos (2.0 * phase);
        h3Sin += x * std::sin (3.0 * phase);
        h3Cos += x * std::cos (3.0 * phase);
        ++count;
    }

    result.rms = std::sqrt (sumSquares / count);
    result.h2 = 2.0 * std::hypot (h2Sin, h2Cos) / count;
    result.h3 = 2.0 * std::hypot (h3Sin, h3Cos) / count;
    return result;
}

Metrics render (float frequencyHz, float driveDb, float harmonicRatio,
                bool useADAA, float amplitude)
{
    return renderAt (96000.0, frequencyHz, driveDb, harmonicRatio, useADAA, amplitude);
}
}

int main()
{
    bool passed = true;

    // 1. The stage stays bounded at extreme drive, including over-unity input.
    //    Reference point: the previous unbounded curve returned ~1140 for the
    //    same x, so a 1.1 ceiling is still a meaningful regression bound. The
    //    small excess above 1 is the 20 Hz DC blocker ringing on a square-ish wave.
    for (float ratio : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto r = render (1000.0f, 24.0f, ratio, false, 4.0f);
        std::printf ("ratio %.2f @ 24 dB drive: max %.4f rms %.4f\n",
                     (double) ratio, (double) r.maxAbs, r.rms);
        passed = passed && r.finite && r.maxAbs <= 1.1f * modelMatchGain && r.maxAbs > 0.5f;
    }

    // 2. ADAA reduces aliasing without reshaping the tone. At a low fundamental
    //    the anti-aliased output tracks the directly shaped one closely.
    {
        const auto direct = render (100.0f, 12.0f, 0.5f, false, 0.5f);
        const auto adaa = render (100.0f, 12.0f, 0.5f, true, 0.5f);
        const double relative = std::abs (adaa.rms - direct.rms) / juce::jmax (1.0e-9, direct.rms);
        std::printf ("ADAA vs direct @ 100 Hz: rms %.6f vs %.6f (%.3f%%)\n",
                     direct.rms, adaa.rms, relative * 100.0);
        passed = passed && adaa.finite && relative < 0.05;
    }

    // 3. Harmonic balance still works with ADAA engaged.
    {
        const auto oddHeavy = render (1000.0f, 18.0f, 0.0f, true, 0.6f);
        const auto evenHeavy = render (1000.0f, 18.0f, 1.0f, true, 0.6f);
        std::printf ("ADAA H2: odd %.5f -> even %.5f\n", oddHeavy.h2, evenHeavy.h2);
        passed = passed && evenHeavy.h2 > oddHeavy.h2 * 2.0;
    }

    // 4. The stage must stay finite at the 8x oversampled rate too. A biquad DC
    //    blocker at 20 Hz against 384 kHz put a pole outside the unit circle once
    //    its coefficients were stored as float, and this stage diverged there.
    {
        for (bool useADAA : { false, true })
        {
            const auto r = renderAt (384000.0, 1000.0f, 12.0f, 0.5f, useADAA, 0.25f);
            std::printf ("8x rate (384 kHz) adaa %d: max %.4f rms %.4f\n",
                         useADAA ? 1 : 0, (double) r.maxAbs, r.rms);
            passed = passed && r.finite && r.maxAbs <= 1.1f * modelMatchGain
                   && r.rms < 1.0 * modelMatchGain;
        }
    }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
