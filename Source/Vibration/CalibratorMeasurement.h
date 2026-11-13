#pragma once
#include "Biquad.h"
#include <algorithm>

namespace vib
{
//==============================================================================
// Handheld-calibrator measurement on one raw input (vibration spec §4, method 1).
//
// A band-pass (2nd-order Butterworth HP at f_ref/2 + LP at 2·f_ref) suppresses
// hum and sensor noise. After a settling time the RMS is measured over
// sub-blocks; the frequency comes from interpolated positive zero crossings.
// The RMS is corrected by the band-pass gain at the measured frequency, so a
// calibrator slightly off its nominal frequency still reads exactly.
//
// Real-time safe: no allocation; prepare() on any thread while nobody processes.
class CalibratorMeasurement
{
public:
    enum class Status : int { Running = 0, Ok, TooLow, Clipped, FrequencyOff, Unstable };

    static constexpr double kSettleSec         = 0.5;
    static constexpr double kMeasureSec        = 3.0;
    static constexpr double kMaxFreqDeviation  = 0.05;    // ±5 %
    static constexpr double kMaxFluctuationDb  = 0.2;     // ±0.2 dB
    static constexpr double kMinRmsFS          = 1.0e-4;  // −80 dBFS
    static constexpr double kClipFS            = 0.99;

    struct Result
    {
        Status status       = Status::Running;
        double rmsFS        = 0.0;   // band-pass corrected RMS (full scale = 1.0 peak)
        double freqHz       = 0.0;
        double fluctuationDb = 0.0;  // ± half of the sub-block RMS spread
        double peakFS       = 0.0;
    };

    void prepare (double fs, double refHz)
    {
        fs_    = fs;
        refHz_ = refHz;
        const double Q = 1.0 / std::sqrt (2.0);
        hp_ = Biquad::highPass (0.5 * refHz, Q, fs);
        lp_ = Biquad::lowPass  (std::min (2.0 * refHz, 0.45 * fs), Q, fs);

        // Sub-blocks span ≥ 20 periods, so partial periods move the RMS by < 0.01 dB
        subBlockLen_ = (long long) std::ceil (std::max (0.25, 20.0 / refHz) * fs);
        const double measureSec = std::max (kMeasureSec, 8.0 * (double) subBlockLen_ / fs);
        settleLen_  = (long long) std::ceil (kSettleSec * fs);
        measureLen_ = ((long long) std::ceil (measureSec * fs) / subBlockLen_) * subBlockLen_;
        reset();
    }

    void reset() noexcept
    {
        hp_.reset(); lp_.reset();
        n_ = 0;
        sumSq_ = subSumSq_ = 0.0;
        subCount_ = 0;
        subMin_ = 1.0e300; subMax_ = 0.0;
        prevY_ = 0.0;
        crossings_ = 0;
        firstCross_ = lastCross_ = 0.0;
        peak_ = 0.0;
        result_ = {};
    }

    bool   finished() const noexcept { return result_.status != Status::Running; }
    double progress() const noexcept { return std::min (1.0, (double) n_ / (double) (settleLen_ + measureLen_)); }
    const Result& result() const noexcept { return result_; }
    double referenceHz() const noexcept { return refHz_; }

    void process (const float* x, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples && ! finished(); ++i)
        {
            const double raw = x[i];
            const double y = lp_.process (hp_.process (raw));
            const long long k = n_++;
            if (k < settleLen_)
            {
                prevY_ = y;
                continue;
            }

            peak_ = std::max (peak_, std::fabs (raw));
            sumSq_    += y * y;
            subSumSq_ += y * y;

            if (prevY_ < 0.0 && y >= 0.0)
            {
                const double t = (double) (k - 1) + prevY_ / (prevY_ - y);   // interpolated
                if (crossings_++ == 0) firstCross_ = t;
                lastCross_ = t;
            }
            prevY_ = y;

            if (++subCount_ == subBlockLen_)
            {
                const double ms = subSumSq_ / (double) subBlockLen_;
                subMin_ = std::min (subMin_, ms);
                subMax_ = std::max (subMax_, ms);
                subSumSq_ = 0.0;
                subCount_ = 0;
            }

            if (k + 1 - settleLen_ >= measureLen_)
                finish();
        }
    }

private:
    void finish() noexcept
    {
        Result r;
        r.peakFS = peak_;
        r.freqHz = crossings_ >= 2 ? (double) (crossings_ - 1) * fs_ / (lastCross_ - firstCross_) : 0.0;
        const double gain = r.freqHz > 0.0
                          ? std::abs (hp_.response (r.freqHz, fs_) * lp_.response (r.freqHz, fs_))
                          : 1.0;
        r.rmsFS = std::sqrt (sumSq_ / (double) measureLen_) / std::max (gain, 1.0e-6);
        r.fluctuationDb = subMax_ > 0.0 && subMin_ > 0.0 ? 5.0 * std::log10 (subMax_ / subMin_) : 0.0;

        if      (r.peakFS >= kClipFS)                                    r.status = Status::Clipped;
        else if (r.rmsFS < kMinRmsFS)                                    r.status = Status::TooLow;
        else if (std::fabs (r.freqHz / refHz_ - 1.0) > kMaxFreqDeviation) r.status = Status::FrequencyOff;
        else if (r.fluctuationDb > kMaxFluctuationDb)                    r.status = Status::Unstable;
        else                                                             r.status = Status::Ok;
        result_ = r;
    }

    double    fs_ = 48000.0, refHz_ = 159.2;
    Biquad    hp_, lp_;
    long long settleLen_ = 0, measureLen_ = 1, subBlockLen_ = 1;
    long long n_ = 0, subCount_ = 0;
    double    sumSq_ = 0.0, subSumSq_ = 0.0, subMin_ = 1.0e300, subMax_ = 0.0;
    double    prevY_ = 0.0, firstCross_ = 0.0, lastCross_ = 0.0, peak_ = 0.0;
    int       crossings_ = 0;
    Result    result_;
};
} // namespace vib
