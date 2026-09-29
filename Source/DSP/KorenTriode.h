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

/** Plate current and its derivative with respect to Vpk, in closed form.

    The Newton solver needs the slope as well as the value. Deriving it
    analytically costs one transcendental evaluation per iteration, where a
    central difference needs three, so this is the difference between roughly
    three and one evaluation per Newton step.

    With s = 1/mu + Vgk/sqrt(Kvb + Vpk^2), z = Kp*s and L = ln(1 + exp(z)):

        E1        = (Vpk/Kp) * L
        dE1/dVpk  = L/Kp + Vpk * logistic(z) * ds/dVpk
        ds/dVpk   = -Vgk * Vpk * (Kvb + Vpk^2)^(-3/2)
        dIp/dVpk  = (Ex/Kg1) * E1^(Ex-1) * dE1/dVpk
*/
struct KorenPlateResult
{
    double ip = 0.0;
    double dIpdVpk = 0.0;
};

inline KorenPlateResult korenPlateCurrentAndSlope (double Vgk, double Vpk,
                                                   const KorenTriodeParams& p) noexcept
{
    KorenPlateResult result;

    const double kvbTerm = p.kvb + Vpk * Vpk;
    if (kvbTerm <= 1.0e-9)
        return result;

    const double sqrtKvbTerm = std::sqrt (kvbTerm);
    const double s = 1.0 / p.mu + Vgk / sqrtKvbTerm;
    const double e1Arg = p.kp * s;

    // log(1 + exp(x)) and logistic(x) share the same exp(x). Computing it once
    // drops a transcendental from every model evaluation, which is the hot path
    // under oversampling. The large-x branch keeps exp() away from overflow and
    // reproduces the previous asymptotes exactly (softplus -> x, logistic -> 1).
    double softplus = 0.0;
    double logistic = 0.0;
    if (e1Arg > 30.0)
    {
        softplus = e1Arg;
        logistic = 1.0;
    }
    else
    {
        const double exponential = std::exp (e1Arg);
        softplus = std::log1p (exponential);
        logistic = exponential / (1.0 + exponential);
    }

    const double e1 = (Vpk / p.kp) * softplus;

    // Cutoff: the model carries no current, and the slope is zero with it.
    if (e1 <= 0.0)
        return result;

    // e1^ex and e1^(ex-1) share one pow: e1^ex = e1 * e1^(ex-1).
    const double e1PowM1 = std::pow (e1, p.ex - 1.0);
    const double e1Pow = e1 * e1PowM1;
    result.ip = e1Pow / p.kg1;

    const double dsDVpk = -Vgk * Vpk / (kvbTerm * sqrtKvbTerm);
    const double dE1dVpk = softplus / p.kp + Vpk * logistic * dsDVpk;
    result.dIpdVpk = (p.ex / p.kg1) * e1PowM1 * dE1dVpk;

    return result;
}

inline double korenPlateCurrent (double Vgk, double Vpk, const KorenTriodeParams& p) noexcept
{
    return korenPlateCurrentAndSlope (Vgk, Vpk, p).ip;
}
