#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Vibration/AccelChannel.h"
#include "Vibration/CalibratorMeasurement.h"
#include "Vibration/Units.h"

#include <cmath>
#include <string>
#include <vector>

// Vibration spec §4, §5.1, §5.2: calibration, decimation, integration and
// ISO 8041 weightings of the accelerometer signal chain.

using Catch::Approx;
using namespace vib;

namespace
{
double dB (double m) { return 20.0 * std::log10 (m); }

// 1/3-octave band n: nominal (preferred-number) and exact (10^(n/10)) centre frequencies
double exactFreq (int band) { return std::pow (10.0, band / 10.0); }

struct RefRow { int band; double nominal; std::vector<double> values; };

// Reference weightings in dB from Virtins Technology (2024), "Frequency Weightings for
// Sound and Vibration Perceived by Humans" (spec §14 [V]). Only the cleanly extracted
// rows are used. That table evaluates Wm and Wh at exact 1/3-octave frequencies and
// the other weightings at nominal frequencies.
const std::vector<Weighting> kLowCols { Weighting::Wk, Weighting::Wd, Weighting::Wf, Weighting::Wc,
                                        Weighting::We, Weighting::Wj, Weighting::Wm, Weighting::Wb };
const std::vector<RefRow> kLowRows {
    { -10, 0.1,   { -30.11, -24.09,  -3.16, -24.11, -24.08, -30.18, -36.00, -32.04 } },
    {  -9, 0.125, { -26.26, -20.24,  -0.96, -20.25, -20.22, -26.32, -32.00, -28.20 } },
    {  -8, 0.16,  { -22.05, -16.01,   0.05, -16.03, -15.98, -22.11, -28.01, -23.98 } },
    {  -7, 0.2,   { -18.33, -12.28,  -0.07, -12.30, -12.23, -18.38, -24.02, -20.23 } },
    {  -6, 0.25,  { -14.81,  -8.75,  -1.37,  -8.78,  -8.67, -14.86, -20.05, -16.71 } },
    {  -5, 0.315, { -11.60,  -5.52,  -4.17,  -5.56,  -5.41, -11.65, -16.12, -13.51 } },
    {  -4, 0.4,   {  -9.07,  -2.94,  -8.31,  -3.01,  -2.81,  -9.10, -12.29, -10.98 } },
    {  -3, 0.5,   {  -7.57,  -1.38, -13.00,  -1.48,  -1.29,  -7.60,  -8.67,  -9.53 } },
    {  -2, 0.63,  {  -6.77,  -0.50, -18.69,  -0.64,  -0.55,  -6.78,  -5.51,  -8.71 } },
};
const std::vector<Weighting> kHighCols { Weighting::Wk, Weighting::Wd, Weighting::Wc, Weighting::We,
                                         Weighting::Wj, Weighting::Wm, Weighting::Wb, Weighting::Wh };
const std::vector<RefRow> kHighRows {
    { 10,  10.0, {  -0.10, -13.91,  -2.20, -19.98,   0.26,  -6.12,  -0.23,  -0.43 } },
    { 11,  12.5, {  -0.89, -15.87,  -3.79, -21.93,   0.22,  -7.71,  -0.85,  -0.38 } },
    { 12,  16.0, {  -2.28, -18.03,  -5.82, -24.08,   0.16,  -9.44,  -1.83,  -0.96 } },
    { 13,  20.0, {  -3.93, -19.99,  -7.77, -26.02,   0.10, -11.25,  -3.00,  -2.14 } },
    { 14,  25.0, {  -5.80, -21.94,  -9.76, -27.97,   0.06, -13.14,  -4.44,  -3.78 } },
    { 15,  31.5, {  -7.86, -23.98, -11.84, -30.01,   0.00, -15.09,  -6.16,  -5.69 } },
    { 16,  40.0, { -10.05, -26.13, -14.02, -32.15,  -0.08, -17.10,  -8.11,  -7.72 } },
    { 17,  50.0, { -12.19, -28.22, -16.13, -34.24,  -0.24, -19.23, -10.09,  -9.78 } },
    { 18,  63.0, { -14.61, -30.60, -18.53, -36.62,  -0.62, -21.58, -12.43, -11.83 } },
    { 19,  80.0, { -17.56, -33.53, -21.47, -39.55,  -1.48, -24.38, -15.34, -13.88 } },
    { 20, 100.0, { -21.04, -36.99, -24.94, -43.01,  -3.01, -27.93, -18.72, -15.91 } },
    { 21, 125.0, { -25.35, -41.28, -29.24, -47.31,  -5.36, -32.37, -23.00, -17.93 } },
    { 22, 160.0, { -30.91, -46.84, -34.80, -52.86,  -8.78, -37.55, -28.56, -19.94 } },
    { 23, 200.0, { -36.38, -52.30, -40.26, -58.33, -12.30, -43.18, -34.03, -21.95 } },
    { 24, 250.0, { -42.04, -57.97, -45.92, -63.99, -16.03, -49.02, -39.69, -23.96 } },
    { 25, 315.0, { -48.00, -63.92, -51.88, -69.94, -19.98, -54.95, -45.65, -25.97 } },
    { 26, 400.0, { -54.20, -70.12, -58.08, -76.14, -24.10, -60.92, -51.84, -28.00 } },
};
const std::vector<std::pair<int, double>> kWhHigh {   // Wh only, 500 Hz … 4 kHz
    { 27, -30.07 }, { 28, -32.23 }, { 29, -34.60 }, { 30, -37.42 }, { 31, -40.97 },
    { 32, -45.42 }, { 33, -50.60 }, { 34, -56.23 }, { 35, -62.07 }, { 36, -68.01 },
};

double refFreq (Weighting w, const RefRow& r)
{
    return (w == Weighting::Wm || w == Weighting::Wh) ? exactFreq (r.band) : r.nominal;
}

// Wb rests on a single source and deviates systematically by up to 0.05 dB (spec §13)
double refTol (Weighting w) { return w == Weighting::Wb ? 0.06 : 0.02; }

const char* nameOf (Weighting w)
{
    static const char* names[] = { "None", "BandLimit", "Wb", "Wc", "Wd", "We", "Wf", "Wh", "Wj", "Wk", "Wm", "KB" };
    return names[static_cast<int> (w)];
}

// Steady-state RMS of a processed sine (skips the settling time)
template <typename Fn>
double sineRms (Fn&& process, double f, double fs, double amp, double settleSec, double measureSec)
{
    const int settle  = (int) (settleSec * fs);
    const int measure = (int) (measureSec * fs);
    double sum = 0.0; int count = 0;
    for (int n = 0; n < settle + measure; ++n)
    {
        double y;
        const bool produced = process (amp * std::sin (2.0 * kPi * f * n / fs), y);
        if (produced && n >= settle) { sum += y * y; ++count; }
    }
    return std::sqrt (sum / count);
}
} // namespace

