#pragma once

#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

/**
    Log-domain, stereo-linked (peak-detected) compressor with selectable
    feedforward or causal feedback detector topology.

    Signal flow per sample:
      1. Stereo-linked peak level -> dB. In feedback mode the level comes from
         the preceding compressed output sample.
      2. Static soft-knee gain computer (threshold/ratio/knee) gives the
         *target* gain reduction in dB for that instant.
      3. One-pole attack/release ballistics smooth the target into the
         gain actually applied, so transients don't click and releases
         don't pump unmusically.

    This sits ahead of the tube/saturation stage: it does the "comp" part
    of Tube Comp, while TriodeStage/TubeSaturator do the harmonic coloring.
*/
class Compressor
{
public:
    void prepare (double newSampleRate, int numChannels);
    void reset();

    void setThresholdDb (float dB) noexcept { thresholdDb = dB; }
    void setRatio (float r) noexcept { ratio = juce::jmax (1.0f, r); }
    void setKneeDb (float dB) noexcept { kneeDb = juce::jmax (0.0f, dB); }
    void setAttackMs (float ms) noexcept;
    void setReleaseMs (float ms) noexcept;
    /** 0 = custom attack/release, 1..6 = Fairchild-style time constants. */
    void setTimeConstant (int choice) noexcept;
    /** 0 = independent L/R, 1 = linked stereo, 2 = lateral/vertical M/S. */
    void setLinkMode (int mode) noexcept;
    void setLookAheadMs (float ms) noexcept;
    void setSidechainHPFHz (float hz) noexcept;

    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Configured look-ahead length in samples; contributes to the reported plugin latency. */
    size_t getLookAheadSamples() const noexcept { return lookAheadSamples; }

    /** Upper bound of the look-ahead parameter. Delay buffers are preallocated for this in prepare(). */
    static constexpr float maxLookAheadMs = 20.0f;

    /** Select the detector topology. Feedback uses the previous compressed output
        as its detector source, which is the causal digital form of a
        vari-mu feedback loop. */
    void setFeedbackMode (bool enabled) noexcept;
    bool isFeedbackMode() const noexcept { return feedbackMode; }

    /** Gain reduction applied to the last processed sample, in dB (>= 0). Useful for a meter. */
    float getCurrentGainReductionDb() const noexcept { return currentGainDbAtomic.load (std::memory_order_relaxed); }
    float getChannelGainReductionDb (int channel) const noexcept
    {
        return (channel >= 0 && channel < 2) ? channelGainDbAtomic[static_cast<size_t> (channel)].load (std::memory_order_relaxed)
                                              : 0.0f;
    }

    /** Peak gain reduction over the most recently processed block, in dB (>= 0),
        for channel 0 (L) / 1 (R). The vari-mu coupling reads this once per block
        to shift the tube operating point; the end-of-block value alone would miss
        transients that only clip the first samples of the block. */
    float getBlockGainReductionDb (int channel) const noexcept
    {
        return (channel >= 0 && channel < 2) ? blockGainDbAtomic[static_cast<size_t> (channel)].load (std::memory_order_relaxed)
                                              : 0.0f;
    }

    /** Post-gain output level for channel 0 (L) / 1 (R), VU-ballistics smoothed, in dBFS.
        Safe to call from the message/UI thread while the audio thread updates it. */
    float getChannelLevelDb (int channel) const noexcept
    {
        return (channel >= 0 && channel < 2) ? meterLevelDb[static_cast<size_t> (channel)].load (std::memory_order_relaxed)
                                              : -100.0f;
    }

private:
    float computeTargetGainReductionDb (float levelDb) const noexcept;

    double sampleRate = 44100.0;
    int channelCount = 2;

    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float kneeDb = 6.0f;

    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;

    std::array<float, 2> currentGainDb { 0.0f, 0.0f }; // per detector path, dB
    std::atomic<float> currentGainDbAtomic { 0.0f };
    std::array<std::atomic<float>, 2> channelGainDbAtomic { { 0.0f, 0.0f } };
    std::array<float, 2> blockGainDb { 0.0f, 0.0f };
    std::array<std::atomic<float>, 2> blockGainDbAtomic { { 0.0f, 0.0f } };

    int timeConstantChoice = 0; // 0 = custom, 1..6 = Fairchild-style
    int linkMode = 1;            // 0 = L/R, 1 = linked, 2 = Lateral/Vertical
    float automaticReleaseState = 0.0f;

    bool feedbackMode = false;
    // One-sample causal state for the feedback detector. The detector reads
    // the output of the preceding sample, so it never needs an implicit solve
    // or a potentially unstable same-sample algebraic loop.
    std::array<float, 2> feedbackOutput { 0.0f, 0.0f };

    // VU-style output metering (separate ballistics from the control-rate detector).
    float meterReleaseCoeff = 0.0f;
    std::array<float, 2> meterEnvelopeLinear { 0.0f, 0.0f };
    std::array<std::atomic<float>, 2> meterLevelDb { { -100.0f, -100.0f } };

    // Look-ahead delay: buffers delayed signals so detector sees future.
    size_t lookAheadSamples = 0;
    std::array<std::vector<float>, 2> delayBuffer;
    std::array<size_t, 2> delayWritePos { 0, 0 };

    // Sidechain HPF: removes low frequencies from detection path (kicks/bass don't pump).
    std::array<std::array<float, 2>, 2> sidechainHPFState { { { 0.0f, 0.0f }, { 0.0f, 0.0f } } };
    float sidechainHPFCoeff = 0.0f;
};
