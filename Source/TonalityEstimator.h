#pragma once
#include <cmath>
#include <algorithm>

/**
    Real-time tonality estimator using the Spectral Flatness Measure (SFM)
    on an 8-band critical-band filterbank (same centres as SharpnessEstimator).

    Tonality describes how tone-like (periodic) a sound is versus noise-like
    (broadband). It is derived from:

        SFM = geometric_mean(band_energies) / arithmetic_mean(band_energies)

        Tonality (%) = (1 − SFM) × 100

    Output range: 0–100 %.
      100 % → pure tone     (one band dominates → geometric mean ≈ 0)
        0 % → white noise   (flat spectrum     → geometric mean ≈ arithmetic mean)
*/
class TonalityEstimator
{
public:
    static constexpr int kNumBands = 8;

    void prepare (double sampleRate) noexcept
    {
        static constexpr double centerHz[kNumBands] = { 100, 300,  630, 1000, 2000, 4000, 7000, 10000 };
        static constexpr double bandQ   [kNumBands] = { 1.0, 2.8,  5.0,  6.2,  6.6,  5.8,  4.5,   4.0 };

        for (int i = 0; i < kNumBands; ++i)
        {
            const double fc  = std::min (centerHz[i], sampleRate * 0.45);
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

        // Energy smoother: ~8 Hz (same time constant as SharpnessEstimator)
        alpha_energy = 1.0 - std::exp (-2.0 * pi * 8.0 / sampleRate);

        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kNumBands; ++i)
            s1_[i] = s2_[i] = energy_[i] = 0.0;
        tonality_ = 0.0f;
    }

    float processSample (float x) noexcept
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            const double y = b0_[i] * x + s1_[i];
            s1_[i] = b1_[i] * x - a1_[i] * y + s2_[i];
            s2_[i] = b2_[i] * x - a2_[i] * y;
            energy_[i] += alpha_energy * (y * y - energy_[i]);
        }

        // Arithmetic mean
        double arith = 0.0;
        for (int i = 0; i < kNumBands; ++i)
            arith += energy_[i];
        arith /= kNumBands;

        // Geometric mean via log-sum (safe: add small floor)
        double logSum = 0.0;
        for (int i = 0; i < kNumBands; ++i)
            logSum += std::log (energy_[i] + 1e-20);
        const double geom = std::exp (logSum / kNumBands);

        const double sfm = (arith > 1e-20) ? (geom / arith) : 1.0;
        tonality_ = static_cast<float> (std::max (0.0, std::min (100.0, (1.0 - sfm) * 100.0)));
        return tonality_;
    }

    float getTonality() const noexcept { return tonality_; }

private:
    static constexpr double pi = 3.14159265358979323846;

    double alpha_energy = 0.0;
    double b0_[kNumBands] {}, b1_[kNumBands] {}, b2_[kNumBands] {};
    double a1_[kNumBands] {}, a2_[kNumBands] {};
    double s1_[kNumBands] {}, s2_[kNumBands] {};
    double energy_[kNumBands] {};

    float tonality_ = 0.0f;
};
