#include "Compressor.h"

#include <array>

namespace
{
constexpr std::array<float, 6> fairchildAttackMs { 0.2f, 0.2f, 0.4f, 0.4f, 0.4f, 0.2f };
constexpr std::array<float, 6> fairchildReleaseMs { 300.0f, 800.0f, 2000.0f, 5000.0f, 2000.0f, 800.0f };
constexpr float inverseSqrtTwo = 0.7071067811865475f;

std::array<float, 2> toControlDomain (float left, float right, int linkMode) noexcept
{
    if (linkMode == 2)
        return { (left + right) * inverseSqrtTwo, (left - right) * inverseSqrtTwo };

    return { left, right };
}
}

void Compressor::prepare (double newSampleRate, int numChannels, int maxBlockSize)
{
    sampleRate = newSampleRate;
    channelCount = numChannels;
    gainReductionTrace.assign (static_cast<size_t> (juce::jmax (1, maxBlockSize)), 0.0f);
    gainReductionTraceLength = 0;

    // Preallocate the longest possible look-ahead once, here on an allocation-
    // safe thread. setLookAheadMs() runs from processBlock() and must never
    // resize (i.e. allocate) the delay buffers mid-stream.
    const auto maxSamples = static_cast<size_t> (
        std::llround (sampleRate * 0.001 * static_cast<double> (maxLookAheadMs))) + 1;
    for (auto& buf : delayBuffer)
        buf.assign (maxSamples, 0.0f);

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
    currentGainDb.fill (0.0f);
    currentGainDbAtomic.store (0.0f, std::memory_order_relaxed);
    for (auto& gain : channelGainDbAtomic)
        gain.store (0.0f, std::memory_order_relaxed);
    blockGainDb.fill (0.0f);
    for (auto& gain : blockGainDbAtomic)
        gain.store (0.0f, std::memory_order_relaxed);
    automaticReleaseState = 0.0f;
    meterEnvelopeLinear.fill (0.0f);
    for (auto& v : meterLevelDb)
        v.store (-100.0f, std::memory_order_relaxed);
    for (auto& buf : delayBuffer)
        std::fill (buf.begin(), buf.end(), 0.0f);
    delayWritePos.fill (0);
    feedbackOutput.fill (0.0f);
    sidechainHPFState.fill ({ { 0.0f, 0.0f } });
}

void Compressor::setFeedbackMode (bool enabled) noexcept
{
    if (feedbackMode == enabled)
        return;

    feedbackMode = enabled;
    // The detector changes signal domain when the topology is switched. Clear
    // only detector history so a mode change cannot inject a stale raw/output
    // sample into the HPF; the gain-reduction ballistics remain continuous.
    feedbackOutput.fill (0.0f);
    sidechainHPFState.fill ({ { 0.0f, 0.0f } });
}