//==============================================================================
TEST_CASE ("Vibration weightings: parameters reproduce the public reference table", "[vibration][weighting]")
{
    for (const auto& row : kLowRows)
        for (size_t c = 0; c < kLowCols.size(); ++c)
        {
            const auto w = kLowCols[c];
            INFO (nameOf (w) << " at " << row.nominal << " Hz");
            CHECK (dB (analogMagnitude (w, refFreq (w, row))) == Approx (row.values[c]).margin (refTol (w)));
        }
    for (const auto& row : kHighRows)
        for (size_t c = 0; c < kHighCols.size(); ++c)
        {
            const auto w = kHighCols[c];
            INFO (nameOf (w) << " at " << row.nominal << " Hz");
            CHECK (dB (analogMagnitude (w, refFreq (w, row))) == Approx (row.values[c]).margin (refTol (w)));
        }
    for (const auto& [band, value] : kWhHigh)
    {
        INFO ("Wh at band " << band);
        CHECK (dB (analogMagnitude (Weighting::Wh, exactFreq (band))) == Approx (value).margin (0.02));
    }
}

TEST_CASE ("Vibration weightings: digital filters match the analog definition", "[vibration][weighting]")
{
    // Nominal frequency range per weighting (ISO 2631-1/-2/-4, ISO 5349-1, DIN 4150-2).
    // Inside: ±0.1 dB (±0.2 dB in the top 1/3 octave, where bilinear warping starts).
    // Up to two octaves outside: never more than 1 dB *less* attenuation than the analog
    // weighting; more attenuation is fine once the response is below −40 dB (near the
    // decimated Nyquist frequency bilinear warping adds attenuation).
    struct Range { Weighting w; int bandLo, bandHi; };
    const std::vector<Range> ranges {
        { Weighting::Wb, -3, 19 }, { Weighting::Wc, -3, 19 }, { Weighting::Wd, -3, 19 },
        { Weighting::We, -3, 19 }, { Weighting::Wj, -3, 19 }, { Weighting::Wk, -3, 19 },
        { Weighting::Wm,  0, 19 }, { Weighting::Wf, -10, -3 }, { Weighting::Wh,  9, 30 },
        { Weighting::KB,  0, 19 },
    };
    for (double fs : { 11025.0, 12000.0 })   // decimated rates for 44.1 / 48 / 96 kHz
        for (const auto& r : ranges)
        {
            WeightingFilter filter;
            filter.prepare (r.w, fs);
            for (int band = r.bandLo - 6; band <= r.bandHi + 6; ++band)
            {
                const double f   = exactFreq (band);
                const double ref = dB (analogMagnitude (r.w, f));
                const double dig = dB (filter.magnitude (f));
                INFO (nameOf (r.w) << " at " << f << " Hz, fs " << fs << ", analog " << ref
                                   << " dB, digital " << dig << " dB");
                if (band >= r.bandLo && band <= r.bandHi)
                {
                    CHECK (dig == Approx (ref).margin (band == r.bandHi ? 0.2 : 0.1));
                }
                else
                {
                    CHECK (dig <= ref + 1.0);
                    if (ref > -40.0)
                        CHECK (dig >= ref - 1.0);
                }
            }
        }
}

