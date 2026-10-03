#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdio>
#include "../Source/DSP/Compressor.h"

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

struct RenderResult
{
    float gainReductionDb = 0.0f;
    double outputRmsDb = -100.0;
};

RenderResult renderSteadyTone (bool feedback)
{
    Compressor compressor;
    compressor.prepare (sampleRate, 1);
    compressor.setThresholdDb (-18.0f);
    compressor.setRatio (4.0f);
    compressor.setKneeDb (0.0f);
    compressor.setAttackMs (1.0f);
    compressor.setReleaseMs (100.0f);
    compressor.setLookAheadMs (0.0f);
    compressor.setSidechainHPFHz (20.0f);
    compressor.setFeedbackMode (feedback);

    constexpr int totalSamples = static_cast<int> (sampleRate * 2.0);
    juce::AudioBuffer<float> block (1, blockSize);
    double sumSquares = 0.0;
    int counted = 0;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int count = juce::jmin (blockSize, totalSamples - start);
        block.setSize (1, count, false, false, true);
        auto* data = block.getWritePointer (0);

        for (int i = 0; i < count; ++i)
        {
            const auto sampleIndex = start + i;
            data[i] = 0.9f * std::sin (2.0 * juce::MathConstants<double>::pi
                                       * 1000.0 * sampleIndex / sampleRate);
        }

        compressor.process (block);

        if (start >= totalSamples / 2)
            for (int i = 0; i < count; ++i)
            {
                sumSquares += static_cast<double> (data[i]) * data[i];
                ++counted;
            }
    }

    return { compressor.getCurrentGainReductionDb(),
             20.0 * std::log10 (std::sqrt (sumSquares / juce::jmax (1, counted))) };
}

bool renderFeedbackStress()
{
    Compressor compressor;
    compressor.prepare (sampleRate, 2);
    compressor.setThresholdDb (-30.0f);
    compressor.setRatio (20.0f);
    compressor.setKneeDb (0.0f);
    compressor.setAttackMs (0.1f);
    compressor.setReleaseMs (10.0f);
    compressor.setLookAheadMs (5.0f);
    compressor.setSidechainHPFHz (20.0f);
    compressor.setFeedbackMode (true);

    juce::AudioBuffer<float> block (2, blockSize);
    for (int blockIndex = 0; blockIndex < static_cast<int> (sampleRate * 3.0 / blockSize); ++blockIndex)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = block.getWritePointer (ch);
            for (int i = 0; i < blockSize; ++i)
            {
                const int sampleIndex = blockIndex * blockSize + i;
                const bool burst = (sampleIndex / 2048) % 2 == 0;
                const float alternating = (sampleIndex & 1) == 0 ? 1.4f : -1.4f;
                data[i] = burst ? alternating : 0.0f;
            }
        }

        compressor.process (block);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const float sample = block.getSample (ch, i);
                if (! std::isfinite (sample) || std::abs (sample) > 2.0f)
                    return false;
            }

        const float gr = compressor.getCurrentGainReductionDb();
        if (! std::isfinite (gr) || gr < 0.0f || gr > 100.0f)
            return false;
    }

    return true;
}

bool renderLookAheadSwitch()
{
    Compressor compressor;
    compressor.prepare (sampleRate, 1);
    compressor.setThresholdDb (0.0f);
    compressor.setRatio (1.0f);
    compressor.setKneeDb (0.0f);
    compressor.setLookAheadMs (0.0f);

    juce::AudioBuffer<float> block (1, blockSize);
    block.clear();
    block.setSample (0, 0, 1.0f);
    compressor.process (block);

    // Re-enable a long delay after the zero-latency block. A fixed-capacity
    // history must retain the impulse and return it at the new delay position.
    compressor.setLookAheadMs (10.0f);
    constexpr int expectedOffset = 480 - blockSize;
    int peak = -1;
    for (int start = 0; start < 512; start += blockSize)
    {
        block.clear();
        compressor.process (block);
        for (int i = 0; i < blockSize; ++i)
            if (std::abs (block.getSample (0, i)) > 0.5f)
                peak = start + i;
    }

    std::printf ("look-ahead switch: peak %d, expected %d\n", peak, expectedOffset);
    return peak == expectedOffset;
}

bool renderLookAheadHistory()
{
    Compressor compressor;
    compressor.prepare (sampleRate, 1);
    compressor.setThresholdDb (0.0f);
    compressor.setRatio (1.0f);
    compressor.setKneeDb (0.0f);
    compressor.setLookAheadMs (20.0f);

    constexpr int historySamples = 700;
    juce::AudioBuffer<float> block (1, historySamples);
    for (int i = 0; i < historySamples; ++i)
        block.setSample (0, i, static_cast<float> (i));
    compressor.process (block);

    // The write cursor is now beyond the next 10 ms delay length. It must stay
    // on the fixed-capacity ring: at absolute sample 700, a 480-sample delay
    // reads the sample written at absolute sample 220.
    compressor.setLookAheadMs (10.0f);
    juce::AudioBuffer<float> next (1, 1);
    next.clear();
    compressor.process (next);

    const float actual = next.getSample (0, 0);
    std::printf ("look-ahead history: %.1f, expected 220.0\n", (double) actual);
    return std::abs (actual - 220.0f) < 1.0e-6f;
}
}

int main()
{
    const auto feedforward = renderSteadyTone (false);
    const auto feedback = renderSteadyTone (true);

    std::printf ("feedforward: GR %.2f dB, output %.2f dBFS\n",
                 static_cast<double> (feedforward.gainReductionDb), feedforward.outputRmsDb);
    std::printf ("feedback:    GR %.2f dB, output %.2f dBFS\n",
                 static_cast<double> (feedback.gainReductionDb), feedback.outputRmsDb);

    bool passed = true;
    // The feedback gain computer is the inverse of the feed-forward one, so
    // both topologies settle on the same static curve: Ratio and Knee mean the
    // same thing in either mode (the topologies differ in their dynamics).
    // This used to assert a weaker feedback curve, which was the 2 - 1/R ratio
    // cap bug (knob 4 -> 1.75:1); see FeedbackRatioTest for the full curve.
    passed = passed && feedforward.gainReductionDb > 8.0f;
    passed = passed && feedback.gainReductionDb > 8.0f;
    passed = passed && std::abs (feedforward.gainReductionDb - feedback.gainReductionDb) < 1.0f;
    passed = passed && std::abs (feedback.outputRmsDb - feedforward.outputRmsDb) < 1.0;
    passed = passed && renderFeedbackStress();
    passed = passed && renderLookAheadSwitch();
    passed = passed && renderLookAheadHistory();

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
