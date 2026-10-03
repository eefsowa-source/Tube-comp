// Regression: the WDF triode path must not invert polarity.
//
// A common-cathode stage inverts, and the model used to return the plate swing
// as-is. The wet path then sat ~180 degrees from the dry path, so the Mix knob
// cancelled instead of blending (a level-matched 50 % mix nulled by ~34 dB) and
// toggling bypass flipped polarity.

#include "RegressionHelpers.h"
#include "../Source/DSP/TriodeStage.h"

using namespace tc_test;

namespace
{
double stagePhaseDegrees()
{
    constexpr double sampleRate = 192000.0;
    constexpr double frequency = 1000.0;
    constexpr int samples = static_cast<int> (sampleRate / 2);

    TriodeStage stage;
    stage.prepare ({ sampleRate, static_cast<juce::uint32> (samples), 1 });
    stage.setDrive (0.0f);
    stage.setHarmonicRatio (0.5f);
    stage.setBiasDrive (0.5f);
    stage.setBrightness (0.5f);

    juce::AudioBuffer<float> buffer (1, samples);
    std::vector<float> input (static_cast<size_t> (samples));
    for (int i = 0; i < samples; ++i)
    {
        input[static_cast<size_t> (i)] = 0.01f * static_cast<float> (std::sin (2.0 * pi * frequency * i / sampleRate));
        buffer.setSample (0, i, input[static_cast<size_t> (i)]);
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    stage.process (context);

    std::vector<float> output (buffer.getReadPointer (0), buffer.getReadPointer (0) + samples);
    const int start = samples / 2;
    const int length = wholePeriods (frequency, sampleRate, samples - start);
    const auto h = lockIn (output, start, length, frequency, sampleRate)
                 / lockIn (input, start, length, frequency, sampleRate);
    return std::arg (h) * 180.0 / pi;
}

std::complex<double> processorResponse (float mix, int model)
{
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 1000.0;
    constexpr int blockSize = 512;
    constexpr int total = 48000;

    auto processor = makeProcessor ({ { ParamIDs::circuitModel, static_cast<float> (model) },
                                      { ParamIDs::oversample, 0.0f },
                                      { ParamIDs::threshold, 0.0f },
                                      { ParamIDs::ratio, 1.0f },
                                      { ParamIDs::drive, 0.0f },
                                      { ParamIDs::mix, mix } },
                                    sampleRate, blockSize);
    auto signal = [] (int n) { return 0.01f * static_cast<float> (std::sin (2.0 * pi * frequency * n / sampleRate)); };
    const auto out = render (*processor, signal, total, blockSize);

    std::vector<float> in (static_cast<size_t> (total));
    for (int i = 0; i < total; ++i)
        in[static_cast<size_t> (i)] = signal (i);

    const int latency = processor->getLatencySamples();
    const int start = total / 2;
    const int length = wholePeriods (frequency, sampleRate, total - start - latency);
    return lockIn (out, start + latency, length, frequency, sampleRate)
         / lockIn (in, start, length, frequency, sampleRate);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    bool passed = true;

    const double stagePhase = stagePhaseDegrees();
    std::printf ("triode stage 1 kHz phase: %+.1f deg\n", stagePhase);
    passed &= check (std::abs (stagePhase) < 30.0, "TriodeStage output is in phase with its input");

    for (int model : { 0, 1 })
    {
        const auto dry = processorResponse (0.0f, model);
        const auto wet = processorResponse (1.0f, model);
        const auto half = processorResponse (0.5f, model);
        const double wetPhase = std::arg (wet / dry) * 180.0 / pi;
        const double inPhaseSum = 0.5 * (std::abs (dry) + std::abs (wet));
        std::printf ("%s: wet-vs-dry phase %+.1f deg, mix 50%% %.2f dB, in-phase blend %.2f dB\n",
                     model == 1 ? "Tube(WDF)" : "Fast     ", wetPhase, toDb (std::abs (half)), toDb (inPhaseSum));
        passed &= check (std::abs (wetPhase) < 30.0, "wet path is in phase with dry");
        passed &= check (std::abs (half) > inPhaseSum * juce::Decibels::decibelsToGain (-1.0),
                         "50 % mix blends within 1 dB of the in-phase sum (no cancellation)");
    }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
