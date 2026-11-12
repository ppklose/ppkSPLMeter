#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <type_traits>

namespace vib
{
//==============================================================================
// Axis slots of the vibration readout. A uniaxial sensor uses slot X.
enum Axis : int { AxisX = 0, AxisY = 1, AxisZ = 2, AxisSum = 3, kNumAxisSlots = 4 };

struct AxisValues
{
    float rms     = 0.0f;   // running RMS (exponential, time constant τ)
    float peak    = 0.0f;   // max |x| in the last snapshot interval
    float peakMax = 0.0f;   // max |x| since the last reset
    float p2p     = 0.0f;   // peak-to-peak in the last snapshot interval
};

// Published every snapshot interval (125 ms). Units follow the quantity:
// acceleration m/s², velocity m/s, displacement m.
struct VibrationSnapshot
{
    bool       valid     = false;   // an accelerometer is configured and running
    bool       uniaxial  = false;
    int        axisMask  = 0;       // bit per present slot (X/Y/Z); Σ present if > 1 axis
    int        quantity  = 0;       // vib::Quantity
    int        weighting[3] {};     // vib::Weighting per X/Y/Z
    double     seconds   = 0.0;     // metered time since the last reset
    AxisValues axis[kNumAxisSlots];
};
static_assert (std::is_trivially_copyable_v<VibrationSnapshot>);

//==============================================================================
// Single-writer seqlock for trivially copyable data (audio thread writes,
// GUI thread reads; neither blocks).
template <typename T>
class SeqLock
{
public:
    void write (const T& v) noexcept
    {
        const unsigned s = seq_.load (std::memory_order_relaxed);
        seq_.store (s + 1, std::memory_order_relaxed);   // odd: write in progress
        std::atomic_thread_fence (std::memory_order_release);
        copy (data_, v);
        std::atomic_thread_fence (std::memory_order_release);
        seq_.store (s + 2, std::memory_order_release);
    }

    // Returns false only if the writer kept interfering (practically never)
    bool read (T& out) const noexcept
    {
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const unsigned s1 = seq_.load (std::memory_order_acquire);
            if (s1 & 1u) continue;
            copy (out, data_);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq_.load (std::memory_order_relaxed) == s1)
                return true;
        }
        return false;
    }

private:
    // Volatile byte copy: the compiler may not cache or reorder it across the fences
    static void copy (T& dst, const T& src) noexcept
    {
        auto* d = reinterpret_cast<volatile unsigned char*> (&dst);
        auto* s = reinterpret_cast<const volatile unsigned char*> (&src);
        for (size_t i = 0; i < sizeof (T); ++i) d[i] = s[i];
    }

    T                     data_ {};
    std::atomic<unsigned> seq_ { 0 };
};

//==============================================================================
// Detectors of one axis (or of the vector sum), fed with decimated samples.
class AxisDetector
{
public:
    void prepare (double fs, double tauSec) noexcept
    {
        alpha_ = 1.0 - std::exp (-1.0 / (std::max (1.0e-3, tauSec) * fs));
        reset();
    }

    void reset() noexcept
    {
        meanSquare_ = 0.0;
        peakMax_ = 0.0;
        startInterval();
    }

    // x: signed sample (axes); for the vector sum pass the instantaneous magnitude
    void process (double x) noexcept
    {
        meanSquare_ += alpha_ * (x * x - meanSquare_);
        const double a = std::fabs (x);
        peak_    = std::max (peak_, a);
        peakMax_ = std::max (peakMax_, a);
        min_     = std::min (min_, x);
        max_     = std::max (max_, x);
    }

    // Running mean square — the vector sum combines these
    double meanSquare() const noexcept { return meanSquare_; }

    AxisValues values() const noexcept
    {
        AxisValues v;
        v.rms     = (float) std::sqrt (meanSquare_);
        v.peak    = (float) peak_;
        v.peakMax = (float) peakMax_;
        v.p2p     = (max_ >= min_) ? (float) (max_ - min_) : 0.0f;
        return v;
    }

    void startInterval() noexcept
    {
        peak_ = 0.0;
        min_  =  1.0e300;
        max_  = -1.0e300;
    }

private:
    double alpha_ = 0.0, meanSquare_ = 0.0;
    double peak_ = 0.0, peakMax_ = 0.0, min_ = 1.0e300, max_ = -1.0e300;
};
} // namespace vib
