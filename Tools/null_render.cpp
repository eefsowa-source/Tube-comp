/*
    Deterministic offline render used to compare two builds sample by sample.

    The signal deliberately sweeps input level from -30 to 0 dBFS, adds bursts
    and impulses, and ends with a hashed noise tail, so a change inside the
    nonlinear solver is exercised across its whole operating range. The
    generator is stateless integer hashing, so it is identical on every build
    and platform.

    Usage:
        null_render <output.raw>          render and write interleaved float32
        null_render --compare <a> <b>     compare two raw files
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../Source/PluginProcessor.h"

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr double silenceSeconds = 0.1;
constexpr double signalSeconds = 2.4;
constexpr int silenceSamples = static_cast<int> (sampleRate * silenceSeconds);
constexpr int totalSamples = static_cast<int> (sampleRate * (silenceSeconds + signalSeconds));

void setParam (TubeCompAudioProcessor& processor, const juce::String& parameterId, float value)
{
    auto* parameter = processor.apvts.getParameter (parameterId);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

/** Fixed, representative operating point: WDF path at 4x, drive 12 dB. */
void configure (TubeCompAudioProcessor& processor)
{
    setParam (processor, ParamIDs::inputGain, 0.0f);
    setParam (processor, ParamIDs::outputGain, 0.0f);
    setParam (processor, ParamIDs::drive, 12.0f);
    setParam (processor, ParamIDs::harmonics, 0.5f);
    setParam (processor, ParamIDs::brightness, 0.5f);
    setParam (processor, ParamIDs::oversample, 2.0f);
    setParam (processor, ParamIDs::mix, 1.0f);
    setParam (processor, ParamIDs::circuitModel, 1.0f);
    setParam (processor, ParamIDs::threshold, 0.0f);
    setParam (processor, ParamIDs::ratio, 1.0f);
    setParam (processor, ParamIDs::attack, 10.0f);
    setParam (processor, ParamIDs::release, 100.0f);
    setParam (processor, ParamIDs::knee, 0.0f);
    setParam (processor, ParamIDs::bypass, 0.0f);
    setParam (processor, ParamIDs::antiAlias, 0.0f);
    setParam (processor, ParamIDs::lookAhead, 0.1f);
    setParam (processor, ParamIDs::sidechainHPF, 20.0f);
    setParam (processor, ParamIDs::biasDrive, 0.5f);
}

/** Stateless integer hash mapped to (-1, 1). */
float noiseAt (int index) noexcept
{
    std::uint32_t x = static_cast<std::uint32_t> (index) * 2654435761u + 0x9E3779B9u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return static_cast<float> (static_cast<double> (x) / 2147483648.0 - 1.0);
}

float signalAt (int sample) noexcept
{
    if (sample < silenceSamples)
        return 0.0f;

    const int i = sample - silenceSamples;
    const int segmentLength = static_cast<int> (sampleRate * 0.4);
    const int segment = juce::jmin (5, i / segmentLength);
    const int localIndex = i - segment * segmentLength;
    const double t = static_cast<double> (localIndex) / sampleRate;

    const auto sine = [] (double frequency, double amplitudeDb, double time)
    {
        return static_cast<float> (juce::Decibels::decibelsToGain (amplitudeDb)
                                   * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * time));
    };

    switch (segment)
    {
        case 0:  return sine (1000.0, -30.0, t);
        case 1:  return sine (1000.0, -18.0, t);
        case 2:  return sine (1000.0, -6.0, t);
        case 3:  return sine (1000.0, 0.0, t);
        case 4:
        {
            float value = sine (3000.0, -3.0, t);
            // Impulses every 0.1 s force the solver to restart from a cold state.
            if (localIndex % static_cast<int> (sampleRate * 0.1) == 0)
                value += 0.9f;
            return value;
        }
        default: return 0.3f * noiseAt (i);
    }
}

std::vector<float> render (double& prepareSeconds)
{
    TubeCompAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    configure (processor);

    const auto prepareStart = juce::Time::getHighResolutionTicks();
    processor.prepareToPlay (sampleRate, blockSize);
    prepareSeconds = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - prepareStart);

    std::vector<float> output (static_cast<size_t> (totalSamples) * 2, 0.0f);

    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int count = juce::jmin (blockSize, totalSamples - start);
        block.setSize (2, count, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = block.getWritePointer (ch);
            for (int i = 0; i < count; ++i)
                data[i] = signalAt (start + i);
        }

        processor.processBlock (block, midi);

        for (int i = 0; i < count; ++i)
        {
            output[static_cast<size_t> ((start + i) * 2)] = block.getSample (0, i);
            output[static_cast<size_t> ((start + i) * 2 + 1)] = block.getSample (1, i);
        }
    }

    return output;
}

bool writeRaw (const juce::File& file, const std::vector<float>& samples)
{
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    stream->write (samples.data(), samples.size() * sizeof (float));
    stream->flush();
    return true;
}

std::vector<float> readRaw (const juce::File& file, bool& ok)
{
    std::vector<float> samples;
    ok = false;

    if (! file.existsAsFile())
        return samples;

    juce::FileInputStream stream (file);
    if (! stream.openedOk())
        return samples;

    const auto size = stream.getTotalLength();
    samples.resize (static_cast<size_t> (size / static_cast<juce::int64> (sizeof (float))));
    stream.read (samples.data(), static_cast<int> (size));
    ok = true;
    return samples;
}