TEST_CASE ("Vibration weightings: KB and band-limit", "[vibration][weighting]")
{
    // KB: first-order high-pass at 5.6 Hz → -3 dB at 5.6 Hz (plus small band-limit effect)
    CHECK (dB (analogMagnitude (Weighting::KB, 5.6)) == Approx (-3.01).margin (0.05));
    CHECK (dB (analogMagnitude (Weighting::KB, 20.0)) == Approx (-0.33).margin (0.05));

    // Band-limit 10–1000 Hz (ISO 20816): -3 dB at both corners, flat in between
    WeightingFilter bl;
    bl.prepare (Weighting::BandLimit, 12000.0, 10.0, 1000.0);
    CHECK (dB (bl.magnitude (10.0))   == Approx (-3.01).margin (0.05));
    CHECK (dB (bl.magnitude (1000.0)) == Approx (-3.01).margin (0.05));
    CHECK (dB (bl.magnitude (100.0))  == Approx (0.0).margin (0.01));

    WeightingFilter none;
    none.prepare (Weighting::None, 12000.0);
    CHECK (none.magnitude (3.0) == Approx (1.0));
}

//==============================================================================
TEST_CASE ("Vibration decimator: rate selection, passband and ≥ 90 dB stopband", "[vibration][decimator]")
{
    Decimator d;
    d.prepare (48000.0);  CHECK (d.factor() == 4);  CHECK (d.outputRate (48000.0) == 12000.0);
    d.prepare (44100.0);  CHECK (d.factor() == 4);  CHECK (d.outputRate (44100.0) == 11025.0);
    d.prepare (96000.0);  CHECK (d.factor() == 8);
    d.prepare (192000.0); CHECK (d.factor() == 16);
    d.prepare (8000.0);   CHECK (d.factor() == 1);

    const HalfBandStage stage;
    double worstPass = 0.0, worstStop = -1000.0;
    for (double fn = 0.0; fn <= 0.2; fn += 0.002)  worstPass = std::max (worstPass, std::fabs (dB (stage.magnitude (fn))));
    for (double fn = 0.3; fn <= 0.5; fn += 0.001)  worstStop = std::max (worstStop, dB (stage.magnitude (fn)));
    INFO ("passband ripple " << worstPass << " dB, stopband " << worstStop << " dB");
    CHECK (worstPass < 0.01);
    CHECK (worstStop < -90.0);
}

TEST_CASE ("Vibration decimator: passes in-band tones, rejects aliases", "[vibration][decimator]")
{
    const double fs = 48000.0;
    auto rmsAfter = [&] (double f)
    {
        Decimator d;
        d.prepare (fs);
        return sineRms ([&] (double x, double& y) { return d.push (x, y); }, f, fs, 1.0, 0.05, 0.5);
    };
    CHECK (dB (rmsAfter (1000.0) * std::sqrt (2.0)) == Approx (0.0).margin (0.01));
    CHECK (dB (rmsAfter (4000.0) * std::sqrt (2.0)) == Approx (0.0).margin (0.01));
    CHECK (dB (rmsAfter (20000.0) * std::sqrt (2.0)) < -90.0);   // would alias to 4 kHz
    CHECK (dB (rmsAfter (8000.0) * std::sqrt (2.0)) < -90.0);    // would alias to 4 kHz in stage 2
}

