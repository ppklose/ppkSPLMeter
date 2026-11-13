#include <catch2/catch_test_macros.hpp>
#include "ProcessorTestHelpers.h"

#include <algorithm>
#include <cstring>

// Vibration spec §7.4: per-input FFT ring buffers and the "fftSource" parameter
// (Mix / Dual (1+2) / Input 1…8) replacing the Dual FFT toggle.

using namespace ProcTest;

namespace
{
bool bitEqual (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; }

std::unique_ptr<SPLMeterAudioProcessor> makeProcessor (int numInputs)
{
    auto proc = std::make_unique<SPLMeterAudioProcessor>();
    auto layout = proc->getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (numInputs);
    REQUIRE (proc->setBusesLayout (layout));
    proc->prepareToPlay (kSampleRate, kBlockSize);
    return proc;
}

// Distinct deterministic signal per input
float inputSignal (int ch, int n) { return sine (100.0 * (ch + 1), 0.05 * (ch + 1), n); }

int runBlocks (SPLMeterAudioProcessor& proc, int numInputs, int numBlocks)
{
    juce::AudioBuffer<float> buffer (juce::jmax (proc.getTotalNumInputChannels(),
                                                 proc.getTotalNumOutputChannels()), kBlockSize);
    juce::MidiBuffer midi;
    int n = 0;
    for (int b = 0; b < numBlocks; ++b, n += kBlockSize)
    {
        buffer.clear();
        for (int ch = 0; ch < numInputs; ++ch)
            for (int s = 0; s < kBlockSize; ++s)
                buffer.setSample (ch, s, inputSignal (ch, n + s));
        proc.processBlock (buffer, midi);
    }
    return n;
}

juce::MemoryBlock stateOf (SPLMeterAudioProcessor& proc)
{
    juce::MemoryBlock blob;
    proc.getStateInformation (blob);
    return blob;
}
} // namespace

TEST_CASE ("FFT buffers: every input up to 8 is recorded individually", "[fft]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr int numInputs = 6;
    auto proc = makeProcessor (numInputs);
    const int n = runBlocks (*proc, numInputs, 30);

    const int len = 2048;
    std::vector<float> buf ((size_t) len);
    for (int ch = 0; ch < numInputs; ++ch)
    {
        proc->copyFftWindow (buf.data(), len, ch);
        int diffs = 0;
        for (int i = 0; i < len; ++i)
            if (! bitEqual (buf[(size_t) i], inputSignal (ch, n - len + i))) ++diffs;
        INFO ("input " << ch + 1);
        CHECK (diffs == 0);
    }

    // Inputs the device does not have read as silence (no buffer allocated)
    proc->copyFftWindow (buf.data(), len, 7);
    CHECK (std::all_of (buf.begin(), buf.end(), [] (float v) { return v == 0.0f; }));
    proc->copyFftWindow (buf.data(), len, 42);
    CHECK (std::all_of (buf.begin(), buf.end(), [] (float v) { return v == 0.0f; }));
}

TEST_CASE ("FFT buffers: mono device still provides inputs 1 + 2 for Dual FFT", "[fft]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto proc = makeProcessor (1);
    const int n = runBlocks (*proc, 1, 10);

    std::vector<float> buf (1024);
    proc->copyFftWindow (buf.data(), 1024, 0);
    CHECK (bitEqual (buf.back(), inputSignal (0, n - 1)));
    proc->copyFftWindow (buf.data(), 1024, 1);
    CHECK (std::all_of (buf.begin(), buf.end(), [] (float v) { return v == 0.0f; }));
}

TEST_CASE ("FFT buffers: more inputs after a device change get buffers too", "[fft]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto proc = makeProcessor (2);
    runBlocks (*proc, 2, 5);

    auto layout = proc->getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (8);
    REQUIRE (proc->setBusesLayout (layout));
    proc->prepareToPlay (kSampleRate, kBlockSize);
    const int n = runBlocks (*proc, 8, 5);

    std::vector<float> buf (512);
    proc->copyFftWindow (buf.data(), 512, 7);
    CHECK (bitEqual (buf.back(), inputSignal (7, n - 1)));
}

TEST_CASE ("FFT source: parameter, legacy fftDual flag and migration", "[fft][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("default is the mix")
    {
        auto proc = std::make_unique<SPLMeterAudioProcessor>();
        CHECK (proc->getFftSource() == SPLMeterAudioProcessor::FftSourceMix);
    }

    SECTION ("setFftSource keeps fftDual in step")
    {
        auto proc = std::make_unique<SPLMeterAudioProcessor>();
        proc->setFftSource (SPLMeterAudioProcessor::FftSourceDual);
        CHECK (proc->apvts.getRawParameterValue ("fftDual")->load() > 0.5f);
        proc->setFftSource (SPLMeterAudioProcessor::FftSourceInput1 + 2);
        CHECK (proc->getFftSource() == SPLMeterAudioProcessor::FftSourceInput1 + 2);
        CHECK (proc->apvts.getRawParameterValue ("fftDual")->load() < 0.5f);
        proc->setFftSource (999);   // clamped
        CHECK (proc->getFftSource() == SPLMeterAudioProcessor::kNumFftSources - 1);
    }

    SECTION ("source round-trips through the plugin state")
    {
        auto proc = std::make_unique<SPLMeterAudioProcessor>();
        proc->setFftSource (SPLMeterAudioProcessor::FftSourceInput1 + 4);
        const auto blob = stateOf (*proc);

        auto restored = std::make_unique<SPLMeterAudioProcessor>();
        restored->setStateInformation (blob.getData(), (int) blob.getSize());
        CHECK (restored->getFftSource() == SPLMeterAudioProcessor::FftSourceInput1 + 4);
    }

    SECTION ("v3.5.2 session with Dual FFT on opens as Dual (1+2)")
    {
        const auto fixture = juce::File (SPLMETER_GOLDEN_DIR).getChildFile ("state_v3.5.2.xml");
        const auto xml = juce::XmlDocument::parse (fixture);
        REQUIRE (xml != nullptr);
        REQUIRE (xml->getChildByAttribute ("id", "fftSource") == nullptr);   // truly an old session

        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (*xml, blob);
        auto proc = std::make_unique<SPLMeterAudioProcessor>();
        proc->setFftSource (SPLMeterAudioProcessor::FftSourceInput1);   // must be overridden
        proc->setStateInformation (blob.getData(), (int) blob.getSize());
        CHECK (proc->getFftSource() == SPLMeterAudioProcessor::FftSourceDual);
    }

    SECTION ("old session with Dual FFT off opens as Mix")
    {
        const auto fixture = juce::File (SPLMETER_GOLDEN_DIR).getChildFile ("state_v3.5.2.xml");
        auto xml = juce::XmlDocument::parse (fixture);
        REQUIRE (xml != nullptr);
        auto* dual = xml->getChildByAttribute ("id", "fftDual");
        REQUIRE (dual != nullptr);
        dual->setAttribute ("value", 0.0);

        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (*xml, blob);
        auto proc = std::make_unique<SPLMeterAudioProcessor>();
        proc->setFftSource (SPLMeterAudioProcessor::FftSourceDual);
        proc->setStateInformation (blob.getData(), (int) blob.getSize());
        CHECK (proc->getFftSource() == SPLMeterAudioProcessor::FftSourceMix);
    }
}
