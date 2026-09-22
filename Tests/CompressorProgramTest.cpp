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

float releaseAfterSilence (int timeConstantChoice)
{
    Compressor compressor;
    compressor.prepare (sampleRate, 1);
    compressor.setThresholdDb (-24.0f);
    compressor.setRatio (4.0f);
    compressor.setKneeDb (0.0f);
    compressor.setLookAheadMs (0.0f);
    compressor.setSidechainHPFHz (20.0f);
    compressor.setFeedbackMode (true);
    compressor.setTimeConstant (timeConstantChoice);

    juce::AudioBuffer<float> sample (1, 1);
    for (int i = 0; i < static_cast<int> (sampleRate * 0.5); ++i)
    {
        sample.setSample (0, 0, 0.8f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                  * 1000.0 * i / sampleRate));
        compressor.process (sample);
    }

    sample.clear();
    for (int i = 0; i < static_cast<int> (sampleRate * 0.1); ++i)
        compressor.process (sample);

    return compressor.getCurrentGainReductionDb();
}

float renderLinkedLevel (int linkMode)
{
    Compressor compressor;
    compressor.prepare (sampleRate, 2);
    compressor.setThresholdDb (-24.0f);
    compressor.setRatio (4.0f);
    compressor.setKneeDb (0.0f);
    compressor.setLookAheadMs (0.0f);
    compressor.setSidechainHPFHz (20.0f);
    compressor.setFeedbackMode (true);
    compressor.setLinkMode (linkMode);

    juce::AudioBuffer<float> block (2, 64);
    double sumSquares = 0.0;
    int count = 0;
    for (int blockIndex = 0; blockIndex < static_cast<int> (sampleRate * 0.5 / 64); ++blockIndex)
    {
        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            const int sampleIndex = blockIndex * block.getNumSamples() + i;
            const float tone = std::sin (2.0 * juce::MathConstants<double>::pi
                                         * 1000.0 * sampleIndex / sampleRate);
            block.setSample (0, i, 0.9f * tone);
            block.setSample (1, i, 0.25f * tone);
        }

        compressor.process (block);
        if (blockIndex > 200)
            for (int i = 0; i < block.getNumSamples(); ++i)
            {
                const double sampleValue = block.getSample (1, i);
                sumSquares += sampleValue * sampleValue;
                ++count;
            }
    }

    return static_cast<float> (std::sqrt (sumSquares / juce::jmax (1, count)));
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

    const float tc1Release = releaseAfterSilence (1);
    const float tc4Release = releaseAfterSilence (4);
    std::printf ("Fairchild time constants: TC1 %.2f dB, TC4 %.2f dB after 100 ms\n",
                 static_cast<double> (tc1Release), static_cast<double> (tc4Release));
    passed = passed && tc4Release > tc1Release + 1.0f;

    const float independentRight = renderLinkedLevel (0);
    const float linkedRight = renderLinkedLevel (1);
    const float lateralVerticalRight = renderLinkedLevel (2);
    std::printf ("link modes: L/R %.5f, linked %.5f, Lat/Ver %.5f\n",
                 static_cast<double> (independentRight), static_cast<double> (linkedRight),
                 static_cast<double> (lateralVerticalRight));
    passed = passed && std::isfinite (independentRight)
                   && std::isfinite (linkedRight)
                   && std::isfinite (lateralVerticalRight)
                   && linkedRight < independentRight * 0.95f
                   && lateralVerticalRight > 0.0f;

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
