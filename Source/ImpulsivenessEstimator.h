#pragma once
#include <cmath>
#include <algorithm>

/**
    Real-time impulsiveness estimator based on the short-term crest factor.

    Impulsiveness measures how transient-dominated a signal is, as opposed to
    sustained or quasi-stationary. It is computed as:

        CF (dB) = 20 * log10( peak / RMS )

    where:
      - peak     : short-term absolute peak with slow exponential decay (~400 ms)
      - RMS      : exponentially smoothed RMS (IEC FAST, τ = 125 ms)

    Output range: 0–40 dB.
      ~ 3 dB   → pure sine wave (minimum impulsiveness)
      ~ 20 dB  → moderately impulsive
      ~ 40 dB  → highly impulsive (sharp percussive transients)
*/
class ImpulsivenessEstimator
{
public:
    void prepare (double sampleRate) noexcept
    {
        alpha_peak = std::exp (-1.0 / (0.4 * sampleRate));
        alpha_rms  = 1.0 - std::exp (-1.0 / (0.125 * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        peak_      = 0.0;
        rmsSmooth_ = 0.0;
        crestDB_   = 0.0f;
    }

    float processSample (float x) noexcept
    {
        const double absX = std::fabs (static_cast<double> (x));

        peak_      = std::max (absX, peak_ * alpha_peak);
        rmsSmooth_ += alpha_rms * (absX * absX - rmsSmooth_);

        const double rms = std::sqrt (rmsSmooth_);
        if (rms > 1e-10 && peak_ > 1e-10)
            crestDB_ = static_cast<float> (20.0 * std::log10 (peak_ / rms));
        else
            crestDB_ = 0.0f;

        crestDB_ = std::max (0.0f, std::min (40.0f, crestDB_));
        return crestDB_;
    }

    float getImpulsiveness() const noexcept { return crestDB_; }

private:
    double alpha_peak  = 0.0;
    double alpha_rms   = 0.0;
    double peak_       = 0.0;
    double rmsSmooth_  = 0.0;
    float  crestDB_    = 0.0f;
};
