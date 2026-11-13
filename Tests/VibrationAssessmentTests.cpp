#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Vibration/Assessment.h"

#include <cmath>
#include <string>

// Vibration spec §9.3: assessment of a measurement against the reference values
// of EU 2002/44/EC, ISO 2631-1, ISO 10816-3 / 20816-3, DIN 4150-2 and DIN 4150-3.

using Catch::Approx;
using namespace vib;

namespace
{
const double kOne[3] { 1.0, 1.0, 1.0 };
const double kHealth[3] { 1.4, 1.4, 1.0 };

VibrationSnapshot snap (Quantity q, double seconds = 3600.0)
{
    VibrationSnapshot s;
    s.valid = true;
    s.axisMask = 7;
    s.quantity = (int) q;
    s.seconds = seconds;
    return s;
}

bool contains (const std::string& s, const char* part) { return s.find (part) != std::string::npos; }
}

TEST_CASE ("Assessment: default limits are the standards' values", "[vibration][assessment]")
{
    const auto l = defaultAssessmentLimits();
    CHECK (l.wbAction == 0.5);
    CHECK (l.wbLimitXY == 1.15);
    CHECK (l.wbLimitZ == 1.15);
    CHECK (l.haAction == 2.5);
    CHECK (l.haLimit == 5.0);
    CHECK (l.machAB == 1.4);   // group 2, rigid
    CHECK (l.machCD == 4.5);
    CHECK (l.din2Au == 0.15);  // residential, day
    CHECK (l.din2Ao == 3.0);
    CHECK (l.din2Ar == 0.07);
    CHECK (l.din2ExposureHours == 16.0);
    CHECK (l.din3At10 == 5.0); // line 2, foundation
    CHECK (l.din3At50 == 15.0);
    CHECK (l.din3At100 == 20.0);
}

TEST_CASE ("Assessment: whole-body health A(8), EU and Germany", "[vibration][assessment]")
{
    auto l = defaultAssessmentLimits();
    auto s = snap (Quantity::Acceleration);

    // Z only: A(8) = a_wz at T = 8 h
    s.axis[AxisZ].awRms = 0.49f;
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 8.0, l).verdict == Verdict::Ok);
    s.axis[AxisZ].awRms = 0.51f;
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 8.0, l).verdict == Verdict::Warning);
    s.axis[AxisZ].awRms = 1.0f;
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 8.0, l).verdict == Verdict::Warning);
    // T = 2 h halves A(8)
    s.axis[AxisZ].awRms = 0.9f;
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 2.0, l).verdict == Verdict::Ok);

    // Germany: Z limit 0.8, X/Y 1.15
    applyWholeBodyPreset (l, WholeBodyPreset::Germany);
    CHECK (l.wbLimitZ == 0.8);
    s.axis[AxisZ].awRms = 0.81f;
    auto a = assess (Profile::WholeBodyHealth, s, kHealth, 8.0, l);
    CHECK (a.verdict == Verdict::Exceeded);
    CHECK (contains (a.text, "(Z)"));

    // X with k = 1.4: 0.8 · 1.4 = 1.12 < 1.15, 0.83 · 1.4 = 1.162 > 1.15
    s.axis[AxisZ].awRms = 0.0f;
    s.axis[AxisX].awRms = 0.80f;
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 8.0, l).verdict == Verdict::Warning);
    s.axis[AxisX].awRms = 0.83f;
    a = assess (Profile::WholeBodyHealth, s, kHealth, 8.0, l);
    CHECK (a.verdict == Verdict::Exceeded);
    CHECK (contains (a.text, "(X)"));
}

TEST_CASE ("Assessment: whole-body health VDV, scaled to the daily exposure", "[vibration][assessment]")
{
    auto l = defaultAssessmentLimits();
    l.wbCriterion = WholeBodyCriterion::Vdv;
    auto s = snap (Quantity::Acceleration, 3600.0);

    // 1 h measured, 16 h exposure: factor 16^¼ = 2
    CHECK (dailyVdv (5.0, 3600.0, 16.0) == Approx (10.0));
    s.axis[AxisZ].vdv = 4.5f;   // → 9.0
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 16.0, l).verdict == Verdict::Ok);
    s.axis[AxisZ].vdv = 4.6f;   // → 9.2
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 16.0, l).verdict == Verdict::Warning);
    s.axis[AxisZ].vdv = 10.6f;  // → 21.2
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 16.0, l).verdict == Verdict::Exceeded);
}

