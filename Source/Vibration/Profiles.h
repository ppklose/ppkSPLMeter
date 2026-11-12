#pragma once
#include "VibrationEngine.h"
#include "Units.h"
#include <iterator>
#include <string>

namespace vib
{
//==============================================================================
// Measurement profiles (vibration spec §6). A profile sets the defaults for
// quantity, weighting, filters, k-factors, time constant, main value and display
// unit; every value stays editable. When the settings no longer match the stored
// profile it is shown as "Custom (based on …)".
// Values are persisted — only append new entries.
enum class Profile : int
{
    Custom = 0,
    StructureBorne,
    MachineryIso20816,
    MachineryLowSpeed,
    WholeBodyHealth,
    WholeBodyComfort,
    HandArm,
    BuildingPerception,
    BuildingStructural,
    NumProfiles
};

struct ProfileDefinition
{
    const char*  name;        // UI and report
    const char*  key;         // CSV vib_profile, JSON
    const char*  mainLabel;   // symbol of the main value, UTF-8
    VibrationSettings settings;
    DisplayUnit  unit;
};

inline VibrationSettings makeSettings (Quantity q, Weighting wx, Weighting wy, Weighting wz,
                                       double bandLow, double bandHigh, double hp,
                                       double kx, double ky, double kz, double tau, MainValue main)
{
    VibrationSettings s;
    s.quantity   = q;
    s.weighting[0] = wx; s.weighting[1] = wy; s.weighting[2] = wz;
    s.bandLowHz  = bandLow;
    s.bandHighHz = bandHigh;
    s.highPassHz = hp;
    s.k[0] = kx; s.k[1] = ky; s.k[2] = kz;
    s.tauSec     = tau;
    s.main       = main;
    return s;
}

// Main values that need the accumulating detectors of step 10 (VDV, A(8), KB_FTr,
// f_dom) use the closest running value until then.
inline const ProfileDefinition& profileDefinition (Profile p)
{
    using Q = Quantity;
    using W = Weighting;
    using M = MainValue;
    static const ProfileDefinition defs[] = {
        { "Custom", "custom", "",
          VibrationSettings {}, DisplayUnit::Linear },
        { "Structure-borne", "structure-borne", "L_a",
          makeSettings (Q::Acceleration, W::None, W::None, W::None, 10.0, 1000.0, 1.0, 1, 1, 1, 0.125, M::MaxAxisRms),
          DisplayUnit::Db },
        { "Machinery (ISO 20816)", "machinery-iso20816", "v_rms",
          makeSettings (Q::Velocity, W::BandLimit, W::BandLimit, W::BandLimit, 10.0, 1000.0, 2.0, 1, 1, 1, 1.0, M::MaxAxisRms),
          DisplayUnit::Linear },
        { "Machinery, low speed", "machinery-low-speed", "v_rms",
          makeSettings (Q::Velocity, W::BandLimit, W::BandLimit, W::BandLimit, 2.0, 1000.0, 1.0, 1, 1, 1, 1.0, M::MaxAxisRms),
          DisplayUnit::Linear },
        { "Whole-body health (ISO 2631-1)", "whole-body-health", "a_w",
          makeSettings (Q::Acceleration, W::Wd, W::Wd, W::Wk, 10.0, 1000.0, 0.1, 1.4, 1.4, 1.0, 1.0, M::MaxAxisKRms),
          DisplayUnit::Linear },
        { "Whole-body comfort (ISO 2631-1)", "whole-body-comfort", "a_v",
          makeSettings (Q::Acceleration, W::Wd, W::Wd, W::Wk, 10.0, 1000.0, 0.1, 1, 1, 1, 1.0, M::SumRms),
          DisplayUnit::Linear },
        { "Hand-arm (ISO 5349-1)", "hand-arm", "a_hv",
          makeSettings (Q::Acceleration, W::Wh, W::Wh, W::Wh, 10.0, 1000.0, 2.0, 1, 1, 1, 1.0, M::SumRms),
          DisplayUnit::Linear },
        { "Building, perception (DIN 4150-2)", "building-perception", "KB_F",
          makeSettings (Q::Velocity, W::KB, W::KB, W::KB, 10.0, 1000.0, 0.5, 1, 1, 1, 0.125, M::MaxAxisRms),
          DisplayUnit::Linear },
        { "Building, structural (DIN 4150-3)", "building-structural", "PPV",
          makeSettings (Q::Velocity, W::BandLimit, W::BandLimit, W::BandLimit, 1.0, 315.0, 0.5, 1, 1, 1, 0.125, M::MaxAxisPeakMax),
          DisplayUnit::Linear },
    };
    static_assert (std::size (defs) == (size_t) Profile::NumProfiles);
    const int i = (int) p;
    return defs[(i >= 0 && i < (int) Profile::NumProfiles) ? i : 0];
}

inline Profile profileFromInt (int v) noexcept
{
    return (v >= 0 && v < (int) Profile::NumProfiles) ? (Profile) v : Profile::Custom;
}

// Do the settings still equal the profile's? (The display unit is a view
// choice and does not make a profile custom.)
inline bool settingsMatchProfile (const VibrationSettings& s, Profile p)
{
    if (p == Profile::Custom)
        return false;
    const auto& d = profileDefinition (p).settings;
    auto near = [] (double a, double b) { return std::abs (a - b) <= 1.0e-6 * std::max (1.0, std::abs (b)); };
    bool bandUsed = false;
    for (int a = 0; a < 3; ++a)
    {
        if (s.weighting[a] != d.weighting[a] || ! near (s.k[a], d.k[a]))
            return false;
        bandUsed = bandUsed || d.weighting[a] == Weighting::BandLimit;
    }
    if (bandUsed && (! near (s.bandLowHz, d.bandLowHz) || ! near (s.bandHighHz, d.bandHighHz)))
        return false;
    return s.quantity == d.quantity && s.main == d.main
        && near (s.highPassHz, d.highPassHz) && near (s.tauSec, d.tauSec);
}

// "Whole-body health (ISO 2631-1)", "Custom (based on …)" or "Custom" (UTF-8)
inline std::string profileLabel (const VibrationSettings& s, Profile base)
{
    if (base == Profile::Custom)
        return "Custom";
    if (settingsMatchProfile (s, base))
        return profileDefinition (base).name;
    return std::string ("Custom (based on ") + profileDefinition (base).name + ")";
}

// Symbol of the main value for the panel and report
inline std::string mainValueLabel (const VibrationSettings& s, Profile base)
{
    if (base != Profile::Custom && s.main == profileDefinition (base).settings.main)
        return profileDefinition (base).mainLabel;
    switch (s.main)
    {
        case MainValue::MaxAxisRms:     return "max RMS";
        case MainValue::MaxAxisKRms:    return "max k\xc2\xb7RMS";
        case MainValue::SumRms:         return "\xce\xa3 RMS";
        case MainValue::MaxAxisPeakMax: return "max peak";
        case MainValue::None:           break;
    }
    return {};
}
} // namespace vib
