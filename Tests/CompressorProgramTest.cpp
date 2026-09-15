#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdio>
#include "../Source/DSP/Compressor.h"

namespace
{
constexpr double sampleRate = 48000.0;

float steadyToneGainReduction (float frequencyHz, float sidechainHz)
{
    Compressor compressor;
    compressor.prepare (sampleRate, 1);
    compressor.setThresholdDb (-30.0f);
    compressor.setRatio (10.0f);
    compressor.setKneeDb (0.0f);
    compressor.setAttackMs (1.0f);
    compressor.setReleaseMs (100.0f);
    compressor.setLookAheadMs (0.0f);
    compressor.setSidechainHPFHz (sidechainHz);

    juce::AudioBuffer<float> block (1, static_cast<int> (sampleRate));
    auto* samples = block.getWritePointer (0);
    for (int i = 0; i < block.getNumSamples(); ++i)
        samples[i] = 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * frequencyHz * i / sampleRate);

    compressor.process (block);
    return compressor.getCurrentGainReductionDb();
}
}

int main()
{
    bool passed = true;

    // Exact look-ahead latency: unity ratio leaves the impulse amplitude intact.
    Compressor delay;
    delay.prepare (sampleRate, 1);
    delay.setRatio (1.0f);
    delay.setLookAheadMs (5.0f);
    juce::AudioBuffer<float> impulse (1, 320);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    delay.process (impulse);

    int impulseIndex = -1;
    for (int i = 0; i < impulse.getNumSamples(); ++i)
        if (std::abs (impulse.getSample (0, i)) > 0.99f)
            impulseIndex = i;

    constexpr int expectedDelay = 240;
    std::printf ("look-ahead impulse index = %d (expected %d)\n", impulseIndex, expectedDelay);
    passed = passed && impulseIndex == expectedDelay;

    // A 120 Hz sidechain HPF should strongly reduce 40 Hz bass-triggered GR,
    // while leaving a 1 kHz program component substantially in control.
    const float bassGr = steadyToneGainReduction (40.0f, 120.0f);
    const float midGr = steadyToneGainReduction (1000.0f, 120.0f);
    std::printf ("SC HPF GR: 40 Hz = %.2f dB, 1 kHz = %.2f dB, delta = %.2f dB\n",
                 static_cast<double> (bassGr), static_cast<double> (midGr),
                 static_cast<double> (midGr - bassGr));
    passed = passed && midGr > bassGr + 5.0f;

    // Synthetic program envelope: 250 ms tone establishes GR, then silence
    // verifies that the user-selected release produces a smooth recovery.
    Compressor envelope;
    envelope.prepare (sampleRate, 1);
    envelope.setThresholdDb (-24.0f);
    envelope.setRatio (4.0f);
    envelope.setKneeDb (6.0f);
    envelope.setAttackMs (10.0f);
    envelope.setReleaseMs (100.0f);
    envelope.setLookAheadMs (0.0f);
    envelope.setSidechainHPFHz (60.0f);

    juce::AudioBuffer<float> sample (1, 1);
    for (int i = 0; i < static_cast<int> (sampleRate * 0.25); ++i)
    {
        sample.setSample (0, 0, 0.7f * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sampleRate));
        envelope.process (sample);
    }
    const float settledGr = envelope.getCurrentGainReductionDb();

    sample.setSample (0, 0, 0.0f);
    for (int i = 0; i < static_cast<int> (sampleRate * 0.1); ++i)
        envelope.process (sample);
    const float release100msGr = envelope.getCurrentGainReductionDb();
    const float releaseRatio = release100msGr / juce::jmax (settledGr, 0.001f);
    std::printf ("program release: settled = %.2f dB, after 100 ms = %.2f dB (%.3f)\n",
                 static_cast<double> (settledGr), static_cast<double> (release100msGr),
                 static_cast<double> (releaseRatio));
    passed = passed && settledGr > 8.0f && releaseRatio > 0.30f && releaseRatio < 0.45f;

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