TEST_CASE ("Assessment: hand-arm A(8) of a_hv", "[vibration][assessment]")
{
    const auto l = defaultAssessmentLimits();
    auto s = snap (Quantity::Acceleration);
    s.axis[AxisSum].awRms = 2.4f;
    CHECK (assess (Profile::HandArm, s, kOne, 8.0, l).verdict == Verdict::Ok);
    s.axis[AxisSum].awRms = 2.6f;
    CHECK (assess (Profile::HandArm, s, kOne, 8.0, l).verdict == Verdict::Warning);
    s.axis[AxisSum].awRms = 5.1f;
    CHECK (assess (Profile::HandArm, s, kOne, 8.0, l).verdict == Verdict::Exceeded);
    // 10 m/s² for 2 h: A(8) = 5.0 → not above the limit
    s.axis[AxisSum].awRms = 10.0f;
    CHECK (assess (Profile::HandArm, s, kOne, 2.0, l).verdict == Verdict::Warning);
}

TEST_CASE ("Assessment: ISO 2631-1 comfort bands are informative", "[vibration][assessment]")
{
    const auto l = defaultAssessmentLimits();
    auto s = snap (Quantity::Acceleration);
    s.axis[AxisSum].awRms = 0.2f;
    auto a = assess (Profile::WholeBodyComfort, s, kOne, 8.0, l);
    CHECK (a.verdict == Verdict::Info);
    CHECK (a.text == "Not uncomfortable");
    CHECK (std::string (comfortBand (0.4)) == "A little uncomfortable");
    CHECK (std::string (comfortBand (0.55)) == "A little to fairly uncomfortable");
    CHECK (std::string (comfortBand (1.1)) == "Uncomfortable");
    CHECK (std::string (comfortBand (1.8)) == "Very uncomfortable");
    CHECK (std::string (comfortBand (3.0)) == "Extremely uncomfortable");
}

TEST_CASE ("Assessment: machinery zones A-D", "[vibration][assessment]")
{
    auto l = defaultAssessmentLimits();
    auto s = snap (Quantity::Velocity);
    auto zone = [&] (double mms) { s.axis[AxisY].awRms = (float) (mms * 1.0e-3); return assess (Profile::MachineryIso20816, s, kOne, 8.0, l); };
    CHECK (zone (1.3).text == "Zone A");
    CHECK (zone (1.5).text == "Zone B");
    CHECK (zone (1.5).verdict == Verdict::Ok);
    CHECK (zone (3.0).text == "Zone C");
    CHECK (zone (3.0).verdict == Verdict::Warning);
    CHECK (zone (4.6).verdict == Verdict::Exceeded);

    applyMachineryPreset (l, MachineGroup::Group1, Foundation::Flexible);
    CHECK (l.machAB == 3.5);
    CHECK (l.machBC == 7.1);
    CHECK (l.machCD == 11.0);
    CHECK (zone (4.6).text == "Zone B");
    CHECK (assess (Profile::MachineryLowSpeed, s, kOne, 8.0, l).text == "Zone B");
}

