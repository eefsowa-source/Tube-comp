#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include "../Source/PluginProcessor.h"
#include "../Source/UI/VUMeterComponent.h"

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

/** Gain reduction left after a 0.5 s tone followed by 0.15 s of silence.
    Used to show which attack/release pair the compressor actually applied. */
double gainReductionAfterRelease (int tcChoice, float attackMs, float releaseMs)
{
    auto processor = std::make_unique<TubeCompAudioProcessor>();
    processor->setPlayConfigDetails (2, 2, sampleRate, toneBlockSize);
    setParam (*processor, ParamIDs::threshold, -24.0f);
    setParam (*processor, ParamIDs::ratio, 4.0f);
    setParam (*processor, ParamIDs::knee, 0.0f);
    setParam (*processor, ParamIDs::topology, 1.0f); // feedback vari-mu
    setParam (*processor, ParamIDs::mix, 1.0f);
    setParam (*processor, ParamIDs::sidechainHPF, 20.0f);
    setParam (*processor, ParamIDs::lookAhead, 0.1f);
    setParam (*processor, ParamIDs::oversample, 0.0f);
    setParam (*processor, ParamIDs::attack, attackMs);
    setParam (*processor, ParamIDs::release, releaseMs);
    setParam (*processor, ParamIDs::timeConstant, static_cast<float> (tcChoice));
    processor->prepareToPlay (sampleRate, toneBlockSize);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, toneBlockSize);
    int position = 0;

    auto render = [&] (int totalSamples, bool tone)
    {
        for (int start = 0; start < totalSamples; start += toneBlockSize)
        {
            const int count = juce::jmin (toneBlockSize, totalSamples - start);
            block.setSize (2, count, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = block.getWritePointer (ch);
                for (int i = 0; i < count; ++i)
                {
                    const double t = static_cast<double> (position + i) / sampleRate;
                    data[i] = tone ? static_cast<float> (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t))
                                   : 0.0f;
                }
            }
            position += count;
            processor->processBlock (block, midi);
        }
    };

    render (static_cast<int> (sampleRate * 0.5), true);
    render (static_cast<int> (sampleRate * 0.15), false);
    return processor->getGainReductionDb();
}

/** Goertzel magnitude (linear amplitude of a real sinusoid) in dB at one frequency. */
double goertzelMagDb (const float* x, int n, double freq)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
    const double c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double s0 = static_cast<double> (x[i]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double real = s1 - s2 * std::cos (w);
    const double imag = s2 * std::sin (w);
    return 20.0 * std::log10 (juce::jmax (1.0e-12, std::sqrt (real * real + imag * imag) / (n * 0.5)));
}

/** Second-harmonic level (dBc) and output level (dB) of a 1 kHz tone through a
    bare triode stage parked at the requested vari-mu control bias. */
struct TriodeTone { double h2Dbc = 0.0; double rmsDb = 0.0; };

TriodeTone measureTriodeTone (double controlBiasVolts, double levelDb)
{
    constexpr int blockSize = 4096;
    constexpr int blocks = 8;

    TriodeStage stage;
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
    stage.prepare (spec);
    stage.setDrive (6.0f);
    stage.setHarmonicRatio (0.5f);
    stage.setBrightness (0.5f);
    stage.setBiasDrive (0.5f);
    stage.setControlBiasVolts (controlBiasVolts);

    juce::AudioBuffer<float> buffer (2, blockSize);
    double sumSquares = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < blockSize; ++i)
            {
                const double t = static_cast<double> (b * blockSize + i) / sampleRate;
                data[i] = static_cast<float> (juce::Decibels::decibelsToGain (levelDb)
                                              * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
            }
        }

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        stage.process (context);

        if (b == blocks - 1)
            for (int i = 0; i < blockSize; ++i)
                sumSquares += static_cast<double> (buffer.getSample (0, i)) * buffer.getSample (0, i);
    }

    TriodeTone result;
    const auto* last = buffer.getReadPointer (0);
    result.h2Dbc = goertzelMagDb (last, blockSize, 2000.0) - goertzelMagDb (last, blockSize, 1000.0);
    result.rmsDb = 10.0 * std::log10 (juce::jmax (1.0e-24, sumSquares / blockSize));
    return result;
}

