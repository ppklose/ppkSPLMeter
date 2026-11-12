#pragma once
#include "AccelChannel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace vib
{
//==============================================================================
// Display units of the vibration readout (vibration spec §3.2 displayUnit).
// Detector values are SI (m/s², m/s, m); the display converts them.
// Values are persisted in UIState/Accel — only append new entries.
enum class DisplayUnit : int
{
    Linear = 0,   // acceleration m/s², velocity mm/s, displacement µm
    G      = 1,   // acceleration in g (other quantities fall back to Linear)
    Db     = 2    // dB re 1 µm/s² / 1 nm/s / 1 pm (ISO 1683)
};

namespace units
{
    // ISO 1683 reference values
    inline double dbReference (Quantity q) noexcept
    {
        switch (q)
        {
            case Quantity::Velocity:     return 1.0e-9;
            case Quantity::Displacement: return 1.0e-12;
            case Quantity::Acceleration: break;
        }
        return 1.0e-6;
    }

    inline double levelDb (double si, Quantity q) noexcept
    {
        return 20.0 * std::log10 (std::max (si, 1.0e-30) / dbReference (q));
    }

    // The unit actually used (G only applies to acceleration)
    inline DisplayUnit effective (DisplayUnit u, Quantity q) noexcept
    {
        return (u == DisplayUnit::G && q != Quantity::Acceleration) ? DisplayUnit::Linear : u;
    }

    // UTF-8 unit label
    inline const char* label (DisplayUnit u, Quantity q) noexcept
    {
        switch (effective (u, q))
        {
            case DisplayUnit::G:  return "g";
            case DisplayUnit::Db:
                switch (q)
                {
                    case Quantity::Velocity:     return "dB re 1 nm/s";
                    case Quantity::Displacement: return "dB re 1 pm";
                    case Quantity::Acceleration: break;
                }
                return "dB re 1 \xc2\xb5m/s\xc2\xb2";
            case DisplayUnit::Linear: break;
        }
        switch (q)
        {
            case Quantity::Velocity:     return "mm/s";
            case Quantity::Displacement: return "\xc2\xb5m";
            case Quantity::Acceleration: break;
        }
        return "m/s\xc2\xb2";
    }

    // SI value → number in the display unit
    inline double convert (double si, DisplayUnit u, Quantity q) noexcept
    {
        switch (effective (u, q))
        {
            case DisplayUnit::G:  return si / calibration::kGravity;
            case DisplayUnit::Db: return levelDb (si, q);
            case DisplayUnit::Linear: break;
        }
        switch (q)
        {
            case Quantity::Velocity:     return si * 1.0e3;
            case Quantity::Displacement: return si * 1.0e6;
            case Quantity::Acceleration: break;
        }
        return si;
    }

    // Number in the display unit, 3 significant digits (dB: one decimal)
    inline std::string format (double si, DisplayUnit u, Quantity q)
    {
        const double v = convert (si, u, q);
        char buf[32];
        if (effective (u, q) == DisplayUnit::Db)
        {
            std::snprintf (buf, sizeof (buf), "%.1f", v);
            return buf;
        }
        const double a = std::fabs (v);
        const int decimals = a >= 100.0 ? 0 : a >= 10.0 ? 1 : a >= 1.0 ? 2 : a >= 0.1 ? 3 : 4;
        std::snprintf (buf, sizeof (buf), "%.*f", decimals, v);
        return buf;
    }

    // Bar range of the panel in dB re the ISO 1683 reference (100 dB span)
    inline void barRangeDb (Quantity q, double& lo, double& hi) noexcept
    {
        switch (q)
        {
            case Quantity::Velocity:     lo = 60.0; hi = 160.0; return;   // 1 µm/s … 100 mm/s
            case Quantity::Displacement: lo = 80.0; hi = 180.0; return;   // 10 nm … 1 mm
            case Quantity::Acceleration: break;
        }
        lo = 60.0; hi = 160.0;                                            // 1 mm/s² … 100 m/s²
    }
}
} // namespace vib