TEST_CASE ("Assessment: DIN 4150-2 decision flow", "[vibration][assessment]")
{
    auto l = defaultAssessmentLimits();   // residential, day: A_u 0.15, A_o 3, A_r 0.07
    auto s = snap (Quantity::Velocity);
    auto run = [&] (double fmax, double ftm)
    {
        s.axis[AxisZ].kbFmax = (float) (fmax * 1.0e-3);
        s.axis[AxisZ].kbFTm  = (float) (ftm  * 1.0e-3);
        return assess (Profile::BuildingPerception, s, kOne, 8.0, l);
    };
    CHECK (run (0.15, 0.1).verdict == Verdict::Ok);           // ≤ A_u
    CHECK (contains (run (0.15, 0.1).text, "A_u"));
    CHECK (run (3.1, 0.01).verdict == Verdict::Exceeded);     // > A_o
    CHECK (contains (run (3.1, 0.01).text, "A_o"));
    CHECK (run (1.0, 0.07).verdict == Verdict::Ok);           // KB_FTr = KB_FTm with T_e = T_r
    CHECK (run (1.0, 0.08).verdict == Verdict::Exceeded);
    CHECK (contains (run (1.0, 0.08).text, "A_r"));

    // KB_FTr = KB_FTm · √(T_e / T_r): 4 h of 16 h halves it
    l.din2ExposureHours = 4.0;
    CHECK (kbFTr (0.1, l) == Approx (0.05));
    CHECK (run (1.0, 0.12).verdict == Verdict::Ok);
    // T_e is capped at T_r
    l.din2ExposureHours = 24.0;
    CHECK (kbFTr (0.1, l) == Approx (0.1));

    // Night preset
    applyDin4150_2Preset (l, Din4150Zone::Residential, true);
    CHECK (l.din2Au == 0.1);
    CHECK (l.din2Ao == 0.2);
    CHECK (l.din2Ar == 0.05);
    CHECK (l.din2ExposureHours == 8.0);
    CHECK (run (0.3, 0.01).verdict == Verdict::Exceeded);
    applyDin4150_2Preset (l, Din4150Zone::Industrial, false);
    CHECK (l.din2Au == 0.4);
    CHECK (l.din2Ao == 6.0);
}

TEST_CASE ("Assessment: DIN 4150-3 frequency-dependent foundation values", "[vibration][assessment]")
{
    auto l = defaultAssessmentLimits();   // line 2: 5 / 5–15 / 15–20
    CHECK (din4150_3FoundationLimit (l, 0.0)   == Approx (5.0));    // unknown → lowest
    CHECK (din4150_3FoundationLimit (l, 4.0)   == Approx (5.0));
    CHECK (din4150_3FoundationLimit (l, 10.0)  == Approx (5.0));
    CHECK (din4150_3FoundationLimit (l, 30.0)  == Approx (10.0));
    CHECK (din4150_3FoundationLimit (l, 50.0)  == Approx (15.0));
    CHECK (din4150_3FoundationLimit (l, 75.0)  == Approx (17.5));
    CHECK (din4150_3FoundationLimit (l, 100.0) == Approx (20.0));
    CHECK (din4150_3FoundationLimit (l, 250.0) == Approx (20.0));

    applyDin4150_3Preset (l, Din4150Line::Line1, Din4150Location::Foundation, false);
    CHECK (din4150_3FoundationLimit (l, 30.0) == Approx (30.0));
    applyDin4150_3Preset (l, Din4150Line::Line3, Din4150Location::Foundation, false);
    CHECK (din4150_3FoundationLimit (l, 75.0) == Approx (9.0));
}

TEST_CASE ("Assessment: DIN 4150-3 PPV per axis and location", "[vibration][assessment]")
{
    auto l = defaultAssessmentLimits();
    auto s = snap (Quantity::Velocity);
    // X: 8 mm/s at 30 Hz (limit 10) → ok; Y: 6 mm/s at 8 Hz (limit 5) → exceeded
    s.axis[AxisX].peakMax = 8.0e-3f; s.axis[AxisX].fDom = 30.0f;
    s.axis[AxisY].peakMax = 6.0e-3f; s.axis[AxisY].fDom = 8.0f;
    auto a = assess (Profile::BuildingStructural, s, kOne, 8.0, l);
    CHECK (a.verdict == Verdict::Exceeded);
    CHECK (contains (a.text, "(Y, 8 Hz)"));
    s.axis[AxisY].fDom = 40.0f;   // limit 12.5
    CHECK (assess (Profile::BuildingStructural, s, kOne, 8.0, l).verdict == Verdict::Ok);

    // Top floor, horizontal: X/Y against one value; Z is ignored
    applyDin4150_3Preset (l, Din4150Line::Line2, Din4150Location::TopFloorHorizontal, false);
    CHECK (l.din3Single == 15.0);
    s.axis[AxisZ].peakMax = 30.0e-3f;
    CHECK (assess (Profile::BuildingStructural, s, kOne, 8.0, l).verdict == Verdict::Ok);
    applyDin4150_3Preset (l, Din4150Line::Line2, Din4150Location::TopFloorHorizontal, true);
    CHECK (l.din3Single == 5.0);
    CHECK (assess (Profile::BuildingStructural, s, kOne, 8.0, l).verdict == Verdict::Exceeded);
    applyDin4150_3Preset (l, Din4150Line::Line3, Din4150Location::TopFloorHorizontal, true);
    CHECK (l.din3Single == 2.5);
    applyDin4150_3Preset (l, Din4150Line::Line1, Din4150Location::TopFloorHorizontal, true);
    CHECK (l.din3Single == 10.0);

    // Floor slab, vertical: Z only; 20 short-term, 10 long-term for all lines
    applyDin4150_3Preset (l, Din4150Line::Line3, Din4150Location::FloorVertical, false);
    CHECK (l.din3Single == 20.0);
    s.axis[AxisZ].peakMax = 19.0e-3f;
    CHECK (assess (Profile::BuildingStructural, s, kOne, 8.0, l).verdict == Verdict::Ok);
    applyDin4150_3Preset (l, Din4150Line::Line3, Din4150Location::FloorVertical, true);
    CHECK (l.din3Single == 10.0);
    CHECK (assess (Profile::BuildingStructural, s, kOne, 8.0, l).verdict == Verdict::Exceeded);

    // Long-term values are not defined at the foundation
    applyDin4150_3Preset (l, Din4150Line::Line2, Din4150Location::Foundation, true);
    CHECK (assess (Profile::BuildingStructural, s, kOne, 8.0, l).verdict == Verdict::None);
}

