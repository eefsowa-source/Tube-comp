#pragma once

// Shared helpers for the processor-level regression tests. Each test builds a
// processor the way a host recalls a session (parameters first, then
// prepareToPlay), renders a deterministic signal, and measures it.

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>
#include "../Source/PluginProcessor.h"

namespace tc_test
{
inline constexpr double pi = juce::MathConstants<double>::pi;

inline void setParam (juce::AudioProcessor& processor, const juce::String& id, float value)
{
    auto& p = static_cast<TubeCompAudioProcessor&> (processor);
    auto* parameter = p.apvts.getParameter (id);
    jassert (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

using ParamList = std::initializer_list<std::pair<juce::String, float>>;

inline std::unique_ptr<TubeCompAudioProcessor> makeProcessor (ParamList params,
                                                             double sampleRate = 48000.0,
                                                             int blockSize = 512)
{
    auto processor = std::make_unique<TubeCompAudioProcessor>();
    processor->setPlayConfigDetails (2, 2, sampleRate, blockSize);
    for (const auto& [id, value] : params)
        setParam (*processor, id, value);
    processor->prepareToPlay (sampleRate, blockSize);
    return processor;
}

/** Renders `totalSamples` of `signal` (same on both channels) in blocks of
    `blockSize` and returns channel 0 of the output. */
inline std::vector<float> render (TubeCompAudioProcessor& processor,
                                  const std::function<float (int)>& signal,
                                  int totalSamples, int blockSize)
{
    std::vector<float> out;
    out.reserve (static_cast<size_t> (totalSamples));
    juce::MidiBuffer midi;
    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int count = juce::jmin (blockSize, totalSamples - start);
        juce::AudioBuffer<float> block (2, count);
        for (int i = 0; i < count; ++i)
        {
            const float s = signal (start + i);
            block.setSample (0, i, s);
            block.setSample (1, i, s);
        }
        processor.processBlock (block, midi);
        for (int i = 0; i < count; ++i)
            out.push_back (block.getSample (0, i));
    }
    return out;
}

/** Complex amplitude of `frequency` in x[start, start+length). */
inline std::complex<double> lockIn (const std::vector<float>& x, int start, int length,
                                    double frequency, double sampleRate)
{
    std::complex<double> acc = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double phase = -2.0 * pi * frequency * (start + i) / sampleRate;
        acc += static_cast<double> (x[static_cast<size_t> (start + i)]) * std::polar (1.0, phase);
    }
    return acc * 2.0 / static_cast<double> (length);
}

/** Whole number of periods of `frequency` that fits in `maxLength` samples. */
inline int wholePeriods (double frequency, double sampleRate, int maxLength)
{
    const double periods = std::floor (maxLength * frequency / sampleRate);
    return static_cast<int> (std::llround (periods * sampleRate / frequency));
}

inline double toDb (double gain) { return 20.0 * std::log10 (juce::jmax (1.0e-12, gain)); }

inline bool check (bool condition, const char* what)
{
    std::printf ("  [%s] %s\n", condition ? "ok" : "FAIL", what);
    return condition;
}
}
