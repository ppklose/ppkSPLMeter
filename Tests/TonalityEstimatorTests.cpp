#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include "TonalityEstimator.h"

using Catch::Approx;

TEST_CASE ("TonalityEstimator: silence gives zero", "[dsp][tonality]")
{
    TonalityEstimator est;
    est.prepare (48000.0);

    auto silence = generateSilence (48000);
    for (auto s : silence)
        est.processSample (s);

    REQUIRE (est.getTonality() == 0.0f);
}

TEST_CASE ("TonalityEstimator: pure tone has high tonality", "[dsp][tonality]")
{
    TonalityEstimator est;
    est.prepare (48000.0);

    // A pure 1 kHz tone should produce high tonality (energy in one band)
    auto sine = generateSine (1000.0, 48000.0, 48000 * 2, 0.5f);
    for (auto s : sine)
        est.processSample (s);

    REQUIRE (est.getTonality() > 50.0f);
}

TEST_CASE ("TonalityEstimator: white noise has low tonality", "[dsp][tonality]")
{
    TonalityEstimator est;
    est.prepare (48000.0);

    // White noise has relatively flat spectrum → low tonality
    auto noise = generateNoise (48000 * 2, 0.3f);
    for (auto s : noise)
        est.processSample (s);

    REQUIRE (est.getTonality() < 60.0f);
}

TEST_CASE ("TonalityEstimator: tone more tonal than noise", "[dsp][tonality]")
{
    TonalityEstimator estTone;
    estTone.prepare (48000.0);
    auto sine = generateSine (1000.0, 48000.0, 48000 * 2, 0.3f);
    for (auto s : sine)
        estTone.processSample (s);

    TonalityEstimator estNoise;
    estNoise.prepare (48000.0);
    auto noise = generateNoise (48000 * 2, 0.3f);
    for (auto s : noise)
        estNoise.processSample (s);

    REQUIRE (estTone.getTonality() > estNoise.getTonality());
}

TEST_CASE ("TonalityEstimator: output in 0-100 range", "[dsp][tonality]")
{
    TonalityEstimator est;
    est.prepare (48000.0);

    auto noise = generateNoise (48000, 0.5f);
    for (auto s : noise)
    {
        float t = est.processSample (s);
        REQUIRE (t >= 0.0f);
        REQUIRE (t <= 100.0f);
    }
}

TEST_CASE ("TonalityEstimator: reset clears state", "[dsp][tonality]")
{
    TonalityEstimator est;
    est.prepare (48000.0);

    auto sine = generateSine (1000.0, 48000.0, 48000, 0.5f);
    for (auto s : sine)
        est.processSample (s);

    REQUIRE (est.getTonality() > 0.0f);

    est.reset();
    REQUIRE (est.getTonality() == 0.0f);
}
