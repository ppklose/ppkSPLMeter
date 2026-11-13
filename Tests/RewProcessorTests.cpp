#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"

// REW sequencer spec §3: the last sequence persists with the plugin state

TEST_CASE ("REW sequencer in processor: the sequence persists in the plugin state", "[rew][processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto src = std::make_unique<SPLMeterAudioProcessor>();
    CHECK (src->getRewSequence().steps.empty());   // default without a stored node

    rew::Sequence q;
    q.name = "Living room";
    q.loops = 3;
    rew::Step s;
    s.name = "L";
    s.output = juce::String ("L");
    s.level = rew::Level { -20.0, "dBFS" };
    q.steps = { s };
    rew::addPositions (q, 0, 2);
    src->setRewSequence (q);

    juce::MemoryBlock blob;
    src->getStateInformation (blob);
    auto restored = std::make_unique<SPLMeterAudioProcessor>();
    restored->setStateInformation (blob.getData(), (int) blob.getSize());
    CHECK (restored->getRewSequence() == q);
}

#include "FakeRewTransport.h"

namespace
{
// The processor owns its transport; the test keeps the simulated REW
struct ForwardingTransport : rew::Transport
{
    explicit ForwardingTransport (FakeRewTransport& f) : fake (f) {}
    rew::Response request (const juce::String& m, const juce::String& p, const juce::String& b = {}) override
    {
        return fake.request (m, p, b);
    }
    FakeRewTransport& fake;
};

rew::RunOptions fastRun()
{
    rew::RunOptions o;
    o.pollIntervalMs = 1;
    o.retryWaitMs = 5;
    o.quietMs = 20;
    o.timeoutSec = 5.0;
    return o;
}

rew::Sequence twoSteps()
{
    rew::Sequence q;
    q.betweenStepsSec = 0.0;
    rew::Step s;
    s.name = "L";
    s.output = juce::String ("L");
    s.level = rew::Level { -20.0, "dBFS" };
    q.steps = { s, s };
    q.steps[1].name = "R";
    q.steps[1].output = juce::String ("R");
    return q;
}
}

TEST_CASE ("REW sequencer in processor: runs the stored sequence, one run at a time", "[rew][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRewTransport fake;
    auto p = std::make_unique<SPLMeterAudioProcessor>();
    p->setRewSequence (twoSteps());

    fake.hangNextSweep = true;
    REQUIRE (p->startRewRun (false, std::make_unique<ForwardingTransport> (fake), fastRun()));
    CHECK_FALSE (p->startRewRun (false, std::make_unique<ForwardingTransport> (fake), fastRun()));
    for (int i = 0; i < 2000 && p->getRewSequencer().progress().state != rew::RunState::Measuring; ++i)
        juce::Thread::sleep (1);
    fake.request ("POST", "/measure/command", "{\"command\":\"Cancel\"}");   // let the hung sweep end…
    fake.hangNextSweep = false;
    p->getRewSequencer().abort();                                           // …and stop the run
    p->getRewSequencer().waitUntilFinished (5000);
    CHECK (p->getRewSequencer().progress().state == rew::RunState::Aborted);

    REQUIRE (p->startRewRun (false, std::make_unique<ForwardingTransport> (fake), fastRun()));
    p->getRewSequencer().waitUntilFinished (10000);
    const auto prog = p->getRewSequencer().progress();
    CHECK (prog.state == rew::RunState::Done);
    CHECK (prog.results.size() == 2);
    CHECK (fake.measurementTitle (2) == "R");
}

TEST_CASE ("REW sequencer in processor: destroying the processor aborts and restores REW", "[rew][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRewTransport fake;
    {
        auto p = std::make_unique<SPLMeterAudioProcessor>();
        p->setRewSequence (twoSteps());
        fake.hangNextSweep = true;
        REQUIRE (p->startRewRun (false, std::make_unique<ForwardingTransport> (fake), fastRun()));
        for (int i = 0; i < 2000 && p->getRewSequencer().progress().state != rew::RunState::Measuring; ++i)
            juce::Thread::sleep (1);
    }
    CHECK_FALSE (fake.busy());                                                  // Cancel sent
    CHECK ((double) fake.value ("/measure/level").getProperty ("value", 0.0) == -14.0);
    CHECK ((bool) fake.value ("/application/blocking"));
}

