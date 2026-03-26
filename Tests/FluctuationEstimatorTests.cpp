#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include "FluctuationStrengthEstimator.h"

using Catch::Approx;

TEST_CASE ("FluctuationEstimator: silence gives zero", "[dsp][fluctuation]")
{
    FluctuationStrengthEstimator est;
    est.prepare (48000.0);

    auto silence = generateSilence (48000 * 2); // 2 seconds (> 1s window)
    for (auto s : silence)
        est.processSample (s);

    REQUIRE (est.getFluctuation() == 0.0f);
}

TEST_CASE ("FluctuationEstimator: pure tone has low fluctuation", "[dsp][fluctuation]")
{
    FluctuationStrengthEstimator est;
    est.prepare (48000.0);

    // Unmodulated tone — minimal fluctuation
    auto sine = generateSine (1000.0, 48000.0, 48000 * 3, 0.5f);
    for (auto s : sine)
        est.processSample (s);

    REQUIRE (est.getFluctuation() < 15.0f);
}

TEST_CASE ("FluctuationEstimator: AM at 4 Hz produces fluctuation", "[dsp][fluctuation]")
{
    FluctuationStrengthEstimator est;
    est.prepare (48000.0);

    // 1 kHz carrier AM-modulated at 4 Hz (peak fluctuation frequency)
    auto am = generateAMSine (1000.0, 4.0, 1.0f, 48000.0, 48000 * 4, 0.5f);
    for (auto s : am)
        est.processSample (s);

    // Should have measurable fluctuation
    REQUIRE (est.getFluctuation() > 3.0f);
}

TEST_CASE ("FluctuationEstimator: reset clears state", "[dsp][fluctuation]")
{
    FluctuationStrengthEstimator est;
    est.prepare (48000.0);

    auto sine = generateSine (440.0, 48000.0, 48000 * 2);
    for (auto s : sine)
        est.processSample (s);

    est.reset();
    REQUIRE (est.getFluctuation() == 0.0f);
}

TEST_CASE ("FluctuationEstimator: output is non-negative", "[dsp][fluctuation]")
{
    FluctuationStrengthEstimator est;
    est.prepare (48000.0);

    auto noise = generateNoise (48000 * 2, 0.3f);
    for (auto s : noise)
    {
        float fl = est.processSample (s);
        REQUIRE (fl >= 0.0f);
    }
}
