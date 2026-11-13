#pragma once
#include "Biquad.h"
#include <array>
#include <vector>

namespace vib
{
//==============================================================================
// Decimation by a power of two with a cascade of half-band FIR stages
// (vibration spec §5.1). Each stage: Kaiser-windowed half-band, passband up to
// 0.2·fs_in, stopband from 0.3·fs_in with ≥ 90 dB attenuation.
class HalfBandStage
{
public:
    static constexpr int kTaps = 75;   // 4k+3 → non-zero end taps; −92 dB at Δf = 0.1·fs

    HalfBandStage()
    {
        const int    M    = kTaps - 1;
        const double beta = 9.0;   // Kaiser β; with 75 taps: stopband −92 dB, ripple 0.0002 dB
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum  += term;
            }
            return sum;
        };
        double total = 0.0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double m    = n - M / 2.0;
            const double sinc = (m == 0.0) ? 0.5 : std::sin (kPi * m / 2.0) / (kPi * m);
            const double r    = 2.0 * n / M - 1.0;
            const double win  = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            h_[(size_t) n] = (n != M / 2 && ((n - M / 2) % 2) == 0) ? 0.0 : sinc * win;   // exact zeros
            total += h_[(size_t) n];
        }
        for (auto& v : h_) v /= total;   // unity DC gain
        reset();
    }

    void reset() noexcept { line_.fill (0.0); pos_ = 0; phase_ = 0; }

    // Feeds one input sample; returns true and sets out when an output sample is due
    bool push (double x, double& out) noexcept
    {
        line_[(size_t) pos_] = x;
        line_[(size_t) (pos_ + kTaps)] = x;   // mirrored copy → contiguous window
        pos_ = (pos_ + 1) % kTaps;
        phase_ ^= 1;
        if (phase_ != 0)
            return false;

        const double* w = line_.data() + pos_;   // oldest … newest
        double acc = 0.0;
        for (int n = 0; n < kTaps; ++n)
            acc += h_[(size_t) n] * w[n];
        out = acc;
        return true;
    }

    // Frequency response (normalised frequency f / fs_in)
    double magnitude (double fNorm) const
    {
        std::complex<double> sum (0.0, 0.0);
        for (int n = 0; n < kTaps; ++n)
            sum += h_[(size_t) n] * std::polar (1.0, -2.0 * kPi * fNorm * n);
        return std::abs (sum);
    }

private:
    std::array<double, kTaps>     h_ {};
    std::array<double, 2 * kTaps> line_ {};
    int pos_ = 0, phase_ = 0;
};

class Decimator
{
public:
    static constexpr int kMaxStages = 5;   // up to ÷32 (e.g. 384 kHz → 12 kHz)

    // Chooses the largest power-of-two factor that keeps fs / D ≥ minRate
    void prepare (double fs, double minRate = 8000.0)
    {
        numStages_ = 0;
        while (numStages_ < kMaxStages && fs / (double) (2 << numStages_) >= minRate)
            ++numStages_;
        reset();
    }

    void reset() noexcept { for (auto& s : stages_) s.reset(); }

    int    factor() const noexcept                { return 1 << numStages_; }
    double outputRate (double fs) const noexcept  { return fs / factor(); }

    // Pushes one input sample; returns true when a decimated sample is produced
    bool push (double x, double& out) noexcept
    {
        double v = x;
        for (int i = 0; i < numStages_; ++i)
            if (! stages_[(size_t) i].push (v, v))
                return false;
        out = v;
        return true;
    }

    const HalfBandStage& stage (int i) const { return stages_[(size_t) i]; }
    int numStages() const noexcept           { return numStages_; }

private:
    std::array<HalfBandStage, kMaxStages> stages_;
    int numStages_ = 0;
};
} // namespace vib
