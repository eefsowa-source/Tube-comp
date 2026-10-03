// Regression: the output must not depend on the host block size.
//
// The vari-mu coupling set the tube bias once per block from that block's peak
// gain reduction, so the bias trajectory (and with it the tube's harmonics and
// level) changed with the host buffer size: block 1024 vs 32 differed by up to
// -18.7 dB re peak. Offline bounces, different buffer settings and hosts that
// split blocks irregularly all rendered different audio.

#include "RegressionHelpers.h"

using namespace tc_test;

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int maxBlock = 1024;
constexpr int totalSamples = 96000;

float programme (int n)
{
    // 1 kHz with 50 ms bursts 20 dB up every 250 ms: GR moves constantly.
    const double t = n / sampleRate;
    const double a = std::fmod (t, 0.25) < 0.05 ? 0.5 : 0.05;
    return static_cast<float> (a * std::sin (2.0 * pi * 1000.0 * t));
}

/** Renders with a repeating pattern of block sizes (all <= maxBlock). */
std::vector<float> renderWithBlocks (int model, float quality, const std::vector<int>& pattern)
{
    auto processor = makeProcessor ({ { ParamIDs::circuitModel, static_cast<float> (model) },
                                      { ParamIDs::oversample, quality } },
                                    sampleRate, maxBlock);
    std::vector<float> out;
    out.reserve (totalSamples);
    juce::MidiBuffer midi;
    size_t p = 0;
    for (int start = 0; start < totalSamples;)
    {
        const int count = juce::jmin (pattern[p++ % pattern.size()], totalSamples - start);
        juce::AudioBuffer<float> block (2, count);
        for (int i = 0; i < count; ++i)
            for (int ch = 0; ch < 2; ++ch)
                block.setSample (ch, i, programme (start + i));
        processor->processBlock (block, midi);
        for (int i = 0; i < count; ++i)
            out.push_back (block.getSample (0, i));
        start += count;
    }
    return out;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    bool passed = true;

    for (float quality : { 0.0f, 2.0f })
        for (int model : { 0, 1 })
        {
            const auto reference = renderWithBlocks (model, quality, { 32 });
            double peak = 0.0;
            for (auto v : reference)
                peak = juce::jmax (peak, static_cast<double> (std::abs (v)));

            const std::vector<std::vector<int>> patterns { { 256 }, { 1024 }, { 17, 512, 1, 300, 1024, 64 } };
            for (const auto& pattern : patterns)
            {
                const auto other = renderWithBlocks (model, quality, pattern);
                double worst = 0.0;
                for (size_t i = 0; i < reference.size(); ++i)
                    worst = juce::jmax (worst, static_cast<double> (std::abs (other[i] - reference[i])));
                std::printf ("%s %dx, blocks %s vs 32: max diff %.2e (%.1f dB re peak %.3f)\n",
                             model ? "Tube(WDF)" : "Fast     ", 1 << static_cast<int> (quality),
                             pattern.size() > 1 ? "mixed" : juce::String (pattern[0]).toRawUTF8(),
                             worst, toDb (worst / peak), peak);
                passed &= check (worst < 1.0e-5 * peak, "output independent of block size (< -100 dB)");
            }
        }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
