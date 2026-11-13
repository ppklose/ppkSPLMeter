#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "VibrationReport.h"

// Vibration spec §9: the HTML report (self-contained, sections, values, assessment)

using Catch::Approx;

namespace
{
VibrationReport::Input baseInput (vib::Profile profile)
{
    VibrationReport::Input in;
    in.meta = { "Pump <A> & B", "Basement", "PPK", "Stud, M5", "Line 1\nLine 2" };
    in.version = "9.9.9";
    in.created = juce::Time (2026, 10, 13, 14, 30);
    in.device = "Test Interface (CoreAudio)";
    in.sampleRate = 48000.0;
    in.serial = "SN 4711";
    in.profile = profile;
    const auto& def = vib::profileDefinition (profile);
    in.settings = def.settings;
    in.profileLabel = juce::String::fromUTF8 (def.name);
    in.mainLabel = juce::String::fromUTF8 (def.mainLabel);
    in.limits = vib::defaultAssessmentLimits();
    in.channels = { { "IN 1", "Mic", 0.0, {}, false },
                    { "IN 2", "Accel X", 151.5, juce::String::fromUTF8 ("2026-11-12 · calibrator"), true },
                    { "IN 3", "Accel Y", 151.0, {}, true },
                    { "IN 4", "Accel Z", 150.5, {}, true } };

    auto& s = in.snap;
    s.valid = true;
    s.axisMask = 7;
    s.quantity = (int) def.settings.quantity;
    for (int a = 0; a < 3; ++a) s.weighting[a] = (int) def.settings.weighting[a];
    s.seconds = 600.0;
    for (int a = 0; a < 4; ++a)
    {
        s.axis[a].rms = s.axis[a].awRms = 0.002f * (float) (a + 1);
        s.axis[a].peakMax = 0.006f * (float) (a + 1);
        s.axis[a].crest = 3.0f;
        s.axis[a].fDom = 25.0f;
        s.axis[a].kbFmax = 0.0002f;
        s.axis[a].kbFTm = 0.00005f;
    }
    s.numBands = 32;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 32; ++b)
            s.bandRms[a][b] = 1.0e-4f * (float) (b + 1);

    // 20 s of log, one marker, one pause
    const juce::int64 t0 = 1'800'000'000'000LL;
    for (int i = 0; i < 160; ++i)
    {
        LogEntry e;
        e.timestampMs = t0 + i * 125;
        e.rmsDBASPL = 60.0f;
        e.rmsDBCSPL = 70.0f + (i == 50 ? 10.0f : 0.0f);
        e.vib.valid = true;
        e.vib.axisMask = 7;
        e.vib.quantity = s.quantity;
        for (int a = 0; a < 4; ++a) e.vib.rms[a] = 0.002f * (float) (a + 1);
        in.log.push_back (e);
    }
    in.events = { { t0 + 5000, 0, "Start <pump>", false }, { t0 + 10000, 3000, {}, true } };
    in.hasMic = true;
    return in;
}

bool has (const juce::String& h, const char* part) { return h.contains (juce::String::fromUTF8 (part)); }
}

