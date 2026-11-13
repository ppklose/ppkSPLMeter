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
