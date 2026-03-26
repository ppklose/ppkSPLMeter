#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include "SharpnessEstimator.h"

using Catch::Approx;

TEST_CASE ("SharpnessEstimator: silence gives zero sharpness", "[dsp][sharpness]")
{
    SharpnessEstimator est;
    est.prepare (48000.0);

    auto silence = generateSilence (48000);
    for (auto s : silence)
        est.processSample (s);

    REQUIRE (est.getSharpness() == 0.0f);
}

TEST_CASE ("SharpnessEstimator: low-frequency tone has low sharpness", "[dsp][sharpness]")
{
    SharpnessEstimator est;
    est.prepare (48000.0);

    // 200 Hz tone — should be low sharpness
    auto sine = generateSine (200.0, 48000.0, 48000 * 2, 0.5f);
    for (auto s : sine)
        est.processSample (s);

    REQUIRE (est.getSharpness() < 2.0f);
}

TEST_CASE ("SharpnessEstimator: high-frequency tone has higher sharpness", "[dsp][sharpness]")
{
    SharpnessEstimator est;
    est.prepare (48000.0);

    // 8 kHz tone — should have higher sharpness due to high-frequency weighting
    auto sine = generateSine (8000.0, 48000.0, 48000 * 2, 0.5f);
    for (auto s : sine)
        est.processSample (s);

    float sharpnessHigh = est.getSharpness();

    // Compare with low-frequency tone
    SharpnessEstimator estLow;
    estLow.prepare (48000.0);
    auto sineLow = generateSine (200.0, 48000.0, 48000 * 2, 0.5f);
    for (auto s : sineLow)
        estLow.processSample (s);

    REQUIRE (sharpnessHigh > estLow.getSharpness());
}

TEST_CASE ("SharpnessEstimator: reset clears state", "[dsp][sharpness]")
{
    SharpnessEstimator est;
    est.prepare (48000.0);

    auto sine = generateSine (4000.0, 48000.0, 48000);
    for (auto s : sine)
        est.processSample (s);

    REQUIRE (est.getSharpness() > 0.0f);

    est.reset();
    REQUIRE (est.getSharpness() == 0.0f);
}

TEST_CASE ("SharpnessEstimator: output is non-negative", "[dsp][sharpness]")
{
    SharpnessEstimator est;
    est.prepare (48000.0);

    auto noise = generateNoise (48000, 0.3f);
    for (auto s : noise)
    {
        float sh = est.processSample (s);
        REQUIRE (sh >= 0.0f);
    }
}
