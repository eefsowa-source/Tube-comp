#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdio>
#include "../Source/DSP/Compressor.h"

int main()
{
    constexpr double sampleRate = 44100.0;
    constexpr int numSamples = static_cast<int> (sampleRate * 1.0);

    Compressor comp;
    comp.prepare (sampleRate, 2);
    comp.setThresholdDb (-18.0f);
    comp.setRatio (4.0f);
    comp.setKneeDb (6.0f);
    comp.setAttackMs (5.0f);
    comp.setReleaseMs (80.0f);

    // A loud tone well above threshold: 0 dBFS sine.
    juce::AudioBuffer<float> buffer (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sampleRate);
    }

    comp.process (buffer);

    // Measure RMS over the back half (after attack has settled).
    double sumSquares = 0.0;
    int count = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = buffer.getReadPointer (ch);
        for (int i = numSamples / 2; i < numSamples; ++i)
        {
            sumSquares += static_cast<double> (data[i]) * data[i];
            ++count;
        }
    }
    const double rms = std::sqrt (sumSquares / count);
    const double rmsDb = 20.0 * std::log10 (rms);

    // A 0 dBFS sine (peak) has RMS ~ -3 dB. With threshold -18 dB, 4:1 ratio,
    // steady-state gain reduction on the peak detector should land the
    // output well below the input level.
    std::printf ("output RMS = %.2f dBFS, steady-state GR = %.2f dB\n",
                 rmsDb, static_cast<double> (comp.getCurrentGainReductionDb()));

    const bool grIsPositive = comp.getCurrentGainReductionDb() > 3.0f;
    const bool outputReducedVsInput = rmsDb < -4.0; // input RMS is ~-3 dBFS

    if (!grIsPositive || !outputReducedVsInput)
    {
        std::printf ("FAIL: expected meaningful gain reduction on a loud tone\n");
        return 1;
    }

    // Now a quiet tone well below threshold: should see ~0 dB reduction.
    comp.reset();
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = 0.01f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sampleRate);
    }
    comp.process (buffer);
    std::printf ("quiet-signal steady-state GR = %.4f dB\n",
                 static_cast<double> (comp.getCurrentGainReductionDb()));

    if (comp.getCurrentGainReductionDb() > 0.1f)
    {
        std::printf ("FAIL: expected ~0 dB reduction below threshold\n");
        return 1;
    }

    std::printf ("PASS\n");
    return 0;
}
