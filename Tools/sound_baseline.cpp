/*
    Offline sound baseline for EON-Vari mu DSP.

    Measures the real plugin chain (the harness links the plugin's own shared
    code, it does not re-implement it) and prints three tables:

    1. Tone table: THD, 2nd/3rd harmonic level, RMS and crest factor for a probe
       whose period divides the sample rate exactly, so a plain rectangular DFT
       has no leakage.
    2. Alias table: a probe deliberately not commensurate with the sample rate.
       A Hann-windowed FFT is split into harmonic neighbourhoods and everything
       else, and that "everything else" is reported as alias/noise in dBc. With a
       commensurate probe an alias always folds onto another harmonic bin, so this
       off-grid probe is what makes aliasing measurable on its own.
    3. Processing cost as a percentage of real time, plus the compressor's static
       curve.

    The chain is set to unity gain, unity ratio and no look-ahead so the tone
    tables show the saturation stage rather than the compressor.
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../Source/PluginProcessor.h"

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr double totalSeconds = 2.0;
constexpr double analysisSeconds = 1.0;

constexpr int fftOrder = 15;
constexpr int fftSize = 1 << fftOrder;         // 32768 samples = 1.4648 Hz per bin
constexpr double harmonicHalfWidthHz = 4.5;    // covers the Hann main lobe
constexpr double bandLowHz = 20.0;             // below the DC blocker corner
constexpr double bandHighFraction = 0.45;      // of the sample rate

struct Config
{
    float oversample;    // 0..3 -> 1x, 2x, 4x, 8x
    float circuitModel;  // 0 = Fast waveshaper, 1 = WDF triode
    float adaa;          // 0 = oversampling only, 1 = ADAA inside the saturator
    std::string label;
};

struct ToneMetrics
{
    double thdPercent = 0.0;
    double h2Db = 0.0;
    double h3Db = 0.0;
    double rmsDb = 0.0;
    double crestDb = 0.0;
};

void setParam (TubeCompAudioProcessor& processor, const juce::String& parameterId, float value)
{
    auto* parameter = processor.apvts.getParameter (parameterId);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

/** Unity gain, unity ratio, no look-ahead: the tone path with the compressor transparent. */
void configureNeutralChain (TubeCompAudioProcessor& processor)
{
    setParam (processor, ParamIDs::inputGain, 0.0f);
    setParam (processor, ParamIDs::outputGain, 0.0f);
    setParam (processor, ParamIDs::mix, 1.0f);
    setParam (processor, ParamIDs::bypass, 0.0f);
    setParam (processor, ParamIDs::threshold, 0.0f);
    setParam (processor, ParamIDs::ratio, 1.0f);
    setParam (processor, ParamIDs::knee, 0.0f);
    setParam (processor, ParamIDs::attack, 10.0f);
    setParam (processor, ParamIDs::release, 100.0f);
    setParam (processor, ParamIDs::lookAhead, 0.1f);
    setParam (processor, ParamIDs::sidechainHPF, 20.0f);
    setParam (processor, ParamIDs::drive, 12.0f);
    setParam (processor, ParamIDs::harmonics, 0.5f);
    setParam (processor, ParamIDs::brightness, 0.5f);
    setParam (processor, ParamIDs::biasDrive, 0.5f);
    setParam (processor, ParamIDs::transformer, 0.0f);
}

TubeCompAudioProcessor& prepare (TubeCompAudioProcessor& processor, const Config& config)
{
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    configureNeutralChain (processor);
    setParam (processor, ParamIDs::oversample, config.oversample);
    setParam (processor, ParamIDs::circuitModel, config.circuitModel);
    setParam (processor, ParamIDs::antiAlias, config.adaa);
    processor.prepareToPlay (sampleRate, blockSize);
    return processor;
}

