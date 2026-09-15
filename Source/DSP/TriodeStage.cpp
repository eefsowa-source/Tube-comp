#include "TriodeStage.h"

namespace
{
    // Newton-Raphson solve of f(Vpk) = Vpk + Rp*Ip(Vgk,Vpk) - openCircuitV = 0
    // for the WDF plate-circuit root. Central-difference derivative keeps this
    // robust without hand-deriving the (fairly gnarly) analytic Koren partials.
    double solvePlateVoltage (double Vgk, double openCircuitV, double Rp,
                               const KorenTriodeParams& params, double warmStart) noexcept
    {
        double Vpk = juce::jlimit (0.5, 500.0, warmStart);

        constexpr double h = 1.0e-3;
        constexpr int maxIters = 12;
        constexpr double tol = 1.0e-6;

        for (int iter = 0; iter < maxIters; ++iter)
        {
            const double ip = korenPlateCurrent (Vgk, Vpk, params);
            const double f = Vpk + Rp * ip - openCircuitV;

            const double ipPlus  = korenPlateCurrent (Vgk, Vpk + h, params);
            const double ipMinus = korenPlateCurrent (Vgk, Vpk - h, params);
            const double dIpdVpk = (ipPlus - ipMinus) / (2.0 * h);
            const double fPrime = 1.0 + Rp * dIpdVpk;

            if (std::abs (fPrime) < 1.0e-9)
                break;

            const double step = f / fPrime;
            Vpk -= step;
            Vpk = juce::jlimit (0.5, 500.0, Vpk);

            if (std::abs (step) < tol)
                break;
        }

        return Vpk;
    }
}

void TriodeStage::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    gridHighpassCoeff   = std::exp (-1.0 / (Rg * Cin * sampleRate));
    outputHighpassCoeff = std::exp (-1.0 / (Rload * Cout * sampleRate));
    cathodeLowpassCoeff = std::exp (-1.0 / (Rk * Ck * sampleRate));

    channels.assign (spec.numChannels, ChannelState {});

    // Seed a sane DC operating point (Vg = 0, grid referenced through Rg to
    // ground) via a few fixed-point iterations, so the Newton solve starts
    // warm instead of from an arbitrary guess.
    double Vk = 2.0;
    double Vpk = 150.0;
    for (int i = 0; i < 50; ++i)
    {
        const double Vgk = 0.0 - Vk;
        Vpk = solvePlateVoltage (Vgk, Vb - Vk, Rp, triodeParams, Vpk);
        const double ip = korenPlateCurrent (Vgk, Vpk, triodeParams);
        Vk = ip * Rk;
    }

    for (auto& ch : channels)
    {
        ch.cathodeV = Vk;
        ch.plateVpk = Vpk;
    }

    tiltFilter.prepare (spec);
    setBrightness (0.5f);
}

void TriodeStage::reset()
{
    for (auto& ch : channels)
    {
        ch.gridPrevIn = ch.gridPrevOut = 0.0;
        ch.outPrevIn = ch.outPrevOut = 0.0;
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
}

float TriodeStage::processSample (ChannelState& s, float xIn) noexcept
{
    const double vinGrid = static_cast<double> (xIn) * driveVolts;

    // Grid coupling network: RC highpass (Cin, Rg), DC-blocker form.
    const double vg = vinGrid - s.gridPrevIn + gridHighpassCoeff * s.gridPrevOut;
    s.gridPrevIn = vinGrid;
    s.gridPrevOut = vg;

    const double vgk = vg - s.cathodeV;
    const double openCircuitV = Vb - s.cathodeV;

    s.plateVpk = solvePlateVoltage (vgk, openCircuitV, Rp, triodeParams, s.plateVpk);
    const double ip = korenPlateCurrent (vgk, s.plateVpk, triodeParams);

    // Cathode bypass sag: slow one-pole tracking of Ip*Rk models the
    // Rk||Ck time constant, giving program-dependent self-bias compression.
    s.cathodeV += (1.0 - cathodeLowpassCoeff) * (ip * Rk - s.cathodeV);

    const double vp = s.plateVpk + s.cathodeV; // plate-to-ground

    // Output coupling network: RC highpass (Cout, Rload).
    const double vOut = vp - s.outPrevIn + outputHighpassCoeff * s.outPrevOut;
    s.outPrevIn = vp;
    s.outPrevOut = vOut;

    return static_cast<float> (vOut * outputTrim);
}
