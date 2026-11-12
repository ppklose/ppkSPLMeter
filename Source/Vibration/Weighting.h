#pragma once
#include "Biquad.h"
#include <limits>

namespace vib
{
//==============================================================================
// Frequency weightings (vibration spec §5.2).
//
// ISO 8041 weightings are the product of band-limiting (2nd-order Butterworth HP at
// f1 and LP at f2), an a–v transition, an upward step and a gain K. Parameters were
// cross-checked against public sources (spec §14 [V], band limits [I]).
// Values are persisted — only append new entries.
enum class Weighting : int
{
    None = 0,     // unweighted (only the channel high-pass applies)
    BandLimit,    // user band: 2nd-order Butterworth HP + LP
    Wb, Wc, Wd, We, Wf, Wh, Wj, Wk, Wm,
    KB            // DIN 4150-2 / DIN 45669-1, applied to velocity
};

constexpr double kInf = std::numeric_limits<double>::infinity();

struct WeightingParams
{
    double f1, f2;              // band-limiting HP / LP (Hz); Q1 = Q2 = 1/√2
    double f3, f4, Q4;          // a–v transition (kInf = section absent)
    double f5, Q5, f6, Q6;      // upward step     (kInf = section absent)
    double K;                   // gain
};

inline bool isIsoWeighting (Weighting w) noexcept
{
    return static_cast<int> (w) >= static_cast<int> (Weighting::Wb)
        && static_cast<int> (w) <= static_cast<int> (Weighting::Wm);
}

inline WeightingParams isoParams (Weighting w) noexcept
{
    const double s28 = 1.0 / (0.028 * 2.0 * kPi);   // Wm f3 = f4 ≈ 5.684 Hz
    const double h16 = 100.0 / (2.0 * kPi);          // Wh f3 = f4 ≈ 15.915 Hz
    switch (w)
    {
        //                         f1                     f2                     f3    f4     Q4    f5      Q5    f6    Q6    K
        case Weighting::Wb: return { 0.4,                 100.0,                 16.0, 16.0,  0.55, 2.5,    0.90, 4.0,  0.95, 1.024 };
        case Weighting::Wc: return { 0.4,                 100.0,                 8.0,  8.0,   0.63, kInf,   1.0,  kInf, 1.0,  1.0 };
        case Weighting::Wd: return { 0.4,                 100.0,                 2.0,  2.0,   0.63, kInf,   1.0,  kInf, 1.0,  1.0 };
        case Weighting::We: return { 0.4,                 100.0,                 1.0,  1.0,   0.63, kInf,   1.0,  kInf, 1.0,  1.0 };
        case Weighting::Wf: return { 0.08,                0.63,                  kInf, 0.25,  0.86, 0.0625, 0.80, 0.10, 0.80, 1.0 };
        case Weighting::Wh: return { std::pow (10.0, 0.8), std::pow (10.0, 3.1), h16,  h16,   0.64, kInf,   1.0,  kInf, 1.0,  1.0 };
        case Weighting::Wj: return { 0.4,                 100.0,                 kInf, kInf,  1.0,  3.75,   0.91, 5.32, 0.91, 1.0 };
        case Weighting::Wk: return { 0.4,                 100.0,                 12.5, 12.5,  0.63, 2.37,   0.91, 3.35, 0.91, 1.0 };
        case Weighting::Wm: return { std::pow (10.0, -0.1), 100.0,               s28,  s28,   0.50, kInf,   1.0,  kInf, 1.0,  1.0 };
        case Weighting::None:
        case Weighting::BandLimit:
        case Weighting::KB:
        default:            return { 0.0, kInf, kInf, kInf, 1.0, kInf, 1.0, kInf, 1.0, 1.0 };
    }
}

// KB weighting (DIN 4150-2): first-order high-pass at f0 on velocity,
// |H| = 1/√(1 + (f0/f)²), band-limited to 1–80 Hz.
// ⚠ f0 and the band-limit filter type are not yet cross-checked (spec §13).
constexpr double kKbF0      = 5.6;
constexpr double kKbBandLow = 1.0;
constexpr double kKbBandHigh = 80.0;

//==============================================================================
// Exact analog magnitude of a weighting — the reference the digital filter must
// reproduce (used by the unit tests). bandLow/bandHigh only for BandLimit.
inline double analogMagnitude (Weighting w, double f, double bandLow = 0.0, double bandHigh = kInf)
{
    const std::complex<double> s (0.0, 2.0 * kPi * f);
    auto hp2 = [&] (double fc) { const double wc = 2.0 * kPi * fc;
                                 return (s * s) / (s * s + s * wc * std::sqrt (2.0) + wc * wc); };
    auto lp2 = [&] (double fc) { const double wc = 2.0 * kPi * fc;
                                 return (wc * wc) / (s * s + s * wc * std::sqrt (2.0) + wc * wc); };

    switch (w)
    {
        case Weighting::None:      return 1.0;
        case Weighting::BandLimit:
        {
            std::complex<double> h (1.0, 0.0);
            if (bandLow > 0.0)       h *= hp2 (bandLow);
            if (std::isfinite (bandHigh)) h *= lp2 (bandHigh);
            return std::abs (h);
        }
        case Weighting::KB:
        {
            const double w0 = 2.0 * kPi * kKbF0;
            return std::abs (s / (s + w0) * hp2 (kKbBandLow) * lp2 (kKbBandHigh));
        }
        default: break;
    }

    const auto p = isoParams (w);
    std::complex<double> h = hp2 (p.f1) * lp2 (p.f2) * p.K;
    if (std::isfinite (p.f4))
    {
        const double w4 = 2.0 * kPi * p.f4;
        const std::complex<double> num = std::isfinite (p.f3) ? 1.0 + s / (2.0 * kPi * p.f3)
                                                              : std::complex<double> (1.0, 0.0);
        h *= num / (1.0 + s / (w4 * p.Q4) + s * s / (w4 * w4));
    }
    if (std::isfinite (p.f5))
    {
        const double w5 = 2.0 * kPi * p.f5, w6 = 2.0 * kPi * p.f6;
        h *= (1.0 + s / (w5 * p.Q5) + s * s / (w5 * w5))
           / (1.0 + s / (w6 * p.Q6) + s * s / (w6 * w6))
           * ((p.f5 / p.f6) * (p.f5 / p.f6));
    }
    return std::abs (h);
}

//==============================================================================
// Digital weighting filter: each analog section is mapped with the bilinear
// transform, prewarped at its own characteristic frequency.
class WeightingFilter
{
public:
    void prepare (Weighting w, double fs, double bandLow = 0.0, double bandHigh = kInf)
    {
        weighting_ = w;
        fs_ = fs;
        cascade_.clear();
        const double Q = 1.0 / std::sqrt (2.0);
        auto inBand = [fs] (double f) { return f > 0.0 && std::isfinite (f) && f < 0.45 * fs; };

        switch (w)
        {
            case Weighting::None:
                break;

            case Weighting::BandLimit:
                if (inBand (bandLow))  cascade_.add (Biquad::highPass (bandLow, Q, fs));
                if (inBand (bandHigh)) cascade_.add (Biquad::lowPass (bandHigh, Q, fs));
                break;

            case Weighting::KB:
            {
                const double w0 = 2.0 * kPi * kKbF0;   // s / (s + ω0)
                cascade_.add (Biquad::fromAnalog (0.0, 1.0 / w0, 0.0, 1.0, 1.0 / w0, 0.0, fs, kKbF0));
                cascade_.add (Biquad::highPass (kKbBandLow, Q, fs));
                cascade_.add (Biquad::lowPass (kKbBandHigh, Q, fs));
                break;
            }

            default:
            {
                const auto p = isoParams (w);
                cascade_.add (Biquad::highPass (p.f1, Q, fs));
                if (inBand (p.f2)) cascade_.add (Biquad::lowPass (p.f2, Q, fs));
                if (std::isfinite (p.f4))
                {
                    const double w4 = 2.0 * kPi * p.f4;
                    const double B1 = std::isfinite (p.f3) ? 1.0 / (2.0 * kPi * p.f3) : 0.0;
                    cascade_.add (Biquad::fromAnalog (1.0, B1, 0.0,
                                                      1.0, 1.0 / (w4 * p.Q4), 1.0 / (w4 * w4), fs, p.f4));
                }
                if (std::isfinite (p.f5))
                {
                    const double w5 = 2.0 * kPi * p.f5, w6 = 2.0 * kPi * p.f6;
                    const double g  = (p.f5 / p.f6) * (p.f5 / p.f6);
                    cascade_.add (Biquad::fromAnalog (g, g / (w5 * p.Q5), g / (w5 * w5),
                                                      1.0, 1.0 / (w6 * p.Q6), 1.0 / (w6 * w6),
                                                      fs, std::sqrt (p.f5 * p.f6)));
                }
                cascade_.gain = p.K;
                break;
            }
        }
        cascade_.reset();
    }

    void   reset() noexcept              { cascade_.reset(); }
    double process (double x) noexcept   { return cascade_.process (x); }
    double magnitude (double f) const    { return std::abs (cascade_.response (f, fs_)); }
    Weighting weighting() const noexcept { return weighting_; }

private:
    BiquadCascade<4> cascade_;
    Weighting        weighting_ = Weighting::None;
    double           fs_ = 48000.0;
};
} // namespace vib