#include "ProcessorTestHelpers.h"
#include <catch2/catch_approx.hpp>
#include <future>

namespace
{
// Feeds the processor block by block (the test is the audio thread)
struct AudioRig
{
    std::unique_ptr<SPLMeterAudioProcessor> proc = std::make_unique<SPLMeterAudioProcessor>();
    int inputs, n = 0;
    std::function<float (int ch, int n)> signal;

    explicit AudioRig (int numInputs, std::function<float (int, int)> sig) : inputs (numInputs), signal (std::move (sig))
    {
        auto layout = proc->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (inputs);
        REQUIRE (proc->setBusesLayout (layout));
    }
    void prepare() { proc->prepareToPlay (ProcTest::kSampleRate, ProcTest::kBlockSize); }
    void block()
    {
        juce::AudioBuffer<float> buffer (juce::jmax (proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels()),
                                         ProcTest::kBlockSize);
        juce::MidiBuffer midi;
        buffer.clear();
        for (int ch = 0; ch < inputs; ++ch)
            for (int s = 0; s < ProcTest::kBlockSize; ++s)
                buffer.setSample (ch, s, signal (ch, n + s));
        n += ProcTest::kBlockSize;
        proc->processBlock (buffer, midi);
    }
    void seconds (double sec) { for (int b = 0; b < (int) (sec * ProcTest::kSampleRate / ProcTest::kBlockSize); ++b) block(); }

    // end runs on another thread while audio keeps coming, as in the app
    rew::StepLevels endWhileProcessing()
    {
        auto f = std::async (std::launch::async, [this] { return proc->endRewStepMeasurement(); });
        while (f.wait_for (std::chrono::milliseconds (0)) != std::future_status::ready)
            block();
        return f.get();
    }
};
}

TEST_CASE ("REW sequencer in processor: per-step levels of a known sine", "[rew][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using Catch::Approx;
    const double amp = 0.1;
    AudioRig rig (1, [amp] (int, int n) { return ProcTest::sine (1000.0, amp, n); });
    rig.prepare();
    rig.seconds (5.0);                             // settle the filters and the SLOW detector (τ = 1 s)
    const float cal = rig.proc->apvts.getRawParameterValue ("calOffset")->load();

    rig.proc->beginRewStepMeasurement();
    rig.seconds (2.0);
    const auto l = rig.endWhileProcessing();
    REQUIRE (l.valid);
    const double rmsDb = 20.0 * std::log10 (amp / std::sqrt (2.0)) + cal;
    CHECK (l.lzeq == Approx (rmsDb).margin (0.05));
    CHECK (l.laeq == Approx (rmsDb).margin (0.2));     // A ≈ 0 dB at 1 kHz
    CHECK (l.lceq == Approx (rmsDb).margin (0.2));
    CHECK (l.lafmax == Approx (rmsDb).margin (0.3));   // steady sine
    CHECK (l.lcsmax == Approx (rmsDb).margin (0.3));
    CHECK (l.lcpeak == Approx (rmsDb + 3.01).margin (0.3));
    CHECK (l.seconds == Approx (2.0).margin (0.1));
    CHECK (l.pausedSeconds == 0.0);
    CHECK_FALSE (l.vibValid);

    SECTION ("paused time is counted, not measured")
    {
        rig.proc->beginRewStepMeasurement();
        rig.seconds (0.5);
        rig.proc->setPaused (true);
        rig.seconds (0.5);
        rig.proc->setPaused (false);
        const auto p = rig.endWhileProcessing();
        CHECK (p.pausedSeconds == Approx (0.5).margin (0.02));
        CHECK (p.laeq == Approx (rmsDb).margin (0.2));
    }

    SECTION ("without audio the end gives up and returns no levels")
    {
        rig.proc->beginRewStepMeasurement();
        CHECK_FALSE (rig.proc->endRewStepMeasurement().valid);   // the audio thread never took the begin
    }
}

