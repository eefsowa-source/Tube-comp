#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include "../Source/PluginProcessor.h"

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int numSamples = 4096;
constexpr int lookAheadMs = 10;
constexpr int expectedLookAheadSamples = 480;

void setParam (TubeCompAudioProcessor& processor, const juce::String& parameterId, float value)
{
    auto* parameter = processor.apvts.getParameter (parameterId);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

/** Builds a processor with the parameters applied before prepareToPlay, the way
    a host recalls a session, so the smoothed values do not ramp from defaults. */
std::unique_ptr<TubeCompAudioProcessor> makeProcessor (float mix, float bypass,
                                                      float oversample, int blockSize)
{
    auto processor = std::make_unique<TubeCompAudioProcessor>();
    processor->setPlayConfigDetails (2, 2, sampleRate, blockSize);
    setParam (*processor, ParamIDs::mix, mix);
    setParam (*processor, ParamIDs::bypass, bypass);
    setParam (*processor, ParamIDs::lookAhead, static_cast<float> (lookAheadMs));
    setParam (*processor, ParamIDs::oversample, oversample);
    processor->prepareToPlay (sampleRate, blockSize);
    return processor;
}

juce::AudioBuffer<float> renderImpulse (TubeCompAudioProcessor& processor, int blockSize)
{
    juce::AudioBuffer<float> input (2, numSamples);
    input.clear();
    input.setSample (0, 0, 0.5f);
    input.setSample (1, 0, -0.5f);

    juce::AudioBuffer<float> output (2, numSamples);
    output.clear();

    juce::MidiBuffer midi;

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int count = juce::jmin (blockSize, numSamples - start);
        juce::AudioBuffer<float> block (2, count);
        block.clear();
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom (ch, 0, input, ch, start, count);

        processor.processBlock (block, midi);

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, start, block, ch, 0, count);
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

int countNonZero (const juce::AudioBuffer<float>& buffer, int channel)
{
    int count = 0;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (channel, i)) > 1.0e-9f)
            ++count;
    return count;
}

float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float worst = 0.0f;
    for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
        for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
            worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return worst;
}

constexpr int toneBlockSize = 256;