/** Amplitude of one DFT bin, expressed as a peak amplitude. */
double binMagnitude (const float* data, int count, double frequency)
{
    const double omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
    double real = 0.0, imaginary = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const double phase = omega * static_cast<double> (i);
        real += static_cast<double> (data[i]) * std::cos (phase);
        imaginary += static_cast<double> (data[i]) * std::sin (phase);
    }

    return 2.0 * std::hypot (real, imaginary) / static_cast<double> (count);
}

/** Renders a steady sine through the plugin; returns the output of channel 0. */
std::vector<float> renderTone (TubeCompAudioProcessor& processor, double frequency, double amplitude)
{
    const int totalSamples = static_cast<int> (sampleRate * totalSeconds);
    std::vector<float> output (static_cast<size_t> (totalSamples), 0.0f);

    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;
    int written = 0;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int count = juce::jmin (blockSize, totalSamples - start);
        block.setSize (2, count, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = block.getWritePointer (ch);
            for (int i = 0; i < count; ++i)
            {
                const double t = static_cast<double> (start + i) / sampleRate;
                data[i] = static_cast<float> (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * t));
            }
        }

        processor.processBlock (block, midi);

        for (int i = 0; i < count; ++i)
            output[static_cast<size_t> (written + i)] = block.getSample (0, i);

        written += count;
    }

    return output;
}

ToneMetrics analyseTone (const std::vector<float>& output, double frequency)
{
    const int analysisCount = static_cast<int> (sampleRate * analysisSeconds);
    const auto* data = output.data() + (static_cast<int> (output.size()) - analysisCount);

    std::vector<double> harmonic (9, 0.0);
    for (int k = 1; k <= 8; ++k)
        harmonic[static_cast<size_t> (k)] = binMagnitude (data, analysisCount, frequency * k);

    const double fundamental = juce::jmax (1.0e-12, harmonic[1]);

    double harmonicPower = 0.0;
    for (int k = 2; k <= 8; ++k)
        harmonicPower += harmonic[static_cast<size_t> (k)] * harmonic[static_cast<size_t> (k)];

    double sumSquares = 0.0;
    double peak = 0.0;
    for (int i = 0; i < analysisCount; ++i)
    {
        const double x = static_cast<double> (data[i]);
        sumSquares += x * x;
        peak = juce::jmax (peak, std::abs (x));
    }

    const double rms = std::sqrt (sumSquares / analysisCount);
    const auto toDb = [] (double value) { return 20.0 * std::log10 (juce::jmax (1.0e-12, value)); };

    ToneMetrics metrics;
    metrics.thdPercent = 100.0 * std::sqrt (harmonicPower) / fundamental;
    metrics.h2Db = toDb (harmonic[2] / fundamental);
    metrics.h3Db = toDb (harmonic[3] / fundamental);
    metrics.rmsDb = toDb (rms);
    metrics.crestDb = toDb (peak / juce::jmax (1.0e-12, rms));
    return metrics;
}

/** Splits the spectrum into harmonic neighbourhoods and everything else. */
double measureAliasDbc (const std::vector<float>& output, double frequency)
{
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> fftData (static_cast<size_t> (2 * fftSize), 0.0f);

    const int start = static_cast<int> (output.size()) - fftSize;

    for (int i = 0; i < fftSize; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                    * static_cast<double> (i) / (fftSize - 1));
        fftData[static_cast<size_t> (i)] = static_cast<float> (output[static_cast<size_t> (start + i)] * window);
    }

    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const double binHz = sampleRate / fftSize;
    const double highestHz = sampleRate * bandHighFraction;

    double totalPower = 0.0;
    double harmonicPower = 0.0;
    double fundamentalPower = 0.0;

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const double binFrequency = bin * binHz;
        if (binFrequency < bandLowHz || binFrequency > highestHz)
            continue;

        const double magnitude = static_cast<double> (fftData[static_cast<size_t> (bin)]);
        const double power = magnitude * magnitude;
        totalPower += power;

        // A bin belongs to a harmonic when it sits inside that harmonic's main lobe.
        bool isHarmonic = false;
        for (int k = 1; k <= 40 && ! isHarmonic; ++k)
        {
            const double centre = frequency * k;
            if (centre > highestHz)
                break;

            if (std::abs (binFrequency - centre) <= harmonicHalfWidthHz)
            {
                isHarmonic = true;
                harmonicPower += power;
                if (k == 1)
                    fundamentalPower += power;
            }
        }
    }

    const double aliasPower = juce::jmax (0.0, totalPower - harmonicPower);
    const double fundamental = juce::jmax (1.0e-30, fundamentalPower);
    return 10.0 * std::log10 (juce::jmax (1.0e-30, aliasPower) / fundamental);
}

