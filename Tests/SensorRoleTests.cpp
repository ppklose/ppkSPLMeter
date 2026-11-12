#include <catch2/catch_test_macros.hpp>
#include "ProcessorTestHelpers.h"
#include "SensorConfig.h"

#include <cstring>

// Vibration spec §3.1: per-input sensor roles. Accelerometer / Off inputs must be
// excluded from the microphone mix, the 20 Hz bandpass and the correction filter.

using namespace ProcTest;

namespace
{
// Exact comparison on purpose (identical computations must give identical bits)
bool bitEqual (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; }

SensorConfig::Roles rolesOf (std::initializer_list<SensorRole> list)
{
    auto r = SensorConfig::defaultRoles();
    size_t i = 0;
    for (auto role : list) r[i++] = role;
    return r;
}

std::vector<std::pair<std::string, float>> withoutKeysStartingWith (
    const std::vector<std::pair<std::string, float>>& values, std::initializer_list<const char*> prefixes)
{
    std::vector<std::pair<std::string, float>> out;
    for (const auto& kv : values)
    {
        bool skip = false;
        for (auto* p : prefixes) skip = skip || kv.first.rfind (p, 0) == 0;
        if (! skip) out.push_back (kv);
    }
    return out;
}

// Last n samples of an FFT ring-buffer channel
std::vector<float> lastSamples (const SPLMeterAudioProcessor& p, int channel, int n)
{
    std::vector<float> v ((size_t) n);
    p.copyFftWindow (v.data(), n, channel);
    return v;
}
} // namespace

TEST_CASE ("SensorConfig: validation of role combinations", "[roles]")
{
    using SensorConfig::Validity;
    using R = SensorRole;
    CHECK (SensorConfig::validate (SensorConfig::defaultRoles()) == Validity::NoAccelerometer);
    CHECK (SensorConfig::validate (rolesOf ({ R::Mic, R::AccelX, R::AccelY, R::AccelZ })) == Validity::Triaxial);
    CHECK (SensorConfig::validate (rolesOf ({ R::AccelZ, R::Off, R::AccelX })) == Validity::Biaxial);
    CHECK (SensorConfig::validate (rolesOf ({ R::Mic, R::AccelZ })) == Validity::SingleAxis);
    CHECK (SensorConfig::validate (rolesOf ({ R::Mic, R::AccelUniaxial })) == Validity::Uniaxial);
    CHECK (SensorConfig::validate (rolesOf ({ R::AccelX, R::AccelX })) == Validity::DuplicateAxis);
    CHECK (SensorConfig::validate (rolesOf ({ R::AccelUniaxial, R::AccelZ })) == Validity::MixedKinds);
    CHECK (SensorConfig::validate (rolesOf ({ R::AccelUniaxial, R::AccelUniaxial })) == Validity::MixedKinds);
    CHECK_FALSE (SensorConfig::isValid (Validity::DuplicateAxis));
    CHECK (SensorConfig::isValid (Validity::Triaxial));

    // Only the channels the device actually has are considered
    CHECK (SensorConfig::validate (rolesOf ({ R::Mic, R::Mic, R::AccelX, R::AccelX }), 3) == Validity::SingleAxis);

    // Unknown persisted values fall back to Mic
    CHECK (SensorConfig::fromInt (99) == R::Mic);
    CHECK (SensorConfig::fromInt (-1) == R::Mic);
}

