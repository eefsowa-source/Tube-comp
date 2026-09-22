#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdio>
#include "../Source/DSP/DryDelayLine.h"

namespace
{
juce::AudioBuffer<float> renderThroughLine (DryDelayLine& line, int delaySamples,
                                            int blockSize, int numSamples,
                                            const juce::AudioBuffer<float>& input)
{
    line.setDelaySamples (delaySamples);

    const int numChannels = input.getNumChannels();
    juce::AudioBuffer<float> output (numChannels, numSamples);
    output.clear();

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int count = juce::jmin (blockSize, numSamples - start);

        juce::AudioBuffer<float> block (numChannels, count);
        block.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            block.copyFrom (ch, 0, input, ch, start, count);

        juce::AudioBuffer<float> dry (numChannels, count);
        dry.clear();
        line.process (block, dry, count);

        for (int ch = 0; ch < numChannels; ++ch)
            output.copyFrom (ch, start, dry, ch, 0, count);
    }

    return output;
}

int findPeakIndex (const juce::AudioBuffer<float>& buffer, int channel)
{
    int peak = -1;
    float best = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float value = std::abs (buffer.getSample (channel, i));
        if (value > best) { best = value; peak = i; }
    }
    return peak;
}

int countNonZero (const juce::AudioBuffer<float>& buffer, int channel, float threshold)
{
    int count = 0;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (channel, i)) > threshold)
            ++count;
    return count;
}
}

int main()
{
    bool passed = true;

    constexpr int numSamples = 1000;
    constexpr int delaySamples = 100;

    juce::AudioBuffer<float> impulse (2, numSamples);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    impulse.setSample (1, 10, 1.0f);

    // 1. The delay is exact and does not depend on the host block size.
    int referencePeak = -1;
    for (int blockSize : { 64, 137, 512 })
    {
        DryDelayLine line;
        line.prepare (2, 200);
        const auto out = renderThroughLine (line, delaySamples, blockSize, numSamples, impulse);

        const int peakL = findPeakIndex (out, 0);
        const int peakR = findPeakIndex (out, 1);
        std::printf ("block %3d -> L peak %d, R peak %d\n", blockSize, peakL, peakR);

        passed = passed && peakL == delaySamples && peakR == delaySamples + 10;
        passed = passed && std::abs (out.getSample (0, delaySamples) - 1.0f) < 1.0e-6f;
        passed = passed && countNonZero (out, 0, 1.0e-6f) == 1;

        if (referencePeak < 0)
            referencePeak = peakL;
        passed = passed && peakL == referencePeak;
    }

    // 2. A zero delay is a true pass-through.
    {
        DryDelayLine line;
        line.prepare (2, 200);
        const auto out = renderThroughLine (line, 0, 64, numSamples, impulse);
        std::printf ("zero delay -> L peak %d, R peak %d\n",
                     findPeakIndex (out, 0), findPeakIndex (out, 1));
        passed = passed && findPeakIndex (out, 0) == 0
               && findPeakIndex (out, 1) == 10
               && countNonZero (out, 0, 1.0e-6f) == 1;
    }

    // 3. Requests beyond the prepared capacity clamp instead of overrunning.
    {
        DryDelayLine line;
        line.prepare (1, 8);
        line.setDelaySamples (1000);
        std::printf ("clamped delay = %d, capacity = %d\n",
                     line.getDelaySamples(), line.getCapacity());
        passed = passed && line.getDelaySamples() == line.getCapacity() - 1;
    }

    // 4. Changing the delay mid-stream keeps the reference intact.
    {
        DryDelayLine line;
        line.prepare (1, 200);

        juce::AudioBuffer<float> tone (1, 512);
        for (int i = 0; i < 512; ++i)
            tone.setSample (0, i, 0.5f);

        juce::AudioBuffer<float> dry (1, 512);
        line.setDelaySamples (0);
        line.process (tone, dry, 512);
        const float before = dry.getSample (0, 511);

        line.setDelaySamples (50);
        line.process (tone, dry, 512);
        const float after = dry.getSample (0, 511);

        std::printf ("delay switch: %.4f then %.4f\n", (double) before, (double) after);
        passed = passed && std::abs (before - 0.5f) < 1.0e-6f
               && std::abs (after - 0.5f) < 1.0e-6f;
    }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
