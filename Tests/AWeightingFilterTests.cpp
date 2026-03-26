#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "TestHelpers.h"
#include "AWeightingFilter.h"

using Catch::Approx;

// Measure the gain of the filter at a given frequency by running a sine through it
static float measureGainDB (AWeightingFilter& f, double freq, double sampleRate)
{
    f.prepare (sampleRate);

    const int numSamples = static_cast<int> (sampleRate * 2); // 2 seconds for settling
    auto sine = generateSine (freq, sampleRate, numSamples);

    for (int i = 0; i < numSamples; ++i)
        sine[i] = f.processSample (sine[i]);

    // Measure RMS of last 0.5 seconds (steady state)
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

TEST_CASE ("AWeightingFilter: 0 dB at 1 kHz", "[dsp][a-weighting]")
{
    AWeightingFilter f;
    float gain = measureGainDB (f, 1000.0, 48000.0);
    REQUIRE (gain == Approx (0.0f).margin (0.1f));
}

TEST_CASE ("AWeightingFilter: 0 dB at 1 kHz (44100)", "[dsp][a-weighting]")
{
    AWeightingFilter f;
    float gain = measureGainDB (f, 1000.0, 44100.0);
    REQUIRE (gain == Approx (0.0f).margin (0.1f));
}

TEST_CASE ("AWeightingFilter: strong attenuation at low frequencies", "[dsp][a-weighting]")
{
    // IEC 61672 A-weighting: ~-30 dB at 50 Hz
    AWeightingFilter f;
    float gain50 = measureGainDB (f, 50.0, 48000.0);
    REQUIRE (gain50 < -25.0f);
    REQUIRE (gain50 > -40.0f);
}

TEST_CASE ("AWeightingFilter: moderate attenuation at 200 Hz", "[dsp][a-weighting]")
{
    // ~-10.8 dB at 200 Hz
    AWeightingFilter f;
    float gain = measureGainDB (f, 200.0, 48000.0);
    REQUIRE (gain < -8.0f);
    REQUIRE (gain > -15.0f);
}

TEST_CASE ("AWeightingFilter: slight boost around 2-4 kHz", "[dsp][a-weighting]")
{
    // A-weighting has a ~+1.2 dB peak near 2.5 kHz
    AWeightingFilter f;
    float gain = measureGainDB (f, 2500.0, 48000.0);
    REQUIRE (gain > 0.0f);
    REQUIRE (gain < 2.5f);
}

TEST_CASE ("AWeightingFilter: attenuation at high frequencies", "[dsp][a-weighting]")
{
    // ~-6.6 dB at 10 kHz
    AWeightingFilter f;
    float gain = measureGainDB (f, 10000.0, 48000.0);
    REQUIRE (gain < -3.0f);
    REQUIRE (gain > -12.0f);
}

TEST_CASE ("AWeightingFilter: silence in, silence out", "[dsp][a-weighting]")
{
    AWeightingFilter f;
    f.prepare (48000.0);

    auto silence = generateSilence (4800);
    for (int i = 0; i < 4800; ++i)
        REQUIRE (f.processSample (silence[i]) == 0.0f);
}

TEST_CASE ("AWeightingFilter: reset clears state", "[dsp][a-weighting]")
{
    AWeightingFilter f;
    f.prepare (48000.0);

    // Process some audio
    auto sine = generateSine (440.0, 48000.0, 4800);
    for (auto s : sine)
        f.processSample (s);

    // Reset and feed silence — output should be zero immediately
    f.reset();
    REQUIRE (f.processSample (0.0f) == 0.0f);
}

TEST_CASE ("AWeightingFilter: consistent across sample rates", "[dsp][a-weighting]")
{
    // Gain at 1 kHz should be ~0 dB regardless of sample rate
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        AWeightingFilter f;
        float gain = measureGainDB (f, 1000.0, sr);
        REQUIRE (gain == Approx (0.0f).margin (0.15f));
    }
}
