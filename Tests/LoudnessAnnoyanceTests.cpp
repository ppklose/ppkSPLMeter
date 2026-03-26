#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

using Catch::Approx;

// Standalone reimplementation of the loudness/annoyance formulas from
// PluginProcessor.h for unit testing without JUCE dependencies.

static float computeLoudnessSone (float phons)
{
    if (phons < 2.0f) return 0.0f;
    return (phons >= 40.0f)
           ? std::pow (2.0f, (phons - 40.0f) / 10.0f)
           : std::pow (phons / 40.0f, 2.642f);
}

static float computePsychoAnnoyance (float loudnessSone, float sharpness,
                                      float roughnessPct, float fluctuationPct)
{
    const float N = loudnessSone;
    if (N < 0.01f) return 0.0f;
    const float S  = sharpness;
    const float R  = roughnessPct / 100.0f;
    const float F  = fluctuationPct / 100.0f;
    const float wS  = (S > 1.75f) ? (S - 1.75f) / 4.0f * N / (N + 10.0f) : 0.0f;
    const float wFR = std::pow (N, 0.4f) * (0.07f * std::sqrt (F) + 0.2f * std::sqrt (R));
    return N * (1.0f + std::sqrt (wS * wS + wFR * wFR));
}

//==============================================================================
// Loudness (sone)
//==============================================================================
TEST_CASE ("Loudness: 40 phon = 1 sone", "[psychoacoustic][loudness]")
{
    REQUIRE (computeLoudnessSone (40.0f) == Approx (1.0f).margin (0.001f));
}

TEST_CASE ("Loudness: 50 phon = 2 sone", "[psychoacoustic][loudness]")
{
    REQUIRE (computeLoudnessSone (50.0f) == Approx (2.0f).margin (0.001f));
}

TEST_CASE ("Loudness: 60 phon = 4 sone", "[psychoacoustic][loudness]")
{
    REQUIRE (computeLoudnessSone (60.0f) == Approx (4.0f).margin (0.001f));
}

TEST_CASE ("Loudness: 80 phon = 16 sone", "[psychoacoustic][loudness]")
{
    REQUIRE (computeLoudnessSone (80.0f) == Approx (16.0f).margin (0.001f));
}

TEST_CASE ("Loudness: below 2 phon = 0 sone", "[psychoacoustic][loudness]")
{
    REQUIRE (computeLoudnessSone (1.0f) == 0.0f);
    REQUIRE (computeLoudnessSone (0.0f) == 0.0f);
    REQUIRE (computeLoudnessSone (-5.0f) == 0.0f);
}

TEST_CASE ("Loudness: low-level formula (< 40 phon) is continuous at 40", "[psychoacoustic][loudness]")
{
    float below = computeLoudnessSone (39.99f);
    float at40  = computeLoudnessSone (40.0f);
    REQUIRE (below == Approx (at40).margin (0.02f));
}

TEST_CASE ("Loudness: monotonically increasing", "[psychoacoustic][loudness]")
{
    float prev = 0.0f;
    for (float phon = 2.0f; phon <= 120.0f; phon += 1.0f)
    {
        float sone = computeLoudnessSone (phon);
        REQUIRE (sone >= prev);
        prev = sone;
    }
}

//==============================================================================
// Psychoacoustic annoyance
//==============================================================================
TEST_CASE ("Annoyance: zero loudness gives zero annoyance", "[psychoacoustic][annoyance]")
{
    REQUIRE (computePsychoAnnoyance (0.0f, 2.0f, 50.0f, 50.0f) == 0.0f);
}

TEST_CASE ("Annoyance: no sharpness/roughness/fluctuation → annoyance = loudness", "[psychoacoustic][annoyance]")
{
    // With S <= 1.75, R = 0, F = 0: wS = 0, wFR = 0 → annoyance = N * 1 = N
    float N = 4.0f;
    float annoyance = computePsychoAnnoyance (N, 1.0f, 0.0f, 0.0f);
    REQUIRE (annoyance == Approx (N).margin (0.001f));
}

TEST_CASE ("Annoyance: high sharpness increases annoyance", "[psychoacoustic][annoyance]")
{
    float N = 4.0f;
    float base   = computePsychoAnnoyance (N, 1.0f, 0.0f, 0.0f);
    float sharper = computePsychoAnnoyance (N, 3.0f, 0.0f, 0.0f);
    REQUIRE (sharper > base);
}

TEST_CASE ("Annoyance: high roughness increases annoyance", "[psychoacoustic][annoyance]")
{
    float N = 4.0f;
    float base    = computePsychoAnnoyance (N, 1.0f, 0.0f, 0.0f);
    float rougher = computePsychoAnnoyance (N, 1.0f, 80.0f, 0.0f);
    REQUIRE (rougher > base);
}

TEST_CASE ("Annoyance: high fluctuation increases annoyance", "[psychoacoustic][annoyance]")
{
    float N = 4.0f;
    float base  = computePsychoAnnoyance (N, 1.0f, 0.0f, 0.0f);
    float fluct = computePsychoAnnoyance (N, 1.0f, 0.0f, 80.0f);
    REQUIRE (fluct > base);
}

TEST_CASE ("Annoyance: always >= loudness", "[psychoacoustic][annoyance]")
{
    // The formula is N * (1 + sqrt(...)) where sqrt(...) >= 0
    for (float N : { 0.5f, 1.0f, 4.0f, 16.0f })
        for (float S : { 0.5f, 1.75f, 3.0f })
            for (float R : { 0.0f, 50.0f })
                for (float F : { 0.0f, 50.0f })
                {
                    float annoyance = computePsychoAnnoyance (N, S, R, F);
                    REQUIRE (annoyance >= N - 0.001f);
                }
}