void printToneTable (double frequency, double levelDb, const std::vector<Config>& configs)
{
    std::printf ("--- tone probe %.0f Hz at %.1f dBFS (THD includes harmonics 2..8) ---\n",
                 frequency, levelDb);
    std::printf ("%-14s %8s %8s %8s %10s %10s\n",
                 "config", "THD[%]", "H2[dBc]", "H3[dBc]", "rms[dB]", "crest[dB]");

    for (const auto& config : configs)
    {
        TubeCompAudioProcessor processor;
        prepare (processor, config);
        const auto metrics = analyseTone (renderTone (processor, frequency,
                                                      juce::Decibels::decibelsToGain (levelDb)),
                                          frequency);
        std::printf ("%-14s %8.3f %8.1f %8.1f %10.2f %10.2f\n",
                     config.label.c_str(), metrics.thdPercent, metrics.h2Db, metrics.h3Db,
                     metrics.rmsDb, metrics.crestDb);
    }

    std::printf ("\n");
}

void printAliasTable (double frequency, double levelDb, const std::vector<Config>& configs)
{
    std::printf ("--- alias probe %.0f Hz at %.1f dBFS (non-harmonic bins, dBc) ---\n",
                 frequency, levelDb);
    std::printf ("%-14s %12s\n", "config", "alias[dBc]");

    for (const auto& config : configs)
    {
        TubeCompAudioProcessor processor;
        prepare (processor, config);
        const auto output = renderTone (processor, frequency, juce::Decibels::decibelsToGain (levelDb));
        std::printf ("%-14s %12.1f\n", config.label.c_str(), measureAliasDbc (output, frequency));
    }

    std::printf ("\n");
}

double measureCostPercent (TubeCompAudioProcessor& processor);

double measureCostPercent (const Config& config)
{
    TubeCompAudioProcessor processor;
    prepare (processor, config);

    return measureCostPercent (processor);
}

double measureCostPercent (TubeCompAudioProcessor& processor)
{
    const double audioSeconds = 10.0;
    const int totalSamples = static_cast<int> (sampleRate * audioSeconds);

    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = block.getWritePointer (ch);
        for (int i = 0; i < blockSize; ++i)
            data[i] = static_cast<float> (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sampleRate));
    }

    const auto startTicks = juce::Time::getHighResolutionTicks();

    for (int start = 0; start < totalSamples; start += blockSize)
        processor.processBlock (block, midi);

    const auto elapsed = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - startTicks);

    return 100.0 * elapsed / audioSeconds;
}

void printCostTable (const std::vector<Config>& configs)
{
    std::printf ("--- processing cost (percent of real time, 48 kHz, 512 sample blocks) ---\n");
    for (const auto& config : configs)
        std::printf ("%-14s %6.2f%%\n", config.label.c_str(), measureCostPercent (config));
    std::printf ("\n");
}

/** Output RMS in dB for one model at a given drive and input level. */
double modelToneRmsDb (float circuitModel, float driveDb, double levelDb)
{
    TubeCompAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    configureNeutralChain (processor);
    setParam (processor, ParamIDs::oversample, 2.0f);
    setParam (processor, ParamIDs::circuitModel, circuitModel);
    setParam (processor, ParamIDs::drive, driveDb);
    processor.prepareToPlay (sampleRate, blockSize);

    return analyseTone (renderTone (processor, 1000.0, juce::Decibels::decibelsToGain (levelDb)), 1000.0).rmsDb;
}

