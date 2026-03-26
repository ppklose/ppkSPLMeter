#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include "RoughnessEstimator.h"

using Catch::Approx;

TEST_CASE ("RoughnessEstimator: silence gives zero roughness", "[dsp][roughness]")
{
    RoughnessEstimator est;
    est.prepare (48000.0);

    auto silence = generateSilence (48000); // 1 second
    for (auto s : silence)
        est.processSample (s);

    REQUIRE (est.getRoughness() == 0.0f);
}

TEST_CASE ("RoughnessEstimator: pure tone has low roughness", "[dsp][roughness]")
{
    RoughnessEstimator est;
    est.prepare (48000.0);

    // Unmodulated 1 kHz tone — should have very low roughness
    auto sine = generateSine (1000.0, 48000.0, 48000 * 2, 0.5f);
    for (auto s : sine)
        est.processSample (s);

    REQUIRE (est.getRoughness() < 15.0f);
}

TEST_CASE ("RoughnessEstimator: AM at 70 Hz produces high roughness", "[dsp][roughness]")
{
    RoughnessEstimator est;
    est.prepare (48000.0);

    // 1 kHz carrier AM-modulated at 70 Hz (peak roughness frequency) with 100% depth
    auto am = generateAMSine (1000.0, 70.0, 1.0f, 48000.0, 48000 * 3, 0.5f);
    for (auto s : am)
        est.processSample (s);

    // Should show significant roughness
    REQUIRE (est.getRoughness() > 5.0f);
}

TEST_CASE ("RoughnessEstimator: reset clears state", "[dsp][roughness]")
{
    RoughnessEstimator est;
    est.prepare (48000.0);

    auto sine = generateSine (440.0, 48000.0, 48000);
    for (auto s : sine)
        est.processSample (s);

    est.reset();
    REQUIRE (est.getRoughness() == 0.0f);
}

TEST_CASE ("RoughnessEstimator: output is non-negative", "[dsp][roughness]")
{
    RoughnessEstimator est;
    est.prepare (48000.0);

    auto noise = generateNoise (48000, 0.3f);
    for (auto s : noise)
    {
        float r = est.processSample (s);
        REQUIRE (r >= 0.0f);
    }
}
