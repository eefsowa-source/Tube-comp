#include "TriodeStage.h"

namespace
{
    struct PlateSolve
    {
        double vpk = 0.0;
        double ip = 0.0;
    };

    // Newton-Raphson solve of f(Vpk) = Vpk + Rp*Ip(Vgk,Vpk) - openCircuitV = 0
    // for the WDF plate-circuit root. The derivative comes from the closed-form
    // Koren expression, so each iteration costs one transcendental evaluation
    // instead of the three a central difference would need.
    //
    // The loop evaluates once at the warm start and then only steps while the
    // step is still above tolerance, so the Koren evaluation at the returned Vpk
    // is the one already in hand. Returning its current with the root removes a
    // full extra model evaluation per sample (the caller used to recompute
    // korenPlateCurrent at the solved voltage).
    PlateSolve solvePlateVoltage (double Vgk, double openCircuitV, double Rp,
                                  const KorenTriodeParams& params, double warmStart) noexcept
    {
        double Vpk = juce::jlimit (0.5, 500.0, warmStart);

        constexpr int maxIters = 12;
        constexpr double tol = 1.0e-6;

        KorenPlateResult koren = korenPlateCurrentAndSlope (Vgk, Vpk, params);

        for (int iter = 0; iter < maxIters; ++iter)
        {
            const double f = Vpk + Rp * koren.ip - openCircuitV;
            const double fPrime = 1.0 + Rp * koren.dIpdVpk;

            if (! (std::abs (fPrime) > 1.0e-12))
                break;

            const double step = f / fPrime;

            if (std::abs (step) < tol)
                break;

            Vpk = juce::jlimit (0.5, 500.0, Vpk - step);
            koren = korenPlateCurrentAndSlope (Vgk, Vpk, params);
        }

        return { Vpk, koren.ip };
    }
}

void TriodeStage::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    gridHighpassCoeff   = std::exp (-1.0 / (Rg * Cin * sampleRate));
    outputHighpassCoeff = std::exp (-1.0 / (Rload * Cout * sampleRate));

    // Resolve Rk from the current harmonic/bias trims before seeding the DC
    // operating point, so the quiescent state matches the bias actually used and
    // the stage does not drift to it over the cathode time constant.
    updateCathodeResistance();

    channels.assign (spec.numChannels, ChannelState {});

    // Control-bias smoothing runs at the oversampled rate this stage is prepared
    // with, so the vari-mu coupling tracks the GR envelope without block-rate steps.
    controlBiasSmoothCoeff = std::exp (-1.0 / (sampleRate * controlBiasSmoothSeconds));
    updateControlBiasCompensation();

    // Solve the DC operating point (grid at 0 V through Rg to ground).
    //
    // The naive fixed-point iteration Vk <- Ip(-Vk, Vpk(Vk)) * Rk has a loop
    // gain of roughly gm*Rk, which is about 3 for these values, so it oscillates
    // instead of settling and leaves the seed well off the true operating point.
    // Damping it converges. An exactly converged operating point means the stage
    // starts at rest on its own, with no warm-up pass over silence.
    double Vk = 2.0;
    double Vpk = 150.0;
    constexpr double relaxation = 0.25;
    for (int i = 0; i < 1000; ++i)
    {
        const double Vgk = 0.0 - Vk;
        Vpk = solvePlateVoltage (Vgk, Vb - Vk, Rp, triodeParams, Vpk).vpk;
        const double targetVk = korenPlateCurrent (Vgk, Vpk, triodeParams) * Rk;
        const double nextVk = Vk + relaxation * (targetVk - Vk);
        const double change = std::abs (nextVk - Vk);
        Vk = nextVk;

        if (change < 1.0e-12)
            break;
    }

    quiescentVk = Vk;
    quiescentVpk = Vpk;

    for (auto& ch : channels)
    {
        ch.cathodeV = quiescentVk;
        ch.plateVpk = quiescentVpk;
        ch.plateVpkPrev = quiescentVpk;
        // The output coupling capacitor starts charged to the DC plate-to-ground
        // voltage. Without this the first sample passes that whole DC voltage.
        ch.outPrevIn = quiescentVpk + quiescentVk;
    }

    tiltFilter.prepare (spec);
    setBrightness (0.5f);
}

void TriodeStage::reset()
{
    for (auto& ch : channels)
    {
        ch.gridPrevIn = ch.gridPrevOut = 0.0;
        ch.cathodeV = quiescentVk;
        ch.plateVpk = quiescentVpk;
        ch.plateVpkPrev = quiescentVpk;
        ch.outPrevIn = quiescentVpk + quiescentVk;
        ch.outPrevOut = 0.0;
        ch.controlBias = controlBiasTargetVolts;
        ch.compGain = controlBiasGainComp;
    }
    tiltFilter.reset();
}

void TriodeStage::setDrive (float driveDb) noexcept
{
    // 0 dB -> ~1.5 Vpk grid swing (gentle), up to a few tens of volts at max
    // drive, which is where the Koren nonlinearity really bites.
    driveVolts = 1.5 * juce::Decibels::decibelsToGain (driveDb);
}

