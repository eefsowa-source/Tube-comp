#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/**
    Latency-aligned dry reference for a processor whose wet path is delayed.

    The line writes every incoming sample into a ring and returns the sample that
    entered exactly `delaySamples` samples earlier, so a dry path can be blended
    with a wet path that reports the same latency. Writing before reading the
    same slot makes zero delay a true pass-through and any positive delay exact.
*/
class DryDelayLine
{
public:
    /** Allocates for delays up to `maxDelaySamples` (clamped to at least 1). */
    void prepare (int numChannels, int maxDelaySamples)
    {
        capacity = juce::jmax (2, maxDelaySamples + 1);
        buffer.setSize (juce::jmax (1, numChannels), capacity, false, false, true);
        reset();
    }

    void reset()
    {
        buffer.clear();
        writePos = 0;
    }

    void setDelaySamples (int newDelaySamples) noexcept
    {
        delaySamples = juce::jlimit (0, juce::jmax (0, capacity - 1), newDelaySamples);
    }

    int getDelaySamples() const noexcept { return delaySamples; }
    int getCapacity() const noexcept { return capacity; }

    /** Fills the first `numSamples` of `destination` with the delayed reference
        and stores the same span of `source` for later reads. */
    void process (const juce::AudioBuffer<float>& source,
                  juce::AudioBuffer<float>& destination,
                  int numSamples) noexcept
    {
        const int channels = juce::jmin (juce::jmin (source.getNumChannels(),
                                                     destination.getNumChannels()),
                                         buffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            int readPos = writePos - delaySamples;
            if (readPos < 0)
                readPos += capacity;

            for (int ch = 0; ch < channels; ++ch)
            {
                auto* ring = buffer.getWritePointer (ch);
                ring[writePos] = source.getReadPointer (ch)[i];
                destination.getWritePointer (ch)[i] = ring[readPos];
            }

            if (++writePos >= capacity)
                writePos = 0;
        }
    }

    /** In-place variant for raw channel pointers (e.g. an oversampled
        AudioBlock): each sample is stored before the delayed one is read back,
        so source and destination may alias. */
    void processInPlace (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const int usable = juce::jmin (numChannels, buffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            int readPos = writePos - delaySamples;
            if (readPos < 0)
                readPos += capacity;

            for (int ch = 0; ch < usable; ++ch)
            {
                auto* ring = buffer.getWritePointer (ch);
                ring[writePos] = channels[ch][i];
                channels[ch][i] = ring[readPos];
            }

            if (++writePos >= capacity)
                writePos = 0;
        }
    }

private:
    juce::AudioBuffer<float> buffer;
    int capacity = 0;
    int delaySamples = 0;
    int writePos = 0;
};
