#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_core/juce_core.h>
#include <string>
#include <vector>
#include <map>
#include <cmath>

//==============================================================================
// Replicate MarkerEvent struct (standalone, no LogComponent dependency)
//==============================================================================
struct MarkerEvent
{
    juce::int64    timestampMs;
    juce::String   text;
};

//==============================================================================
// Replicate the CSV marker-matching logic from PluginEditor::writeCsvRows
//==============================================================================
static std::map<juce::int64, juce::String> matchMarkersToTimestamps (
    const std::vector<MarkerEvent>& markers,
    const std::vector<juce::int64>& logTimestamps)
{
    std::map<juce::int64, juce::String> result;
    for (const auto& mk : markers)
    {
        juce::int64 bestTs   = 0;
        juce::int64 bestDist = 200; // max snap distance ms
        for (auto ts : logTimestamps)
        {
            juce::int64 dist = std::abs (ts - mk.timestampMs);
            if (dist < bestDist) { bestDist = dist; bestTs = ts; }
        }
        if (bestTs != 0)
            result[bestTs] = mk.text.isEmpty() ? juce::String ("*") : mk.text;
    }
    return result;
}

//==============================================================================
// MarkerEvent struct tests
//==============================================================================
TEST_CASE ("MarkerEvent: default construction", "[marker]")
{
    MarkerEvent mk { 0, {} };
    REQUIRE (mk.timestampMs == 0);
    REQUIRE (mk.text.isEmpty());
}

TEST_CASE ("MarkerEvent: stores timestamp and text", "[marker]")
{
    MarkerEvent mk { 1000, "test marker" };
    REQUIRE (mk.timestampMs == 1000);
    REQUIRE (mk.text == "test marker");
}

TEST_CASE ("MarkerEvent: empty text is valid", "[marker]")
{
    MarkerEvent mk { 500, {} };
    REQUIRE (mk.text.isEmpty());
}

//==============================================================================
// CSV marker-matching tests
//==============================================================================
TEST_CASE ("Marker CSV: exact timestamp match", "[marker][csv]")
{
    std::vector<MarkerEvent> markers = { { 1000, "Start" } };
    std::vector<juce::int64> logTs   = { 875, 1000, 1125 };

    auto result = matchMarkersToTimestamps (markers, logTs);
    REQUIRE (result.size() == 1);
    REQUIRE (result[1000] == "Start");
}

TEST_CASE ("Marker CSV: snaps to nearest log entry within 200 ms", "[marker][csv]")
{
    // Marker at 1050, log entries at 1000 and 1125 → snaps to 1000 (50 ms away)
    std::vector<MarkerEvent> markers = { { 1050, "Near" } };
    std::vector<juce::int64> logTs   = { 875, 1000, 1125 };

    auto result = matchMarkersToTimestamps (markers, logTs);
    REQUIRE (result.size() == 1);
    REQUIRE (result.count (1000) == 1);
    REQUIRE (result[1000] == "Near");
}

TEST_CASE ("Marker CSV: no match if too far from any log entry", "[marker][csv]")
{
    // Marker at 5000, log entries end at 1125 → distance 3875 ms > 200 ms
    std::vector<MarkerEvent> markers = { { 5000, "Far away" } };
    std::vector<juce::int64> logTs   = { 875, 1000, 1125 };

    auto result = matchMarkersToTimestamps (markers, logTs);
    REQUIRE (result.empty());
}

TEST_CASE ("Marker CSV: empty text becomes asterisk", "[marker][csv]")
{
    std::vector<MarkerEvent> markers = { { 1000, {} } };
    std::vector<juce::int64> logTs   = { 1000 };

    auto result = matchMarkersToTimestamps (markers, logTs);
    REQUIRE (result.size() == 1);
    REQUIRE (result[1000] == "*");
}

TEST_CASE ("Marker CSV: multiple markers at different timestamps", "[marker][csv]")
{
    std::vector<MarkerEvent> markers = {
        { 1000, "First" },
        { 2000, "Second" },
        { 3000, "Third" }
    };
    std::vector<juce::int64> logTs = { 875, 1000, 1125, 1875, 2000, 2125, 2875, 3000, 3125 };

    auto result = matchMarkersToTimestamps (markers, logTs);
    REQUIRE (result.size() == 3);
    REQUIRE (result[1000] == "First");
    REQUIRE (result[2000] == "Second");
    REQUIRE (result[3000] == "Third");
}

TEST_CASE ("Marker CSV: later marker overwrites earlier at same snap target", "[marker][csv]")
{
    // Two markers both snap to same log entry (1000)
    std::vector<MarkerEvent> markers = {
        { 990,  "First" },
        { 1010, "Second" }
    };
    std::vector<juce::int64> logTs = { 1000 };

    auto result = matchMarkersToTimestamps (markers, logTs);
    REQUIRE (result.size() == 1);
    // Second marker wins (later in the loop)
    REQUIRE (result[1000] == "Second");
}

TEST_CASE ("Marker CSV: empty markers vector produces empty result", "[marker][csv]")
{
    std::vector<MarkerEvent> markers;
    std::vector<juce::int64> logTs = { 1000, 2000, 3000 };

    auto result = matchMarkersToTimestamps (markers, logTs);
    REQUIRE (result.empty());
}

TEST_CASE ("Marker CSV: empty log timestamps produces empty result", "[marker][csv]")
{
    std::vector<MarkerEvent> markers = { { 1000, "Test" } };
    std::vector<juce::int64> logTs;

    auto result = matchMarkersToTimestamps (markers, logTs);
    REQUIRE (result.empty());
}

//==============================================================================
// Marker pruning tests (replicates the pruning logic from timerCallback)
//==============================================================================
TEST_CASE ("Marker pruning: removes markers older than cutoff", "[marker]")
{
    std::vector<MarkerEvent> markers = {
        { 100, "Old" },
        { 500, "Keep1" },
        { 900, "Keep2" }
    };

    juce::int64 cutoff = 400;
    markers.erase (std::remove_if (markers.begin(), markers.end(),
        [cutoff] (const MarkerEvent& e) { return e.timestampMs < cutoff; }),
        markers.end());

    REQUIRE (markers.size() == 2);
    REQUIRE (markers[0].text == "Keep1");
    REQUIRE (markers[1].text == "Keep2");
}

TEST_CASE ("Marker pruning: keeps all if none older than cutoff", "[marker]")
{
    std::vector<MarkerEvent> markers = {
        { 500, "A" },
        { 600, "B" }
    };

    juce::int64 cutoff = 400;
    markers.erase (std::remove_if (markers.begin(), markers.end(),
        [cutoff] (const MarkerEvent& e) { return e.timestampMs < cutoff; }),
        markers.end());

    REQUIRE (markers.size() == 2);
}
