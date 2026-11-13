#pragma once
#include <array>
#include <cstddef>

//==============================================================================
// Per-input sensor roles (vibration spec §3.1).
//
// Every physical input has a role. The default (all Mic) is the pre-3.6
// behaviour: every input is part of the microphone mix. Accelerometer and Off
// inputs are excluded from the mix, the 20 Hz bandpass and the correction filter.
// The numeric values are persisted in UIState/Inputs — never reorder them.
enum class SensorRole : int
{
    Mic           = 0,
    AccelX        = 1,
    AccelY        = 2,
    AccelZ        = 3,
    AccelUniaxial = 4,
    Off           = 5
};

namespace SensorConfig
{
    static constexpr int kMaxInputs = 32;

    using Roles = std::array<SensorRole, kMaxInputs>;

    inline Roles defaultRoles() noexcept
    {
        Roles r;
        r.fill (SensorRole::Mic);
        return r;
    }

    inline bool isAccel (SensorRole r) noexcept
    {
        return r == SensorRole::AccelX || r == SensorRole::AccelY
            || r == SensorRole::AccelZ || r == SensorRole::AccelUniaxial;
    }

    inline SensorRole fromInt (int v) noexcept
    {
        return (v >= 0 && v <= static_cast<int> (SensorRole::Off)) ? static_cast<SensorRole> (v)
                                                                     : SensorRole::Mic;
    }

    enum class Validity
    {
        NoAccelerometer,   // only Mic / Off — valid, accelerometer path inactive
        Triaxial,          // one channel each for X, Y and Z
        Biaxial,           // two of X/Y/Z (allowed: e.g. 2-channel interface)
        SingleAxis,        // exactly one of X/Y/Z
        Uniaxial,          // exactly one AccelUniaxial
        DuplicateAxis,     // invalid: an axis is assigned more than once
        MixedKinds         // invalid: AccelUniaxial combined with X/Y/Z, or >1 uniaxial
    };

    // One accelerometer sensor per session (spec §1): either up to one channel
    // per axis X/Y/Z, or exactly one uniaxial channel.
    inline Validity validate (const Roles& roles, int numInputs = kMaxInputs) noexcept
    {
        int x = 0, y = 0, z = 0, uni = 0;
        const int n = numInputs < kMaxInputs ? numInputs : kMaxInputs;
        for (int ch = 0; ch < n; ++ch)
        {
            switch (roles[static_cast<size_t> (ch)])
            {
                case SensorRole::AccelX:        ++x;   break;
                case SensorRole::AccelY:        ++y;   break;
                case SensorRole::AccelZ:        ++z;   break;
                case SensorRole::AccelUniaxial: ++uni; break;
                case SensorRole::Mic:
                case SensorRole::Off:           break;
            }
        }

        const int axes = (x > 0) + (y > 0) + (z > 0);
        if (uni > 1 || (uni == 1 && axes > 0)) return Validity::MixedKinds;
        if (x > 1 || y > 1 || z > 1)           return Validity::DuplicateAxis;
        if (uni == 1)                          return Validity::Uniaxial;
        switch (axes)
        {
            case 3:  return Validity::Triaxial;
            case 2:  return Validity::Biaxial;
            case 1:  return Validity::SingleAxis;
            default: return Validity::NoAccelerometer;
        }
    }

    inline bool isValid (Validity v) noexcept
    {
        return v != Validity::DuplicateAxis && v != Validity::MixedKinds;
    }
}