TEST_CASE ("Vibration report: self-contained HTML with all sections", "[vibration][report]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto h = VibrationReport::generateHtml (baseInput (vib::Profile::MachineryIso20816));

    CHECK (h.startsWith ("<!DOCTYPE html>"));
    CHECK (h.trimEnd().endsWith ("</html>"));
    for (auto* section : { "Measurement", "Setup", "Results", "Assessment", "Time history",
                           "1/3-octave spectrum", "Events", "Sound level (microphone)", "Disclaimer" })
        CHECK (has (h, section));

    // No external resources: no scripts, links, images or stylesheets
    CHECK_FALSE (h.containsIgnoreCase ("<script"));
    CHECK_FALSE (h.containsIgnoreCase ("<link"));
    CHECK_FALSE (h.containsIgnoreCase ("<img"));
    CHECK_FALSE (h.containsIgnoreCase ("src="));
    CHECK_FALSE (h.containsIgnoreCase ("href="));
    CHECK_FALSE (h.containsIgnoreCase ("url("));

    // User text is escaped
    CHECK (has (h, "Pump &lt;A&gt; &amp; B"));
    CHECK_FALSE (has (h, "<A>"));
    CHECK (has (h, "Start &lt;pump&gt;"));
    CHECK (has (h, "Line 1<br>Line 2"));

    // Setup
    CHECK (has (h, "Test Interface (CoreAudio)"));
    CHECK (has (h, "SN 4711"));
    CHECK (has (h, "151.50"));
    CHECK (has (h, "2026-11-12 · calibrator"));
    CHECK (has (h, "SPLMeter v9.9.9"));
    CHECK (has (h, "10 min 0 s"));

    // Charts: time history, spectrum; no PPV chart for machinery
    CHECK (h.indexOf ("<svg") >= 0);
    CHECK (h.indexOf ("<svg") != h.lastIndexOf ("<svg"));
    CHECK_FALSE (has (h, "PPV and dominant frequency"));

    // Machinery: largest axis v_rms 6 mm/s → zone D for group 2 rigid
    CHECK (has (h, "Zone D"));
    CHECK (has (h, "class=\"verdict bad\""));

    // Mic summary: LAeq 60.0, LAFmax 60.0, LCeq above 70
    CHECK (has (h, "L<sub>Aeq</sub></th><td>60.0 dB"));
    CHECK (has (h, "L<sub>AFmax</sub>"));

    // Events: marker and pause with duration
    CHECK (has (h, "Marker: Start &lt;pump&gt;"));
    CHECK (has (h, "Pause (3.0 s)"));
    CHECK (has (h, "Measured with SPLMeter v9.9.9"));
}

TEST_CASE ("Vibration report: building profiles show their values", "[vibration][report]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto in = baseInput (vib::Profile::BuildingStructural);
    auto h = VibrationReport::generateHtml (in);
    CHECK (has (h, "PPV and dominant frequency (DIN 4150-3)"));
    CHECK (has (h, "DIN 4150-3, short-term vibration"));
    CHECK (has (h, "Line 2"));
    // X: 6 mm/s at 25 Hz, value 5 + 10 · 15/40 = 8.75 mm/s
    CHECK (has (h, "6.00 mm/s at 25.0 Hz · value 8.75 mm/s"));
    CHECK (has (h, "class=\"verdict bad\""));   // Z: 18 mm/s > 8.75

    in = baseInput (vib::Profile::BuildingPerception);
    in.limits.din2ExposureHours = 4.0;
    h = VibrationReport::generateHtml (in);
    CHECK (has (h, "KB<sub>Fmax</sub>"));
    CHECK (has (h, "Residential (WR, WA)"));
    CHECK (has (h, "T_e = 4.00 h"));
    // KB_FTr = 0.05 · √(4/16) = 0.025
    CHECK (has (h, "<td>0.0250</td>"));
    CHECK (has (h, "KB_Fmax > A_u") == false);
    CHECK (has (h, "class=\"verdict ok\""));    // KB_Fmax 0.2 > A_u 0.15, KB_FTr 0.025 ≤ A_r 0.07
}

TEST_CASE ("Vibration report: human exposure, no assessment, no mic", "[vibration][report]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto in = baseInput (vib::Profile::HandArm);
    in.snap.axis[vib::AxisSum].awRms = 3.0f;
    in.exposureHours = 8.0;
    in.hasMic = false;
    auto h = VibrationReport::generateHtml (in);
    CHECK (has (h, "Directive 2002/44/EC, ISO 5349-1"));
    CHECK (has (h, "Action value 2.5 m/s² exceeded"));
    CHECK (has (h, "class=\"verdict warn\""));
    CHECK (has (h, "VDV"));
    CHECK_FALSE (has (h, "Sound level (microphone)"));

    in = baseInput (vib::Profile::StructureBorne);
    h = VibrationReport::generateHtml (in);
    CHECK (has (h, "No assessment for this profile."));

    in.events.clear();
    in.log.clear();
    h = VibrationReport::generateHtml (in);
    CHECK (has (h, "No markers or pauses."));
    CHECK_FALSE (has (h, "Time history covers"));
}

TEST_CASE ("Vibration report: suggested file name", "[vibration][report]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto in = baseInput (vib::Profile::HandArm);
    CHECK (VibrationReport::suggestedFileName (in) == "SPLMeter_Vibration_Pump_A__B_2026-11-13_14-30.html");
    in.meta.project = {};
    CHECK (VibrationReport::suggestedFileName (in) == "SPLMeter_Vibration_2026-11-13_14-30.html");
}
