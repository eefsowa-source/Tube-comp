#include "Compressor.h"

void Compressor::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channelCount = numChannels;
    setAttackMs (10.0f);
    setReleaseMs (100.0f);

    // Classic VU ballistics: ~300ms integration time.
    meterReleaseCoeff = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.3)));

    // Look-ahead delay default 5ms, sidechain HPF default 100Hz.
    setLookAheadMs (5.0f);
    setSidechainHPFHz (100.0f);

    reset();
}

void Compressor::reset()
{
    currentGainDb = 0.0f;
    currentGainDbAtomic.store (0.0f, std::memory_order_relaxed);
    meterEnvelopeLinear.fill (0.0f);
    for (auto& v : meterLevelDb)
        v.store (-100.0f, std::memory_order_relaxed);
    for (auto& buf : delayBuffer)
        std::fill (buf.begin(), buf.end(), 0.0f);
    delayWritePos.fill (0);
    sidechainHPFState.fill ({ { 0.0f, 0.0f } });
}

void Compressor::setLookAheadMs (float ms) noexcept
{
    const auto newLookAheadSamples = static_cast<size_t> (
        std::llround (juce::jmax (0.0, sampleRate * static_cast<double> (ms) * 0.001)));
    if (newLookAheadSamples == lookAheadSamples)
        return;
    lookAheadSamples = newLookAheadSamples;
    for (auto& buf : delayBuffer)
        buf.resize (lookAheadSamples, 0.0f);
}

void Compressor::setSidechainHPFHz (float hz) noexcept
{
    // One-pole high-pass: y[n] = x[n] - x[n-1] + pole * y[n-1].
    // Filtering each signed channel before rectification preserves the intended
    // frequency response; filtering a linked absolute-peak envelope does not.
    const auto safeHz = juce::jlimit (5.0, sampleRate * 0.45, static_cast<double> (hz));
    sidechainHPFCoeff = static_cast<float> (
        std::exp (-2.0 * juce::MathConstants<double>::pi * safeHz / sampleRate));
}

void Compressor::setAttackMs (float ms) noexcept
{
    // Parameter is expressed in milliseconds; coefficient time is seconds.
    const double t = juce::jmax (0.00005, static_cast<double> (ms) * 0.001);
    attackCoeff = static_cast<float> (std::exp (-1.0 / (sampleRate * t)));
}

void Compressor::setReleaseMs (float ms) noexcept
{
    const double t = juce::jmax (0.0005, static_cast<double> (ms) * 0.001);
    releaseCoeff = static_cast<float> (std::exp (-1.0 / (sampleRate * t)));
}

float Compressor::computeTargetGainReductionDb (float levelDb) const noexcept
{
    const float overshoot = levelDb - thresholdDb;

    if (kneeDb <= 1.0e-6f)
    {
        if (overshoot <= 0.0f)
            return 0.0f;
        return overshoot * (1.0f - 1.0f / ratio);
    }

    const float halfKnee = kneeDb * 0.5f;

    if (overshoot <= -halfKnee)
        return 0.0f;

    if (overshoot >= halfKnee)
        return overshoot * (1.0f - 1.0f / ratio);

    // Soft-knee quadratic blend region.
    const float delta = overshoot + halfKnee;
    return (1.0f - 1.0f / ratio) * (delta * delta) / (2.0f * kneeDb);
}

void Compressor::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), channelCount);

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;

        // Detect the current sample and apply the resulting gain to the sample
        // that is leaving the delay line. This makes look-ahead a real latency.
        std::array<float, 2> delayedSamples { 0.0f, 0.0f };
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float sample = buffer.getSample (ch, i);

            // Read before overwriting: a buffer of N samples then produces an
            // exact N-sample delay (the old write position is the oldest item).
            if (lookAheadSamples > 0 && static_cast<size_t> (ch) < delayBuffer.size())
            {
                const auto channelIndex = static_cast<size_t> (ch);
                delayedSamples[channelIndex] = delayBuffer[channelIndex][delayWritePos[channelIndex]];
                delayBuffer[channelIndex][delayWritePos[channelIndex]] = sample;
                delayWritePos[channelIndex] = (delayWritePos[channelIndex] + 1) % lookAheadSamples;
            }
            else
            {
                delayedSamples[static_cast<size_t> (ch)] = sample;
            }

            // Sidechain HPF runs on signed audio per channel, then the filtered
            // magnitudes are peak-linked. This keeps stereo linking while
            // preventing bass energy from dominating the detector.
            auto& hpf = sidechainHPFState[static_cast<size_t> (ch)];
            const float filtered = sample - hpf[0] + sidechainHPFCoeff * hpf[1];
            hpf[0] = sample;
            hpf[1] = filtered;
            peak = juce::jmax (peak, std::abs (filtered));
        }

        const float levelDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        const float targetGainDb = computeTargetGainReductionDb (levelDb);

        // Attack when gain reduction needs to increase (signal got louder),
        // release when it needs to decrease (signal got quieter).
        const float coeff = (targetGainDb > currentGainDb) ? attackCoeff : releaseCoeff;
        currentGainDb = targetGainDb + coeff * (currentGainDb - targetGainDb);
        currentGainDbAtomic.store (currentGainDb, std::memory_order_relaxed);

        const float linearGain = juce::Decibels::decibelsToGain (-currentGainDb);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, delayedSamples[static_cast<size_t> (ch)] * linearGain);
    }

    // VU-style output metering, per channel, instant attack / slow release.
    for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
    {
        float env = meterEnvelopeLinear[static_cast<size_t> (ch)];
        auto* data = buffer.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float absSample = std::abs (data[i]);
            env = (absSample > env) ? absSample : (absSample + meterReleaseCoeff * (env - absSample));
        }

        meterEnvelopeLinear[static_cast<size_t> (ch)] = env;
        meterLevelDb[static_cast<size_t> (ch)].store (juce::Decibels::gainToDecibels (env, -100.0f),
                                                        std::memory_order_relaxed);
    }
}
