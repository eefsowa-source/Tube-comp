#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "KorenTriode.h"

/**
    Physically modeled common-cathode triode gain stage (12AX7-style), the
    core nonlinearity used by Fairchild/Manley-class hybrid tube compressors.

    Circuit modeled, per channel:

        Vin --[Cin]--+--[Rg to GND]--> Vg (grid)
                      |
                      grid RC network: linear, no grid current assumed
                      (standard "grid never draws current" simplification)

        B+ (Vb) --[Rp]-- Vp (plate) --(triode Ip(Vgk,Vpk))-- cathode
                                                                |
                                                     [Rk] || [Ck bypass] -- GND

        Vp --[Cout]--> output (AC-coupled)

    The grid and output coupling networks are simple linear RC highpass
    filters (no nonlinearity there, so a direct digital one-pole is used
    instead of building generic WDF adaptors for them).

    The plate network is where the physics live: it is a genuine one-port
    Wave Digital Filter root. The linear part (B+ source in series with Rp)
    reduces to a Thevenin one-port with port resistance Rp and open-circuit
    wave equal to (Vb - Vk). The triode's plate-cathode nonlinearity is the
    WDF root element; each sample we solve

        f(Vpk) = Vpk + Rp * Ip(Vgk, Vpk) - (Vb - Vk) = 0

    via Newton-Raphson, using the previous sample's Vpk as the warm-start
    (audio-rate steps are small, so this converges in a handful of
    iterations, especially under oversampling).

    The cathode voltage Vk is not fixed: it is tracked with a slow one-pole
    filter representing the cathode resistor/bypass-cap (Rk*Ck) time
    constant, so heavy signal raises the average plate current, raises Vk,
    and self-biases the tube colder -- the classic "cathode sag" mechanism
    that gives real triode stages their program-dependent, compressive feel.
*/
class TriodeStage
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Drive in dB maps the incoming (roughly -1..+1) audio sample to an
        actual grid-swing voltage in volts. */
    void setDrive (float driveDb) noexcept;

    /** 0..1: interpolates the cathode resistor between a "hot" bias (lower
        Rk, earlier/harder onset, 2nd-harmonic-heavy) and a "cold" bias
        (higher Rk, later onset, richer odd-harmonic content under drive). */
    void setHarmonicRatio (float ratio01) noexcept;

    void setBrightness (float brightness01) noexcept;

    /** Bias point: 0=cold (high Rk, more odd harmonics), 1=hot (low Rk, more 2nd harmonic).
        This modulates the DC quiescent current independently of drive, affecting saturation onset. */
    void setBiasDrive (float bias01) noexcept;

    /** Vari-mu control voltage: a DC grid-cathode offset in volts (<= 0) injected
        inside the tube equation, past the input coupling high-pass, so it moves
        the operating point instead of being blocked by Cin. Heavier gain
        reduction drives the grid colder, which raises the harmonic content
        exactly when the compressor is working. A small-signal gain trim
        (see getControlBiasGainComp) keeps the static level law with the
        compressor rather than double-counting the reduction. */
    void setControlBiasVolts (double volts) noexcept;

    /** Current requested control bias, in volts. */
    double getControlBiasVolts() const noexcept { return controlBiasTargetVolts; }

    /** Linear gain applied to undo the small-signal loss caused by the control
        bias, so the coupling changes the harmonic character without changing the
        steady-state level. */
    double getControlBiasGainComp() const noexcept { return controlBiasGainComp; }

    /** Deepest control-bias shift the coupling may request. */
    static constexpr double maxControlBiasVolts = 1.8;

    /** Smoothing time constant for the control bias, in seconds. This is a
        compromise: fast enough to follow the compressor's GR envelope on musical
        material, slow enough that the DC plate-voltage change stays gentle
        relative to the output coupling network, so the operating-point shift
        does not thump on a transient. */
    static constexpr double controlBiasSmoothSeconds = 0.012;

    template <typename ProcessContext>
    void process (const ProcessContext& context) noexcept
    {
        auto&& outputBlock = context.getOutputBlock();
        auto&& inputBlock = context.getInputBlock();

        const auto numChannels = juce::jmin (outputBlock.getNumChannels(), channels.size());
        const auto numSamples = outputBlock.getNumSamples();
        jassert (outputBlock.getNumChannels() <= channels.size());

        // The control trace (smoothed bias, gain trim, resting plate voltage)
        // is shared by all channels, so it is rendered once per chunk into a
        // buffer preallocated in prepare(). Chunking keeps any block length
        // safe without allocating.
        const auto chunkCapacity = controlTrace.size();
        if (chunkCapacity == 0)
            return;

        for (size_t offset = 0; offset < numSamples; offset += chunkCapacity)
        {
            const auto count = juce::jmin (chunkCapacity, numSamples - offset);
            renderControlTrace (count);

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                auto* in = inputBlock.getChannelPointer (ch) + offset;
                auto* out = outputBlock.getChannelPointer (ch) + offset;
                auto& state = channels[ch];

                for (size_t i = 0; i < count; ++i)
                    out[i] = processSample (state, in[i], controlTrace[i]);
            }
        }

        tiltFilter.process (context);
    }

