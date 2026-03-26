#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include <cmath>

using Catch::Approx;

// These tests verify the SPL calculation math used in PluginProcessor
// without instantiating the full JUCE AudioProcessor.

//==============================================================================
// linearToDBFS
//==============================================================================
TEST_CASE ("linearToDBFS: unity gives 0 dBFS", "[spl]")
{
    REQUIRE (linearToDBFS (1.0f) == Approx (0.0f).margin (0.001f));
}

TEST_CASE ("linearToDBFS: half amplitude gives ~-6 dBFS", "[spl]")
{
    REQUIRE (linearToDBFS (0.5f) == Approx (-6.0206f).margin (0.01f));
}

TEST_CASE ("linearToDBFS: very small value doesn't produce -inf", "[spl]")
{
    float db = linearToDBFS (1e-12f);
    REQUIRE (std::isfinite (db));
    REQUIRE (db <= -200.0f);
}

TEST_CASE ("linearToDBFS: zero clamped to floor", "[spl]")
{
    float db = linearToDBFS (0.0f);
    REQUIRE (std::isfinite (db));
}

//==============================================================================
// RMS alpha (IEC 61672 time weighting)
//==============================================================================
static double computeRmsAlpha (double tau, double sampleRate)
{
    return 1.0 - std::exp (-1.0 / (tau * sampleRate));
}

TEST_CASE ("RMS alpha: FAST (tau=125ms) at 48kHz", "[spl]")
{
    double alpha = computeRmsAlpha (0.125, 48000.0);
    // Should be a small positive number (≈ 0.000167)
    REQUIRE (alpha > 0.0);
    REQUIRE (alpha < 0.001);
    REQUIRE (alpha == Approx (1.0 - std::exp (-1.0 / (0.125 * 48000.0))).margin (1e-10));
}

TEST_CASE ("RMS alpha: SLOW (tau=1s) at 48kHz", "[spl]")
{
    double alpha = computeRmsAlpha (1.0, 48000.0);
    REQUIRE (alpha > 0.0);
    REQUIRE (alpha < 0.001);
    // SLOW alpha should be smaller than FAST alpha
    double alphaFast = computeRmsAlpha (0.125, 48000.0);
    REQUIRE (alpha < alphaFast);
}

TEST_CASE ("RMS alpha: FAST responds faster than SLOW", "[spl]")
{
    double alphaFast = computeRmsAlpha (0.125, 48000.0);
    double alphaSlow = computeRmsAlpha (1.0, 48000.0);

    // Larger alpha = faster response
    REQUIRE (alphaFast > alphaSlow);
    REQUIRE (alphaFast / alphaSlow == Approx (8.0).margin (0.5));
}

//==============================================================================
// Exponential RMS smoothing simulation
//==============================================================================
TEST_CASE ("RMS smoothing: converges to correct value for constant signal", "[spl]")
{
    const double sampleRate = 48000.0;
    const double alpha = computeRmsAlpha (0.125, sampleRate);
    const float amplitude = 0.5f;

    auto sine = generateSine (1000.0, sampleRate, 48000 * 2, amplitude);

    double rmsSmoothed = 0.0;
    for (auto s : sine)
        rmsSmoothed += alpha * (static_cast<double> (s) * s - rmsSmoothed);

    float rms = static_cast<float> (std::sqrt (rmsSmoothed));

    // RMS of a sine = amplitude / sqrt(2)
    float expected = amplitude / std::sqrt (2.0f);
    REQUIRE (rms == Approx (expected).margin (0.005f));
}

TEST_CASE ("RMS smoothing: silence gives near-zero RMS", "[spl]")
{
    const double alpha = computeRmsAlpha (0.125, 48000.0);
    double rmsSmoothed = 0.0;

    for (int i = 0; i < 48000; ++i)
        rmsSmoothed += alpha * (0.0 - rmsSmoothed);

    REQUIRE (std::sqrt (rmsSmoothed) < 1e-10);
}

//==============================================================================
// SPL = linearToDBFS(rms) + calOffset
//==============================================================================
TEST_CASE ("SPL calculation: known signal at default calibration", "[spl]")
{
    const float calOffset = 127.0f;
    const float amplitude = 0.5f;
    float rms = amplitude / std::sqrt (2.0f);
    float splDB = linearToDBFS (rms) + calOffset;

    // 0.5 amplitude sine: rms ≈ 0.3536 → dBFS ≈ -9.03 → SPL ≈ 117.97
    REQUIRE (splDB == Approx (117.97f).margin (0.1f));
}

TEST_CASE ("SPL calculation: full-scale sine at default cal", "[spl]")
{
    const float calOffset = 127.0f;
    float rms = 1.0f / std::sqrt (2.0f);
    float splDB = linearToDBFS (rms) + calOffset;

    // RMS of full-scale sine ≈ 0.707 → dBFS ≈ -3.01 → SPL ≈ 123.99
    REQUIRE (splDB == Approx (123.99f).margin (0.1f));
}

//==============================================================================
// Peak tracking
//==============================================================================
TEST_CASE ("Peak tracking: captures absolute maximum", "[spl]")
{
    float peak = 0.0f;
    int holdCounter = 0;
    const int holdSamples = 48000; // 1 second hold

    auto sine = generateSine (1000.0, 48000.0, 48000, 0.8f);

    for (auto s : sine)
    {
        float absVal = std::fabs (s);
        if (absVal >= peak) { peak = absVal; holdCounter = holdSamples; }
        else if (holdCounter > 0) { --holdCounter; }
        else { peak *= 0.9999f; }
    }

    // Peak of 0.8 amplitude sine should be very close to 0.8
    REQUIRE (peak == Approx (0.8f).margin (0.001f));
}

TEST_CASE ("Peak tracking: decays after hold time", "[spl]")
{
    float peak = 1.0f;
    int holdCounter = 0;
    const int holdSamples = 100;

    // Feed silence after peak — should decay
    for (int i = 0; i < holdSamples + 1000; ++i)
    {
        float absVal = 0.0f;
        if (absVal >= peak) { peak = absVal; holdCounter = holdSamples; }
        else if (holdCounter > 0) { --holdCounter; }
        else { peak *= 0.9999f; }
    }

    REQUIRE (peak < 1.0f);
    REQUIRE (peak > 0.89f); // slow decay
}