TEST_CASE ("Sensor roles: accelerometer and Off inputs are excluded from the SPL mix", "[roles]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Reference: the microphone signal alone on a mono input
    const Scenario mono { "mono_ref", 1, 2.0,
                          [] (int, int n) { return sine (1000.0, 0.1, n) + noise (0, n, 0.01f); },
                          [] (SPLMeterAudioProcessor&) {} };

    // Same mic signal on input 1, loud signals on accelerometer / Off inputs
    const Scenario quad { "quad_roles", 4, 2.0,
                          [] (int ch, int n)
                          {
                              switch (ch)
                              {
                                  case 0:  return sine (1000.0, 0.1, n) + noise (0, n, 0.01f);
                                  case 1:  return sine (80.0, 0.8, n);
                                  case 2:  return noise (2, n, 0.5f);
                                  default: return sine (5000.0, 0.7, n);
                              }
                          },
                          [] (SPLMeterAudioProcessor& p)
                          {
                              p.setChannelRole (1, SensorRole::AccelX);
                              p.setChannelRole (2, SensorRole::AccelY);
                              p.setChannelRole (3, SensorRole::Off);
                          } };

    // fft0/fft1 hold inputs 1/2 individually (input 2 differs by design)
    const auto ref = withoutKeysStartingWith (runScenario (mono), { "fft0.", "fft1." });
    const auto got = withoutKeysStartingWith (runScenario (quad), { "fft0.", "fft1." });

    REQUIRE (ref.size() == got.size());
    int mismatches = 0;
    std::string report;
    for (size_t i = 0; i < ref.size(); ++i)
    {
        REQUIRE (ref[i].first == got[i].first);
        if (! bitEqual (ref[i].second, got[i].second) && ++mismatches <= 10)
            report += ref[i].first + ": " + std::to_string (ref[i].second)
                    + " vs " + std::to_string (got[i].second) + "\n";
    }
    INFO (mismatches << " mismatches\n" << report);
    CHECK (mismatches == 0);   // identical computation, so bit-identical
}

TEST_CASE ("Sensor roles: SPL is silent when no input is a microphone", "[roles]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto proc = std::make_unique<SPLMeterAudioProcessor>();
    auto layout = proc->getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (2);
    REQUIRE (proc->setBusesLayout (layout));
    proc->setChannelRole (0, SensorRole::AccelZ);
    proc->setChannelRole (1, SensorRole::Off);
    proc->prepareToPlay (kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer (proc->getTotalNumInputChannels(), kBlockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < 100; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < kBlockSize; ++s)
                buffer.setSample (ch, s, sine (440.0, 0.5, b * kBlockSize + s));
        proc->processBlock (buffer, midi);
    }
    CHECK (proc->getRMSSPL() < 0.0f);   // -inf / floor: no microphone signal at all
    CHECK (proc->getPeakSPL() < 0.0f);
}

TEST_CASE ("Sensor roles: bandpass and correction filter leave accelerometer inputs untouched", "[roles]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto proc = std::make_unique<SPLMeterAudioProcessor>();
    auto layout = proc->getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (2);
    REQUIRE (proc->setBusesLayout (layout));
    proc->setChannelRole (1, SensorRole::AccelX);

    // Input 1 (Mic) and input 2 (Accel) get the same 5 Hz + 200 Hz signal
    auto signal = [] (int n) { return sine (5.0, 0.4, n) + sine (200.0, 0.2, n); };

    auto runBlocks = [&] (int numBlocks, int& n, bool slowDown)
    {
        juce::AudioBuffer<float> buffer (juce::jmax (proc->getTotalNumInputChannels(),
                                                     proc->getTotalNumOutputChannels()), kBlockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < numBlocks; ++b)
        {
            buffer.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < kBlockSize; ++s)
                    buffer.setSample (ch, s, signal (n + s));
            n += kBlockSize;
            proc->processBlock (buffer, midi);
            if (slowDown)   // let the convolution load its impulse response in the background
                juce::Thread::sleep (5);
        }
    };

    auto expectAccelUntouched = [&] (int n)
    {
        const int len = 4096;
        const auto accel = lastSamples (*proc, 1, len);
        const auto mic   = lastSamples (*proc, 0, len);
        int accelDiffs = 0;
        double micDiff = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const float input = signal (n - len + i);
            if (! bitEqual (accel[(size_t) i], input)) ++accelDiffs;
            micDiff = std::max (micDiff, (double) std::fabs (mic[(size_t) i] - input));
        }
        CHECK (accelDiffs == 0);     // accelerometer input passes unchanged
        CHECK (micDiff > 0.05);      // while the mic input was really filtered
    };

    SECTION ("bandpass")
    {
        setParam (*proc, "bandpassEnabled", 1.0f);
        proc->prepareToPlay (kSampleRate, kBlockSize);
        int n = 0;
        runBlocks (100, n, false);
        expectAccelUntouched (n);
    }

    SECTION ("correction filter")
    {
        proc->prepareToPlay (kSampleRate, kBlockSize);

        const auto tmp = juce::File::createTempFile ("splmeter-correction.txt");
        REQUIRE (tmp.replaceWithText ("20 -12\n100 -12\n1000 0\n10000 6\n20000 6\n"));
        proc->loadCorrectionFilter (tmp);
        REQUIRE (proc->apvts.getRawParameterValue ("correctionEnabled")->load() > 0.5f);

        int n = 0;
        runBlocks (60, n, true);    // warm-up while the IR loads
        runBlocks (100, n, false);
        REQUIRE (proc->isCorrectionLoaded());
        expectAccelUntouched (n);

        proc->clearCorrectionFilter();
        tmp.deleteFile();
    }
}

