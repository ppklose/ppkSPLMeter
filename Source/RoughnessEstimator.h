#pragma once
#include <cmath>
#include <algorithm>

/**
    Simplified real-time roughness estimator.

    Roughness is caused by amplitude modulations in the 15-300 Hz range
    (perceptual peak ~ 70 Hz). This estimator computes:

        Roughness (%) = RMS(envelope AC, 15-300 Hz) / envelope DC  × 100

    0 % = no modulation; 100 % = fully AM-modulated signal in the roughness band.
    Values are gated to 0 when the input level is below ~-80 dBFS to avoid
    noise-floor artefacts.
*/
class RoughnessEstimator
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;

        // Envelope follower: full-wave rect + 1st-order LPF at 300 Hz
        alpha_lp = 1.0 - std::exp (-2.0 * pi * 300.0 / fs);

        // Slow DC tracker of the envelope at 2 Hz (for normalisation)
        alpha_dc = 1.0 - std::exp (-2.0 * pi * 2.0 / fs);

        // 1st-order HP on envelope at 15 Hz → AC modulation component
        coeff_hp = std::exp (-2.0 * pi * 15.0 / fs);

        // RMS integration window: 200 ms
        rmsWindowSamples = std::max (1, static_cast<int> (0.200 * fs));

        reset();
    }

    void reset() noexcept
    {
        envLP = envDC = envHP = envPrev = 0.0;
        rmsSumAC = 0.0;
        rmsCount = 0;
        roughness = 0.0f;
    }

    /** Call once per sample (mono, any amplitude). */
    float processSample (float x) noexcept
    {
        const double rect = std::abs (static_cast<double> (x));

        // Gate: skip update for very quiet signals
        if (rect < 1e-5)
        {
            ++rmsCount;
            if (rmsCount >= rmsWindowSamples) { roughness = 0.0f; rmsSumAC = 0.0; rmsCount = 0; }
            return roughness;
        }

        // Envelope LPF (captures AM up to 300 Hz)
        envLP += alpha_lp * (rect - envLP);

        // Slow DC of envelope
        envDC += alpha_dc * (envLP - envDC);

        // 1st-order HP: y[n] = c * (y[n-1] + x[n] - x[n-1])
        const double hp = coeff_hp * (envHP + envLP - envPrev);
        envPrev = envLP;
        envHP   = hp;

        // Accumulate RMS of AC modulation
        rmsSumAC += hp * hp;
        ++rmsCount;

        if (rmsCount >= rmsWindowSamples)
        {
            const double rmsAC = std::sqrt (rmsSumAC / rmsCount);
            const double dc    = std::max (envDC, 1e-10);
            roughness  = static_cast<float> (rmsAC / dc * 100.0);
            rmsSumAC   = 0.0;
            rmsCount   = 0;
        }

        return roughness;
    }

    float getRoughness() const noexcept { return roughness; }

private:
    static constexpr double pi = 3.14159265358979323846;

    double fs             = 44100.0;
    double alpha_lp       = 0.0;
    double alpha_dc       = 0.0;
    double coeff_hp       = 0.0;

    double envLP  = 0.0, envDC  = 0.0;
    double envHP  = 0.0, envPrev = 0.0;

    double rmsSumAC = 0.0;
    int    rmsWindowSamples = 0;
    int    rmsCount = 0;

    float roughness = 0.0f;
};
