#pragma once
#include "Profiles.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace vib
{
//==============================================================================
// Assessment of a measurement against reference values (vibration spec §9.3).
// All limits are editable; the presets below fill them from the standards.
// No JUCE: unit-tested in Tests/VibrationAssessmentTests.cpp.

enum class Verdict : int
{
    None = 0,   // no assessment (profile without one, no data, wrong quantity)
    Info,       // informative only (ISO 2631-1 comfort scale)
    Ok,
    Warning,    // action value exceeded, zone C
    Exceeded    // limit value exceeded, zone D, guideline value not met
};

struct Assessment
{
    Verdict     verdict = Verdict::None;
    std::string text;       // short, for the panel header (UTF-8)
};

// Values are persisted — only append new entries.
enum class WholeBodyPreset : int { Eu = 0, Germany };
enum class WholeBodyCriterion : int { A8 = 0, Vdv };
enum class MachineGroup : int { Group1 = 0, Group2 };
enum class Foundation : int { Rigid = 0, Flexible };
enum class Din4150Zone : int { Industrial = 0, Commercial, Mixed, Residential, Sensitive };
enum class Din4150Line : int { Line1 = 0, Line2, Line3 };
enum class Din4150Location : int { Foundation = 0, TopFloorHorizontal, FloorVertical };

struct AssessmentLimits
{
    // Whole-body health: EU 2002/44/EC, Germany LärmVibrationsArbSchV §9 (m/s², m/s^1.75)
    WholeBodyPreset    wbPreset    = WholeBodyPreset::Eu;
    WholeBodyCriterion wbCriterion = WholeBodyCriterion::A8;
    double wbAction = 0.5, wbLimitXY = 1.15, wbLimitZ = 1.15;
    double wbVdvAction = 9.1, wbVdvLimit = 21.0;

    // Hand-arm: EU 2002/44/EC (m/s²)
    double haAction = 2.5, haLimit = 5.0;

    // Machinery: ISO 10816-3 / 20816-3 zone boundaries A/B, B/C, C/D (mm/s RMS)
    MachineGroup machGroup      = MachineGroup::Group2;
    Foundation   machFoundation = Foundation::Rigid;
    double machAB = 1.4, machBC = 2.8, machCD = 4.5;

    // Building perception: DIN 4150-2 Table 1 (KB values), exposure time within T_r
    Din4150Zone din2Zone  = Din4150Zone::Residential;
    bool        din2Night = false;
    double din2Au = 0.15, din2Ao = 3.0, din2Ar = 0.07;
    double din2ExposureHours = 16.0;   // T_e; clamped to T_r (day 16 h, night 8 h)

    // Building structural: DIN 4150-3 Tables 1 and 3 (PPV, mm/s)
    Din4150Line     din3Line     = Din4150Line::Line2;
    Din4150Location din3Location = Din4150Location::Foundation;
    bool            din3LongTerm = false;
    double din3At10 = 5.0, din3At50 = 15.0, din3At100 = 20.0;   // foundation: 1–10 Hz, 50 Hz, 100 Hz
    double din3Single = 15.0;                                    // top floor / floor slab, all frequencies
};

//==============================================================================
// Presets (sources: spec §14)
inline void applyWholeBodyPreset (AssessmentLimits& l, WholeBodyPreset p) noexcept
{
    l.wbPreset    = p;
    l.wbAction    = 0.5;
    l.wbLimitXY   = 1.15;
    l.wbLimitZ    = p == WholeBodyPreset::Germany ? 0.8 : 1.15;
    l.wbVdvAction = 9.1;
    l.wbVdvLimit  = 21.0;
}

inline void applyMachineryPreset (AssessmentLimits& l, MachineGroup g, Foundation f) noexcept
{
    static const double t[2][2][3] = { { { 2.3, 4.5, 7.1 }, { 3.5, 7.1, 11.0 } },    // group 1: rigid, flexible
                                       { { 1.4, 2.8, 4.5 }, { 2.3, 4.5, 7.1 } } };   // group 2
    l.machGroup = g;
    l.machFoundation = f;
    const auto& v = t[(int) g == 0 ? 0 : 1][(int) f == 0 ? 0 : 1];
    l.machAB = v[0]; l.machBC = v[1]; l.machCD = v[2];
}

inline void applyDin4150_2Preset (AssessmentLimits& l, Din4150Zone z, bool night) noexcept
{
    static const double day[5][3]   = { { 0.4, 6, 0.2 }, { 0.3, 6, 0.15 }, { 0.2, 5, 0.1 }, { 0.15, 3, 0.07 }, { 0.1, 3, 0.05 } };
    static const double nightT[5][3] = { { 0.3, 0.6, 0.15 }, { 0.2, 0.4, 0.1 }, { 0.15, 0.3, 0.07 }, { 0.1, 0.2, 0.05 }, { 0.1, 0.15, 0.05 } };
    const int i = std::clamp ((int) z, 0, 4);
    const auto& v = night ? nightT[i] : day[i];
    l.din2Zone = (Din4150Zone) i;
    l.din2Night = night;
    l.din2Au = v[0]; l.din2Ao = v[1]; l.din2Ar = v[2];
    l.din2ExposureHours = night ? 8.0 : 16.0;
}

inline void applyDin4150_3Preset (AssessmentLimits& l, Din4150Line line, Din4150Location loc, bool longTerm) noexcept
{
    const int i = std::clamp ((int) line, 0, 2);
    static const double fdn[3][3]     = { { 20, 40, 50 }, { 5, 15, 20 }, { 3, 8, 10 } };   // Table 1: 1–10 Hz, 50 Hz, 100 Hz
    static const double topShort[3]   = { 40, 15, 8 };     // Table 1, top floor horizontal
    static const double topLong[3]    = { 10, 5, 2.5 };    // Table 3, top floor horizontal
    l.din3Line = (Din4150Line) i;
    l.din3Location = loc;
    l.din3LongTerm = longTerm;
    l.din3At10 = fdn[i][0]; l.din3At50 = fdn[i][1]; l.din3At100 = fdn[i][2];
    switch (loc)
    {
        case Din4150Location::TopFloorHorizontal: l.din3Single = longTerm ? topLong[i] : topShort[i]; break;
        case Din4150Location::FloorVertical:      l.din3Single = longTerm ? 10.0 : 20.0; break;   // all lines
        case Din4150Location::Foundation:         l.din3Single = topShort[i]; break;
    }
}

inline AssessmentLimits defaultAssessmentLimits() noexcept
{
    AssessmentLimits l;
    applyWholeBodyPreset (l, l.wbPreset);
    applyMachineryPreset (l, l.machGroup, l.machFoundation);
    applyDin4150_2Preset (l, l.din2Zone, l.din2Night);
    applyDin4150_3Preset (l, l.din3Line, l.din3Location, l.din3LongTerm);
    return l;
}

//==============================================================================
// Helpers (also used by the report)

// DIN 4150-3 Table 1, foundation: constant up to 10 Hz, linear 10–50 and
// 50–100 Hz, the 100 Hz value above. Unknown frequency (≤ 0): the 1–10 Hz value.
inline double din4150_3FoundationLimit (const AssessmentLimits& l, double fHz) noexcept
{
    if (fHz <= 10.0)  return l.din3At10;
    if (fHz <= 50.0)  return l.din3At10 + (l.din3At50 - l.din3At10) * (fHz - 10.0) / 40.0;
    if (fHz <= 100.0) return l.din3At50 + (l.din3At100 - l.din3At50) * (fHz - 50.0) / 50.0;
    return l.din3At100;
}

// DIN 4150-2: KB_FTr = KB_FTm · √(T_e / T_r), T_r = 16 h (day) or 8 h (night)
inline double din4150_2RatingHours (bool night) noexcept { return night ? 8.0 : 16.0; }

inline double kbFTr (double kbFTm, const AssessmentLimits& l) noexcept
{
    const double tr = din4150_2RatingHours (l.din2Night);
    return kbFTm * std::sqrt (std::clamp (l.din2ExposureHours, 0.0, tr) / tr);
}

// ISO 2631-1 Annex C (informative): the bands overlap, overlaps get both names
inline const char* comfortBand (double av) noexcept
{
    if (av < 0.315) return "Not uncomfortable";
    if (av < 0.5)   return "A little uncomfortable";
    if (av < 0.63)  return "A little to fairly uncomfortable";
    if (av < 0.8)   return "Fairly uncomfortable";
    if (av < 1.0)   return "Fairly uncomfortable to uncomfortable";
    if (av < 1.25)  return "Uncomfortable";
    if (av < 1.6)   return "Uncomfortable to very uncomfortable";
    if (av < 2.0)   return "Very uncomfortable";
    if (av < 2.5)   return "Very to extremely uncomfortable";
    return "Extremely uncomfortable";
}

// Daily VDV from a measurement of `measuredSec`: VDV · (T_exp / T_meas)^¼ (ISO 2631-1 §6.3.2)
inline double dailyVdv (double vdv, double measuredSec, double exposureHours) noexcept
{
    if (measuredSec <= 0.0) return vdv;
    return vdv * std::pow (std::max (0.0, exposureHours) * 3600.0 / measuredSec, 0.25);
}

namespace detail
{
    inline std::string num (double v)
    {
        char b[32];
        std::snprintf (b, sizeof (b), "%g", std::round (v * 1000.0) / 1000.0);
        return b;
    }
    // v > limit, with a relative tolerance: snapshot values are floats, so a value
    // on the boundary must not count as exceeded
    inline bool above (double v, double limit) noexcept { return v > limit * (1.0 + 1.0e-6); }

    inline const char* axisName (int a) noexcept { static const char* n[3] = { "X", "Y", "Z" }; return n[a]; }
    inline bool hasAxis (const VibrationSnapshot& s, int a) noexcept { return (s.axisMask & (1 << a)) != 0; }
    inline int numAxes (const VibrationSnapshot& s) noexcept
    {
        return (int) hasAxis (s, 0) + (int) hasAxis (s, 1) + (int) hasAxis (s, 2);
    }
    // Σ a_w over the measurement, or the single axis
    inline double sumAw (const VibrationSnapshot& s) noexcept
    {
        if (numAxes (s) > 1 && ! s.uniaxial) return s.axis[AxisSum].awRms;
        for (int a = 0; a < 3; ++a) if (hasAxis (s, a)) return s.axis[a].awRms;
        return 0.0;
    }
}

//==============================================================================
// The evaluators. `s` holds SI values (m/s², m/s); k are the vector-sum factors.

inline Assessment assessWholeBodyHealth (const VibrationSnapshot& s, const double (&k)[3],
                                         double exposureHours, const AssessmentLimits& l)
{
    using detail::num;
    if (l.wbCriterion == WholeBodyCriterion::Vdv)
    {
        double vdv = 0.0;
        for (int a = 0; a < 3; ++a)
            if (detail::hasAxis (s, a))
                vdv = std::max (vdv, k[a] * s.axis[a].vdv);
        vdv = dailyVdv (vdv, s.seconds, exposureHours);
        if (detail::above (vdv, l.wbVdvLimit))  return { Verdict::Exceeded, "VDV limit value " + num (l.wbVdvLimit) + " exceeded" };
        if (detail::above (vdv, l.wbVdvAction)) return { Verdict::Warning,  "VDV action value " + num (l.wbVdvAction) + " exceeded" };
        return { Verdict::Ok, "VDV below action value " + num (l.wbVdvAction) };
    }

    // A(8) of the highest k-weighted axis; the limit can differ for Z (Germany)
    double maxA8 = 0.0;
    int over = -1;
    for (int a = 0; a < 3; ++a)
    {
        if (! detail::hasAxis (s, a)) continue;
        const double a8 = dailyExposureA8 (k[a] * s.axis[a].awRms, exposureHours);
        maxA8 = std::max (maxA8, a8);
        // A uniaxial sensor uses slot X with X's weighting and k, so the X/Y limit
        if (detail::above (a8, a == 2 ? l.wbLimitZ : l.wbLimitXY) && over < 0)
            over = a;
    }
    if (over >= 0)
    {
        const double lim = over == 2 ? l.wbLimitZ : l.wbLimitXY;
        return { Verdict::Exceeded, "Limit value " + num (lim) + " m/s\xc2\xb2 exceeded ("
                                    + (s.uniaxial ? std::string ("axis") : std::string (detail::axisName (over))) + ")" };
    }
    if (detail::above (maxA8, l.wbAction))
        return { Verdict::Warning, "Action value " + num (l.wbAction) + " m/s\xc2\xb2 exceeded" };
    return { Verdict::Ok, "Below action value " + num (l.wbAction) + " m/s\xc2\xb2" };
}

inline Assessment assessHandArm (const VibrationSnapshot& s, double exposureHours, const AssessmentLimits& l)
{
    using detail::num;
    const double a8 = dailyExposureA8 (detail::sumAw (s), exposureHours);
    if (detail::above (a8, l.haLimit))  return { Verdict::Exceeded, "Limit value " + num (l.haLimit) + " m/s\xc2\xb2 exceeded" };
    if (detail::above (a8, l.haAction)) return { Verdict::Warning,  "Action value " + num (l.haAction) + " m/s\xc2\xb2 exceeded" };
    return { Verdict::Ok, "Below action value " + num (l.haAction) + " m/s\xc2\xb2" };
}

inline Assessment assessComfort (const VibrationSnapshot& s)
{
    return { Verdict::Info, comfortBand (detail::sumAw (s)) };
}

// Largest axis RMS over the measurement against zones A–D
inline Assessment assessMachinery (const VibrationSnapshot& s, const AssessmentLimits& l)
{
    double v = 0.0;
    for (int a = 0; a < 3; ++a)
        if (detail::hasAxis (s, a))
            v = std::max (v, (double) s.axis[a].awRms);
    v *= 1000.0;   // mm/s
    if (detail::above (v, l.machCD)) return { Verdict::Exceeded, "Zone D" };
    if (detail::above (v, l.machBC)) return { Verdict::Warning,  "Zone C" };
    if (detail::above (v, l.machAB)) return { Verdict::Ok,       "Zone B" };
    return { Verdict::Ok, "Zone A" };
}

// KB_Fmax ≤ A_u → met; KB_Fmax > A_o → not met; otherwise KB_FTr ≤ A_r → met
inline Assessment assessDin4150_2 (const VibrationSnapshot& s, const AssessmentLimits& l)
{
    double fmax = 0.0, ftm = 0.0;
    for (int a = 0; a < 3; ++a)
        if (detail::hasAxis (s, a))
        {
            fmax = std::max (fmax, (double) s.axis[a].kbFmax);
            ftm  = std::max (ftm,  (double) s.axis[a].kbFTm);
        }
    fmax *= 1000.0;   // KB values are numerically mm/s
    const double ftr = kbFTr (ftm * 1000.0, l);
    auto le = [] (double v, double limit) { return ! detail::above (v, limit); };
    if (le (fmax, l.din2Au))  return { Verdict::Ok,       "KB_Fmax \xe2\x89\xa4 A_u: met" };
    if (! le (fmax, l.din2Ao)) return { Verdict::Exceeded, "KB_Fmax > A_o: not met" };
    if (le (ftr, l.din2Ar))   return { Verdict::Ok,       "KB_FTr \xe2\x89\xa4 A_r: met" };
    return { Verdict::Exceeded, "KB_FTr > A_r: not met" };
}

// PPV per axis (peak since reset) against the guideline value for the location
inline Assessment assessDin4150_3 (const VibrationSnapshot& s, const AssessmentLimits& l)
{
    using detail::num;
    if (l.din3LongTerm && l.din3Location == Din4150Location::Foundation)
        return { Verdict::None, "Long-term: measure on the top floor or a floor slab" };

    double worstRatio = 0.0, worstLimit = 0.0, worstF = 0.0;
    int worstAxis = -1;
    for (int a = 0; a < 3; ++a)
    {
        if (! detail::hasAxis (s, a)) continue;
        // Top floor: horizontal axes; floor slab: vertical axis; uniaxial: its own axis
        if (! s.uniaxial)
        {
            if (l.din3Location == Din4150Location::TopFloorHorizontal && a == 2) continue;
            if (l.din3Location == Din4150Location::FloorVertical && a != 2) continue;
        }
        const double ppv = s.axis[a].peakMax * 1000.0;   // mm/s
        const double lim = l.din3Location == Din4150Location::Foundation
                         ? din4150_3FoundationLimit (l, s.axis[a].fDom) : l.din3Single;
        if (lim <= 0.0) continue;
        if (ppv / lim > worstRatio || worstAxis < 0)
        {
            worstRatio = ppv / lim; worstLimit = lim; worstF = s.axis[a].fDom; worstAxis = a;
        }
    }
    if (worstAxis < 0)
        return { Verdict::None, "No axis for this location" };
    std::string where = s.uniaxial ? std::string() : std::string (" (") + detail::axisName (worstAxis);
    if (l.din3Location == Din4150Location::Foundation)
        where += (where.empty() ? " (" : ", ") + (worstF > 0.0 ? num (worstF) + " Hz" : std::string ("f unknown"));
    if (! where.empty()) where += ")";
    if (detail::above (worstRatio, 1.0))
        return { Verdict::Exceeded, "Guideline value " + num (worstLimit) + " mm/s exceeded" + where };
    return { Verdict::Ok, "Below guideline value " + num (worstLimit) + " mm/s" + where };
}

//==============================================================================
// Assessment for a profile. Profiles without one, an invalid snapshot, no
// measured time yet or a quantity that does not match give Verdict::None.
inline Assessment assess (Profile base, const VibrationSnapshot& s, const double (&k)[3],
                          double exposureHours, const AssessmentLimits& l)
{
    if (! s.valid || s.axisMask == 0 || s.seconds <= 0.0f)
        return {};
    auto needs = [&] (Quantity q) { return s.quantity == (int) q; };
    switch (base)
    {
        case Profile::WholeBodyHealth:
            return needs (Quantity::Acceleration) ? assessWholeBodyHealth (s, k, exposureHours, l)
                                                  : Assessment { Verdict::None, "Needs acceleration" };
        case Profile::WholeBodyComfort:
            return needs (Quantity::Acceleration) ? assessComfort (s) : Assessment { Verdict::None, "Needs acceleration" };
        case Profile::HandArm:
            return needs (Quantity::Acceleration) ? assessHandArm (s, exposureHours, l)
                                                  : Assessment { Verdict::None, "Needs acceleration" };
        case Profile::MachineryIso20816:
        case Profile::MachineryLowSpeed:
            return needs (Quantity::Velocity) ? assessMachinery (s, l) : Assessment { Verdict::None, "Needs velocity" };
        case Profile::BuildingPerception:
            return needs (Quantity::Velocity) ? assessDin4150_2 (s, l) : Assessment { Verdict::None, "Needs velocity" };
        case Profile::BuildingStructural:
            return needs (Quantity::Velocity) ? assessDin4150_3 (s, l) : Assessment { Verdict::None, "Needs velocity" };
        case Profile::Custom:
        case Profile::StructureBorne:
        case Profile::NumProfiles:
            break;
    }
    return {};
}
} // namespace vib
