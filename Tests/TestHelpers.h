#pragma once
#include <cmath>
#include <vector>

static constexpr double kPi = 3.14159265358979323846;

// Generate a sine wave buffer (mono, float)
inline std::vector<float> generateSine (double frequency, double sampleRate, int numSamples, float amplitude = 1.0f)
{
    std::vector<float> buf (numSamples);
    for (int i = 0; i < numSamples; ++i)
        buf[i] = amplitude * static_cast<float> (std::sin (2.0 * kPi * frequency * i / sampleRate));
    return buf;
}

// Generate white noise (deterministic via simple LCG for reproducibility)
inline std::vector<float> generateNoise (int numSamples, float amplitude = 1.0f)
{
    std::vector<float> buf (numSamples);
    uint32_t seed = 12345;
    for (int i = 0; i < numSamples; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        buf[i] = amplitude * (static_cast<float> (seed) / static_cast<float> (UINT32_MAX) * 2.0f - 1.0f);
    }
    return buf;
}

// Generate silence
inline std::vector<float> generateSilence (int numSamples)
{
    return std::vector<float> (numSamples, 0.0f);
}

// Generate AM-modulated sine: carrier * (1 + depth * sin(modFreq * t))
inline std::vector<float> generateAMSine (double carrierFreq, double modFreq, float modDepth,
                                           double sampleRate, int numSamples, float amplitude = 1.0f)
{
    std::vector<float> buf (numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        double t = static_cast<double> (i) / sampleRate;
        float carrier = static_cast<float> (std::sin (2.0 * kPi * carrierFreq * t));
        float mod     = 1.0f + modDepth * static_cast<float> (std::sin (2.0 * kPi * modFreq * t));
        buf[i] = amplitude * carrier * mod;
    }
    return buf;
}

// Compute RMS of a buffer
inline float computeRMS (const std::vector<float>& buf)
{
    double sum = 0.0;
    for (auto s : buf)
        sum += static_cast<double> (s) * s;
    return static_cast<float> (std::sqrt (sum / buf.size()));
}

// Convert linear amplitude to dBFS
inline float linearToDBFS (float linear)
{
    return 20.0f * std::log10 (std::max (linear, 1e-10f));
}