void TriodeStage::setHarmonicRatio (float ratio01) noexcept
{
    harmonicRatio = juce::jlimit (0.0f, 1.0f, ratio01);
    updateCathodeResistance();
}

void TriodeStage::setBrightness (float brightness01) noexcept
{
    const float gainDb = juce::jmap (brightness01, 0.0f, 1.0f, -3.0f, 3.0f);
    *tiltFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, 3000.0f, 0.707f, juce::Decibels::decibelsToGain (gainDb));
}

void TriodeStage::setBiasDrive (float bias01) noexcept
{
    // Bias sweeps the quiescent cathode voltage:
    // hotter = lower Rk (2nd harmonic), colder = higher Rk (odd harmonics).
    // This is independent of input drive; it sets the "idle" compression point.
    biasDrive = juce::jlimit (0.0f, 1.0f, bias01);
    updateCathodeResistance();
}

void TriodeStage::updateCathodeResistance() noexcept
{
    const double harmonicRk = juce::jmap (static_cast<double> (harmonicRatio), RkHot, RkCold);
    // Bias remains an independent trim around the harmonic-balance setting.
    const double biasScale = juce::jmap (static_cast<double> (biasDrive), 1.15, 0.85);
    Rk = juce::jlimit (RkHot * 0.7, RkCold * 1.3, harmonicRk * biasScale);
    cathodeLowpassCoeff = std::exp (-1.0 / (Rk * Ck * sampleRate));
    // The compensation curve depends on Rk, so keep it current with the bias.
    updateControlBiasCompensation();
}

void TriodeStage::setControlBiasVolts (double volts) noexcept
{
    controlBiasTargetVolts = juce::jlimit (-maxControlBiasVolts, 0.0, volts);
    updateControlBiasCompensation();
}

void TriodeStage::updateControlBiasCompensation() noexcept
{
    const double depth = juce::jlimit (0.0, maxControlBiasVolts, -controlBiasTargetVolts);

    // Small-signal gain loss (dB) vs control-bias depth, measured through the
    // stage (drive 6 dB, bias ratio 0.5, -6 dBFS 1 kHz):
    //   0.45 V -> 0.74 dB, 0.90 V -> 1.70 dB, 1.35 V -> 2.93 dB, 1.80 V -> 4.49 dB
    // It lands close to the Koren DC transconductance ratio at the hot/cold
    // extremes, but the direct measurement is what the tone actually shows, and
    // it is nearly independent of the cathode resistor. Compensating it keeps the
    // compressor as the sole owner of the level law: the coupling moves the
    // harmonic character, not the loudness.
    controlBiasGainComp = juce::Decibels::decibelsToGain (1.36 * depth + 0.63 * depth * depth);
}

float TriodeStage::processSample (ChannelState& s, float xIn) noexcept
{
    const double vinGrid = static_cast<double> (xIn) * driveVolts;

    // Vari-mu control voltage: one-pole toward the block target so the operating
    // point cannot step between blocks.
    s.controlBias += (1.0 - controlBiasSmoothCoeff) * (controlBiasTargetVolts - s.controlBias);
    s.compGain += (1.0 - controlBiasSmoothCoeff) * (controlBiasGainComp - s.compGain);

    // Grid coupling network: RC highpass (Cin, Rg), DC-blocker form.
    const double vg = vinGrid - s.gridPrevIn + gridHighpassCoeff * s.gridPrevOut;
    s.gridPrevIn = vinGrid;
    s.gridPrevOut = vg;

    const double vgk = vg - s.cathodeV + s.controlBias;
    const double openCircuitV = Vb - s.cathodeV;

    // Linear prediction from the last two plate voltages. The signal moves
    // smoothly at audio rate, so the extrapolated point is a much better Newton
    // warm start than the previous value alone (measured mean 2.80 -> 2.19
    // iterations). A transient that breaks the prediction only costs extra
    // bounded iterations, never a wrong root.
    const double warmStart = 2.0 * s.plateVpk - s.plateVpkPrev;
    const auto solved = solvePlateVoltage (vgk, openCircuitV, Rp, triodeParams, warmStart);
    s.plateVpkPrev = s.plateVpk;
    s.plateVpk = solved.vpk;
    const double ip = solved.ip;

    // Cathode bypass sag: slow one-pole tracking of Ip*Rk models the
    // Rk||Ck time constant, giving program-dependent self-bias compression.
    s.cathodeV += (1.0 - cathodeLowpassCoeff) * (ip * Rk - s.cathodeV);

    const double vp = s.plateVpk + s.cathodeV; // plate-to-ground

    // Output coupling network: RC highpass (Cout, Rload).
    const double vOut = vp - s.outPrevIn + outputHighpassCoeff * s.outPrevOut;
    s.outPrevIn = vp;
    s.outPrevOut = vOut;

    // A common-cathode stage inverts: a positive grid swing pulls the plate
    // down. Hardware restores absolute polarity with a second stage or the
    // output transformer; the model does it here, so the wet path stays in
    // phase with the dry reference (Mix blends instead of cancelling) and
    // bypass does not flip polarity.
    return static_cast<float> (-vOut * outputTrim * s.compGain);
}
