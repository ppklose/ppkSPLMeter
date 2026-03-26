#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include "ImpulsivenessEstimator.h"

using Catch::Approx;

TEST_CASE ("ImpulsivenessEstimator: silence gives zero", "[dsp][impulsiveness]")
{
    ImpulsivenessEstimator est;
    est.prepare (48000.0);

    auto silence = generateSilence (48000);
    for (auto s : silence)
        est.processSample (s);

    REQUIRE (est.getImpulsiveness() == 0.0f);
}

TEST_CASE ("ImpulsivenessEstimator: pure sine ~3 dB crest factor", "[dsp][impulsiveness]")
{
    ImpulsivenessEstimator est;
    est.prepare (48000.0);

    // A pure sine has a crest factor of sqrt(2) ≈ 3.01 dB
    auto sine = generateSine (1000.0, 48000.0, 48000 * 2, 0.5f);
    for (auto s : sine)
        est.processSample (s);

    float cf = est.getImpulsiveness();
    REQUIRE (cf >= 2.0f);
    REQUIRE (cf <= 5.0f);
}

TEST_CASE ("ImpulsivenessEstimator: impulse produces high crest factor", "[dsp][impulsiveness]")
{
    ImpulsivenessEstimator est;
    est.prepare (48000.0);

    // Run a quiet signal first to establish low RMS
    auto quiet = generateSine (1000.0, 48000.0, 48000, 0.01f);
    for (auto s : quiet)
        est.processSample (s);

    // Then hit with a single loud impulse
    est.processSample (1.0f);

    // Feed more quiet signal
    for (int i = 0; i < 4800; ++i)
        est.processSample (0.01f * static_cast<float> (std::sin (2.0 * kPi * 1000.0 * i / 48000.0)));

    float cf = est.getImpulsiveness();
    REQUIRE (cf > 10.0f);
}

TEST_CASE ("ImpulsivenessEstimator: output clamped to 0-40 dB", "[dsp][impulsiveness]")
{
    ImpulsivenessEstimator est;
    est.prepare (48000.0);

    auto noise = generateNoise (48000, 0.5f);
    for (auto s : noise)
    {
        float cf = est.processSample (s);
        REQUIRE (cf >= 0.0f);
        REQUIRE (cf <= 40.0f);
    }
}

TEST_CASE ("ImpulsivenessEstimator: reset clears state", "[dsp][impulsiveness]")
{
    ImpulsivenessEstimator est;
    est.prepare (48000.0);

    auto sine = generateSine (440.0, 48000.0, 48000, 0.5f);
    for (auto s : sine)
        est.processSample (s);

    est.reset();
    REQUIRE (est.getImpulsiveness() == 0.0f);
}
