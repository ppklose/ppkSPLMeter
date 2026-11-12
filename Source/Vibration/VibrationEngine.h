#pragma once
#include "AccelChannel.h"
#include "Detectors.h"

namespace vib
{
//==============================================================================
// The single value a measurement profile is about (vibration spec §6), computed
// from the snapshot. Values are persisted — only append new entries.
enum class MainValue : int
{
    None = 0,
    MaxAxisRms,       // largest axis running RMS (machinery v_rms, KB_F)
    MaxAxisKRms,      // largest k-weighted axis RMS (ISO 2631-1 health: 1.4·a_wx, 1.4·a_wy, a_wz)
    SumRms,           // vector sum Σ (a_v, a_hv); the axis for a single axis
    MaxAxisPeakMax    // largest |x| since reset over the axes (PPV)
};

inline float computeMainValue (MainValue m, const VibrationSnapshot& s, const double (&k)[3]) noexcept
{
    float best = 0.0f;
    int axes = 0;
    for (int a = 0; a < 3; ++a)
    {
        if (! (s.axisMask & (1 << a))) continue;
        ++axes;
        switch (m)
        {
            case MainValue::MaxAxisRms:     best = std::max (best, s.axis[a].rms); break;
            case MainValue::MaxAxisKRms:    best = std::max (best, (float) k[a] * s.axis[a].rms); break;
            case MainValue::SumRms:         best = std::max (best, s.axis[a].rms); break;
            case MainValue::MaxAxisPeakMax: best = std::max (best, s.axis[a].peakMax); break;
            case MainValue::None:           break;
        }
    }
    if (m == MainValue::SumRms && axes > 1 && ! s.uniaxial)
        return s.axis[AxisSum].rms;
    return best;
}

//==============================================================================
// Settings shared by all axes of the accelerometer sensor (spec §3.2).
// Persisted in UIState/Accel; per-input calibration lives in UIState/Inputs.
struct VibrationSettings
{
    double    highPassHz  = 1.0;
    Quantity  quantity    = Quantity::Acceleration;
    Weighting weighting[3] { Weighting::None, Weighting::None, Weighting::None };   // X, Y, Z (uniaxial: X)
    double    bandLowHz   = 10.0;      // Weighting::BandLimit
    double    bandHighHz  = 1000.0;
    double    tauSec      = 1.0;       // running RMS time constant
    double    k[3]        { 1.0, 1.0, 1.0 };   // vector-sum multiplying factors
    MainValue main        = MainValue::None;
};

// Which input feeds which axis (-1 = absent) and its calibration
struct AxisInput
{
    int    inputChannel = -1;
    double calDb        = 140.0;
};

//==============================================================================
// One accelerometer sensor (1–3 axes): signal chain + detectors + vector sum.
// Built on the message thread, then handed to the audio thread as a whole.
class VibrationEngine
{
public:
    static constexpr double kSnapshotInterval = 0.125;   // s, like the SPL log

    VibrationEngine (double fs, const VibrationSettings& settings,
                     const AxisInput (&axes)[3], bool uniaxial)
        : settings_ (settings), uniaxial_ (uniaxial)
    {
        for (int a = 0; a < 3; ++a)
        {
            input_[a] = axes[a].inputChannel;
            if (input_[a] < 0) continue;

            AccelChannelConfig cfg;
            cfg.calDb      = axes[a].calDb;
            cfg.highPassHz = settings.highPassHz;
            cfg.quantity   = settings.quantity;
            cfg.weighting  = settings.weighting[a];
            cfg.bandLowHz  = settings.bandLowHz;
            cfg.bandHighHz = settings.bandHighHz;
            channel_[a].prepare (fs, cfg);
            fsd_ = channel_[a].decimatedRate();
            axisMask_ |= 1 << a;
            ++numAxes_;
        }
        for (auto& d : detector_)
            d.prepare (fsd_, settings.tauSec);
        intervalSamples_ = std::max (1, (int) std::lround (kSnapshotInterval * fsd_));
    }

    int  inputChannel (int axis) const noexcept { return input_[axis]; }
    bool hasAxes() const noexcept               { return numAxes_ > 0; }

    void requestReset() noexcept { resetPending_ = true; }

    // in[axis] = input samples for that axis (nullptr if absent). When metering is
    // false (pause / DAW stopped) the filters keep running but detectors hold.
    // Returns true when a new snapshot was written to out.
    bool process (const float* const in[3], int numSamples, bool metering, VibrationSnapshot& out) noexcept
    {
        if (resetPending_)
        {
            for (auto& d : detector_) d.reset();
            meteredSamples_ = 0;
            resetPending_ = false;
        }

        bool published = false;
        for (int s = 0; s < numSamples; ++s)
        {
            double decimated[3] = { 0.0, 0.0, 0.0 };
            bool   produced = false;
            for (int a = 0; a < 3; ++a)
            {
                if (input_[a] < 0) continue;
                double full;
                // All axes share the decimator phase, so they produce samples together
                produced = channel_[a].process (in[a] != nullptr ? in[a][s] : 0.0f, full, decimated[a]);
            }
            if (! produced || ! metering)
                continue;

            double sumSq = 0.0;
            for (int a = 0; a < 3; ++a)
            {
                if (input_[a] < 0) continue;
                detector_[a].process (decimated[a]);
                const double kx = settings_.k[a] * decimated[a];
                sumSq += kx * kx;
            }
            detector_[AxisSum].process (std::sqrt (sumSq));
            ++meteredSamples_;

            if (++intervalCount_ >= intervalSamples_)
            {
                intervalCount_ = 0;
                fillSnapshot (out);
                for (auto& d : detector_) d.startInterval();
                published = true;
            }
        }
        return published;
    }

private:
    void fillSnapshot (VibrationSnapshot& out) const noexcept
    {
        out.valid     = true;
        out.uniaxial  = uniaxial_;
        out.axisMask  = axisMask_;
        out.quantity  = static_cast<int> (settings_.quantity);
        out.seconds   = meteredSamples_ / fsd_;
        double sumMs  = 0.0;
        for (int a = 0; a < 3; ++a)
        {
            out.weighting[a] = static_cast<int> (settings_.weighting[a]);
            out.axis[a] = input_[a] >= 0 ? detector_[a].values() : AxisValues {};
            if (input_[a] >= 0)
                sumMs += settings_.k[a] * settings_.k[a] * detector_[a].meanSquare();
        }
        // Σ: RMS from the k-weighted axis mean squares (ISO 2631-1 a_v); peaks
        // from the sample-wise vector magnitude
        out.axis[AxisSum]     = detector_[AxisSum].values();
        out.axis[AxisSum].rms = (float) std::sqrt (sumMs);
        out.axis[AxisSum].p2p = 0.0f;   // not defined for a magnitude
        out.mainKind = static_cast<int> (settings_.main);
        out.main     = computeMainValue (settings_.main, out, settings_.k);
    }

    VibrationSettings settings_;
    bool              uniaxial_ = false;
    AccelChannel      channel_[3];
    int               input_[3] { -1, -1, -1 };
    AxisDetector      detector_[kNumAxisSlots];
    int               axisMask_ = 0, numAxes_ = 0;
    double            fsd_ = 12000.0;
    int               intervalSamples_ = 1500, intervalCount_ = 0;
    long long         meteredSamples_ = 0;
    bool              resetPending_ = false;
};
} // namespace vib
