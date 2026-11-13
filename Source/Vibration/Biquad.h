#pragma once
#include <cmath>
#include <complex>

namespace vib
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Double-precision biquad (transposed direct form II).
// The vibration path needs double precision: weighting poles down to 0.08 Hz sit
// extremely close to z = 1 even at the decimated sample rate.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() noexcept { z1 = z2 = 0.0; }

    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // Complex frequency response at f (Hz) for sample rate fs
    std::complex<double> response (double f, double fs) const
    {
        const auto z1c = std::polar (1.0, -2.0 * kPi * f / fs);   // z^-1
        const auto z2c = z1c * z1c;
        return (b0 + b1 * z1c + b2 * z2c) / (1.0 + a1 * z1c + a2 * z2c);
    }

    // Analog section H(s) = (B0 + B1 s + B2 s²) / (A0 + A1 s + A2 s²) → digital via the
    // bilinear transform, prewarped so the response is exact at fWarp (Hz).
    static Biquad fromAnalog (double B0, double B1, double B2,
                              double A0, double A1, double A2,
                              double fs, double fWarp)
    {
        double K = 2.0 * fs;
        if (fWarp > 0.0 && fWarp < 0.5 * fs)
        {
            const double w = 2.0 * kPi * fWarp;
            K = w / std::tan (w / (2.0 * fs));
        }
        const double K2 = K * K;
        const double nb0 = B0 + B1 * K + B2 * K2;
        const double nb1 = 2.0 * B0 - 2.0 * B2 * K2;
        const double nb2 = B0 - B1 * K + B2 * K2;
        const double na0 = A0 + A1 * K + A2 * K2;
        const double na1 = 2.0 * A0 - 2.0 * A2 * K2;
        const double na2 = A0 - A1 * K + A2 * K2;

        Biquad q;
        q.b0 = nb0 / na0;  q.b1 = nb1 / na0;  q.b2 = nb2 / na0;
        q.a1 = na1 / na0;  q.a2 = na2 / na0;
        return q;
    }

    // 2nd-order high-pass, H(s) = s² / (s² + ω/Q s + ω²)
    static Biquad highPass (double fc, double Q, double fs)
    {
        const double w = 2.0 * kPi * fc;
        return fromAnalog (0.0, 0.0, 1.0 / (w * w), 1.0, 1.0 / (w * Q), 1.0 / (w * w), fs, fc);
    }

    // 2nd-order low-pass, H(s) = ω² / (s² + ω/Q s + ω²)
    static Biquad lowPass (double fc, double Q, double fs)
    {
        const double w = 2.0 * kPi * fc;
        return fromAnalog (1.0, 0.0, 0.0, 1.0, 1.0 / (w * Q), 1.0 / (w * w), fs, fc);
    }
};

// Fixed cascade of biquads (no allocation)
template <int MaxSections>
struct BiquadCascade
{
    Biquad sections[MaxSections];
    int    numSections = 0;
    double gain        = 1.0;

    void clear() noexcept { numSections = 0; gain = 1.0; }
    void add (const Biquad& q) noexcept { if (numSections < MaxSections) sections[numSections++] = q; }
    void reset() noexcept { for (int i = 0; i < numSections; ++i) sections[i].reset(); }

    double process (double x) noexcept
    {
        x *= gain;
        for (int i = 0; i < numSections; ++i)
            x = sections[i].process (x);
        return x;
    }

    std::complex<double> response (double f, double fs) const
    {
        std::complex<double> h (gain, 0.0);
        for (int i = 0; i < numSections; ++i)
            h *= sections[i].response (f, fs);
        return h;
    }
};
} // namespace vib
