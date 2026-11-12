#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Vibration columns of the CSV log export (vibration spec §8). They are appended
// after the existing columns and left empty without accelerometer readings, so
// existing column positions never move. Values are in the display unit.
namespace VibrationCsv
{
    inline juce::String header()
    {
        return ",vibX_rms,vibY_rms,vibZ_rms,vibSum_rms,vibX_pk,vibY_pk,vibZ_pk,vibSum_pk,vib_main,vib_unit,vib_profile";
    }

    inline juce::String columns (const VibrationLog& v, vib::DisplayUnit unit)
    {
        if (! v.valid)
            return ",,,,,,,,,,,";

        const auto q = (vib::Quantity) v.quantity;
        int axes = 0;
        for (int a = 0; a < 3; ++a)
            axes += (v.axisMask >> a) & 1;
        const bool hasSum = ! v.uniaxial && axes > 1;
        auto present = [&] (int slot) { return slot == vib::AxisSum ? hasSum : ((v.axisMask >> slot) & 1) != 0; };
        auto num = [&] (float si)
        {
            char buf[32];
            std::snprintf (buf, sizeof (buf), "%.6g", vib::units::convert (si, unit, q));
            return juce::String (buf);
        };

        juce::String s;
        for (const float* values : { v.rms, v.peak })
            for (int slot = 0; slot < vib::kNumAxisSlots; ++slot)
                s << ',' << (present (slot) ? num (values[slot]) : juce::String());

        // Main value: Σ when there is a vector sum, otherwise the single axis
        int mainSlot = vib::AxisSum;
        if (! hasSum)
            for (int a = 2; a >= 0; --a)
                if (present (a)) mainSlot = a;
        s << ',' << num (v.rms[mainSlot]);
        s << ',' << juce::String::fromUTF8 (vib::units::label (unit, q));
        s << ',';   // profile: phase 1b
        return s;
    }
}