//==============================================================================
TEST_CASE ("Vibration calibration: calibrator and datasheet methods", "[vibration][calibration]")
{
    // A 10 m/s² RMS calibrator reading −20 dBFS RMS → 160 dB; one FS unit = 100 m/s²
    const double rmsFS = std::pow (10.0, -20.0 / 20.0);
    const double calDb = calibration::calDbFromCalibrator (rmsFS);
    CHECK (calDb == Approx (160.0));
    CHECK (calibration::scale (calDb) * rmsFS == Approx (10.0));
    CHECK (calibration::levelDb (10.0) == Approx (140.0));

    // Datasheet: 100 mV/g sensor, interface +4 dBu at 0 dBFS
    const double S   = calibration::mVperMs2FromMVperG (100.0);   // 10.197 mV/(m/s²)
    const double Vfs = calibration::dBuToVrms (4.0);               // 1.228 Vrms
    const double cal = calibration::calDbFromDatasheet (Vfs, S);
    // A full-scale sine (RMS 1/√2 FS) must read V_FS,rms / S m/s² RMS
    CHECK (calibration::scale (cal) / std::sqrt (2.0) == Approx (Vfs * 1000.0 / S).epsilon (1e-9));
    CHECK (cal == Approx (120.0 + 20.0 * std::log10 (std::sqrt (2.0) * Vfs * 1000.0 / S)));
}

//==============================================================================
TEST_CASE ("Vibration chain: integration to velocity and displacement", "[vibration][chain]")
{
    const double fs = 48000.0;
    const double a  = 1.0;   // m/s² peak
    for (double f : { 5.0, 10.0, 50.0, 200.0, 1000.0 })
    {
        for (auto q : { Quantity::Velocity, Quantity::Displacement })
        {
            AccelChannelConfig cfg;
            cfg.calDb      = 120.0;   // 1 FS unit = 1 m/s²
            cfg.highPassHz = 1.0;     // f ≥ 5 × corner
            cfg.quantity   = q;
            AccelChannel ch;
            ch.prepare (fs, cfg);

            const double rms = sineRms ([&] (double x, double& y)
                                        { double full; return ch.process ((float) x, full, y); },
                                        f, fs, a, 3.0, 2.0);
            const double w        = 2.0 * kPi * f;
            const double expected = (q == Quantity::Velocity ? a / w : a / (w * w)) / std::sqrt (2.0);
            INFO ((q == Quantity::Velocity ? "velocity" : "displacement") << " at " << f << " Hz");
            CHECK (rms == Approx (expected).epsilon (0.005));
        }
    }
}

TEST_CASE ("Vibration chain: calibrated, weighted acceleration end to end", "[vibration][chain]")
{
    const double fs = 48000.0;

    // −20 dBFS sine on a channel calibrated with a 10 m/s² calibrator at −20 dBFS
    AccelChannelConfig cfg;
    cfg.calDb      = calibration::calDbFromCalibrator (std::pow (10.0, -20.0 / 20.0));
    cfg.highPassHz = 0.1;
    cfg.weighting  = Weighting::Wk;
    AccelChannel ch;
    ch.prepare (fs, cfg);
    CHECK (ch.decimatedRate() == 12000.0);

    const double amp = std::pow (10.0, -20.0 / 20.0) * std::sqrt (2.0);   // −20 dBFS RMS
    for (double f : { 2.0, 8.0, 31.5 })
    {
        ch.reset();
        double fullSum = 0.0; int fullCount = 0;
        const double rms = sineRms ([&] (double x, double& y)
                                    {
                                        double full;
                                        const bool produced = ch.process ((float) x, full, y);
                                        fullSum += full * full; ++fullCount;
                                        return produced;
                                    },
                                    f, fs, amp, 8.0, 4.0);
        INFO ("Wk at " << f << " Hz");
        // full-rate acceleration (incl. settling) ≈ 10 m/s² RMS
        CHECK (std::sqrt (fullSum / fullCount) == Approx (10.0).epsilon (0.01));
        CHECK (dB (rms / 10.0) == Approx (dB (analogMagnitude (Weighting::Wk, f))).margin (0.1));
    }
}