TEST_CASE ("Sensor roles: persisted in plugin state, default adds nothing", "[roles][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("default roles leave the state without an Inputs node")
    {
        auto proc = std::make_unique<SPLMeterAudioProcessor>();
        CHECK_FALSE (proc->getUiState().getChildWithName ("Inputs").isValid());
        CHECK (proc->getSensorValidity() == SensorConfig::Validity::NoAccelerometer);
    }

    SECTION ("roles round-trip through get/setStateInformation")
    {
        auto proc = std::make_unique<SPLMeterAudioProcessor>();
        proc->setChannelRole (0, SensorRole::Mic);
        proc->setChannelRole (1, SensorRole::AccelX);
        proc->setChannelRole (2, SensorRole::AccelY);
        proc->setChannelRole (3, SensorRole::AccelZ);
        proc->setChannelRole (7, SensorRole::Off);

        juce::MemoryBlock blob;
        proc->getStateInformation (blob);

        auto restored = std::make_unique<SPLMeterAudioProcessor>();
        restored->setChannelRole (5, SensorRole::AccelUniaxial);   // must be overwritten by the load
        restored->setStateInformation (blob.getData(), (int) blob.getSize());

        CHECK (restored->getChannelRole (0) == SensorRole::Mic);
        CHECK (restored->getChannelRole (1) == SensorRole::AccelX);
        CHECK (restored->getChannelRole (2) == SensorRole::AccelY);
        CHECK (restored->getChannelRole (3) == SensorRole::AccelZ);
        CHECK (restored->getChannelRole (5) == SensorRole::Mic);
        CHECK (restored->getChannelRole (7) == SensorRole::Off);

        // Switching back to Mic removes the entry again
        restored->setChannelRole (7, SensorRole::Mic);
        CHECK_FALSE (restored->getUiState().getChildWithName ("Inputs").hasProperty ("role7"));
    }

    SECTION ("v3.5.2 state loads as all-Mic")
    {
        const auto fixture = juce::File (SPLMETER_GOLDEN_DIR).getChildFile ("state_v3.5.2.xml");
        const auto xml = juce::XmlDocument::parse (fixture);
        REQUIRE (xml != nullptr);
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (*xml, blob);

        auto proc = std::make_unique<SPLMeterAudioProcessor>();
        proc->setChannelRole (2, SensorRole::AccelZ);   // stale role must be cleared by the load
        proc->setStateInformation (blob.getData(), (int) blob.getSize());
        for (int ch = 0; ch < SensorConfig::kMaxInputs; ++ch)
            CHECK (proc->getChannelRole (ch) == SensorRole::Mic);
    }
}
