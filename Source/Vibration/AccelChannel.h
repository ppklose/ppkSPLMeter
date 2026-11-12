#pragma once
#include "Biquad.h"
#include "Calibration.h"
#include "Decimator.h"
#include "Weighting.h"

namespace vib
{
//==============================================================================
// Signal chain of one accelerometer axis (vibration spec §5.1):
//
//   raw (FS) ─► ×cal (m/s²) ─► HP (2nd-order Butterworth)
//            ├─► full-rate output: acceleration, m/s²  (FFT, structure-borne)
//            └─► quantity (a / v / d) ─► decimate ÷D (fs_d ≥ 8 kHz) ─► weighting
//
// Integration uses s / (s² + ω/Q·s + ω²) at the high-pass corner: an integrator
// above the corner that has no pole at DC, so offsets cannot make it drift. It runs
// at the full rate, where the bilinear integrator is accurate to 0.14 % at 1 kHz
// (at 12 kHz it would be 2.3 % low — too much for ISO 20816 up to 1000 Hz).
// Units: acceleration m/s², velocity m/s, displacement m.
enum class Quantity : int { Acceleration = 0, Velocity = 1, Displacement = 2 };

struct AccelChannelConfig
{
    double    calDb      = 140.0;
    double    highPassHz = 1.0;
    Quantity  quantity   = Quantity::Acceleration;
    Weighting weighting  = Weighting::None;
    double    bandLowHz  = 0.0;     // Weighting::BandLimit only
    double    bandHighHz = kInf;
};

class AccelChannel
{
public:
    static constexpr double kMinDecimatedRate = 8000.0;

    void prepare (double fs, const AccelChannelConfig& cfg)
    {
        fs_    = fs;
        cfg_   = cfg;
        scale_ = calibration::scale (cfg.calDb);

        const double Q = 1.0 / std::sqrt (2.0);
        highPass_ = Biquad::highPass (cfg.highPassHz, Q, fs);

        decimator_.prepare (fs, kMinDecimatedRate);
        const double fsd = decimator_.outputRate (fs);

        // Integrators at the HP corner (Q = 1/√2 keeps the corner Butterworth-like)
        const double w = 2.0 * kPi * cfg.highPassHz;
        const auto integ = Biquad::fromAnalog (0.0, 1.0, 0.0, w * w, w / Q, 1.0, fs, 0.0);
        numIntegrators_ = static_cast<int> (cfg.quantity);
        for (auto& i : integrators_) i = integ;

        weighting_.prepare (cfg.weighting, fsd, cfg.bandLowHz, cfg.bandHighHz);
        reset();
    }

    void reset() noexcept
    {
        highPass_.reset();
        decimator_.reset();
        for (auto& i : integrators_) i.reset();
        weighting_.reset();
    }

    // Processes one raw input sample. fullRate receives the calibrated, high-passed
    // acceleration; returns true when a decimated, weighted sample is written to out.
    bool process (float raw, double& fullRate, double& out) noexcept
    {
        fullRate = highPass_.process (static_cast<double> (raw) * scale_);
        double q = fullRate;
        for (int i = 0; i < numIntegrators_; ++i)
            q = integrators_[i].process (q);
        double d;
        if (! decimator_.push (q, d))
            return false;
        out = weighting_.process (d);
        return true;
    }

    double decimatedRate() const noexcept   { return decimator_.outputRate (fs_); }
    int    decimationFactor() const noexcept { return decimator_.factor(); }
    const AccelChannelConfig& config() const noexcept { return cfg_; }

    // Magnitude of quantity + weighting at f (Hz), relative to acceleration input —
    // excludes the channel high-pass and decimator (used by tests and the UI)
    double pathMagnitude (double f) const
    {
        double m = weighting_.magnitude (f);
        for (int i = 0; i < numIntegrators_; ++i)
            m *= std::abs (integrators_[i].response (f, fs_));
        return m;
    }

private:
    AccelChannelConfig cfg_;
    double             fs_    = 48000.0;
    double             scale_ = 1.0;
    Biquad             highPass_;
    Decimator          decimator_;
    Biquad             integrators_[2];
    int                numIntegrators_ = 0;
    WeightingFilter    weighting_;
};
} // namespace vib