//==============================================================================
namespace
{
// Feeds a generated signal to a calibrator measurement in 480-sample blocks
template <typename Signal>
CalibratorMeasurement::Result measureCalibrator (double fs, double refHz, Signal&& signal)
{
    CalibratorMeasurement m;
    m.prepare (fs, refHz);
    std::vector<float> block (480);
    long long n = 0;
    for (int b = 0; b < 100000 && ! m.finished(); ++b)
    {
        for (auto& x : block) x = (float) signal (n++ / fs);
        m.process (block.data(), (int) block.size());
    }
    REQUIRE (m.finished());
    CHECK (m.progress() == Approx (1.0));
    return m.result();
}
} // namespace

TEST_CASE ("Vibration calibrator measurement: level, frequency and acceptance", "[vibration][calibration]")
{
    using Status = CalibratorMeasurement::Status;
    const double twoPi = 2.0 * kPi;

    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        INFO ("fs = " << fs);
        // −20 dBFS RMS at the nominal 159.2 Hz, with hum and noise-like interference
        const double amp = std::sqrt (2.0) * 0.1;
        auto r = measureCalibrator (fs, 159.2, [&] (double t)
        {
            return amp * std::sin (twoPi * 159.2 * t) + 0.002 * std::sin (twoPi * 50.0 * t)
                 + 0.001 * std::sin (twoPi * 3000.0 * t);
        });
        CHECK (r.status == Status::Ok);
        CHECK (r.freqHz == Approx (159.2).epsilon (1e-4));
        CHECK (20.0 * std::log10 (r.rmsFS) == Approx (-20.0).margin (0.01));
        CHECK (r.fluctuationDb < 0.02);
        // The calibration derived from it reads the calibrator exactly
        CHECK (calibration::calDbFromCalibrator (r.rmsFS) == Approx (160.0).margin (0.01));
    }

    SECTION ("calibrator 3 % off its nominal frequency still reads the right level")
    {
        auto r = measureCalibrator (48000.0, 159.2, [&] (double t) { return 0.5 * std::sin (twoPi * 164.0 * t); });
        CHECK (r.status == Status::Ok);
        CHECK (r.freqHz == Approx (164.0).epsilon (1e-4));
        CHECK (r.rmsFS == Approx (0.5 / std::sqrt (2.0)).epsilon (0.001));
    }
    SECTION ("custom low reference frequency")
    {
        auto r = measureCalibrator (48000.0, 16.0, [&] (double t) { return 0.3 * std::sin (twoPi * 16.0 * t); });
        CHECK (r.status == Status::Ok);
        CHECK (r.rmsFS == Approx (0.3 / std::sqrt (2.0)).epsilon (0.002));
    }
    SECTION ("wrong frequency is rejected")
    {
        auto r = measureCalibrator (48000.0, 159.2, [&] (double t) { return 0.3 * std::sin (twoPi * 80.0 * t); });
        CHECK (r.status == Status::FrequencyOff);
    }
    SECTION ("fluctuating level is rejected")
    {
        // ±0.5 dB slow amplitude modulation
        auto r = measureCalibrator (48000.0, 159.2, [&] (double t)
        {
            return 0.3 * (1.0 + 0.06 * std::sin (twoPi * 0.4 * t)) * std::sin (twoPi * 159.2 * t);
        });
        CHECK (r.status == Status::Unstable);
        CHECK (r.fluctuationDb > 0.2);
    }
    SECTION ("too low and clipped signals are rejected")
    {
        CHECK (measureCalibrator (48000.0, 159.2, [&] (double t) { return 1.0e-5 * std::sin (twoPi * 159.2 * t); })
                   .status == Status::TooLow);
        CHECK (measureCalibrator (48000.0, 159.2, [&] (double t) { return std::sin (twoPi * 159.2 * t); })
                   .status == Status::Clipped);
    }
}