/** Output RMS in dB of a steady 1 kHz tone through the current configuration. */
double renderToneRmsDb (TubeCompAudioProcessor& processor, double levelDb)
{
    const int totalSamples = static_cast<int> (sampleRate * 2.0);
    juce::AudioBuffer<float> block (2, toneBlockSize);
    juce::MidiBuffer midi;
    double sumSquares = 0.0;
    int counted = 0;

    for (int start = 0; start < totalSamples; start += toneBlockSize)
    {
        const int count = juce::jmin (toneBlockSize, totalSamples - start);
        block.setSize (2, count, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = block.getWritePointer (ch);
            for (int i = 0; i < count; ++i)
            {
                const double t = static_cast<double> (start + i) / sampleRate;
                data[i] = static_cast<float> (juce::Decibels::decibelsToGain (levelDb)
                                              * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
            }
        }

        processor.processBlock (block, midi);

        if (start >= totalSamples / 2)
        {
            for (int i = 0; i < count; ++i)
            {
                const double x = block.getSample (0, i);
                sumSquares += x * x;
                ++counted;
            }
        }
    }

    return 20.0 * std::log10 (juce::jmax (1.0e-12, std::sqrt (sumSquares / counted)));
}

double measureModelRmsDb (float circuitModel, float driveDb, double levelDb)
{
    TubeCompAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, toneBlockSize);
    setParam (processor, ParamIDs::mix, 1.0f);
    setParam (processor, ParamIDs::threshold, 0.0f);
    setParam (processor, ParamIDs::ratio, 1.0f);
    setParam (processor, ParamIDs::knee, 0.0f);
    setParam (processor, ParamIDs::lookAhead, 0.1f);
    setParam (processor, ParamIDs::drive, driveDb);
    setParam (processor, ParamIDs::circuitModel, circuitModel);
    setParam (processor, ParamIDs::oversample, 2.0f);
    processor.prepareToPlay (sampleRate, toneBlockSize);
    return renderToneRmsDb (processor, levelDb);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    bool passed = true;
    constexpr int blockSize = 64;

    // 1. The wet path must deliver the impulse at exactly the reported latency,
    //    and the triode stage must not thump on the first samples.
    {
        auto processor = makeProcessor (1.0f, 0.0f, 0.0f, blockSize);
        const auto out = renderImpulse (*processor, blockSize);
        const int latency = processor->getLatencySamples();
        const int peak = findPeakIndex (out, 0);

        float preImpulseMax = 0.0f;
        for (int i = 0; i < latency; ++i)
            preImpulseMax = juce::jmax (preImpulseMax, std::abs (out.getSample (0, i)));

        std::printf ("wet: peak %d, reported latency %d, pre-impulse max %.8f\n",
                     peak, latency, (double) preImpulseMax);
        passed = passed && latency == expectedLookAheadSamples && peak == expectedLookAheadSamples;
        // The output coupling capacitor used to start discharged, which passed a
        // ~4.4 DC step into the first samples.
        passed = passed && preImpulseMax < 1.0e-3f;
    }

    // 2. Bypass must be the raw input delayed by exactly the reported latency.
    //    The old implementation delayed by the whole ring capacity instead.
    juce::AudioBuffer<float> bypassOut (2, numSamples);
    {
        auto processor = makeProcessor (1.0f, 1.0f, 0.0f, blockSize);
        bypassOut = renderImpulse (*processor, blockSize);
        const int latency = processor->getLatencySamples();
        const int peak = findPeakIndex (bypassOut, 0);
        std::printf ("bypass: peak %d, latency %d, L %.6f, R %.6f, nonzero %d\n",
                     peak, latency, (double) bypassOut.getSample (0, latency),
                     (double) bypassOut.getSample (1, latency), countNonZero (bypassOut, 0));
        passed = passed && latency == expectedLookAheadSamples && peak == latency;
        passed = passed && std::abs (bypassOut.getSample (0, latency) - 0.5f) < 1.0e-6f;
        passed = passed && std::abs (bypassOut.getSample (1, latency) + 0.5f) < 1.0e-6f;
        passed = passed && countNonZero (bypassOut, 0) == 1;
    }

    // 3. Mix 0% must deliver the same delayed dry reference as bypass; that is
    //    what keeps the blend from comb filtering against the wet path.
    {
        auto processor = makeProcessor (0.0f, 0.0f, 0.0f, blockSize);
        const auto mixZeroOut = renderImpulse (*processor, blockSize);
        const float difference = maxDifference (mixZeroOut, bypassOut);
        std::printf ("mix 0%% vs bypass: max diff %.8f\n", (double) difference);
        passed = passed && difference < 1.0e-6f;
    }

    // 4. Switching quality at run time must move both the reported latency and
    //    the dry alignment onto the newly active stage.
    {
        auto processor = makeProcessor (1.0f, 1.0f, 0.0f, blockSize);
        const int baseLatency = processor->getLatencySamples();

        setParam (*processor, ParamIDs::oversample, 3.0f);
        const auto out = renderImpulse (*processor, blockSize);
        const int latency = processor->getLatencySamples();
        const int peak = findPeakIndex (out, 0);

        std::printf ("quality switch: latency %d -> %d, bypass peak %d\n",
                     baseLatency, latency, peak);
        passed = passed && baseLatency == expectedLookAheadSamples;
        passed = passed && latency >= baseLatency && peak == latency;
        passed = passed && std::abs (out.getSample (0, latency) - 0.5f) < 1.0e-6f;
    }

    // 5. None of the above may depend on the host block size.
    for (int otherBlockSize : { 128, 512 })
    {
        auto processor = makeProcessor (1.0f, 1.0f, 0.0f, otherBlockSize);
        const auto out = renderImpulse (*processor, otherBlockSize);
        const int latency = processor->getLatencySamples();
        const int peak = findPeakIndex (out, 0);
        std::printf ("block %3d: latency %d, peak %d, diff vs block 64 %.8f\n",
                     otherBlockSize, latency, peak, (double) maxDifference (out, bypassOut));
        passed = passed && peak == latency && latency == expectedLookAheadSamples;
        passed = passed && maxDifference (out, bypassOut) < 1.0e-6f;
    }

    // 6. Full state round-trip: every parameter must survive
    //    getStateInformation/setStateInformation (pluginval caught bypass
    //    coming back at a non-zero value here).
    {
        auto source = makeProcessor (1.0f, 0.0f, 0.0f, blockSize);
        for (auto* parameter : source->getParameters())
            parameter->setValueNotifyingHost (0.37f);

        juce::MemoryBlock state;
        source->getStateInformation (state);

        auto destination = makeProcessor (1.0f, 0.0f, 0.0f, blockSize);
        destination->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        int mismatches = 0;
        const auto& sourceParameters = source->getParameters();
        const auto& destinationParameters = destination->getParameters();

        for (int i = 0; i < sourceParameters.size() && i < destinationParameters.size(); ++i)
        {
            const float expected = sourceParameters[i]->getValue();
            const float actual = destinationParameters[i]->getValue();
            if (std::abs (expected - actual) > 1.0e-4f)
            {
                ++mismatches;
                std::printf ("state mismatch: %s expected %.6f, actual %.6f\n",
                             sourceParameters[i]->getName (64).toRawUTF8(),
                             (double) expected, (double) actual);
            }
        }

        std::printf ("state round-trip mismatches: %d\n", mismatches);
        passed = passed && mismatches == 0;
    }

    // 7. Switching circuit model must not jump in level. The Fast path carries a
    //    trim that matches the WDF path over the normal working region; under
    //    combined heavy drive the WDF stays the more compressive of the two.
    {
        const double nominal = std::abs (measureModelRmsDb (1.0f, 6.0f, -12.0)
                                       - measureModelRmsDb (0.0f, 6.0f, -12.0));
        const double hot = std::abs (measureModelRmsDb (1.0f, 12.0f, -6.0)
                                   - measureModelRmsDb (0.0f, 12.0f, -6.0));
        const double extreme = std::abs (measureModelRmsDb (1.0f, 18.0f, 0.0)
                                       - measureModelRmsDb (0.0f, 18.0f, 0.0));

        std::printf ("model match: nominal %.2f dB, hot %.2f dB, extreme %.2f dB\n",
                     nominal, hot, extreme);
        passed = passed && nominal < 0.6 && hot < 1.8 && extreme < 3.5;
    }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
