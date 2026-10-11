// Regression: host bypass must keep the reported latency.
//
// The processor reported look-ahead + oversampling latency but neither exposed
// its bypass parameter to the host nor overrode processBlockBypassed(), so a
// host bypass fell through to JUCE's default pass-through: the signal jumped
// forward by the full latency (impulse at sample 0 instead of 244) against the
// host's delay compensation.

#include "RegressionHelpers.h"

using namespace tc_test;

namespace
{
constexpr double sampleRate = 48000.0;

/** Impulse response through processBlockBypassed(); returns the peak index. */
int bypassedImpulsePeak (TubeCompAudioProcessor& processor, int blockSize, int total)
{
    juce::MidiBuffer midi;
    int peak = -1;
    for (int start = 0; start < total; start += blockSize)
    {
        juce::AudioBuffer<float> block (2, blockSize);
        block.clear();
        if (start == 0)
        {
            block.setSample (0, 0, 1.0f);
            block.setSample (1, 0, 1.0f);
        }
        processor.processBlockBypassed (block, midi);
        for (int i = 0; i < blockSize; ++i)
            if (std::abs (block.getSample (0, i)) > 0.5f)
                peak = start + i;
    }
    return peak;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    bool passed = true;

    // 1. The host must be able to find the plug-in's own bypass parameter, so
    //    host bypass drives the same latency-compensated path as the panel.
    {
        auto processor = makeProcessor ({});
        auto* bypass = processor->getBypassParameter();
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (bypass);
        std::printf ("getBypassParameter(): %s\n", withId != nullptr ? withId->paramID.toRawUTF8() : "nullptr");
        passed &= check (withId != nullptr && withId->paramID == ParamIDs::bypass,
                         "getBypassParameter() returns the 'bypass' parameter");
    }

    // 2. processBlockBypassed() (hosts that bypass without the parameter) must
    //    delay by the reported latency, for every quality step and block size.
    for (float quality : { 0.0f, 2.0f, 3.0f })
        for (int blockSize : { 32, 512 })
        {
            auto processor = makeProcessor ({ { ParamIDs::oversample, quality },
                                              { ParamIDs::topology, 0.0f },
                                              { ParamIDs::lookAhead, 5.0f } },
                                            sampleRate, blockSize);
            const int latency = processor->getLatencySamples();
            const int peak = bypassedImpulsePeak (*processor, blockSize, 4096);
            std::printf ("quality %d, block %3d: latency %d, bypassed impulse at %d\n",
                         static_cast<int> (quality), blockSize, latency, peak);
            passed &= check (latency > 0 && peak == latency, "host-bypassed impulse arrives at the reported latency");
        }

    // 3. Alternating active (Mix 0 %) and host-bypassed blocks must produce one
    //    continuous, correctly delayed signal: both paths share the dry line.
    {
        constexpr int blockSize = 128;
        auto processor = makeProcessor ({ { ParamIDs::mix, 0.0f },
                                          { ParamIDs::topology, 0.0f },
                                          { ParamIDs::lookAhead, 5.0f } },
                                        sampleRate, blockSize);
        const int latency = processor->getLatencySamples();
        auto signal = [] (int n) { return 0.3f * static_cast<float> (std::sin (2.0 * pi * 440.0 * n / sampleRate)); };
        juce::MidiBuffer midi;
        float worst = 0.0f;
        for (int b = 0; b < 64; ++b)
        {
            juce::AudioBuffer<float> block (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    block.setSample (ch, i, signal (b * blockSize + i));
            if (b % 2 == 0)
                processor->processBlock (block, midi);
            else
                processor->processBlockBypassed (block, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                const int n = b * blockSize + i - latency;
                const float expected = n >= 0 ? signal (n) : 0.0f;
                worst = juce::jmax (worst, std::abs (block.getSample (0, i) - expected));
            }
        }
        std::printf ("alternating active/bypassed blocks: max error vs delayed input %.2e\n", (double) worst);
        passed &= check (worst < 1.0e-6f, "active and host-bypassed blocks stay sample-aligned");
    }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
