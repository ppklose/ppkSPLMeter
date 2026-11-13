#pragma once
#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Shared helpers for tests that drive the real SPLMeterAudioProcessor.
namespace ProcTest
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 480;     // deliberately not a power of two
constexpr int    kFftWindow  = 4096;

using SignalFn = std::function<float (int channel, int sampleIndex)>;

struct Scenario
{
    const char*                          name;
    int                                  numChannels;
    double                               seconds;
    SignalFn                             signal;
    std::function<void (SPLMeterAudioProcessor&)> configure;
    // Cross-platform absolute tolerance (tolerance mode only; exact mode is always bit-identical)
    float crossPlatformTol = 1.0e-3f;   // meter/log values (dB, metric units)
    float crossPlatformFftTol = 1.0e-6f; // FFT ring-buffer samples (linear FS)
};

inline float sine (double freq, double amp, int n)
{
    return static_cast<float> (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * n / kSampleRate));
}

// Deterministic white noise, one independent LCG stream per channel
inline float noise (int channel, int n, float amp)
{
    uint32_t seed = 12345u + 7919u * static_cast<uint32_t> (channel) + static_cast<uint32_t> (n) * 2654435761u;
    seed ^= seed >> 15; seed *= 2246822519u; seed ^= seed >> 13; seed *= 3266489917u; seed ^= seed >> 16;
    return amp * (static_cast<float> (seed) / static_cast<float> (UINT32_MAX) * 2.0f - 1.0f);
}

inline void setParam (SPLMeterAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// Runs a scenario and returns the captured outputs as "key value" lines.
inline std::vector<std::pair<std::string, float>> runScenario (const Scenario& sc)
{
    // Heap-allocated like in a host: the processor holds ~6 MB of FFT ring buffers
    auto procPtr = std::make_unique<SPLMeterAudioProcessor>();
    auto& proc = *procPtr;

    auto layout = proc.getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (sc.numChannels);
    REQUIRE (proc.setBusesLayout (layout));

    sc.configure (proc);
    proc.setRateAndBufferSizeDetails (kSampleRate, kBlockSize);
    proc.prepareToPlay (kSampleRate, kBlockSize);

    const int numBusChannels = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (numBusChannels, kBlockSize);
    juce::MidiBuffer midi;

    std::vector<std::pair<std::string, float>> out;
    auto add = [&out] (const std::string& key, float v) { out.emplace_back (key, v); };

    const int totalBlocks = static_cast<int> (sc.seconds * kSampleRate) / kBlockSize;
    for (int b = 0; b < totalBlocks; ++b)
    {
        buffer.clear();
        for (int ch = 0; ch < sc.numChannels; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int s = 0; s < kBlockSize; ++s)
                d[s] = sc.signal (ch, b * kBlockSize + s);
        }
        proc.processBlock (buffer, midi);

        if (b % 10 == 9)
        {
            const std::string k = "blk" + std::to_string (b) + ".";
            add (k + "peakSPL",  proc.getPeakSPL());
            add (k + "peakDBA",  proc.getPeakDBASPL());
            add (k + "peakDBC",  proc.getPeakDBCSPL());
            add (k + "rmsSPL",   proc.getRMSSPL());
            add (k + "rmsDBA",   proc.getRMSDBASPL());
            add (k + "rmsDBC",   proc.getRMSDBCSPL());
            add (k + "rough",    proc.getRoughness());
            add (k + "sharp",    proc.getSharpness());
            add (k + "fluct",    proc.getFluctuation());
            add (k + "impuls",   proc.getImpulsiveness());
            add (k + "tonal",    proc.getTonality());
        }
    }

    const auto log = proc.copyLog();
    add ("log.count", static_cast<float> (log.size()));
    for (size_t i = 0; i < log.size(); ++i)
    {
        const auto& e = log[i];
        const std::string k = "log" + std::to_string (i) + ".";
        add (k + "peakSPL", e.peakSPL);   add (k + "peakDBA", e.peakDBASPL); add (k + "peakDBC", e.peakDBCSPL);
        add (k + "rmsSPL",  e.rmsSPL);    add (k + "rmsDBA",  e.rmsDBASPL);  add (k + "rmsDBC",  e.rmsDBCSPL);
        add (k + "rough",   e.roughness); add (k + "fluct",   e.fluctuation); add (k + "sharp",  e.sharpness);
        add (k + "sone",    e.loudnessSone); add (k + "impuls", e.impulsiveness); add (k + "tonal", e.tonality);
        add (k + "annoy",   e.psychoAnnoyance);
    }

    std::vector<float> fft (kFftWindow);
    for (int ch : { -1, 0, 1 })
    {
        proc.copyFftWindow (fft.data(), kFftWindow, ch);
        for (int i = 0; i < kFftWindow; i += 7)
            add ("fft" + std::to_string (ch) + "." + std::to_string (i), fft[(size_t) i]);
    }

    add ("sessionPeakDBC", proc.getSessionPeakDBCSPL());
    add ("noiseDose",      proc.getNoiseDosePct());

    proc.releaseResources();
    return out;
}

} // namespace ProcTest