/** Level difference between the two circuit models, which a trim has to cover. */
void printModelMatch()
{
    std::printf ("--- model level match (1 kHz, 4x, harmonics 0.5, brightness 0.5, bias 0.5) ---\n");
    std::printf ("%10s %10s %10s %10s %10s\n", "drive[dB]", "in[dBFS]", "fast[dB]", "wdf[dB]", "wdf-fast");

    double total = 0.0;
    int count = 0;

    for (float driveDb : { 0.0f, 6.0f, 12.0f, 18.0f })
    {
        for (double levelDb : { -24.0, -18.0, -12.0, -6.0, 0.0 })
        {
            const double fast = modelToneRmsDb (0.0f, driveDb, levelDb);
            const double wdf = modelToneRmsDb (1.0f, driveDb, levelDb);
            std::printf ("%10.1f %10.1f %10.2f %10.2f %10.2f\n",
                         static_cast<double> (driveDb), levelDb, fast, wdf, wdf - fast);
            total += wdf - fast;
            ++count;
        }
    }

    std::printf ("mean difference %.2f dB over %d points\n\n", total / count, count);
}

/** Static curve: steady 1 kHz tone at several levels with threshold -18, ratio 4.
    Both detector topologies are printed because the feedback loop is intended
    to change the program-dependent compression curve, not merely the label. */
void printCompressorCurve()
{
    std::printf ("--- compressor static curve (threshold -18 dB, ratio 4, knee 6, 1 kHz) ---\n");
    std::printf ("%10s %10s %12s %10s %12s\n",
                 "in[dBFS]", "FF GR[dB]", "FF out[dB]", "FB GR[dB]", "FB out[dB]");

    for (double levelDb : { -36.0, -30.0, -24.0, -18.0, -12.0, -6.0, -3.0, 0.0 })
    {
        std::array<double, 4> values {};
        for (int topology = 0; topology < 2; ++topology)
        {
            TubeCompAudioProcessor processor;
            processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
            configureNeutralChain (processor);
            setParam (processor, ParamIDs::threshold, -18.0f);
            setParam (processor, ParamIDs::ratio, 4.0f);
            setParam (processor, ParamIDs::knee, 6.0f);
            setParam (processor, ParamIDs::drive, 0.0f);
            setParam (processor, ParamIDs::topology, static_cast<float> (topology));
            processor.prepareToPlay (sampleRate, blockSize);

            const auto output = renderTone (processor, 1000.0,
                                             juce::Decibels::decibelsToGain (levelDb));
            const auto metrics = analyseTone (output, 1000.0);
            const auto offset = static_cast<size_t> (topology * 2);
            values[offset] = processor.getGainReductionDb();
            values[offset + 1] = metrics.rmsDb;
        }

        std::printf ("%10.1f %10.2f %12.2f %10.2f %12.2f\n", levelDb,
                     values[0], values[1], values[2], values[3]);
    }

    std::printf ("\n");
}
}