TEST_CASE ("Assessment: no verdict without data, profile or matching quantity", "[vibration][assessment]")
{
    const auto l = defaultAssessmentLimits();
    auto s = snap (Quantity::Acceleration);
    s.axis[AxisSum].awRms = 10.0f;
    CHECK (assess (Profile::StructureBorne, s, kOne, 8.0, l).verdict == Verdict::None);
    CHECK (assess (Profile::Custom, s, kOne, 8.0, l).verdict == Verdict::None);
    CHECK (assess (Profile::MachineryIso20816, s, kOne, 8.0, l).verdict == Verdict::None);   // needs velocity
    s.seconds = 0.0;
    CHECK (assess (Profile::HandArm, s, kOne, 8.0, l).verdict == Verdict::None);
    s.seconds = 10.0;
    s.valid = false;
    CHECK (assess (Profile::HandArm, s, kOne, 8.0, l).verdict == Verdict::None);
}

TEST_CASE ("Assessment: uniaxial sensor uses its single axis", "[vibration][assessment]")
{
    auto l = defaultAssessmentLimits();
    applyWholeBodyPreset (l, WholeBodyPreset::Germany);
    auto s = snap (Quantity::Acceleration);
    s.axisMask = 1;
    s.uniaxial = true;
    // Slot X with its weighting and k: 0.8 · 1.4 = 1.12 → below the X/Y limit 1.15 (not Z's 0.8)
    s.axis[AxisX].awRms = 0.8f;
    CHECK (assess (Profile::WholeBodyHealth, s, kHealth, 8.0, l).verdict == Verdict::Warning);
    s.axis[AxisX].awRms = 0.83f;
    const auto a = assess (Profile::WholeBodyHealth, s, kHealth, 8.0, l);
    CHECK (a.verdict == Verdict::Exceeded);
    CHECK (contains (a.text, "(axis)"));
    s.axis[AxisX].awRms = 2.4f;
    CHECK (assess (Profile::HandArm, s, kOne, 8.0, l).verdict == Verdict::Ok);   // Σ absent: the axis
}

TEST_CASE ("Assessment: a value on the boundary is not exceeded", "[vibration][assessment]")
{
    const auto l = defaultAssessmentLimits();
    auto s = snap (Quantity::Velocity);
    s.axis[AxisX].awRms = 1.4e-3f;                      // zone A/B boundary
    CHECK (assess (Profile::MachineryIso20816, s, kOne, 8.0, l).text == "Zone A");
    s.axis[AxisX].peakMax = 5.0e-3f; s.axis[AxisX].fDom = 5.0f;   // DIN 4150-3 line 2: 5 mm/s
    CHECK (assess (Profile::BuildingStructural, s, kOne, 8.0, l).verdict == Verdict::Ok);
    auto a = snap (Quantity::Acceleration);
    a.axis[AxisSum].awRms = 2.5f;
    CHECK (assess (Profile::HandArm, a, kOne, 8.0, l).verdict == Verdict::Ok);
}
