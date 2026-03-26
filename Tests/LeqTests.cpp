#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>

using Catch::Approx;

// Replicate the LAeq/LCeq energy-averaging formula from PluginEditor
static float computeLeq (const std::vector<float>& dBValues)
{
    if (dBValues.empty()) return 0.0f;
    double sum = 0.0;
    for (auto dB : dBValues)
        sum += std::pow (10.0, static_cast<double> (dB) / 10.0);
    return static_cast<float> (10.0 * std::log10 (sum / dBValues.size()));
}

//==============================================================================
TEST_CASE ("Leq: constant level gives same level back", "[spl][leq]")
{
    // If all readings are 80 dB, LAeq should be 80 dB
    std::vector<float> readings (100, 80.0f);
    REQUIRE (computeLeq (readings) == Approx (80.0f).margin (0.01f));
}

TEST_CASE ("Leq: constant level at different values", "[spl][leq]")
{
    for (float level : { 40.0f, 60.0f, 94.0f, 110.0f })
    {
        std::vector<float> readings (50, level);
        REQUIRE (computeLeq (readings) == Approx (level).margin (0.01f));
    }
}

TEST_CASE ("Leq: single reading returns that reading", "[spl][leq]")
{
    std::vector<float> readings = { 73.5f };
    REQUIRE (computeLeq (readings) == Approx (73.5f).margin (0.01f));
}

TEST_CASE ("Leq: energy average is dominated by louder values", "[spl][leq]")
{
    // 9 readings at 70 dB + 1 reading at 80 dB
    // Leq = 10*log10((9*10^7 + 1*10^8) / 10) = 10*log10(1.9e7) ≈ 72.79 dB
    std::vector<float> readings (9, 70.0f);
    readings.push_back (80.0f);
    float leq = computeLeq (readings);
    REQUIRE (leq > 72.0f);
    REQUIRE (leq < 74.0f);
    // The Leq should be much closer to 70 than to 80, but pulled up by the loud sample
    REQUIRE (leq > 70.0f);
}

TEST_CASE ("Leq: equal mix of two levels", "[spl][leq]")
{
    // 50% at 70 dB + 50% at 80 dB
    // Leq = 10*log10((10^7 + 10^8) / 2) = 10*log10(5.5e7) ≈ 77.4 dB
    std::vector<float> readings;
    for (int i = 0; i < 50; ++i) readings.push_back (70.0f);
    for (int i = 0; i < 50; ++i) readings.push_back (80.0f);
    float leq = computeLeq (readings);
    REQUIRE (leq == Approx (77.4f).margin (0.1f));
}

TEST_CASE ("Leq: 3 dB rule — doubling energy adds 3 dB", "[spl][leq]")
{
    // Two identical sources at 80 dB → Leq = 83 dB
    // Simulated as: all readings at 80 dB is still 80 dB,
    // but if we compute Leq of a signal that has twice the energy:
    // 10*log10(2 * 10^8) = 83.01 dB
    float singleSource = 80.0f;
    float doubledEnergy = static_cast<float> (10.0 * std::log10 (2.0 * std::pow (10.0, singleSource / 10.0)));
    REQUIRE (doubledEnergy == Approx (83.01f).margin (0.02f));
}

TEST_CASE ("Leq: empty input returns zero", "[spl][leq]")
{
    std::vector<float> empty;
    REQUIRE (computeLeq (empty) == 0.0f);
}

TEST_CASE ("Leq: running Leq converges as more samples added", "[spl][leq]")
{
    // Simulate the running CSV Leq: add readings one at a time
    std::vector<float> readings;
    float prevLeq = 0.0f;

    for (int i = 0; i < 100; ++i)
    {
        readings.push_back (75.0f);
        float leq = computeLeq (readings);
        // Should converge to 75 dB
        if (i > 0)
            REQUIRE (std::fabs (leq - 75.0f) <= std::fabs (prevLeq - 75.0f) + 0.01f);
        prevLeq = leq;
    }
    REQUIRE (prevLeq == Approx (75.0f).margin (0.01f));
}