/**
   S3 transformer gate. The linear iron model is only worth keeping if it moves
   the spectrum in a measurable, non-destructive direction, so this prints the
   On-minus-Off delta at three probe bands plus the added harmonics, and the CPU
   it costs at the quality settings that matter.
*/
static void printTransformerDelta()
{
    std::printf ("--- S3 transformer delta (On minus Off, wdf-4x, drive 12) ---\n");
    std::printf ("   %-7s %17s %17s %17s\n",
                 "band", "rms[off/on/delta]", "H3[off/on/delta]", "alias[off/on/delta]");

    struct Band { double freq; const char* name; };
    const Band bands[] = { { 40.0, "40Hz" }, { 1000.0, "1kHz" }, { 15000.0, "15kHz" } };

    for (const auto& band : bands)
    {
        std::array<double, 2> rms {}, h3 {}, alias {};
        for (int on = 0; on < 2; ++on)
        {
            TubeCompAudioProcessor processor;
            processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
            configureNeutralChain (processor);
            setParam (processor, ParamIDs::oversample, 2.0f);
            setParam (processor, ParamIDs::circuitModel, 1.0f);
            setParam (processor, ParamIDs::drive, 12.0f);
            setParam (processor, ParamIDs::transformer, static_cast<float> (on));
            processor.prepareToPlay (sampleRate, blockSize);

            const auto output = renderTone (processor, band.freq, juce::Decibels::decibelsToGain (-12.0));
            rms[static_cast<size_t> (on)] = analyseTone (output, band.freq).rmsDb;
            h3[static_cast<size_t> (on)] = analyseTone (output, band.freq).h3Db;
            alias[static_cast<size_t> (on)] = measureAliasDbc (output, band.freq);
        }

        char rmsText[64], h3Text[64], aliasText[64];
        std::snprintf (rmsText, sizeof rmsText, "%6.2f/%6.2f/%+6.2f", rms[0], rms[1], rms[1] - rms[0]);
        std::snprintf (h3Text, sizeof h3Text, "%6.1f/%6.1f/%+6.1f", h3[0], h3[1], h3[1] - h3[0]);
        std::snprintf (aliasText, sizeof aliasText, "%6.1f/%6.1f/%+6.1f",
                       alias[0], alias[1], alias[1] - alias[0]);
        std::printf ("   %-7s %17s %17s %17s\n", band.name, rmsText, h3Text, aliasText);
    }

    std::printf ("\n--- S3 transformer cost (percent of real time) ---\n");
    std::printf ("   %-10s %8s %8s %8s\n", "quality", "off", "on", "delta");

    for (int factor : { 0, 1, 2, 3 })
    {
        std::array<double, 2> cost {};
        for (int on = 0; on < 2; ++on)
        {
            TubeCompAudioProcessor processor;
            processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
            configureNeutralChain (processor);
            setParam (processor, ParamIDs::oversample, static_cast<float> (factor));
            setParam (processor, ParamIDs::circuitModel, 1.0f);
            setParam (processor, ParamIDs::drive, 12.0f);
            setParam (processor, ParamIDs::transformer, static_cast<float> (on));
            processor.prepareToPlay (sampleRate, blockSize);
            cost[static_cast<size_t> (on)] = measureCostPercent (processor);
        }

        // The parameter is a zero-based choice index, and the plugin doubles
        // the rate per step, so choice 1 is 2x -- not 1x.
        const char* names[] = { "1x", "2x", "4x", "8x" };
        std::printf ("   %-10s %8.2f %8.2f %8.2f\n",
                     names[factor], cost[0], cost[1], cost[1] - cost[0]);
    }

    std::printf ("\n");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const char* oversamplingNames[] = { "1x", "2x", "4x", "8x" };

    std::vector<Config> configs;
    for (int oversampling = 0; oversampling < 4; ++oversampling)
    {
        for (int adaa = 0; adaa < 2; ++adaa)
            configs.push_back ({ static_cast<float> (oversampling), 0.0f, static_cast<float> (adaa),
                                 std::string ("fast-") + oversamplingNames[oversampling]
                                     + (adaa == 1 ? "-adaa" : "") });

        configs.push_back ({ static_cast<float> (oversampling), 1.0f, 0.0f,
                             std::string ("wdf-") + oversamplingNames[oversampling] });
    }

    const auto startTicks = juce::Time::getHighResolutionTicks();

    printToneTable (1000.0, -12.0, configs);
    printToneTable (1000.0, 0.0, configs);
    printToneTable (6000.0, -12.0, configs);
    printAliasTable (7001.0, -12.0, configs);
    printAliasTable (7001.0, -3.0, configs);
    printCostTable (configs);
    printModelMatch();
    printCompressorCurve();
    printTransformerDelta();

    const auto elapsed = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - startTicks);
    std::printf ("baseline run wall time: %.1f s\n", elapsed);
    return 0;
}
