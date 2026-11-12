#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

// Golden-file regression test for the real SPLMeterAudioProcessor.
//
// Deterministic input signals are run through processBlock and every metering
// output (meter atomics per block, log entries, FFT ring buffers, session values)
// is compared with a reference file in Tests/golden/.
//
//   SPLMETER_UPDATE_GOLDEN=1  rewrite the golden files from the current code
//   SPLMETER_GOLDEN_EXACT=1   require bit-identical values (same machine/compiler,
//                             used to verify refactors); default is a tight
//                             tolerance so the test also passes on other platforms

#ifndef SPLMETER_GOLDEN_DIR
 #error "SPLMETER_GOLDEN_DIR must be defined by the build"
#endif

namespace
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
};

float sine (double freq, double amp, int n)
{
    return static_cast<float> (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * n / kSampleRate));
}

// Deterministic white noise, one independent LCG stream per channel
float noise (int channel, int n, float amp)
{
    uint32_t seed = 12345u + 7919u * static_cast<uint32_t> (channel) + static_cast<uint32_t> (n) * 2654435761u;
    seed ^= seed >> 15; seed *= 2246822519u; seed ^= seed >> 13; seed *= 3266489917u; seed ^= seed >> 16;
    return amp * (static_cast<float> (seed) / static_cast<float> (UINT32_MAX) * 2.0f - 1.0f);
}

void setParam (SPLMeterAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

std::vector<Scenario> scenarios()
{
    return {
        { "mono_sine_1k", 1, 3.0,
          [] (int, int n) { return sine (1000.0, 0.1, n); },
          [] (SPLMeterAudioProcessor&) {} },

        { "stereo_noise_sine", 2, 3.0,
          [] (int ch, int n) { return ch == 0 ? noise (ch, n, 0.2f) : sine (100.0, 0.3, n); },
          [] (SPLMeterAudioProcessor&) {} },

        { "quad_silent_impulses", 4, 3.0,
          [] (int ch, int n)
          {
              switch (ch)
              {
                  case 0:  return sine (440.0, 0.05, n);
                  case 1:  return noise (ch, n, 0.02f);
                  case 2:  return 0.0f;                                   // silent input
                  default: return (n % 4800) == 0 ? 0.9f : 0.0f;          // 10 Hz impulse train
              }
          },
          [] (SPLMeterAudioProcessor&) {} },

        { "stereo_bandpass_lowfreq", 2, 3.0,
          [] (int ch, int n) { return ch == 0 ? sine (5.0, 0.5, n) + sine (40.0, 0.1, n)
                                               : sine (12500.0, 0.05, n); },
          [] (SPLMeterAudioProcessor& p) { setParam (p, "bandpassEnabled", 1.0f); } },

        { "stereo_slow_muted", 2, 3.0,
          [] (int ch, int n) { return ch == 0 ? noise (ch, n, 0.1f) : sine (2000.0, 0.5, n); },
          [] (SPLMeterAudioProcessor& p)
          {
              setParam (p, "splTimeWeight", 1.0f);   // SLOW
              setParam (p, "channelMute1", 1.0f);    // input 2 muted
              setParam (p, "calOffset", 110.0f);
          } },
    };
}

// Runs a scenario and returns the captured outputs as "key value" lines.
std::vector<std::pair<std::string, float>> runScenario (const Scenario& sc)
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

std::string formatValue (float v)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), "%.9g", static_cast<double> (v));   // 9 digits round-trip a float exactly
    return buf;
}

bool envSet (const char* name)
{
    const char* v = std::getenv (name);
    return v != nullptr && *v != '\0' && std::string (v) != "0";
}

// Tolerance for cross-platform runs: libm and FMA contraction differ between
// compilers/architectures. Tight enough that any real behaviour change fails.
bool closeEnough (const std::string& key, float expected, float actual)
{
    if (expected == actual) return true;
    if (std::isnan (expected) || std::isnan (actual)) return std::isnan (expected) && std::isnan (actual);
    const float diff = std::fabs (expected - actual);
    if (key.rfind ("fft", 0) == 0)
        return diff <= 1.0e-6f + 1.0e-5f * std::fabs (expected);
    return diff <= 1.0e-3f + 1.0e-4f * std::fabs (expected);
}
} // namespace

TEST_CASE ("Processor regression: outputs match golden files", "[regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const bool update = envSet ("SPLMETER_UPDATE_GOLDEN");
    const bool exact  = envSet ("SPLMETER_GOLDEN_EXACT");
    const juce::File goldenDir (SPLMETER_GOLDEN_DIR);

    for (const auto& sc : scenarios())
    {
        DYNAMIC_SECTION (sc.name)
        {
            const auto values = runScenario (sc);
            const auto file   = goldenDir.getChildFile (juce::String (sc.name) + ".txt");

            if (update)
            {
                goldenDir.createDirectory();
                std::ostringstream os;
                os << "# SPLMeter processor golden output: " << sc.name << "\n";
                for (const auto& [key, v] : values)
                    os << key << ' ' << formatValue (v) << '\n';
                REQUIRE (file.replaceWithText (os.str()));
                WARN ("Golden file written: " << file.getFullPathName());
                continue;
            }

            REQUIRE (file.existsAsFile());
            std::istringstream is (file.loadFileAsString().toStdString());
            std::vector<std::pair<std::string, float>> expected;
            for (std::string line; std::getline (is, line);)
            {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream ls (line);
                std::string key; std::string val;
                ls >> key >> val;
                expected.emplace_back (key, std::strtof (val.c_str(), nullptr));
            }

            REQUIRE (expected.size() == values.size());

            int mismatches = 0;
            float maxDiff = 0.0f;
            std::string report;
            for (size_t i = 0; i < values.size(); ++i)
            {
                const auto& [key, actual] = values[i];
                REQUIRE (key == expected[i].first);
                const bool ok = exact ? (formatValue (actual) == formatValue (expected[i].second))
                                      : closeEnough (key, expected[i].second, actual);
                if (ok) continue;
                maxDiff = std::max (maxDiff, std::fabs (actual - expected[i].second));
                if (++mismatches <= 20)
                    report += key + ": expected " + formatValue (expected[i].second)
                            + ", got " + formatValue (actual) + "\n";
            }
            INFO (mismatches << " mismatches, max abs diff " << maxDiff << "\n" << report);
            CHECK (mismatches == 0);
        }
    }
}