void Compressor::setLookAheadMs (float ms) noexcept
{
    if (delayBuffer.empty() || delayBuffer[0].empty())
    {
        lookAheadSamples = 0;
        return;
    }

    const auto maxSamples = delayBuffer[0].size();
    const auto requested = static_cast<size_t> (
        std::llround (juce::jmax (0.0, sampleRate * static_cast<double> (ms) * 0.001)));

    const auto clamped = juce::jmin (requested, maxSamples);
    if (clamped == lookAheadSamples)
        return;

    lookAheadSamples = clamped;
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

void Compressor::setTimeConstant (int choice) noexcept
{
    const int clampedChoice = juce::jlimit (0, 6, choice);
    if (timeConstantChoice == clampedChoice)
        return;

    timeConstantChoice = clampedChoice;
    automaticReleaseState = 0.0f;

    if (timeConstantChoice > 0)
    {
        const auto index = static_cast<size_t> (timeConstantChoice - 1);
        setAttackMs (fairchildAttackMs[index]);
        setReleaseMs (fairchildReleaseMs[index]);
    }
}

void Compressor::setLinkMode (int mode) noexcept
{
    const int clamped = juce::jlimit (0, 2, mode);
    if (linkMode == clamped)
        return;

    linkMode = clamped;
    feedbackOutput.fill (0.0f);
    sidechainHPFState.fill ({ { 0.0f, 0.0f } });
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

float Compressor::computeFeedbackTargetGainReductionDb (float outputLevelDb) const noexcept
{
    // The loop measures the output, out = in - GR. Inverting the feed-forward
    // curve GR_ff(in) in terms of `out` gives the target that makes the
    // steady state satisfy GR = GR_ff(in), so Ratio and Knee keep their
    // feed-forward meaning. (Feeding the output level into the feed-forward
    // computer instead gives an effective ratio of 2 - 1/R, never above 2:1.)
    const float slope = 1.0f - 1.0f / ratio;  // a in GR_ff = a * overshoot
    if (slope <= 0.0f)
        return 0.0f;

    const float overshoot = outputLevelDb - thresholdDb;

    if (kneeDb <= 1.0e-6f)
        return overshoot <= 0.0f ? 0.0f : overshoot * (ratio - 1.0f);

    // In the input domain the knee spans [T - W/2, T + W/2]; mapped through
    // out = in - GR it spans [T - W/2, T + W/(2R)] in the output domain.
    const float halfKnee = kneeDb * 0.5f;
    if (overshoot <= -halfKnee)
        return 0.0f;

    if (overshoot >= halfKnee / ratio)
        return overshoot * (ratio - 1.0f);

    // Knee region: with d = in - T + W/2 and e = out - T + W/2, the
    // feed-forward knee GR = a d^2 / (2W) and out = in - GR give
    // a d^2 / (2W) - d + e = 0. The smaller root is the physical one and
    // GR = d - e. The discriminant is >= (1 - a)^2 > 0 inside the knee.
    const float e = overshoot + halfKnee;
    const float discriminant = juce::jmax (0.0f, 1.0f - 2.0f * slope * e / kneeDb);
    const float d = (1.0f - std::sqrt (discriminant)) * kneeDb / slope;
    return juce::jmax (0.0f, d - e);
}

void Compressor::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), channelCount);
    const size_t activeLookAhead = getLookAheadSamples();

    // Stability of the one-sample feedback loop. Linearised in dB, the loop
    // gain of the ballistics is c - (1 - c) * (R - 1) for smoothing
    // coefficient c, because the feedback target moves by (R - 1) dB per dB of
    // output. Keeping c >= (R - 1) / R makes the loop monotone (no sample-rate
    // limit cycle) at every ratio. Only very fast attacks at high ratios are
    // affected: at R = 20 the shortest effective time constant is ~20 samples
    // (0.4 ms at 48 kHz); at R = 4 it is ~3.5 samples.
    const float feedbackCoeffFloor = feedbackMode ? (ratio - 1.0f) / ratio : 0.0f;

    blockGainDb.fill (0.0f);
    gainReductionTraceLength = juce::jmin (static_cast<size_t> (juce::jmax (0, numSamples)),
                                           gainReductionTrace.size());

    for (int i = 0; i < numSamples; ++i)
    {
        std::array<float, 2> inputSamples { 0.0f, 0.0f };

        // Detect the current sample and apply the resulting gain to the sample
        // that is leaving the delay line. This makes look-ahead a real latency.
        std::array<float, 2> delayedSamples { 0.0f, 0.0f };
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float sample = buffer.getSample (ch, i);
            inputSamples[static_cast<size_t> (ch)] = sample;

            // Keep the ring at its prepared maximum capacity even when the
            // active look-ahead is shorter (or zero). The parameter is
            // automatable; using lookAheadSamples as the modulo length would
            // discard history whenever the delay changed, so an impulse could
            // disappear when a host switched from 0 ms back to look-ahead.
            if (static_cast<size_t> (ch) < delayBuffer.size()
                && ! delayBuffer[static_cast<size_t> (ch)].empty())
            {
                const auto channelIndex = static_cast<size_t> (ch);
                const auto& ring = delayBuffer[channelIndex];
                const auto capacity = ring.size();

                if (activeLookAhead > 0)
                {
                    const auto readPos = (delayWritePos[channelIndex] + capacity - activeLookAhead) % capacity;
                    delayedSamples[channelIndex] = ring[readPos];
                }
                else
                {
                    delayedSamples[channelIndex] = sample;
                }

                delayBuffer[channelIndex][delayWritePos[channelIndex]] = sample;
                delayWritePos[channelIndex] = (delayWritePos[channelIndex] + 1) % capacity;
            }
            else
            {
                delayedSamples[static_cast<size_t> (ch)] = sample;
            }

        }

    // M/S (Lat/Ver) requires a stereo pair.  A mono instance falls back to
    // linked detection rather than accidentally running a one-channel matrix.
    const int effectiveLinkMode = (linkMode == 2 && numChannels >= 2) ? 2
                                                                       : (linkMode == 2 ? 1 : linkMode);
        const auto detectorSource = feedbackMode
            ? feedbackOutput
            : toControlDomain (inputSamples[0], inputSamples[1], effectiveLinkMode);
        std::array<float, 2> filtered { 0.0f, 0.0f };
        for (size_t path = 0; path < filtered.size(); ++path)
        {
            auto& hpf = sidechainHPFState[path];
            filtered[path] = detectorSource[path] - hpf[0] + sidechainHPFCoeff * hpf[1];
            hpf[0] = detectorSource[path];
            hpf[1] = filtered[path];
        }

        std::array<float, 2> targetGainDb { 0.0f, 0.0f };
        if (effectiveLinkMode == 1)
        {
            const float peak = juce::jmax (std::abs (filtered[0]), std::abs (filtered[1]));
            const float levelDb = juce::Decibels::gainToDecibels (peak, -100.0f);
            const float target = feedbackMode ? computeFeedbackTargetGainReductionDb (levelDb)
                                              : computeTargetGainReductionDb (levelDb);
            targetGainDb = { target, target };
        }
        else
        {
            for (size_t path = 0; path < targetGainDb.size(); ++path)
            {
                const float levelDb = juce::Decibels::gainToDecibels (std::abs (filtered[path]), -100.0f);
                targetGainDb[path] = feedbackMode ? computeFeedbackTargetGainReductionDb (levelDb)
                                                  : computeTargetGainReductionDb (levelDb);
            }
        }

        const float maxTarget = juce::jmax (targetGainDb[0], targetGainDb[1]);
        if (timeConstantChoice >= 5)
        {
            const float targetState = maxTarget > 0.5f ? 1.0f : 0.0f;
            const float stateCoeff = targetState > automaticReleaseState
                ? std::exp (-1.0f / static_cast<float> (sampleRate * 0.1))
                : std::exp (-1.0f / static_cast<float> (sampleRate * 0.75));
            automaticReleaseState = targetState + stateCoeff * (automaticReleaseState - targetState);
        }

        float releaseCoeffForSample = releaseCoeff;
        if (timeConstantChoice >= 5)
        {
            float releaseSeconds = timeConstantChoice == 5 ? 2.0f : 0.8f;
            if (automaticReleaseState > 0.20f)
                releaseSeconds = 10.0f;
            if (timeConstantChoice == 6 && automaticReleaseState > 0.75f)
                releaseSeconds = 25.0f;
            releaseCoeffForSample = static_cast<float> (std::exp (-1.0 / (sampleRate * releaseSeconds)));
        }

        for (size_t path = 0; path < currentGainDb.size(); ++path)
        {
            const float coeff = juce::jmax (feedbackCoeffFloor,
                                            (targetGainDb[path] > currentGainDb[path]) ? attackCoeff : releaseCoeffForSample);
            currentGainDb[path] = targetGainDb[path] + coeff * (currentGainDb[path] - targetGainDb[path]);
        }
        for (size_t path = 0; path < blockGainDb.size(); ++path)
            blockGainDb[path] = juce::jmax (blockGainDb[path], currentGainDb[path]);
        if (static_cast<size_t> (i) < gainReductionTraceLength)
            gainReductionTrace[static_cast<size_t> (i)] = juce::jmax (currentGainDb[0], currentGainDb[1]);
        if (effectiveLinkMode == 2)
        {
            const auto delayedControl = toControlDomain (delayedSamples[0], delayedSamples[1], effectiveLinkMode);
            const float processedVertical = delayedControl[0]
                * juce::Decibels::decibelsToGain (-currentGainDb[0]);
            const float processedLateral = delayedControl[1]
                * juce::Decibels::decibelsToGain (-currentGainDb[1]);
            buffer.setSample (0, i, (processedVertical + processedLateral) * inverseSqrtTwo);
            buffer.setSample (1, i, (processedVertical - processedLateral) * inverseSqrtTwo);
            feedbackOutput = { processedVertical, processedLateral };
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto channelIndex = static_cast<size_t> (ch);
                const float outputSample = delayedSamples[channelIndex]
                    * juce::Decibels::decibelsToGain (-currentGainDb[channelIndex]);
                buffer.setSample (ch, i, outputSample);
                feedbackOutput[channelIndex] = outputSample;
            }
        }
    }

    // Publish once per block instead of once per sample: the message thread
    // polls at UI rate, and this avoids needless atomic traffic on audio.
    for (size_t path = 0; path < currentGainDb.size(); ++path)
        channelGainDbAtomic[path].store (currentGainDb[path], std::memory_order_relaxed);
    for (size_t path = 0; path < blockGainDb.size(); ++path)
        blockGainDbAtomic[path].store (blockGainDb[path], std::memory_order_relaxed);
    currentGainDbAtomic.store (juce::jmax (currentGainDb[0], currentGainDb[1]),
                               std::memory_order_relaxed);

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
