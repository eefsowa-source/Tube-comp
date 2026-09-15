#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdio>
#include "../Source/DSP/TriodeStage.h"

namespace
{
struct Metrics
{
    double rms = 0.0;
    double second = 0.0;
    double third = 0.0;
    bool finite = true;
};

Metrics renderBias (float bias)
{
    constexpr double sampleRate = 192000.0; // nominal 48 kHz session at 4x OS
    constexpr double frequency = 1000.0;
    constexpr int samples = static_cast<int> (sampleRate);

    TriodeStage stage;
    stage.prepare ({ sampleRate, 512, 1 });
    stage.setDrive (12.0f);
    stage.setHarmonicRatio (0.5f);
    stage.setBiasDrive (bias);
    stage.setBrightness (0.5f);

    juce::AudioBuffer<float> buffer (1, samples);
    auto* data = buffer.getWritePointer (0);
    for (int i = 0; i < samples; ++i)
        data[i] = 0.35f * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    stage.process (context);

    Metrics result;
    double sumSquares = 0.0;
    double h2Sin = 0.0, h2Cos = 0.0, h3Sin = 0.0, h3Cos = 0.0;
    int count = 0;
    for (int i = samples / 2; i < samples; ++i)
    {
        const double x = data[i];
        result.finite = result.finite && std::isfinite (x);
        sumSquares += x * x;
        const double phase = 2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate;
        h2Sin += x * std::sin (2.0 * phase);
        h2Cos += x * std::cos (2.0 * phase);
        h3Sin += x * std::sin (3.0 * phase);
        h3Cos += x * std::cos (3.0 * phase);
        ++count;
    }

    result.rms = std::sqrt (sumSquares / count);
    result.second = 2.0 * std::hypot (h2Sin, h2Cos) / count;
    result.third = 2.0 * std::hypot (h3Sin, h3Cos) / count;
    return result;
}
}

int main()
{
    const auto cold = renderBias (0.0f);
    const auto hot = renderBias (1.0f);
    std::printf ("cold bias: rms=%.5f H2=%.6f H3=%.6f\n", cold.rms, cold.second, cold.third);
    std::printf ("hot  bias: rms=%.5f H2=%.6f H3=%.6f\n", hot.rms, hot.second, hot.third);

    const bool biasIsAudiblyMaterial = std::abs (hot.second - cold.second) > 1.0e-4
                                     || std::abs (hot.third - cold.third) > 1.0e-4;
    const bool passed = cold.finite && hot.finite && biasIsAudiblyMaterial;
    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