//==============================================================================
// State compatibility: plugin state written by v3.5.2 (Tests/golden/state_v3.5.2.xml)
// must keep loading into newer versions. The fixture is created once from v3.5.2
// (SPLMETER_WRITE_STATE_FIXTURE=1) and must never be regenerated afterwards.

namespace
{
std::unique_ptr<juce::XmlElement> makeV352StyleState()
{
    auto procPtr = std::make_unique<SPLMeterAudioProcessor>();
    auto& proc = *procPtr;

    // Every parameter gets a non-default value
    const auto& params = proc.getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        const float def = p->getDefaultValue();
        const float v   = def > 0.5f ? 0.13f + 0.002f * (float) (i % 50)
                                     : 0.71f + 0.002f * (float) (i % 50);
        p->setValueNotifyingHost (v);
    }

    auto ui = proc.getUiState();
    ui.setProperty ("dawSync", true, nullptr);
    ui.setProperty ("sdThreshold", 0.55, nullptr);
    ui.setProperty ("logShowSPL", false, nullptr);
    ui.setProperty ("logPsychoMetric", 3, nullptr);
    ui.setProperty ("logFftEnabled", true, nullptr);
    ui.setProperty ("spectroColourMap", 2, nullptr);
    ui.setProperty ("longTermYMin", 35.0, nullptr);
    ui.setProperty ("impulseFidelityFreq", 1000.0, nullptr);

    proc.setMidiCC (0, 7);
    proc.setMidiCC (2, 11);
    proc.setChannelName (0, "Mic left");
    proc.setChannelName (1, "Mic right");

    return proc.createStateXml();
}
} // namespace

TEST_CASE ("State compatibility: v3.5.2 plugin state still loads", "[regression][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto fixture = juce::File (SPLMETER_GOLDEN_DIR).getChildFile ("state_v3.5.2.xml");

    if (envSet ("SPLMETER_WRITE_STATE_FIXTURE"))
    {
        REQUIRE (makeV352StyleState()->writeTo (fixture));
        WARN ("State fixture written: " << fixture.getFullPathName());
        return;
    }

    REQUIRE (fixture.existsAsFile());
    const auto savedXml = juce::XmlDocument::parse (fixture);
    REQUIRE (savedXml != nullptr);

    // Load through the host path (binary blob, as a DAW would)
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary (*savedXml, blob);

    auto procPtr = std::make_unique<SPLMeterAudioProcessor>();
    auto& proc = *procPtr;
    proc.setStateInformation (blob.getData(), (int) blob.getSize());

    const auto saved = juce::ValueTree::fromXml (*savedXml);

    SECTION ("every saved parameter is restored")
    {
        int checked = 0;
        for (const auto& child : saved)
        {
            if (! child.hasType ("PARAM")) continue;
            const auto id = child.getProperty ("id").toString();
            auto* p = proc.apvts.getParameter (id);
            INFO ("parameter " << id);
            REQUIRE (p != nullptr);   // a removed/renamed parameter breaks old sessions
            const float expected = (float) (double) child.getProperty ("value");
            CHECK (p->convertFrom0to1 (p->getValue()) == Catch::Approx (expected).margin (1.0e-4));
            ++checked;
        }
        CHECK (checked > 50);
    }

    SECTION ("UIState, MIDI CC and channel names are restored")
    {
        const auto savedUi = saved.getChildWithName ("UIState");
        REQUIRE (savedUi.isValid());
        auto ui = proc.getUiState();
        for (int i = 0; i < savedUi.getNumProperties(); ++i)
        {
            const auto name = savedUi.getPropertyName (i);
            INFO ("UIState property " << name.toString());
            CHECK (ui.getProperty (name).toString() == savedUi.getProperty (name).toString());
        }
        CHECK (proc.isDawSync());
        CHECK (proc.getMidiCC (0) == 7);
        CHECK (proc.getMidiCC (1) == -1);
        CHECK (proc.getMidiCC (2) == 11);
        CHECK (proc.getChannelName (0) == "Mic left");
        CHECK (proc.getChannelName (1) == "Mic right");
    }

    SECTION ("save after load round-trips unchanged")
    {
        juce::MemoryBlock again;
        proc.getStateInformation (again);
        auto proc2 = std::make_unique<SPLMeterAudioProcessor>();
        proc2->setStateInformation (again.getData(), (int) again.getSize());
        CHECK (proc2->createStateXml()->isEquivalentTo (proc.createStateXml().get(), false));
    }
}
