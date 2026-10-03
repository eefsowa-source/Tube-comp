// Regression: Feedback detector mode must honour the Ratio control, stay
// stable at the fastest attack, and not add look-ahead latency it cannot use.
//
// The feedback path fed the *output* level into the feed-forward gain
// computer, so the effective ratio was 2 - 1/R: knob 4 gave 1.75:1 and knob 20
// gave 1.95:1. Feedback is the default topology. Separately, a feedback
// detector cannot look ahead, yet the look-ahead delay (and its latency) was
// still applied, and the look-ahead parameter could not reach 0 ms.

#include "RegressionHelpers.h"
#include "../Source/DSP/Compressor.h"

using namespace tc_test;

namespace
{
constexpr double sampleRate = 48000.0;

struct CompSettings
{
    float ratio = 4.0f, kneeDb = 0.0f, thresholdDb = -30.0f;
    float attackMs = 1.0f, releaseMs = 100.0f;
    int timeConstant = 0;
    bool feedback = false;
    double rate = sampleRate;
};

void configure (Compressor& c, const CompSettings& s)
{
    c.prepare (s.rate, 1);
    c.setThresholdDb (s.thresholdDb);
    c.setRatio (s.ratio);
    c.setKneeDb (s.kneeDb);
    c.setLookAheadMs (0.0f);
    c.setSidechainHPFHz (20.0f);
    c.setFeedbackMode (s.feedback);
    c.setTimeConstant (s.timeConstant);
    if (s.timeConstant == 0)
    {
        c.setAttackMs (s.attackMs);
        c.setReleaseMs (s.releaseMs);
    }
}

/** Steady-state gain reduction for a 1 kHz square wave at `levelDb`. */
float steadyGainReductionDb (const CompSettings& s, float levelDb)
{
    Compressor c;
    configure (c, s);
    const float a = juce::Decibels::decibelsToGain (levelDb);
    const int halfPeriod = static_cast<int> (s.rate / 2000.0);
    juce::AudioBuffer<float> block (1, 480);
    int n = 0;
    for (int b = 0; b < static_cast<int> (s.rate / 480.0); ++b)
    {
        for (int i = 0; i < 480; ++i, ++n)
            block.setSample (0, i, ((n / halfPeriod) % 2 == 0) ? a : -a);
        c.process (block);
    }
    return c.getCurrentGainReductionDb();
}

/** Per-sample GR trajectory for a loud square wave, used to look for a
    sample-rate limit cycle in the feedback loop. Returns the amplitude of the
    Nyquist-rate component of the GR sequence (dB) over the last 0.5 s. */
double nyquistGainRipple (const CompSettings& s, bool& finite)
{
    Compressor c;
    configure (c, s);
    const int halfPeriod = static_cast<int> (s.rate / 2000.0);
    const int total = static_cast<int> (s.rate);
    juce::AudioBuffer<float> block (1, 1);
    double nyquist = 0.0;
    int counted = 0;
    finite = true;
    for (int n = 0; n < total; ++n)
    {
        block.setSample (0, 0, ((n / halfPeriod) % 2 == 0) ? 1.0f : -1.0f);
        c.process (block);
        const float gr = c.getCurrentGainReductionDb();
        finite = finite && std::isfinite (gr) && std::isfinite (block.getSample (0, 0));
        if (n >= total / 2)
        {
            nyquist += (n % 2 == 0 ? 1.0 : -1.0) * gr;
            ++counted;
        }
    }
    return std::abs (nyquist) / juce::jmax (1, counted);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    bool passed = true;

    // 1. Feedback must produce the same static curve as feed-forward (the
    //    ratio and knee mean the same thing in both topologies).
    std::printf ("static curve, threshold -30 dB:\n");
    for (float knee : { 0.0f, 12.0f })
        for (float ratio : { 2.0f, 4.0f, 20.0f })
        {
            double worst = 0.0;
            for (float level = -50.0f; level <= 0.0f; level += 2.0f)
            {
                CompSettings ff { ratio, knee };
                CompSettings fb = ff;
                fb.feedback = true;
                worst = juce::jmax (worst, static_cast<double> (std::abs (steadyGainReductionDb (ff, level)
                                                                          - steadyGainReductionDb (fb, level))));
            }
            CompSettings fb { ratio, knee };
            fb.feedback = true;
            const float grQuiet = steadyGainReductionDb (fb, -20.0f);
            const float grLoud = steadyGainReductionDb (fb, 0.0f);
            const double effective = 20.0 / (20.0 - (grLoud - grQuiet));
            std::printf ("  knee %4.1f ratio %4.1f: FB effective ratio %.2f:1, worst |GR_fb - GR_ff| %.2f dB\n",
                         knee, ratio, effective, worst);
            passed &= check (std::abs (effective - ratio) < 0.1 * ratio, "feedback effective ratio within 10 % of the knob");
            passed &= check (worst < 0.5, "feedback static curve matches feed-forward within 0.5 dB");
        }

    // 2. Stability: at the fastest attack (custom 0.1 ms, TC1 0.2 ms) and the
    //    highest ratio the one-sample feedback loop has a linearised gain well
    //    below -1. It must neither limit-cycle nor overshoot into extra gain
    //    reduction (the attack/release asymmetry turns an unstable loop into a
    //    ratchet that parks GR above the static curve).
    std::printf ("feedback stability, ratio 20, 0 dBFS square:\n");
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int tc : { 0, 1 })
        {
            CompSettings fb { 20.0f, 0.0f, -30.0f, 0.1f, 10.0f, tc, true, rate };
            CompSettings ff = fb;
            ff.feedback = false;
            bool finite = true;
            const double ripple = nyquistGainRipple (fb, finite);
            const double error = steadyGainReductionDb (fb, 0.0f) - steadyGainReductionDb (ff, 0.0f);
            std::printf ("  %6.0f Hz %s: Nyquist GR ripple %.4f dB, steady GR error vs static curve %+.2f dB\n",
                         rate, tc ? "TC1        " : "attack 0.1 ", ripple, error);
            passed &= check (finite && ripple < 0.05, "no sample-rate limit cycle");
            passed &= check (std::abs (error) < 0.5, "no overshoot past the static curve at the fastest attack");
        }

