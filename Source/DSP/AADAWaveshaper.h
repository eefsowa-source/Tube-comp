#pragma once

#include <cmath>
#include <juce_dsp/juce_dsp.h>

/** Numerically stable log(cosh(x)); the antiderivative of tanh up to a constant. */
inline float logCoshOf (float x) noexcept
{
    const float a = std::abs (x);
    return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718056f;
}

/** Grid bias of the asymmetric (even-harmonic) branch. */
inline constexpr float asymBranchBias = 0.5f;

/** Bounded asymmetric soft clip. Shifting tanh by a grid bias makes the curve
    asymmetric about zero, which is what generates 2nd-harmonic content; scaling
    by 1/(1 + tanh(bias)) keeps the negative peak at -1 and the positive peak at
    exp(-2*bias), so the whole branch stays inside (-1, 1). */
inline float biasedTanh (float x, float bias) noexcept
{
    const float scale = 1.0f / (1.0f + std::tanh (bias));
    return scale * (std::tanh (x + bias) - std::tanh (bias));
}

/** Antiderivative of biasedTanh, up to a constant. */
inline float biasedTanhAntiderivative (float x, float bias) noexcept
{
    const float scale = 1.0f / (1.0f + std::tanh (bias));
    return scale * (logCoshOf (x + bias) - std::tanh (bias) * x);
}

/**
    First-order ADAA (Anti-Derivative Anti-Aliasing) waveshaper.

    ADAA reduces aliasing by integrating the continuous nonlinear function
    over the sample interval, rather than just sampling at the midpoint.
    For a function f(x), the ADAA approximation is:

        y[n] ≈ (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])

    where F is the antiderivative of f. For tanh, this integral is log(cosh(x)).

    This trades one multiply+log per sample for cleaner, less aliased output.
*/
class ADAATanhShaper
{
public:
    void reset() noexcept { prevInput = 0.0; }

    float processSample (float x) noexcept
    {
        constexpr float eps = 1.0e-6f;
        const float delta = x - prevInput;

        float y;
        if (std::abs (delta) < eps)
        {
            // If inputs are nearly identical, fall back to direct tanh
            // to avoid division-by-zero issues.
            y = std::tanh (x);
        }
        else
        {
            // Antiderivative of tanh is log(cosh(x)).
            y = (logCoshOf (x) - logCoshOf (prevInput)) / delta;
        }

        prevInput = x;
        return y;
    }

private:
    float prevInput = 0.0f;
};

/** ADAA for the grid-biased tanh used as the asymmetric branch. */
class ADAAAsymTanhShaper
{
public:
    void setBias (float newBias) noexcept { bias = newBias; }
    void reset() noexcept { prevInput = 0.0f; }

    float processSample (float x) noexcept
    {
        constexpr float eps = 1.0e-6f;
        const float delta = x - prevInput;

        float y;
        if (std::abs (delta) < eps)
            y = biasedTanh (x, bias);
        else
            y = (biasedTanhAntiderivative (x, bias) - biasedTanhAntiderivative (prevInput, bias)) / delta;

        prevInput = x;
        return y;
    }

private:
    float bias = asymBranchBias;
    float prevInput = 0.0f;
};

class ADAASoftClip
{
public:
    void reset() noexcept { shaper.reset(); }

    float processSample (float x, float gainDb) noexcept
    {
        const float gainLin = juce::Decibels::decibelsToGain (gainDb);
        return shaper.processSample (x * gainLin) * 0.5f; // output trim to keep amplitude in check
    }

private:
    ADAATanhShaper shaper;
};
