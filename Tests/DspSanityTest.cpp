#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstdio>
#include "../Source/DSP/TriodeStage.h"

int main()
{
    constexpr double sampleRate = 44100.0 * 4.0; // pretend 4x oversampled
    constexpr int numSamples = static_cast<int> (sampleRate * 2.0); // 2 seconds

    juce::dsp::ProcessSpec spec { sampleRate, 512, 2 };

    TriodeStage stage;
    stage.prepare (spec);
    stage.setDrive (18.0f);       // hot drive to stress the nonlinearity
    stage.setHarmonicRatio (0.5f);
    stage.setBrightness (0.5f);

    juce::AudioBuffer<float> buffer (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            // Sweep amplitude from silence to full scale then to over-unity,
            // and ramp frequency, to stress both the Newton solve and the
            // cathode sag tracker.
            const double t = static_cast<double> (i) / sampleRate;
            const double freq = 100.0 + 2000.0 * (t / 2.0);
            const double amp = juce::jmin (1.4, t); // exceeds full scale near the end
            data[i] = static_cast<float> (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * t));
        }
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    stage.process (context);

    float minV = 1.0e9f, maxV = -1.0e9f;
    bool foundNaN = false, foundInf = false;
    double sumSquares = 0.0;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = data[i];
            if (std::isnan (v)) foundNaN = true;
            if (std::isinf (v)) foundInf = true;
            minV = juce::jmin (minV, v);
            maxV = juce::jmax (maxV, v);
            sumSquares += static_cast<double> (v) * v;
        }
    }

    const double rms = std::sqrt (sumSquares / (numSamples * 2));

    std::printf ("min=%.6f max=%.6f rms=%.6f nan=%s inf=%s\n",
                 static_cast<double> (minV), static_cast<double> (maxV), rms,
                 foundNaN ? "YES" : "no", foundInf ? "YES" : "no");

    if (foundNaN || foundInf)
    {
        std::printf ("FAIL: numerical blow-up detected\n");
        return 1;
    }

    if (maxV > 50.0f || minV < -50.0f)
    {
        std::printf ("FAIL: output magnitude unreasonable (Newton solve likely diverging)\n");
        return 1;
    }

    std::printf ("PASS\n");
    return 0;
}