    // 3. Look-ahead: feedback mode reports and applies none; feed-forward can
    //    now be set to 0 ms (no latency) and keeps exact look-ahead latency.
    {
        auto probe = makeProcessor ({});
        auto* lookAhead = probe->apvts.getParameter (ParamIDs::lookAhead);
        const float minimum = lookAhead->convertFrom0to1 (0.0f);
        std::printf ("look-ahead parameter minimum: %.2f ms\n", minimum);
        passed &= check (std::abs (minimum) < 1.0e-6f, "look-ahead can be set to 0 ms");

        struct Case { float topology, lookAheadMs; int expected; const char* what; };
        for (const auto& c : { Case { 1.0f, 5.0f, 0, "feedback + 5 ms look-ahead adds no latency" },
                               Case { 1.0f, 20.0f, 0, "feedback + 20 ms look-ahead adds no latency" },
                               Case { 0.0f, 5.0f, 240, "feed-forward 5 ms look-ahead = 240 samples" },
                               Case { 0.0f, 0.0f, 0, "feed-forward 0 ms look-ahead = 0 samples" } })
        {
            auto processor = makeProcessor ({ { ParamIDs::oversample, 0.0f },
                                              { ParamIDs::topology, c.topology },
                                              { ParamIDs::lookAhead, c.lookAheadMs } });
            auto out = render (*processor, [] (int n) { return n == 0 ? 0.5f : 0.0f; }, 2048, 512);
            const int latency = processor->getLatencySamples();
            int peak = 0;
            for (int i = 0; i < 2048; ++i)
                if (std::abs (out[static_cast<size_t> (i)]) > std::abs (out[static_cast<size_t> (peak)]))
                    peak = i;
            std::printf ("  topology %s, look-ahead %4.1f ms: latency %d, impulse peak %d\n",
                         c.topology > 0.5f ? "FB" : "FF", c.lookAheadMs, latency, peak);
            passed &= check (latency == c.expected && std::abs (peak - latency) <= 2, c.what);
        }

        // Old sessions stored look-ahead as a plain value; it must restore as-is.
        auto source = makeProcessor ({ { ParamIDs::lookAhead, 5.0f }, { ParamIDs::topology, 0.0f } });
        juce::MemoryBlock state;
        source->getStateInformation (state);
        auto restored = makeProcessor ({});
        restored->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        const float value = restored->apvts.getRawParameterValue (ParamIDs::lookAhead)->load();
        std::printf ("  restored look-ahead: %.2f ms\n", value);
        passed &= check (std::abs (value - 5.0f) < 1.0e-4f, "saved look-ahead value restores unchanged");
    }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
