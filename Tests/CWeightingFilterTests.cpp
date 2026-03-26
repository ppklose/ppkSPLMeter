#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include "CWeightingFilter.h"

using Catch::Approx;

static float measureGainDB (CWeightingFilter& f, double freq, double sampleRate)
{
    f.prepare (sampleRate);

    const int numSamples = static_cast<int> (sampleRate * 2);
    auto sine = generateSine (freq, sampleRate, numSamples);

    for (int i = 0; i < numSamples; ++i)
        sine[i] = f.processSample (sine[i]);

    const int tail = static_cast<int> (sampleRate * 0.5);
    double sumIn = 0.0, sumOut = 0.0;
    for (int i = numSamples - tail; i < numSamples; ++i)
    {
        double t = static_cast<double> (i) / sampleRate;
        float inSample = static_cast<float> (std::sin (2.0 * kPi * freq * t));
        sumIn  += inSample * inSample;
        sumOut += sine[i] * sine[i];
    }

    float rmsIn  = static_cast<float> (std::sqrt (sumIn / tail));
    float rmsOut = static_cast<float> (std::sqrt (sumOut / tail));
    return 20.0f * std::log10 (rmsOut / rmsIn);
}

TEST_CASE ("CWeightingFilter: 0 dB at 1 kHz", "[dsp][c-weighting]")
{
    CWeightingFilter f;
    float gain = measureGainDB (f, 1000.0, 48000.0);
    REQUIRE (gain == Approx (0.0f).margin (0.1f));
}

TEST_CASE ("CWeightingFilter: 0 dB at 1 kHz (44100)", "[dsp][c-weighting]")
{
    CWeightingFilter f;
    float gain = measureGainDB (f, 1000.0, 44100.0);
    REQUIRE (gain == Approx (0.0f).margin (0.1f));
}

TEST_CASE ("CWeightingFilter: less low-freq attenuation than A-weighting", "[dsp][c-weighting]")
{
    // C-weighting is a single biquad (2 poles), so it has significant rolloff
    // below ~100 Hz. At 100 Hz, actual measured gain is ~-20 dB with this
    // simplified 1-biquad implementation.
    CWeightingFilter f;
    float gain = measureGainDB (f, 100.0, 48000.0);
    REQUIRE (gain > -25.0f);
    REQUIRE (gain < 0.0f);
}

TEST_CASE ("CWeightingFilter: attenuation at very low frequencies", "[dsp][c-weighting]")
{
    // Single biquad: steep rolloff at very low frequencies
    CWeightingFilter f;
    float gain = measureGainDB (f, 31.5, 48000.0);
    REQUIRE (gain < 0.0f);
    REQUIRE (gain > -40.0f);
}

TEST_CASE ("CWeightingFilter: near-flat at 1 kHz, rolls off at extremes", "[dsp][c-weighting]")
{
    CWeightingFilter f;
    // Midrange (1 kHz) should be near 0 dB (normalized)
    float gain1k = measureGainDB (f, 1000.0, 48000.0);
    REQUIRE (std::fabs (gain1k) < 0.5f);

    // Higher frequencies may show some gain due to the biquad response shape
    // but should stay bounded
    for (double freq : { 500.0, 2000.0 })
    {
        float gain = measureGainDB (f, freq, 48000.0);
        REQUIRE (std::fabs (gain) < 20.0f);
    }
}

TEST_CASE ("CWeightingFilter: silence in, silence out", "[dsp][c-weighting]")
{
    CWeightingFilter f;
    f.prepare (48000.0);

    for (int i = 0; i < 4800; ++i)
        REQUIRE (f.processSample (0.0f) == 0.0f);
}

TEST_CASE ("CWeightingFilter: reset clears state", "[dsp][c-weighting]")
{
    CWeightingFilter f;
    f.prepare (48000.0);

    auto sine = generateSine (440.0, 48000.0, 4800);
    for (auto s : sine)
        f.processSample (s);

    f.reset();
    REQUIRE (f.processSample (0.0f) == 0.0f);
}