TEST_CASE ("REW sequencer in processor: per-step vibration of a triaxial accelerometer", "[rew][processor][vibration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using Catch::Approx;
    // Mic on input 1, triax on 2–4; 1 FS = 1 m/s² (cal 120 dB); X 0.5, Y 0.25, Z 1.0 peak at 80 Hz
    const double amps[] = { 0.0, 0.5, 0.25, 1.0 };
    AudioRig rig (4, [&amps] (int ch, int n) { return ProcTest::sine (80.0, ch == 0 ? 0.05 : amps[ch], n); });
    for (int i = 0; i < 3; ++i)
    {
        rig.proc->setChannelRole (1 + i, (SensorRole) (1 + i));
        rig.proc->setAccelCalDb (1 + i, 120.0);
    }
    auto s = rig.proc->getVibrationSettings();
    s.tauSec = 1.0;
    rig.proc->setVibrationSettings (s);
    rig.prepare();
    rig.seconds (3.0);

    rig.proc->beginRewStepMeasurement();
    rig.seconds (4.0);
    const auto l = rig.endWhileProcessing();
    REQUIRE (l.vibValid);
    CHECK (l.axisMask == 0b111);
    CHECK (l.vibSlotPresent (3));
    const double k = 1.0 / std::sqrt (2.0);
    CHECK (l.vibRms[0] == Approx (0.5 * k).epsilon (0.03));
    CHECK (l.vibRms[2] == Approx (1.0 * k).epsilon (0.03));
    CHECK (l.vibRms[3] == Approx (std::sqrt (0.25 + 0.0625 + 1.0) * k).epsilon (0.03));
    CHECK (l.vibPeak[2] == Approx (1.0).epsilon (0.03));
}

TEST_CASE ("REW sequencer in processor: a run writes results, levels and markers", "[rew][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRewTransport fake;
    AudioRig rig (1, [] (int, int n) { return ProcTest::sine (1000.0, 0.1, n); });
    rig.prepare();
    rig.seconds (0.3);

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rew_run_test");
    folder.deleteRecursively();
    auto q = twoSteps();
    q.name = "Run test";
    q.resultsFolder = folder.getFullPathName();
    rig.proc->setRewSequence (q);

    fake.sweepPolls = 20;   // the sweep "takes" a few polls, while audio runs
    REQUIRE (rig.proc->startRewRun (false, std::make_unique<ForwardingTransport> (fake), fastRun()));
    auto& sq = rig.proc->getRewSequencer();
    for (int i = 0; i < 20000 && (sq.isRunning() || sq.progress().state == rew::RunState::Preflight); ++i)
    {
        rig.block();
        juce::Thread::sleep (1);
    }
    sq.waitUntilFinished (5000);
    const auto p = sq.progress();
    REQUIRE (p.state == rew::RunState::Done);

    const auto levels = rig.proc->getRewStepLevels();
    REQUIRE (levels.size() == 2);
    CHECK (levels[0].valid);
    CHECK (levels[1].valid);

    const auto csv = rig.proc->getRewResultsFile();
    REQUIRE (csv.existsAsFile());
    CHECK (csv.getParentDirectory() == folder);
    CHECK (csv.loadFileAsString().contains ("Run test"));
    CHECK (p.message.contains (csv.getFullPathName()));

    const auto markers = rig.proc->takePendingMarkers();
    REQUIRE (markers.size() == 2);
    CHECK (markers[0].text == "REW 1/2 L");
    CHECK (markers[1].text == "REW 2/2 R");
    CHECK (rig.proc->takePendingMarkers().empty());
    folder.deleteRecursively();
}