//==============================================================================
TEST_CASE ("Vibration display units: conversion, ISO 1683 levels and labels", "[vibration][units]")
{
    using namespace units;
    // Linear: m/s², mm/s, µm
    CHECK (convert (2.5, DisplayUnit::Linear, Quantity::Acceleration) == Approx (2.5));
    CHECK (convert (0.004, DisplayUnit::Linear, Quantity::Velocity) == Approx (4.0));
    CHECK (convert (3.0e-6, DisplayUnit::Linear, Quantity::Displacement) == Approx (3.0));
    // g only for acceleration
    CHECK (convert (calibration::kGravity, DisplayUnit::G, Quantity::Acceleration) == Approx (1.0));
    CHECK (effective (DisplayUnit::G, Quantity::Velocity) == DisplayUnit::Linear);
    CHECK (std::string (label (DisplayUnit::G, Quantity::Velocity)) == "mm/s");
    // dB re 1 µm/s², 1 nm/s, 1 pm
    CHECK (convert (10.0, DisplayUnit::Db, Quantity::Acceleration) == Approx (140.0));
    CHECK (convert (1.0e-3, DisplayUnit::Db, Quantity::Velocity) == Approx (120.0));
    CHECK (convert (1.0e-6, DisplayUnit::Db, Quantity::Displacement) == Approx (120.0));
    CHECK (std::string (label (DisplayUnit::Db, Quantity::Displacement)) == "dB re 1 pm");
    CHECK (std::string (label (DisplayUnit::Linear, Quantity::Acceleration)) == "m/s\xc2\xb2");
    // Three significant digits
    CHECK (format (0.12345, DisplayUnit::Linear, Quantity::Acceleration) == "0.123");
    CHECK (format (12.345, DisplayUnit::Linear, Quantity::Acceleration) == "12.3");
    CHECK (format (0.0012345, DisplayUnit::Linear, Quantity::Velocity) == "1.23");
    CHECK (format (10.0, DisplayUnit::Db, Quantity::Acceleration) == "140.0");
}

//==============================================================================
#include "Vibration/Profiles.h"

TEST_CASE ("Vibration profiles: definitions follow spec §6", "[vibration][profiles]")
{
    auto def = [] (Profile p) { return profileDefinition (p).settings; };

    const auto health = def (Profile::WholeBodyHealth);
    CHECK (health.quantity == Quantity::Acceleration);
    CHECK (health.weighting[0] == Weighting::Wd);
    CHECK (health.weighting[1] == Weighting::Wd);
    CHECK (health.weighting[2] == Weighting::Wk);
    CHECK (health.k[0] == Approx (1.4));
    CHECK (health.k[2] == Approx (1.0));
    CHECK (health.highPassHz == Approx (0.1));
    CHECK (health.main == MainValue::MaxAxisKAw);

    const auto machinery = def (Profile::MachineryIso20816);
    CHECK (machinery.quantity == Quantity::Velocity);
    CHECK (machinery.weighting[1] == Weighting::BandLimit);
    CHECK (machinery.bandLowHz == Approx (10.0));
    CHECK (machinery.bandHighHz == Approx (1000.0));
    CHECK (def (Profile::MachineryLowSpeed).bandLowHz == Approx (2.0));

    CHECK (def (Profile::HandArm).weighting[2] == Weighting::Wh);
    CHECK (def (Profile::HandArm).main == MainValue::SumAw);
    CHECK (def (Profile::WholeBodyComfort).main == MainValue::SumAw);
    CHECK (def (Profile::BuildingPerception).main == MainValue::MaxAxisKbFmax);
    CHECK (def (Profile::BuildingPerception).weighting[0] == Weighting::KB);
    CHECK (def (Profile::BuildingPerception).tauSec == Approx (0.125));   // KB_F(t)
    CHECK (def (Profile::BuildingStructural).bandHighHz == Approx (315.0));
    CHECK (def (Profile::BuildingStructural).main == MainValue::MaxAxisPeakMax);
    CHECK (profileDefinition (Profile::StructureBorne).unit == DisplayUnit::Db);

    // Every profile matches its own settings; out-of-range values fall back to Custom
    for (int p = 1; p < (int) Profile::NumProfiles; ++p)
        CHECK (settingsMatchProfile (def ((Profile) p), (Profile) p));
    CHECK (profileFromInt (99) == Profile::Custom);
    CHECK_FALSE (settingsMatchProfile (VibrationSettings {}, Profile::Custom));
}

TEST_CASE ("Vibration profiles: edits make the profile custom", "[vibration][profiles]")
{
    auto s = profileDefinition (Profile::WholeBodyHealth).settings;
    CHECK (profileLabel (s, Profile::WholeBodyHealth) == "Whole-body health (ISO 2631-1)");
    CHECK (mainValueLabel (s, Profile::WholeBodyHealth) == "a_w");

    s.k[0] = 1.0;
    CHECK_FALSE (settingsMatchProfile (s, Profile::WholeBodyHealth));
    CHECK (profileLabel (s, Profile::WholeBodyHealth) == "Custom (based on Whole-body health (ISO 2631-1))");
    CHECK (mainValueLabel (s, Profile::WholeBodyHealth) == "a_w");   // main value unchanged

    s = profileDefinition (Profile::WholeBodyHealth).settings;
    s.main = MainValue::SumRms;
    CHECK (mainValueLabel (s, Profile::WholeBodyHealth) == "\xce\xa3 RMS");

    // Band limits only matter for profiles that use the band weighting
    s = profileDefinition (Profile::WholeBodyHealth).settings;
    s.bandLowHz = 3.0;
    CHECK (settingsMatchProfile (s, Profile::WholeBodyHealth));
    s = profileDefinition (Profile::MachineryIso20816).settings;
    s.bandLowHz = 3.0;
    CHECK_FALSE (settingsMatchProfile (s, Profile::MachineryIso20816));
    CHECK (profileLabel (VibrationSettings {}, Profile::Custom) == "Custom");
}

