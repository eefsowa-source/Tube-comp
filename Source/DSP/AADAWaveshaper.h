#pragma once

#include <cmath>
#include <juce_dsp/juce_dsp.h>

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
            const auto logCosh = [] (float value) noexcept
            {
                const float a = std::abs (value);
                return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718056f;
            };
            const float numerator = logCosh (x) - logCosh (prevInput);
            y = numerator / delta;
        }

        prevInput = x;
        return y;
    }

private:
    float prevInput = 0.0f;
};

/**
    ADAA shaper with gain scaling (for use in saturator).
*/
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
