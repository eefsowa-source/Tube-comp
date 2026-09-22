#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "../Source/DSP/KorenTriode.h"

/*
    Validates the closed-form derivative of the Koren plate-current model
    against a Richardson-extrapolated central difference. The Newton solver in
    TriodeStage depends on this slope being right: a wrong slope still converges
    to the correct root, but it converges more slowly and the cost saving it is
    supposed to buy never shows up.
*/

int main()
{
    const KorenTriodeParams params {};
    bool passed = true;

    int checked = 0;
    double worstRelative = 0.0;
    double worstVgk = 0.0, worstVpk = 0.0;

    for (double Vgk = -5.0; Vgk <= 1.5; Vgk += 0.25)
    {
        for (double Vpk = 5.0; Vpk <= 450.0; Vpk += 5.0)
        {
            const auto analytic = korenPlateCurrentAndSlope (Vgk, Vpk, params);

            // The model's cutoff branch returns exactly zero current, and the
            // slope must be zero with it. Immediately above cutoff the current is
            // nonzero but far too small for a numeric probe to mean anything.
            if (analytic.ip == 0.0)
            {
                if (analytic.dIpdVpk != 0.0)
                {
                    std::printf ("FAIL: cutoff slope not zero at Vgk %.2f Vpk %.2f\n", Vgk, Vpk);
                    passed = false;
                }
                continue;
            }

            if (analytic.ip < 1.0e-9)
                continue;

            const double h = 1.0e-3;
            const auto plus = korenPlateCurrent (Vgk, Vpk + h, params);
            const auto minus = korenPlateCurrent (Vgk, Vpk - h, params);
            const auto plusHalf = korenPlateCurrent (Vgk, Vpk + h * 0.5, params);
            const auto minusHalf = korenPlateCurrent (Vgk, Vpk - h * 0.5, params);

            const double coarse = (plus - minus) / (2.0 * h);
            const double fine = (plusHalf - minusHalf) / h;
            const double numeric = (4.0 * fine - coarse) / 3.0;

            const double scale = std::max (std::abs (numeric), 1.0e-12);
            const double relative = std::abs (analytic.dIpdVpk - numeric) / scale;

            if (relative > worstRelative)
            {
                worstRelative = relative;
                worstVgk = Vgk;
                worstVpk = Vpk;
            }

            ++checked;
        }
    }

    std::printf ("checked %d conduction points, worst relative error %.3e at Vgk %.2f Vpk %.2f\n",
                 checked, worstRelative, worstVgk, worstVpk);

    passed = passed && checked > 500;
    passed = passed && worstRelative < 1.0e-5;

    std::printf (passed ? "PASS\n" : "FAIL\n");
    return passed ? 0 : 1;
}
