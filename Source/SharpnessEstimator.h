#pragma once
#include <cmath>

/**
    Simplified real-time sharpness estimator (Zwicker/Aures model, 8-band approximation).

    Sharpness describes the perceived "brightness" of a sound caused by high-frequency
    spectral energy. Formula:

        S = 0.11 * Σ( E_i * g(z_i) * z_i ) / Σ( E_i )

    where z_i = critical band rate (Bark), E_i = band energy,
    g(z) = 1 for z ≤ 16, g(z) = 0.066 * exp(0.171 * z) for z > 16  (von Bismarck weight).

    Unit: acum (approximate). 1 acum ≈ narrow-band noise at 1 kHz, 60 dB SPL, 1 Bark wide.
    Typical range: ~0.5 acum (low sounds) – 5 acum (very sharp/bright sounds).
*/
class SharpnessEstimator
{
public:
    static constexpr int kNumBands = 8;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;

        // Band centres (Hz), approximate Bark values, critical-band Q factors
        static constexpr double centerHz[kNumBands] = { 100, 300,  630, 1000, 2000, 4000, 7000, 10000 };
        static constexpr double barkZ   [kNumBands] = { 1.0, 3.0,  6.0,  8.5, 13.0, 17.0, 20.5,  22.5 };
        static constexpr double bandQ   [kNumBands] = { 1.0, 2.8,  5.0,  6.2,  6.6,  5.8,  4.5,   4.0 };

        for (int i = 0; i < kNumBands; ++i)
        {
            barkZ_[i] = barkZ[i];
            g_[i] = (barkZ[i] <= 16.0)
                    ? 1.0
                    : 0.066 * std::exp (0.171 * barkZ[i]);

            const double fc  = std::min (centerHz[i], sampleRate * 0.45); // safety vs Nyquist
            const double w0  = 2.0 * pi * fc / sampleRate;
            const double alp = std::sin (w0) / (2.0 * bandQ[i]);
            const double cw  = std::cos (w0);
            const double a0  = 1.0 + alp;

            b0_[i] =  alp / a0;
            b1_[i] =  0.0;
            b2_[i] = -alp / a0;
            a1_[i] = -2.0 * cw / a0;
            a2_[i] =  (1.0 - alp) / a0;
        }

        // Energy smoother: 1st-order LPF at 8 Hz (~120 ms time constant)
        alpha_energy = 1.0 - std::exp (-2.0 * pi * 8.0 / sampleRate);

        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kNumBands; ++i)
            s1_[i] = s2_[i] = energy_[i] = 0.0;
        sharpness = 0.0f;
    }

    /** Call once per sample (mono). */
    float processSample (float x) noexcept
    {
        double totalEnergy  = 0.0;
        double weightedSum  = 0.0;

        for (int i = 0; i < kNumBands; ++i)
        {
            // DFII-T biquad bandpass
            const double y = b0_[i] * x + s1_[i];
            s1_[i] = b1_[i] * x - a1_[i] * y + s2_[i];
            s2_[i] = b2_[i] * x - a2_[i] * y;

            // Smooth energy
            energy_[i] += alpha_energy * (y * y - energy_[i]);

            weightedSum  += energy_[i] * g_[i] * barkZ_[i];
            totalEnergy  += energy_[i];
        }

        sharpness = (totalEnergy > 1e-20)
                    ? static_cast<float> (0.11 * weightedSum / totalEnergy)
                    : 0.0f;

        return sharpness;
    }

    float getSharpness() const noexcept { return sharpness; }

private:
    static constexpr double pi = 3.14159265358979323846;

    double fs           = 44100.0;
    double alpha_energy = 0.0;

    double barkZ_[kNumBands] {};
    double g_    [kNumBands] {};
    double b0_   [kNumBands] {}, b1_[kNumBands] {}, b2_[kNumBands] {};
    double a1_   [kNumBands] {}, a2_[kNumBands] {};
    double s1_   [kNumBands] {}, s2_[kNumBands] {};
    double energy_[kNumBands] {};

    float sharpness = 0.0f;
};
