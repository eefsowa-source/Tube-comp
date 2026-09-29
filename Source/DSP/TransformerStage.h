#pragma once

#include <array>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <memory>

/**
    Linear-approximation audio transformer stage (plan 8.2, item S3).

    Fairchild-670-class hybrid compressors pass the audio through input and
    output iron, and that iron is a named part of the sound. The first
    prototype keeps it to three measurable effects, applied per sample:

      low-shelf  : a gentle lift on the low band, standing in for the core's
                   flux crowding.
      saturation : a weak, odd, slope-1-at-origin soft clip. Real cores
                   saturate on flux, which is dominantly odd-harmonic at
                   moderate levels; the blend is deliberately small.
      high-shelf : an attenuation of the top octave, the winding
                   capacitance/leakage rolloff. Placed last so the harmonics
                   the saturation just generated get rolled off the way real
                   windings limit bandwidth.

    No DC blocker is needed here. The stage runs after the tube stage, which
    already has one, and the nonlinearity is odd so it generates no DC of its
    own. A DC blocker was tried and removed: at a 10 ms corner it cost
    -0.64 dB at 40 Hz, which cancelled the low-band shelf the stage exists to
    provide.

    The two shelves are corner-specified in Hz and use JUCE's rate-correct
    biquad design, so their response is the same whether this instance runs at
    48 kHz or 384 kHz. A matched-Z one-pole was tried first and rejected: its
    -3 dB point moves with the sample rate, so one setting dulled 15 kHz by
    0.2 dB at 48 kHz but 1.6 dB at 192 kHz. The saturation's own
    anti-aliasing comes from the oversampling this stage sits inside: one
    instance per quality factor, prepared at that factor's rate.

    The gate for keeping this stage is a measurable spectrum difference with
    no regressions in Tools/sound_baseline.cpp. The nonlinear hysteretic
    inductor (a second WDF root) is deferred until listening says the linear
    version earns its place.
*/
class TransformerStage
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        // The duplicator owns the coefficient state, so write the design
        // straight into it and let prepare() size the per-channel copies.
        *lfDup.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            spec.sampleRate, lfShelfHz, shelfQ, lfShelfGain);
        *hfDup.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            spec.sampleRate, hfShelfHz, shelfQ, hfShelfGain);

        lfDup.prepare (spec);
        hfDup.prepare (spec);
    }

    template <typename ProcessContext>
    void process (const ProcessContext& context) noexcept
    {
        // Low shelf first, as a block filter so JUCE handles channel count and
        // coefficient threading.
        lfDup.process (context);

        auto&& outputBlock = context.getOutputBlock();
        const auto numChannels = juce::jmin (outputBlock.getNumChannels(),
                                             static_cast<size_t> (maxChannels));

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* out = outputBlock.getChannelPointer (ch);

            for (size_t i = 0; i < outputBlock.getNumSamples(); ++i)
            {
                float x = out[i];
                x = x + satMix * (softClip (x) - x);
                out[i] = x;
            }
        }

        // High shelf last: the saturation's harmonics are generated before it,
        // so the winding rolloff limits them the way real iron does.
        hfDup.process (context);
    }

private:
    static constexpr size_t maxChannels = 8;
    // Low-band shelf. The gain is set against the measured result rather than
    // the nominal one: the triode stage ahead of this one already loses level
    // down there, so +0.4 dB nominal moved the output by only +0.09 dB, and
    // +1.0 dB nominal measured +0.67 dB. +0.8 dB nominal lands the measured
    // lift at about +0.5 dB at 40 Hz. The 150 Hz corner keeps the shelf off the
    // midrange, where a boost would read as mud rather than weight.
    static constexpr float lfShelfHz = 150.0f;
    static constexpr float lfShelfGain = 1.096f;

    // Top-octave shelf: about -0.4 dB at 20 kHz. Gentle on purpose; a steeper
    // rolloff measured as a broken treble control rather than iron character.
    static constexpr float hfShelfHz = 19000.0f;
    static constexpr float hfShelfGain = 0.955f;
    static constexpr float shelfQ = 0.707f;

    // The shape is x / sqrt(1 + (x/k)^2) scaled by k: odd, smooth, and slope 1
    // at the origin, so the blend adds no level change to quiet program.
    //
    // It was chosen over tanh and over a hard-clipped cubic because of what it
    // does not generate. At 4x, a 15 kHz probe's harmonics above H6 exceed the
    // 96 kHz oversampled Nyquist and the decimation folds them back into the
    // band, which showed up as a +28 dB alias regression. tanh still put
    // -107 dBc into H5 and -148 dBc into H7; a hard-clipped cubic put nothing
    // into H3 either, which removes the character along with the alias. This
    // shape measures -50 dBc H3 and -88 dBc H5 at peak level, and stays within
    // 0.1 dB of unity through the programme range. The knee sits at 1.5 rather
    // than below programme level: a knee of 0.6 put the corner inside normal
    // material and cost 1.1 dB at 1 kHz, which is a level regression, not a
    // character change. The blend is deliberately low, and is not a tone
    // control. Above the knee the third harmonic is the iron colour; anything
    // higher sits far enough down not to fold back into the band.
    static float softClip (float x) noexcept
    {
        const float scaled = x / satKnee;
        return x / std::sqrt (1.0f + scaled * scaled);
    }

    static constexpr float satMix = 0.15f;
    static constexpr float satKnee = 1.5f;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    Duplicator lfDup, hfDup;
};
