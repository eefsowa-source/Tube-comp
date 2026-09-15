#pragma once

#include <cmath>

/**
    Norman Koren's improved triode SPICE model (as used for 12AX7-family tubes).

    Reference form (Koren, "Improved Vacuum Tube Models for SPICE Simulations", 1996):
        E1 = (Vpk/Kp) * ln(1 + exp(Kp*(1/mu + Vgk/sqrt(Kvb + Vpk^2))))
        Ip = E1^Ex / Kg1          (E1 <= 0 => Ip = 0, cutoff)

    This is a static nonlinearity: given the instantaneous grid-cathode and
    plate-cathode voltages, it returns the plate current. It has no memory of
    its own -- all of the circuit's dynamic (frequency-dependent) behaviour
    comes from the surrounding RC network in TriodeStage.
*/
struct KorenTriodeParams
{
    double mu   = 100.0;   // amplification factor
    double ex   = 1.4;     // exponent
    double kg1  = 1060.0;  // plate current scaling
    double kp   = 600.0;   // "knee" sharpness
    double kvb  = 300.0;   // low-plate-voltage knee correction
};

inline double korenPlateCurrent (double Vgk, double Vpk, const KorenTriodeParams& p) noexcept
{
    const double kvbTerm = p.kvb + Vpk * Vpk;
    if (kvbTerm <= 1.0e-9)
        return 0.0;

    const double e1Arg = p.kp * (1.0 / p.mu + Vgk / std::sqrt (kvbTerm));

    // log(1 + exp(x)) computed in a way that stays finite for large x.
    const double softplus = (e1Arg > 30.0) ? e1Arg : std::log1p (std::exp (e1Arg));

    const double e1 = (Vpk / p.kp) * softplus;
    if (e1 <= 0.0)
        return 0.0;

    return std::pow (e1, p.ex) / p.kg1;
}
