#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Rew/RewRunResults.h"

// REW sequencer spec §6.2 / step 6: run results as CSV and JSON

using Catch::Approx;

namespace
{
struct Fixture
{
    rew::Sequence seq;
    rew::Progress p;
    std::vector<rew::StepLevels> levels;
    rew::RunInfo info;

    Fixture()
    {
        seq.name = "Room, \"A\"";
        rew::Step a;
        a.name = "L";
        a.output = juce::String ("L");
        a.secondOutput = rew::SecondOutput { false, {}, true };
        a.level = rew::Level { -12.0, "dBFS" };
        rew::Step b;
        b.name = "R";
        seq.steps = { a, b };

        p.state = rew::RunState::Done;
        p.version = { "5.40 Beta 147", "0.9.9" };
        rew::StepResult r1;
        r1.step = 0; r1.title = "L"; r1.status = rew::StepStatus::Warning;
        r1.messages = { "REW: Low SNR" };
        r1.uuids = { "u-1" };
        r1.start = juce::Time (2026, 10, 13, 12, 0);
        r1.durationSec = 12.5;
        rew::StepResult r2;
        r2.step = 1; r2.title = "R"; r2.status = rew::StepStatus::Skipped;
        p.results = { r1, r2 };

        rew::StepLevels l;
        l.valid = true;
        l.seconds = 12.4;
        l.laeq = 78.34; l.lceq = 80.0; l.lzeq = 81.0; l.lafmax = 85.0; l.lcsmax = 83.0; l.lcpeak = 99.0;
        l.vibValid = true;
        l.quantity = (int) vib::Quantity::Velocity;
        l.axisMask = 0b111;
        l.vibRms[0] = 0.001; l.vibRms[3] = 0.002;      // m/s
        l.vibPeak[0] = 0.003;
        levels = { l, rew::StepLevels {} };

        info.splmeterVersion = "3.6.0";
        info.calOffsetDb = 127.0;
        info.vibUnit = vib::DisplayUnit::Linear;   // velocity → mm/s
        info.vibProfile = "machinery";
        info.vibCalibration = { "X: IN 2, 151.50 dB" };
        info.finished = juce::Time (2026, 10, 13, 12, 5);
    }
};

juce::StringArray csvRows (const juce::String& csv)
{
    juce::StringArray rows;
    for (const auto& line : juce::StringArray::fromLines (csv))
        if (line.isNotEmpty() && ! line.startsWith ("#"))
            rows.add (line);
    return rows;
}
}

TEST_CASE ("REW run results: CSV header, values, empty cells and escaping", "[rew]")
{
    Fixture f;
    const auto csv = rew::toCsv (f.seq, f.p, f.levels, f.info);
    CHECK (csv.contains ("# Sequence: Room, \"A\""));
    CHECK (csv.contains ("# REW: 5.40 Beta 147, API 0.9.9"));
    CHECK (csv.contains ("# Vibration profile: machinery"));
    CHECK (csv.contains ("# Accelerometer X: IN 2, 151.50 dB"));

    const auto rows = csvRows (csv);
    REQUIRE (rows.size() == 3);
    const auto header = juce::StringArray::fromTokens (rows[0], ",", {});
    CHECK (header.size() == 29);
    CHECK (header.indexOf ("LAeq") == 12);

    const auto r1 = juce::StringArray::fromTokens (rows[1], ",", "\"");
    REQUIRE (r1.size() == 29);
    CHECK (r1[2] == "L");
    CHECK (r1[3] == "warning");
    CHECK (r1[6] == "REW: Low SNR");
    CHECK (r1[7] == "u-1");
    CHECK (r1[9] == "off (inverted)");
    CHECK (r1[11] == "-12.0 dBFS");
    CHECK (r1[12] == "78.3");
    CHECK (r1[20] == "mm/s");
    CHECK (r1[21] == "1");      // 0.001 m/s → 1 mm/s
    CHECK (r1[24] == "2");      // Σ
    CHECK (r1[25] == "3");      // X peak

    const auto r2 = juce::StringArray::fromTokens (rows[2], ",", "\"");
    REQUIRE (r2.size() == 29);
    CHECK (r2[3] == "skipped");
    CHECK (r2[12].isEmpty());   // no levels without a sweep
    CHECK (r2[21].isEmpty());

    CHECK (rew::results::csvField ("a,b") == "\"a,b\"");
    CHECK (rew::results::csvField ("say \"x\"") == "\"say \"\"x\"\"\"");
}

TEST_CASE ("REW run results: JSON and files", "[rew]")
{
    Fixture f;
    const auto v = rew::toJson (f.seq, f.p, f.levels, f.info);
    CHECK (v.getProperty ("format", {}).toString() == "splmeter-rew-run");
    CHECK (v.getProperty ("result", {}).toString() == "Done");
    rew::Sequence back;
    CHECK (rew::sequenceFromVar (v.getProperty ("sequence", {}), back));
    CHECK (back == f.seq);
    const auto steps = v.getProperty ("steps", {});
    REQUIRE (steps.size() == 2);
    CHECK ((double) steps[0].getProperty ("sound", {}).getProperty ("LAeq", 0.0) == Approx (78.34));
    CHECK ((double) steps[0].getProperty ("vibration", {}).getProperty ("Sum", {}).getProperty ("rms", 0.0) == Approx (2.0));
    CHECK (steps[0].getProperty ("vibration", {}).getDynamicObject()->hasProperty ("Y"));   // present axis, value 0
    CHECK (steps[1].getDynamicObject()->hasProperty ("sound") == false);

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rew_results_test");
    folder.deleteRecursively();
    f.seq.resultsFolder = folder.getFullPathName();
    juce::String error;
    const auto csv = rew::writeResults (f.seq, f.p, f.levels, f.info, error);
    REQUIRE (csv.existsAsFile());
    CHECK (csv.getFileName() == "Room A_2026-11-13_12-05-00.csv");   // illegal characters removed
    CHECK (csv.withFileExtension ("json").existsAsFile());
    const auto second = rew::writeResults (f.seq, f.p, f.levels, f.info, error);   // same second: no overwrite
    CHECK (second != csv);
    folder.deleteRecursively();
}
