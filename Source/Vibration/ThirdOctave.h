#pragma once
#include "Biquad.h"
#include "Detectors.h"
#include <algorithm>
#include <cmath>

namespace vib
{
//==============================================================================
// 1/3-octave band spectrum of one axis (vibration spec §9.2: linear average over
// the measurement). Base-10 bands (IEC 61260-1): exact centre 10^(i/10) Hz, so
// band 0 is 1 Hz and band 31 is 1250 Hz (nominal). Bands go up to fs/8.
//
// Each band is a 6th-order Butterworth band-pass (3 biquads, band edges
// prewarped). The bank is multirate: every octave down the signal is low-passed
// (6th-order Butterworth at 0.15·rate) and decimated by 2, so each band runs at
// 8–16 times its centre frequency and the 1 Hz band stays well conditioned.
class ThirdOctaveBank
{
public:
    static constexpr int kMaxBands = kNumThirdOctaveBands;
    static constexpr int kMaxStages = 14;

    static double exactCentre (int band) noexcept { return std::pow (10.0, band / 10.0); }

    // Nominal centre frequency label (IEC 61260-1 Annex E)
    static const char* nominalLabel (int band) noexcept
    {
        static const char* names[kMaxBands] = {
            "1", "1.25", "1.6", "2", "2.5", "3.15", "4", "5", "6.3", "8",
            "10", "12.5", "16", "20", "25", "31.5", "40", "50", "63", "80",
            "100", "125", "160", "200", "250", "315", "400", "500", "630", "800",
            "1000", "1250" };
        return (band >= 0 && band < kMaxBands) ? names[band] : "";
    }

    // 6th-order Butterworth band-pass for edges f1 < f2 at fs: LP→BP transform of
    // the 3rd-order prototype, unity gain at the centre
    static BiquadCascade<3> bandPass (double f1, double f2, double fs)
    {
        const double K  = 2.0 * fs;
        const double w1 = K * std::tan (kPi * f1 / fs);
        const double w2 = K * std::tan (kPi * f2 / fs);
        const double w0sq = w1 * w2, B = w2 - w1;

        BiquadCascade<3> c;
        c.add (Biquad::fromAnalog (0.0, B, 0.0, w0sq, B, 1.0, fs, 0.0));   // prototype pole −1
        const std::complex<double> p (-0.5, std::sqrt (3.0) / 2.0);       // pole pair −½ ± j√3/2
        const auto disc = std::sqrt (p * p * B * B - 4.0 * w0sq);
        for (const auto& s : { (p * B + disc) * 0.5, (p * B - disc) * 0.5 })
            c.add (Biquad::fromAnalog (0.0, B, 0.0, std::norm (s), -2.0 * s.real(), 1.0, fs, 0.0));

        const double f0 = fs / kPi * std::atan (std::sqrt (w0sq) / K);    // digital centre
        c.gain = 1.0 / std::abs (c.response (f0, fs));
        return c;
    }

    void prepare (double fs)
    {
        fs_ = fs;
        numBands_ = 0;
        while (numBands_ < kMaxBands && exactCentre (numBands_) * 8.0 <= fs)
            ++numBands_;

        numStages_ = 1;
        for (int b = 0; b < numBands_; ++b)
        {
            const double fc = exactCentre (b);
            int s = (int) std::floor (std::log2 (fs / (8.0 * fc)));
            s = std::clamp (s, 0, kMaxStages - 1);
            stage_[b] = s;
            numStages_ = std::max (numStages_, s + 1);
            const double rate = fs / std::pow (2.0, s);
            band_[b] = bandPass (fc * std::pow (10.0, -0.05), fc * std::pow (10.0, 0.05), rate);
        }
        static const double q[3] = { 0.51763809, 0.70710678, 1.93185165 };   // 6th-order Butterworth
        for (int s = 0; s + 1 < numStages_; ++s)
        {
            const double rate = fs / std::pow (2.0, s);
            antiAlias_[s].clear();
            for (double qi : q)
                antiAlias_[s].add (Biquad::lowPass (0.15 * rate, qi, rate));
        }
        resetFilters();
        resetSums();
    }

    void resetFilters() noexcept
    {
        for (int b = 0; b < numBands_; ++b) band_[b].reset();
        for (int s = 0; s < numStages_; ++s) { antiAlias_[s].reset(); phase_[s] = false; }
    }

    void resetSums() noexcept
    {
        std::fill (std::begin (sumSq_), std::end (sumSq_), 0.0);
        std::fill (std::begin (count_), std::end (count_), 0LL);
    }

    // One sample at fs; accumulate == false keeps the filters running only
    void process (double x, bool accumulate) noexcept
    {
        double v = x;
        for (int s = 0; s < numStages_; ++s)
        {
            for (int b = 0; b < numBands_; ++b)
                if (stage_[b] == s)
                {
                    const double y = band_[b].process (v);
                    if (accumulate) sumSq_[b] += y * y;
                }
            if (accumulate) ++count_[s];
            if (s + 1 == numStages_)
                break;
            const double lp = antiAlias_[s].process (v);
            phase_[s] = ! phase_[s];
            if (! phase_[s])
                break;   // decimate by 2: every second sample goes down one octave
            v = lp;
        }
    }

    int numBands() const noexcept { return numBands_; }

    // Linear RMS of the band over the accumulated time (0 before any sample)
    double bandRms (int band) const noexcept
    {
        if (band < 0 || band >= numBands_) return 0.0;
        const auto n = count_[stage_[band]];
        return n > 0 ? std::sqrt (sumSq_[band] / (double) n) : 0.0;
    }

private:
    double fs_ = 12000.0;
    int numBands_ = 0, numStages_ = 1;
    int stage_[kMaxBands] {};
    BiquadCascade<3> band_[kMaxBands];
    BiquadCascade<3> antiAlias_[kMaxStages];
    bool phase_[kMaxStages] {};
    double sumSq_[kMaxBands] {};
    long long count_[kMaxStages] {};
};
} // namespace vib