TEST_CASE ("Vibration main values", "[vibration][profiles]")
{
    VibrationSnapshot s;
    s.axisMask = 0b111;
    s.axis[AxisX].rms = 0.3f; s.axis[AxisY].rms = 0.2f; s.axis[AxisZ].rms = 0.35f;
    s.axis[AxisSum].rms = 0.6f;
    s.axis[AxisX].peakMax = 0.9f; s.axis[AxisZ].peakMax = 1.2f;
    const double k[3] { 1.4, 1.4, 1.0 };

    CHECK (computeMainValue (MainValue::MaxAxisRms, s, k) == Approx (0.35));
    CHECK (computeMainValue (MainValue::MaxAxisKRms, s, k) == Approx (0.42));   // 1.4 · 0.3
    CHECK (computeMainValue (MainValue::SumRms, s, k) == Approx (0.6));
    CHECK (computeMainValue (MainValue::MaxAxisPeakMax, s, k) == Approx (1.2));
    CHECK (computeMainValue (MainValue::None, s, k) == 0.0f);

    // A single axis: Σ is that axis; absent axes are ignored
    s.axisMask = 0b100;
    CHECK (computeMainValue (MainValue::SumRms, s, k) == Approx (0.35));
    CHECK (computeMainValue (MainValue::MaxAxisKRms, s, k) == Approx (0.35));
}

//==============================================================================
namespace
{
template <typename Signal>
AxisValues runDetector (double fs, double seconds, Signal&& x, double tau = 0.125)
{
    AxisDetector d;
    d.prepare (fs, tau);
    const auto n = (long long) (seconds * fs);
    for (long long i = 0; i < n; ++i)
        d.process (x (i / fs));
    return d.values();
}
} // namespace

TEST_CASE ("Vibration accumulating detectors: a_w,rms, VDV, MTVV, crest factor", "[vibration][detectors]")
{
    const double twoPi = 2.0 * kPi, fs = 4000.0;
    const double A = 2.0, a = A / std::sqrt (2.0), T = 20.0;
    const auto v = runDetector (fs, T, [&] (double t) { return A * std::sin (twoPi * 50.0 * t); });

    CHECK (v.awRms == Approx (a).epsilon (1e-4));                                  // linear RMS
    CHECK (v.vdv   == Approx (a * std::pow (1.5 * T, 0.25)).epsilon (1e-4));       // spec §11
    CHECK (v.mtvv  == Approx (a).epsilon (0.01));
    CHECK (v.crest == Approx (std::sqrt (2.0)).epsilon (1e-3));

    // MTVV catches a 3 s burst that the linear RMS averages away
    const auto b = runDetector (fs, 30.0, [&] (double t)
    {
        return (t > 10.0 && t < 13.0 ? A : 0.1 * A) * std::sin (twoPi * 50.0 * t);
    });
    CHECK (b.mtvv  == Approx (a * std::sqrt (1.0 - std::exp (-3.0))).epsilon (0.02));
    const double awBurst = a * std::sqrt ((3.0 + 0.01 * 27.0) / 30.0);
    CHECK (b.awRms == Approx (awBurst).epsilon (0.01));
    CHECK (b.crest == Approx (A / awBurst).epsilon (0.01));
}

TEST_CASE ("Vibration accumulating detectors: PPV with dominant frequency", "[vibration][detectors]")
{
    const double twoPi = 2.0 * kPi, fs = 4000.0;
    for (double f : { 4.0, 25.0, 80.0 })
    {
        INFO ("f = " << f);
        // Decaying sine starting at 1 s: the first peak is the PPV
        const auto v = runDetector (fs, 4.0, [&] (double t)
        {
            return t < 1.0 ? 0.0 : 0.012 * std::exp (-1.5 * (t - 1.0)) * std::sin (twoPi * f * (t - 1.0) + 0.3);
        });
        CHECK (v.peakMax == Approx (0.012).epsilon (0.15));
        CHECK (v.fDom == Approx (f).epsilon (0.05));
    }
}

