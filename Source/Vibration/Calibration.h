#pragma once
#include <cmath>

namespace vib
{
//==============================================================================
// Accelerometer calibration (vibration spec §4).
//
// Per-channel calDb maps the input to acceleration level in dB re 1 µm/s² (ISO 1683):
//     L_a = 20·log10(rms_FS) + calDb
// so one full-scale unit (sample value 1.0) corresponds to scale() m/s².
namespace calibration
{
    constexpr double kRefAccel      = 1.0e-6;     // 1 µm/s²
    constexpr double kGravity       = 9.80665;    // m/s² per g
    constexpr double kCalibratorRms = 10.0;       // handheld calibrator, m/s² RMS at 159.2 Hz
    constexpr double kMinCalDb      = 60.0;
    constexpr double kMaxCalDb      = 220.0;

    // m/s² per full-scale unit
    inline double scale (double calDb) noexcept   { return std::pow (10.0, (calDb - 120.0) / 20.0); }

    inline double levelDb (double accelRms) noexcept { return 20.0 * std::log10 (accelRms / kRefAccel); }

    // Calibrator method: the reference acceleration (RMS) produced measuredRmsFS
    inline double calDbFromCalibrator (double measuredRmsFS, double referenceRms = kCalibratorRms) noexcept
    {
        return levelDb (referenceRms) - 20.0 * std::log10 (measuredRmsFS);
    }

    // Datasheet method. A full-scale sine (sample peak 1.0) produces fullScaleVrms at the
    // interface input; the sensor delivers sensitivity_mV_per_ms2 mV per m/s².
    // Sample value 1.0 ↔ √2·V_FS,rms / S m/s².
    inline double calDbFromDatasheet (double fullScaleVrms, double sensitivity_mV_per_ms2) noexcept
    {
        const double peakVolts = std::sqrt (2.0) * fullScaleVrms;
        return 120.0 + 20.0 * std::log10 (peakVolts * 1000.0 / sensitivity_mV_per_ms2);
    }

    inline double mVperMs2FromMVperG (double mVperG) noexcept { return mVperG / kGravity; }

    // dBu (0 dBu = 0.7746 Vrms) → Vrms, for interfaces specified as "+X dBu at 0 dBFS"
    inline double dBuToVrms (double dBu) noexcept { return 0.774597 * std::pow (10.0, dBu / 20.0); }
}
} // namespace vib
