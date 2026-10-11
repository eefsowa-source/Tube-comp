// Regression: gain-reduction-driven tube bias must not thump.
//
// The vari-mu coupling moves the triode's grid bias with gain reduction. That
// shifts the plate's DC operating point, and the 7 Hz output coupling
// high-pass turned every shift into a subsonic transient: with silent input a
// 0 -> -1.8 V bias step produced a +0.6 dBFS thump, and at default settings
// content below 20 Hz sat only ~18 dB under the programme peak.

#include "RegressionHelpers.h"
#include "../Source/DSP/TriodeStage.h"

using namespace tc_test;

namespace
{
/** 4th-order Butterworth low-pass at 20 Hz: isolates subsonic content. */
struct Subsonic
{
    explicit Subsonic (double rate)
        : a (juce::dsp::IIR::Coefficients<double>::makeLowPass (rate, 20.0, 0.5412)),
          b (juce::dsp::IIR::Coefficients<double>::makeLowPass (rate, 20.0, 1.3066)) {}
    double operator() (double x) { return b.processSample (a.processSample (x)); }
    juce::dsp::IIR::Filter<double> a, b;
};

struct StageResult { double peak = 0.0, subsonicPeak = 0.0; };

/** TriodeStage at 4x (192 kHz) with the control bias stepped to `stepVolts`
    between 0.3 s and 0.8 s; `amplitude` is a 1 kHz tone at the grid input. */
StageResult renderStage (double stepVolts, float amplitude)
{
    constexpr double rate = 192000.0;
    constexpr int blockSize = 1024;
    TriodeStage stage;
    stage.prepare ({ rate, static_cast<juce::uint32> (blockSize), 1 });
    stage.setDrive (6.0f);
    stage.setHarmonicRatio (0.5f);
    stage.setBiasDrive (0.5f);
    stage.setBrightness (0.5f);
    stage.reset();

    Subsonic lf (rate);
    StageResult r;
    juce::AudioBuffer<float> buffer (1, blockSize);
    for (int start = 0; start < static_cast<int> (rate * 1.4); start += blockSize)
    {
        const double t = start / rate;
        stage.setControlBiasVolts (t >= 0.3 && t < 0.8 ? stepVolts : 0.0);
        for (int i = 0; i < blockSize; ++i)
            buffer.setSample (0, i, amplitude * static_cast<float> (std::sin (2.0 * pi * 1000.0 * (start + i) / rate)));
        juce::dsp::AudioBlock<float> block (buffer);
        stage.process (juce::dsp::ProcessContextReplacing<float> (block));
        for (int i = 0; i < blockSize; ++i)
        {
            const double y = buffer.getSample (0, i);
            const double l = lf (y);
            if (start + i > rate * 0.2) // after the start-up transient
            {
                r.peak = juce::jmax (r.peak, std::abs (y));
                r.subsonicPeak = juce::jmax (r.subsonicPeak, std::abs (l));
            }
        }
    }
    return r;
}

/** Full plug-in at default settings, 1 kHz bursts (200 ms on, 300 ms off). */
StageResult renderBursts (int model, float levelDb)
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 512;
    auto processor = makeProcessor ({ { ParamIDs::circuitModel, static_cast<float> (model) } }, rate, blockSize);
    const double a = juce::Decibels::decibelsToGain (levelDb);
    auto signal = [a] (int n)
    {
        const double t = n / rate;
        return std::fmod (t, 0.5) < 0.2 ? static_cast<float> (a * std::sin (2.0 * pi * 1000.0 * t)) : 0.0f;
    };
    const auto out = render (*processor, signal, static_cast<int> (rate * 3.0), blockSize);
    Subsonic lf (rate);
    StageResult r;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double l = lf (out[i]);
        if (i > static_cast<size_t> (rate))
        {
            r.peak = juce::jmax (r.peak, static_cast<double> (std::abs (out[i])));
            r.subsonicPeak = juce::jmax (r.subsonicPeak, std::abs (l));
        }
    }
    return r;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    bool passed = true;

    // 1. Silent input: a bias step alone must produce no output at all.
    for (double v : { -0.45, -0.9, -1.8 })
    {
        const auto r = renderStage (v, 0.0f);
        std::printf ("silent stage, bias step 0 -> %.2f V: output peak %.2e\n", v, r.peak);
        passed &= check (r.peak < 1.0e-5, "bias step with no signal is silent");
    }

    // 2. With a tone, a bias step changes the harmonics, and the DC term that
    //    comes with the extra (asymmetric) second harmonic legitimately moves
    //    with it. What must go is the operating-point step itself, which was
    //    ~30 % of the signal peak (-10 dB); the residual is ~-31 dB.
    {
        const auto steady = renderStage (0.0, 0.3f);
        const auto stepped = renderStage (-1.2, 0.3f);
        std::printf ("1 kHz tone, bias step to -1.20 V: subsonic peak %.4f (steady %.4f), output peak %.3f\n",
                     stepped.subsonicPeak, steady.subsonicPeak, stepped.peak);
        passed &= check (stepped.subsonicPeak < 0.05 * stepped.peak,
                         "subsonic transient from a hard bias step stays 26 dB under the signal");
    }

    // 3. Whole plug-in at default settings: the Tube model's subsonic content
    //    must stay in the same class as the Fast model's (which has no bias).
    for (float level : { -12.0f, -6.0f })
    {
        const auto fast = renderBursts (0, level);
        const auto tube = renderBursts (1, level);
        const double fastDb = toDb (fast.subsonicPeak / fast.peak);
        const double tubeDb = toDb (tube.subsonicPeak / tube.peak);
        std::printf ("bursts %+.0f dBFS: subsonic vs output peak  Fast %.1f dB, Tube %.1f dB\n", level, fastDb, tubeDb);
        passed &= check (tubeDb < -30.0, "Tube model subsonic content at least 30 dB under the output peak");
    }

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
