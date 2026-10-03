// Regression: dry and wet must stay phase-aligned at every oversampling
// factor, so partial Mix settings do not comb-filter.
//
// The oversamplers used polyphase IIR half-band filters (not linear phase) and
// the fractional latency was truncated to whole samples for the dry delay.
// At Mix 50 % the dispersed wet path cancelled against the dry path near the
// top octave: -6.6 dB at 15 kHz at 8x (Fast) where +3.3 dB is expected.

#include "RegressionHelpers.h"

using namespace tc_test;

namespace
{
std::complex<double> response (int model, float quality, float mix, double frequency, double rate)
{
    constexpr int blockSize = 512;
    const int total = static_cast<int> (rate * 0.6);
    auto processor = makeProcessor ({ { ParamIDs::circuitModel, static_cast<float> (model) },
                                      { ParamIDs::oversample, quality },
                                      { ParamIDs::threshold, 0.0f },
                                      { ParamIDs::ratio, 1.0f },
                                      { ParamIDs::drive, 0.0f },
                                      { ParamIDs::mix, mix } },
                                    rate, blockSize);
    auto signal = [frequency, rate] (int n) { return 0.001f * static_cast<float> (std::sin (2.0 * pi * frequency * n / rate)); };
    const auto out = render (*processor, signal, total, blockSize);
    std::vector<float> in (static_cast<size_t> (total));
    for (int i = 0; i < total; ++i)
        in[static_cast<size_t> (i)] = signal (i);

    const int latency = processor->getLatencySamples();
    const int start = total / 2;
    const int length = wholePeriods (frequency, rate, total - start - latency);
    return lockIn (out, start + latency, length, frequency, rate) / lockIn (in, start, length, frequency, rate);
}

/** Index of the largest output sample for a small impulse (linear region). */
int wetImpulsePeak (float quality, int& latency)
{
    auto processor = makeProcessor ({ { ParamIDs::circuitModel, 0.0f },
                                      { ParamIDs::oversample, quality },
                                      { ParamIDs::threshold, 0.0f },
                                      { ParamIDs::ratio, 1.0f },
                                      { ParamIDs::drive, 0.0f } });
    const auto out = render (*processor, [] (int n) { return n == 0 ? 0.01f : 0.0f; }, 4096, 512);
    latency = processor->getLatencySamples();
    int peak = 0;
    for (int i = 1; i < 4096; ++i)
        if (std::abs (out[static_cast<size_t> (i)]) > std::abs (out[static_cast<size_t> (peak)]))
            peak = i;
    return peak;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    bool passed = true;

    // 1. The wet path's impulse peak must sit exactly at the reported latency
    //    (linear-phase filters, integer latency), so the dry delay matches.
    for (float quality : { 0.0f, 1.0f, 2.0f, 3.0f })
    {
        int latency = 0;
        const int peak = wetImpulsePeak (quality, latency);
        std::printf ("%dx: reported latency %d, wet impulse peak %d\n", 1 << static_cast<int> (quality), latency, peak);
        passed &= check (peak == latency, "wet impulse peak at the reported latency");
    }

    // 2. Mix 50 % must land on the in-phase blend of dry and wet across the
    //    audio band for every quality step and both models.
    for (double rate : { 44100.0, 48000.0 })
        for (int model : { 0, 1 })
            for (float quality : { 1.0f, 2.0f, 3.0f })
            {
                double worstDb = 0.0, worstPhase = 0.0;
                for (double f : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0 })
                {
                    const auto dry = response (model, quality, 0.0f, f, rate);
                    const auto wet = response (model, quality, 1.0f, f, rate);
                    const auto half = response (model, quality, 0.5f, f, rate);
                    const double inPhase = 0.5 * (std::abs (dry) + std::abs (wet));
                    const double errorDb = toDb (std::abs (half) / inPhase);
                    const double phase = std::abs (std::arg (wet / dry)) * 180.0 / pi;
                    if (std::abs (errorDb) > std::abs (worstDb)) worstDb = errorDb;
                    if (f > 500.0) worstPhase = juce::jmax (worstPhase, phase); // the Fast DC blocker shifts 100 Hz
                }
                std::printf ("%5.1f kHz %s %dx: worst mix-50%% error %+.2f dB, worst wet-dry phase (>=1 kHz) %.1f deg\n",
                             rate / 1000.0, model ? "Tube(WDF)" : "Fast     ", 1 << static_cast<int> (quality), worstDb, worstPhase);
                passed &= check (std::abs (worstDb) < 0.5, "mix 50 % within 0.5 dB of the in-phase blend (100 Hz-19 kHz)");
                passed &= check (worstPhase < 15.0, "wet within 15 deg of dry (1-19 kHz)");
            }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