int compare (const juce::File& a, const juce::File& b)
{
    bool okA = false, okB = false;
    const auto dataA = readRaw (a, okA);
    const auto dataB = readRaw (b, okB);

    if (! okA || ! okB)
    {
        std::printf ("cannot read both files\n");
        return 1;
    }

    if (dataA.size() != dataB.size())
    {
        std::printf ("size mismatch: %zu vs %zu samples\n", dataA.size(), dataB.size());
        return 1;
    }

    double sumSquaresA = 0.0, sumSquaresB = 0.0, sumSquaresDiff = 0.0;
    double maxAbsA = 0.0, maxAbsB = 0.0, maxAbsDiff = 0.0;
    size_t maxDiffIndex = 0;

    for (size_t i = 0; i < dataA.size(); ++i)
    {
        const double x = dataA[i];
        const double y = dataB[i];
        const double d = std::abs (x - y);

        sumSquaresA += x * x;
        sumSquaresB += y * y;
        sumSquaresDiff += d * d;
        maxAbsA = juce::jmax (maxAbsA, std::abs (x));
        maxAbsB = juce::jmax (maxAbsB, std::abs (y));

        if (d > maxAbsDiff)
        {
            maxAbsDiff = d;
            maxDiffIndex = i;
        }
    }

    const double rmsA = std::sqrt (sumSquaresA / static_cast<double> (dataA.size()));
    const double rmsB = std::sqrt (sumSquaresB / static_cast<double> (dataA.size()));
    const double rmsDiff = std::sqrt (sumSquaresDiff / static_cast<double> (dataA.size()));
    const auto toDb = [] (double value) { return 20.0 * std::log10 (juce::jmax (1.0e-30, value)); };

    std::printf ("samples            %zu\n", dataA.size());
    std::printf ("peak |a| / |b|     %.9f / %.9f  (%.2f dBFS)\n", maxAbsA, maxAbsB, toDb (maxAbsA));
    std::printf ("rms  a / b         %.9f / %.9f\n", rmsA, rmsB);
    std::printf ("max |a-b|          %.3e  (%.1f dBFS, sample %zu, ch %d)\n",
                 maxAbsDiff, toDb (maxAbsDiff), maxDiffIndex / 2,
                 static_cast<int> (maxDiffIndex % 2));
    std::printf ("rms |a-b|          %.3e  (%.1f dBFS, %.1f dB below signal)\n",
                 rmsDiff, toDb (rmsDiff), toDb (rmsA) - toDb (rmsDiff));

    // Windowed differences: the lead-in silence is where a start-up change shows
    // up, and the audio region is what a listener would actually hear.
    const auto windowDiff = [&dataA, &dataB] (size_t from, size_t to, double& peak, double& rms)
    {
        double sumSquares = 0.0;
        size_t count = 0;
        peak = 0.0;
        to = juce::jmin (to, dataA.size());

        for (size_t i = from; i < to; ++i)
        {
            const double d = std::abs (static_cast<double> (dataA[i]) - dataB[i]);
            peak = juce::jmax (peak, d);
            sumSquares += d * d;
            ++count;
        }

        rms = count > 0 ? std::sqrt (sumSquares / static_cast<double> (count)) : 0.0;
    };

    const size_t leadInEnd = static_cast<size_t> (silenceSamples) * 2;
    double leadPeak = 0.0, leadRms = 0.0, audioPeak = 0.0, audioRms = 0.0;
    windowDiff (0, leadInEnd, leadPeak, leadRms);
    windowDiff (leadInEnd, dataA.size(), audioPeak, audioRms);

    // If the residual comes from an initial-state perturbation it decays; a
    // systematic change would keep the same level at the end of the file.
    const size_t tailLength = static_cast<size_t> (sampleRate * 0.5) * 2;
    double tailPeak = 0.0, tailRms = 0.0;
    windowDiff (dataA.size() > tailLength ? dataA.size() - tailLength : 0, dataA.size(),
                tailPeak, tailRms);

    std::printf ("lead-in silence    %.3e peak (%.1f dBFS), %.3e rms\n",
                 leadPeak, toDb (leadPeak), leadRms);
    std::printf ("audio region       %.3e peak (%.1f dBFS), %.3e rms (%.1f dB below signal)\n",
                 audioPeak, toDb (audioPeak), audioRms, toDb (rmsA) - toDb (audioRms));
    std::printf ("final 0.5 s        %.3e peak (%.1f dBFS), %.3e rms\n",
                 tailPeak, toDb (tailPeak), tailRms);
    return 0;
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc >= 2 && std::strcmp (argv[1], "--compare") == 0)
    {
        if (argc < 4)
        {
            std::printf ("usage: null_render --compare <a.raw> <b.raw>\n");
            return 1;
        }

        return compare (juce::File (juce::String (argv[2])), juce::File (juce::String (argv[3])));
    }

    if (argc < 2)
    {
        std::printf ("usage: null_render <output.raw> | null_render --compare <a.raw> <b.raw>\n");
        return 1;
    }

    double prepareSeconds = 0.0;
    const auto samples = render (prepareSeconds);
    const juce::File outputFile { juce::String (argv[1]) };

    if (! writeRaw (outputFile, samples))
    {
        std::printf ("failed to write %s\n", outputFile.getFullPathName().toRawUTF8());
        return 1;
    }

    double sumSquares = 0.0, peak = 0.0;
    for (auto value : samples)
    {
        sumSquares += static_cast<double> (value) * value;
        peak = juce::jmax (peak, std::abs (static_cast<double> (value)));
    }

    std::printf ("wrote %s: %zu samples, peak %.6f, rms %.6f\n",
                 outputFile.getFullPathName().toRawUTF8(), samples.size(), peak,
                 std::sqrt (sumSquares / static_cast<double> (samples.size())));
    std::printf ("prepareToPlay    %.1f ms\n", prepareSeconds * 1000.0);
    return 0;
}