private:
    struct ChannelState
    {
        double gridPrevIn = 0.0, gridPrevOut = 0.0;   // Cin/Rg highpass state
        double outPrevIn = 0.0, outPrevOut = 0.0;     // Cout highpass state
        double cathodeV = 2.0;                        // tracked Vk (slow)
        double plateVpk = 100.0;                       // Newton-Raphson warm start
        double plateVpkPrev = 100.0;                   // previous sample, for the linear predictor
    };

    /** Per-sample control values shared by every channel. */
    struct ControlFrame
    {
        double bias = 0.0;        // smoothed vari-mu control voltage
        double gainComp = 1.0;    // small-signal gain trim for that bias
        double restPlateV = 0.0;  // plate-to-ground voltage of the same stage at rest
    };

    /** Resting ("silent twin") copy of the stage: identical circuit, bias and
        cathode resistor, zero grid signal. Its plate voltage is the operating
        point the control bias and Rk move the real stage to; subtracting it
        before the output coupling network removes that DC motion, which the
        7 Hz coupling high-pass otherwise turns into a subsonic thump every time
        the gain reduction changes. The signal path, its harmonics, and its own
        program-dependent cathode sag are untouched. */
    struct RestingState
    {
        double cathodeV = 2.0;
        double plateVpk = 100.0;
        double plateVpkPrev = 100.0;
    };

    void renderControlTrace (size_t numSamples) noexcept;
    static double gainCompForBias (double controlBiasVolts) noexcept;
    float processSample (ChannelState& s, float xIn, const ControlFrame& control) noexcept;
    void updateCathodeResistance() noexcept;
    void updateControlBiasCompensation() noexcept;

    // Circuit constants (typical 12AX7 preamp / hybrid-compressor gain stage).
    static constexpr double Vb  = 250.0;     // B+ supply, volts
    static constexpr double Rp  = 100.0e3;   // plate load resistor
    static constexpr double Rg  = 1.0e6;     // grid leak resistor
    static constexpr double Cin = 22.0e-9;   // input coupling cap
    static constexpr double Cout = 22.0e-9;  // output coupling cap
    static constexpr double Rload = 1.0e6;   // downstream load (next stage grid leak)
    static constexpr double Ck  = 25.0e-6;   // cathode bypass cap

    static constexpr double RkHot  = 1200.0;  // ~"Manley"-ish: hotter bias
    static constexpr double RkCold = 2700.0;  // ~"Fairchild"-ish: colder bias

    KorenTriodeParams triodeParams {};

    double gridHighpassCoeff = 0.0;
    double outputHighpassCoeff = 0.0;
    double cathodeLowpassCoeff = 0.0;
    double Rk = RkHot;
    float harmonicRatio = 0.5f;
    float biasDrive = 0.5f;

    double controlBiasTargetVolts = 0.0;
    double controlBiasSmoothCoeff = 0.0;
    double controlBiasGainComp = 1.0;

    double driveVolts = 3.0;
    double outputTrim = 1.0 / 40.0; // brings plate-swing volts back near unity audio range

    // DC operating point solved in prepare(). reset() restores it so a reset
    // stage resumes from the same bias instead of keeping the last signal's sag.
    double quiescentVk = 2.0;
    double quiescentVpk = 150.0;

    std::vector<ChannelState> channels;
    RestingState resting;
    double smoothedControlBias = 0.0;
    std::vector<ControlFrame> controlTrace; // preallocated in prepare()

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> tiltFilter;
    double sampleRate = 44100.0;
};