TEST_CASE ("Vibration accumulating detectors: KB_Fmax and KB_FTr over 30 s cycles", "[vibration][detectors]")
{
    const double twoPi = 2.0 * kPi, fs = 2000.0;
    // Cycle 1: 0.5 mm/s peak, cycle 2: 1 mm/s, cycle 3: 2 mm/s (rising, so no cycle
    // inherits the decay of a louder one)
    const auto v = runDetector (fs, 90.0, [&] (double t)
    {
        const double A = t < 30.0 ? 0.5e-3 : (t < 60.0 ? 1.0e-3 : 2.0e-3);
        return A * std::sin (twoPi * 100.0 * t);
    }, 1.0);   // the KB detectors use τ = 0.125 s regardless of the running RMS τ

    const double r = 1.0 / std::sqrt (2.0);
    CHECK (v.kbFmax == Approx (2.0e-3 * r).epsilon (0.01));
    CHECK (v.kbFTr  == Approx (r * 1.0e-3 * std::sqrt ((0.25 + 1.0 + 4.0) / 3.0)).epsilon (0.01));

    // Before the first cycle completes, KB_FTr is the running cycle's maximum
    const auto early = runDetector (fs, 10.0, [&] (double t) { return 1.0e-3 * std::sin (twoPi * 100.0 * t); }, 1.0);
    CHECK (early.kbFTr == Approx (1.0e-3 * r).epsilon (0.01));
}

TEST_CASE ("Vibration engine: vector sum of the accumulated values", "[vibration][detectors]")
{
    VibrationSettings s;
    s.k[0] = 1.4; s.k[1] = 1.4; s.k[2] = 1.0;
    s.highPassHz = 1.0;
    AxisInput axes[3] { { 0, 120.0 }, { 1, 120.0 }, { 2, 120.0 } };   // 1 FS = 1 m/s²
    VibrationEngine engine (48000.0, s, axes, false);

    const double twoPi = 2.0 * kPi, fs = 48000.0;
    const double A[3] { 0.5, 0.8, 1.2 };
    std::vector<float> buf[3];
    for (auto& b : buf) b.resize (480);
    VibrationSnapshot snap;
    long long n = 0;
    for (int block = 0; block < 1000; ++block)   // 10 s
    {
        for (int i = 0; i < 480; ++i, ++n)
            for (int a = 0; a < 3; ++a)
                buf[a][(size_t) i] = (float) (A[a] * std::sin (twoPi * (40.0 + 20.0 * a) * n / fs));
        const float* in[3] { buf[0].data(), buf[1].data(), buf[2].data() };
        engine.process (in, 480, true, snap);
    }
    REQUIRE (snap.valid);
    double sumAw2 = 0.0, sumVdv4 = 0.0;
    for (int a = 0; a < 3; ++a)
    {
        sumAw2  += s.k[a] * s.k[a] * snap.axis[a].awRms * snap.axis[a].awRms;
        sumVdv4 += std::pow (s.k[a] * snap.axis[a].vdv, 4.0);
    }
    CHECK (snap.axis[AxisSum].awRms == Approx (std::sqrt (sumAw2)).epsilon (1e-4));
    CHECK (snap.axis[AxisSum].vdv   == Approx (std::pow (sumVdv4, 0.25)).epsilon (1e-4));
    CHECK (snap.axis[AxisZ].awRms   == Approx (1.2 / std::sqrt (2.0)).epsilon (0.01));
    CHECK (snap.axis[AxisSum].mtvv  == Approx (std::sqrt (sumAw2)).epsilon (0.02));

    // Main values over the measurement
    CHECK (computeMainValue (MainValue::MaxAxisKAw, snap, s.k)
           == Approx (std::max ({ 1.4 * snap.axis[0].awRms, 1.4 * snap.axis[1].awRms, (double) snap.axis[2].awRms })));
    CHECK (computeMainValue (MainValue::SumAw, snap, s.k) == Approx (snap.axis[AxisSum].awRms));

    const auto summary = summarize (snap, MainValue::SumAw, s.k);
    CHECK (summary.vdv == Approx (snap.axis[AxisSum].vdv));
    CHECK (summary.ppvAxis == AxisZ);
    CHECK (dailyExposureA8 (0.5, 2.0) == Approx (0.25));
}
