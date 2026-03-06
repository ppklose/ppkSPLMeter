#pragma once
#include <cmath>
#include <algorithm>

/**
    Simplified real-time fluctuation strength estimator.

    Fluctuation strength is caused by amplitude modulations in the 0.5-20 Hz range
    (perceptual peak ~ 4 Hz). Complementary to roughness (15-300 Hz).

    This estimator computes:
        F (%) = RMS(envelope AC, 0.5-20 Hz) / envelope DC  × 100

    0 % = no modulation; 100 % = fully AM-modulated signal in the fluctuation band.
    Integration window: 1 second (fluctuations are slow).
*/
class FluctuationStrengthEstimator
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;

        // Envelope LPF at 20 Hz - captures AM up to 20 Hz
        alpha_lp = 1.0 - std::exp (-2.0 * pi * 20.0 / fs);

        // Slow DC tracker at 0.5 Hz
        alpha_dc = 1.0 - std::exp (-2.0 * pi * 0.5 / fs);

        // 1st-order HP at 0.5 Hz - removes DC from envelope
        coeff_hp = std::exp (-2.0 * pi * 0.5 / fs);

        // RMS window: 1 second
        rmsWindowSamples = std::max (1, static_cast<int> (1.0 * fs));

        reset();
    }

    void reset() noexcept
    {
        envLP = envDC = envHP = envPrev = 0.0;
        rmsSumAC = 0.0;
        rmsCount = 0;
        fluctuation = 0.0f;
    }

    /** Call once per sample (mono). */
    float processSample (float x) noexcept
    {
        const double rect = std::abs (static_cast<double> (x));

        if (rect < 1e-5)
        {
            ++rmsCount;
            if (rmsCount >= rmsWindowSamples) { fluctuation = 0.0f; rmsSumAC = 0.0; rmsCount = 0; }
            return fluctuation;
        }

        // Envelope LPF at 20 Hz
        envLP += alpha_lp * (rect - envLP);

        // DC of envelope at 0.5 Hz
        envDC += alpha_dc * (envLP - envDC);

        // 1st-order HP: y[n] = c * (y[n-1] + x[n] - x[n-1])
        const double hp = coeff_hp * (envHP + envLP - envPrev);
        envPrev = envLP;
        envHP   = hp;

        rmsSumAC += hp * hp;
        ++rmsCount;

        if (rmsCount >= rmsWindowSamples)
        {
            const double rmsAC = std::sqrt (rmsSumAC / rmsCount);
            const double dc    = std::max (envDC, 1e-10);
            fluctuation = static_cast<float> (rmsAC / dc * 100.0);
            rmsSumAC    = 0.0;
            rmsCount    = 0;
        }

        return fluctuation;
    }

    float getFluctuation() const noexcept { return fluctuation; }

private:
    static constexpr double pi = 3.14159265358979323846;

    double fs        = 44100.0;
    double alpha_lp  = 0.0;
    double alpha_dc  = 0.0;
    double coeff_hp  = 0.0;

    double envLP = 0.0, envDC = 0.0;
    double envHP = 0.0, envPrev = 0.0;

    double rmsSumAC = 0.0;
    int    rmsWindowSamples = 0;
    int    rmsCount = 0;

    float fluctuation = 0.0f;
};
