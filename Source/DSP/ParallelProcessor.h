#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/**
    Parallel (New York) compression: split signal into dry & wet paths,
    compress the wet path independently, then recombine at user-controlled
    mix ratio. This preserves the transient character of the dry while
    adding the tone-coloring and glue of the compressed tube stage.
*/
class ParallelProcessor
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    void setMixRatio (float dryWetRatio01) noexcept { mixRatio = dryWetRatio01; }

    /**
        Process signal in-place. Extracts dry copy *before* calling processor,
        then blends result back according to mix ratio.
    */
    template <typename ProcessorFunc>
    void processParallel (juce::AudioBuffer<float>& buffer, ProcessorFunc&& processor) noexcept
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // Store uncompressed copy.
        dryBuffer.makeCopyOf (buffer, true);

        // Process wet path in-place.
        processor (buffer);

        // Blend: (1 - mixRatio) * dry + mixRatio * wet
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wet = buffer.getWritePointer (ch);
            auto* dry = dryBuffer.getReadPointer (ch);
            const float wetGain = mixRatio;
            const float dryGain = 1.0f - mixRatio;

            for (int i = 0; i < numSamples; ++i)
                wet[i] = dry[i] * dryGain + wet[i] * wetGain;
        }
    }

private:
    float mixRatio = 1.0f; // 0 = all dry, 1 = all wet
    juce::AudioBuffer<float> dryBuffer;
};
