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
    float peakMax = 0.0f;   // max |x| since the last reset (PPV for velocity)
    float p2p     = 0.0f;   // peak-to-peak in the last snapshot interval

    // Accumulated since the last reset (spec §5.3)
    float awRms   = 0.0f;   // linear RMS over the measurement (a_w,rms)
    float mtvv    = 0.0f;   // max of the running RMS with τ = 1 s
    float vdv     = 0.0f;   // (∫ x⁴ dt)^¼, e.g. m/s^1.75
    float crest   = 0.0f;   // peakMax / awRms
    float fDom    = 0.0f;   // dominant frequency at peakMax (Hz; half-period between zero crossings)
    float kbFmax  = 0.0f;   // max of the running RMS with τ = 0.125 s (KB_Fmax)
    float kbFTm   = 0.0f;   // √(mean of the squared 30 s cycle maxima) (KB_FTm; KB_FTr = KB_FTm · √(T_e / T_r))
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
    int        mainKind  = 0;       // vib::MainValue of the profile
    float      main      = 0.0f;    // the profile's main value (SI units)
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
    static constexpr double kMtvvTau    = 1.0;     // ISO 2631-1 MTVV
    static constexpr double kKbTau      = 0.125;   // DIN 4150-2 KB_F(t)
    static constexpr double kKbCycleSec = 30.0;    // DIN 4150-2 cycle (Takt)

    void prepare (double fs, double tauSec) noexcept
    {
        fs_     = fs;
        alpha_  = 1.0 - std::exp (-1.0 / (std::max (1.0e-3, tauSec) * fs));
        alpha1_ = 1.0 - std::exp (-1.0 / (kMtvvTau * fs));
        alphaF_ = 1.0 - std::exp (-1.0 / (kKbTau * fs));
        cycleLen_ = (long long) std::llround (kKbCycleSec * fs);
        reset();
    }

    void reset() noexcept
    {
        meanSquare_ = ms1_ = msF_ = 0.0;
        peakMax_ = 0.0;
        sumSq_ = sum4_ = 0.0;
        count_ = 0;
        mtvv_ = kbFmax_ = 0.0;
        cycleMax_ = cycleSumSq_ = 0.0;
        cycleCount_ = 0;
        cyclePos_ = 0;
        prevX_ = 0.0;
        lastZc_ = -1.0;
        peakZc_ = -1.0;
        awaitingZc_ = false;
        fDom_ = 0.0;
        startInterval();
    }

    // x: signed sample (axes); for the vector sum pass the instantaneous magnitude
    void process (double x) noexcept
    {
        const double x2 = x * x;
        meanSquare_ += alpha_  * (x2 - meanSquare_);
        ms1_        += alpha1_ * (x2 - ms1_);
        msF_        += alphaF_ * (x2 - msF_);
        sumSq_ += x2;
        sum4_  += x2 * x2;
        mtvv_   = std::max (mtvv_, ms1_);
        kbFmax_ = std::max (kbFmax_, msF_);

        // 30 s cycles of KB_F (KB_FTm per cycle)
        cycleMax_ = std::max (cycleMax_, msF_);
        if (++cyclePos_ >= cycleLen_)
        {
            cycleSumSq_ += cycleMax_;   // KB_FTi² (msF is already squared)
            ++cycleCount_;
            cycleMax_ = 0.0;
            cyclePos_ = 0;
        }

        // Zero crossings bound the half-cycle that contains the peak
        const double t = (double) count_;
        if ((prevX_ < 0.0 && x >= 0.0) || (prevX_ > 0.0 && x <= 0.0))
        {
            const double zc = t - 1.0 + prevX_ / (prevX_ - x);   // interpolated
            if (awaitingZc_ && zc > peakZc_)
            {
                if (peakZc_ >= 0.0)
                {
                    fDom_ = fs_ / (2.0 * (zc - peakZc_));
                    awaitingZc_ = false;
                }
                else
                {
                    // Peak in the first half-cycle of an event (no crossing before
                    // it): use the following half-cycle instead
                    peakZc_ = zc;
                }
            }
            lastZc_ = zc;
        }
        prevX_ = x;
        ++count_;

        const double a = std::fabs (x);
        if (a > peakMax_)
        {
            peakMax_    = a;
            peakZc_     = lastZc_;   // -1: no crossing yet, see above
            awaitingZc_ = true;
        }
        peak_ = std::max (peak_, a);
        min_  = std::min (min_, x);
        max_  = std::max (max_, x);
    }

    // Running mean square — the vector sum combines these
    double meanSquare() const noexcept       { return meanSquare_; }
    double meanSquare1s() const noexcept     { return ms1_; }
    double linearMeanSquare() const noexcept { return count_ > 0 ? sumSq_ / (double) count_ : 0.0; }
    double fourthPowerIntegral() const noexcept { return sum4_ / fs_; }   // ∫ x⁴ dt
    double seconds() const noexcept          { return (double) count_ / fs_; }

    AxisValues values() const noexcept
    {
        AxisValues v;
        v.rms     = (float) std::sqrt (meanSquare_);
        v.peak    = (float) peak_;
        v.peakMax = (float) peakMax_;
        v.p2p     = (max_ >= min_) ? (float) (max_ - min_) : 0.0f;
        v.awRms   = (float) std::sqrt (linearMeanSquare());
        v.mtvv    = (float) std::sqrt (mtvv_);
        v.vdv     = (float) std::pow (fourthPowerIntegral(), 0.25);
        v.crest   = v.awRms > 0.0f ? v.peakMax / v.awRms : 0.0f;
        v.fDom    = (float) fDom_;
        v.kbFmax  = (float) std::sqrt (kbFmax_);
        // Completed cycles; before the first one completes, the running cycle
        const double ftr2 = cycleCount_ > 0 ? cycleSumSq_ / (double) cycleCount_ : cycleMax_;
        v.kbFTm   = (float) std::sqrt (ftr2);
        return v;
    }

    void startInterval() noexcept
    {
        peak_ = 0.0;
        min_  =  1.0e300;
        max_  = -1.0e300;
    }

private:
    double    fs_ = 12000.0;
    double    alpha_ = 0.0, alpha1_ = 0.0, alphaF_ = 0.0;
    double    meanSquare_ = 0.0, ms1_ = 0.0, msF_ = 0.0;
    double    peak_ = 0.0, peakMax_ = 0.0, min_ = 1.0e300, max_ = -1.0e300;
    double    sumSq_ = 0.0, sum4_ = 0.0;
    long long count_ = 0;
    double    mtvv_ = 0.0, kbFmax_ = 0.0;                       // squared
    double    cycleMax_ = 0.0, cycleSumSq_ = 0.0;               // squared
    long long cycleLen_ = 360000, cyclePos_ = 0;
    int       cycleCount_ = 0;
    double    prevX_ = 0.0, lastZc_ = -1.0, peakZc_ = -1.0, fDom_ = 0.0;
    bool      awaitingZc_ = false;
};
} // namespace vib