/** Control bias the processor requests for a steady tone, in volts (<= 0). */
double appliedControlBiasVolts (float thresholdDb, float ratio, double levelDb)
{
    auto processor = std::make_unique<TubeCompAudioProcessor>();
    processor->setPlayConfigDetails (2, 2, sampleRate, toneBlockSize);
    setParam (*processor, ParamIDs::threshold, thresholdDb);
    setParam (*processor, ParamIDs::ratio, ratio);
    setParam (*processor, ParamIDs::knee, 0.0f);
    setParam (*processor, ParamIDs::mix, 1.0f);
    setParam (*processor, ParamIDs::circuitModel, 1.0f); // WDF triode
    setParam (*processor, ParamIDs::oversample, 1.0f);   // 2x
    setParam (*processor, ParamIDs::topology, 0.0f);     // feedforward, deterministic GR
    setParam (*processor, ParamIDs::lookAhead, 0.1f);
    processor->prepareToPlay (sampleRate, toneBlockSize);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, toneBlockSize);
    for (int b = 0; b < 40; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = block.getWritePointer (ch);
            for (int i = 0; i < toneBlockSize; ++i)
            {
                const double t = static_cast<double> (b * toneBlockSize + i) / sampleRate;
                data[i] = static_cast<float> (juce::Decibels::decibelsToGain (levelDb)
                                              * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
            }
        }
        processor->processBlock (block, midi);
    }

    return processor->getControlBiasVolts();
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

    // 8. A Time Constant preset owns the attack/release timing. The manual
    //    knobs must be ignored while a TC is selected (applying them every
    //    block used to overwrite the TC values), and Custom must still follow
    //    the knobs.
    {
        const double customSlow = gainReductionAfterRelease (0, 1.0f, 900.0f);
        const double tc1WithSlowKnobs = gainReductionAfterRelease (1, 90.0f, 900.0f);
        const double tc1WithFastKnobs = gainReductionAfterRelease (1, 0.1f, 10.0f);
        std::printf ("time-constant ownership: custom(900ms) %.2f dB, TC1+slow knobs %.2f dB, TC1+fast knobs %.2f dB\n",
                     customSlow, tc1WithSlowKnobs, tc1WithFastKnobs);
        // Knobs must not change anything while TC1 owns the timing.
        passed = passed && std::abs (tc1WithSlowKnobs - tc1WithFastKnobs) < 0.5;
        // The 900 ms Custom release must retain clearly more GR than TC1 (300 ms).
        passed = passed && customSlow > tc1WithSlowKnobs + 1.0;
    }

    // 9. Meter taps are non-invasive: Input is sampled before Input Gain and
    //    bypass, while Output is the final delayed plug-in signal. The VU/GR
    //    conversion helpers keep the -18 dBFS = 0 VU calibration and a dense
    //    low-GR scale testable without relying on a painted UI.
    {
        auto processor = makeProcessor (1.0f, 1.0f, 0.0f, blockSize);
        setParam (*processor, ParamIDs::inputGain, 24.0f);
        setParam (*processor, ParamIDs::outputGain, -24.0f);

        juce::MidiBuffer midi;
        juce::AudioBuffer<float> block (2, blockSize);
        for (int pass = 0; pass < 12; ++pass)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    block.setSample (ch, i, 0.25f);
            processor->processBlock (block, midi);
        }

        const float expectedPeakDb = juce::Decibels::gainToDecibels (0.25f);
        const float inputDb = processor->getInputLevelDb (0);
        const float outputDb = processor->getOutputLevelDb (0);
        const float zeroVu = VUMeterComponent::levelDbFsToVuNormalised (-18.0f);
        const float oneDbGr = VUMeterComponent::gainReductionDbToNormalised (1.0f);
        const float twentyFourDbGr = VUMeterComponent::gainReductionDbToNormalised (24.0f);
        std::printf ("meter taps: in %.2f dBFS, out %.2f dBFS, 0VU %.3f, GR 1/24 %.3f/%.3f\n",
                     static_cast<double> (inputDb), static_cast<double> (outputDb),
                     static_cast<double> (zeroVu), static_cast<double> (oneDbGr),
                     static_cast<double> (twentyFourDbGr));
        passed = passed && std::abs (inputDb - expectedPeakDb) < 0.15f;
        passed = passed && std::abs (outputDb - expectedPeakDb) < 0.15f;
        passed = passed && std::abs (zeroVu - 20.0f / 23.0f) < 1.0e-5f;
        passed = passed && oneDbGr > (1.0f / 24.0f) && oneDbGr < 1.0f;
        passed = passed && std::abs (twentyFourDbGr - 1.0f) < 1.0e-5f;
    }

    // 10. Vari-mu coupling: gain reduction must park the tube's operating point
    //     colder, which raises the second harmonic, while the small-signal gain
    //     trim keeps the steady-state level in place (the compressor still owns
    //     the level law; the tube only changes the harmonic character).
    {
        const auto neutral = measureTriodeTone (0.0, -6.0);
        double worstLevelError = 0.0;
        double h2Deep = neutral.h2Dbc;
        for (double d : { 0.45, 0.9, 1.35, 1.8 })
        {
            const auto m = measureTriodeTone (-d, -6.0);
            worstLevelError = juce::jmax (worstLevelError, std::abs (m.rmsDb - neutral.rmsDb));
            if (d > 1.7) h2Deep = m.h2Dbc;
            std::printf ("  bias %.2f V: H2 %.1f dBc (%+.1f vs neutral), level %+.2f dB\n",
                         d, m.h2Dbc, m.h2Dbc - neutral.h2Dbc, m.rmsDb - neutral.rmsDb);
        }
        std::printf ("vari-mu coupling: H2 neutral %.1f dBc, deep %.1f dBc, worst level error %.2f dB\n",
                     neutral.h2Dbc, h2Deep, worstLevelError);
        // Colder bias must add clearly more second harmonic...
        passed = passed && h2Deep > neutral.h2Dbc + 2.0;
        // ...while the small-signal trim keeps the compressor the level owner.
        passed = passed && worstLevelError < 1.0;
    }

    // 11. The coupling is wired end to end: a program being compressed must
    //     request a negative control bias, and more gain reduction must request
    //     a colder bias.
    {
        const double gentle = appliedControlBiasVolts (-18.0f, 2.0f, -12.0);
        const double hard = appliedControlBiasVolts (-18.0f, 8.0f, -6.0);
        std::printf ("coupling wiring: bias gentle %.4f V, hard %.4f V\n", gentle, hard);
        passed = passed && gentle < -0.01 && hard < gentle;
    }

    // 12. Stability stress for the coupling: extreme drive and ratio with a
    //     loud/silent burst train at 8x must stay finite and bounded. Both the
    //     control bias and its gain trim are clamped, so nothing can run away.
    {
        auto processor = std::make_unique<TubeCompAudioProcessor>();
        processor->setPlayConfigDetails (2, 2, sampleRate, toneBlockSize);
        setParam (*processor, ParamIDs::drive, 24.0f);
        setParam (*processor, ParamIDs::threshold, -60.0f);
        setParam (*processor, ParamIDs::ratio, 20.0f);
        setParam (*processor, ParamIDs::knee, 0.0f);
        setParam (*processor, ParamIDs::mix, 1.0f);
        setParam (*processor, ParamIDs::circuitModel, 1.0f);
        setParam (*processor, ParamIDs::oversample, 3.0f); // 8x
        setParam (*processor, ParamIDs::topology, 1.0f);   // feedback
        processor->prepareToPlay (sampleRate, toneBlockSize);

        juce::MidiBuffer midi;
        juce::AudioBuffer<float> block (2, toneBlockSize);
        float peak = 0.0f;
        int nonFinite = 0;

        for (int b = 0; b < 400; ++b)
        {
            const float level = ((b / 5) % 2 == 0) ? 0.9f : 0.02f;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = block.getWritePointer (ch);
                for (int i = 0; i < toneBlockSize; ++i)
                {
                    const double t = static_cast<double> (b * toneBlockSize + i) / sampleRate;
                    data[i] = static_cast<float> (level * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
                }
            }

            processor->processBlock (block, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < toneBlockSize; ++i)
                {
                    const float value = block.getSample (ch, i);
                    if (! std::isfinite (value)) ++nonFinite;
                    peak = juce::jmax (peak, std::abs (value));
                }
        }

        std::printf ("stability stress: peak %.3f, non-finite %d\n", (double) peak, nonFinite);
        passed = passed && nonFinite == 0 && peak < 8.0f;
    }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
