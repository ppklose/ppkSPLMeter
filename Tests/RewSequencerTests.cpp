#include <catch2/catch_test_macros.hpp>
#include "Rew/RewSequencer.h"
#include "FakeRewTransport.h"

// REW sequencer spec §5 / §7 / step 3: the engine against a simulated REW

namespace
{
// Forwards to a FakeRewTransport the test keeps (the sequencer owns its transport)
struct SharedFake : rew::Transport
{
    explicit SharedFake (FakeRewTransport& f) : fake (f) {}
    rew::Response request (const juce::String& m, const juce::String& p, const juce::String& b = {}) override
    {
        return fake.request (m, p, b);
    }
    FakeRewTransport& fake;
};

rew::RunOptions fastOptions()
{
    rew::RunOptions o;
    o.pollIntervalMs = 1;
    o.retryWaitMs = 5;
    o.quietMs = 30;
    o.timeoutSec = 2.0;
    o.maxUnreachablePolls = 3;
    return o;
}

rew::Sequence threeSteps()
{
    rew::Sequence q;
    q.name = "Room";
    q.betweenStepsSec = 0.0;
    for (auto* out : { "L", "R", "L+R" })
    {
        rew::Step s;
        s.name = juce::String (out);
        s.output = juce::String (out);
        q.steps.push_back (s);
    }
    q.steps[0].level = rew::Level { -20.0, "dBFS" };
    q.steps[0].timingRef = juce::String ("Acoustic");
    return q;
}

rew::Progress runToEnd (rew::Sequencer& seq, FakeRewTransport& fake, const rew::Sequence& q,
                        rew::RunOptions o = fastOptions(), rew::Hooks hooks = {})
{
    REQUIRE (seq.start (q, std::make_unique<SharedFake> (fake), o, std::move (hooks)));
    seq.waitUntilFinished (20000);
    return seq.progress();
}

bool waitFor (rew::Sequencer& seq, rew::RunState state, int timeoutMs = 5000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (seq.progress().state == state) return true;
        juce::Thread::sleep (1);
    }
    return false;
}

std::vector<FakeRewTransport::Call> callsTo (const FakeRewTransport& fake, const juce::String& path, const juce::String& method = "POST")
{
    std::vector<FakeRewTransport::Call> out;
    for (auto& c : fake.calls())
        if (c.path == path && c.method == method) out.push_back (c);
    return out;
}

int indexOfCall (const FakeRewTransport& fake, const juce::String& path, const juce::String& bodyPart, int from = 0)
{
    const auto calls = fake.calls();
    for (int i = from; i < (int) calls.size(); ++i)
        if (calls[(size_t) i].path == path && calls[(size_t) i].method != "GET" && calls[(size_t) i].body.contains (bodyPart))
            return i;
    return -1;
}
}

TEST_CASE ("REW sequencer: happy path measures every step and restores REW", "[rew]")
{
    FakeRewTransport fake;
    rew::Sequencer seq;
    std::vector<juce::String> started, finished;
    rew::Hooks hooks;
    hooks.stepStarted  = [&] (const rew::StepResult& r) { started.push_back (r.title); };
    hooks.stepFinished = [&] (const rew::StepResult& r) { finished.push_back (r.title); };

    const auto p = runToEnd (seq, fake, threeSteps(), fastOptions(), hooks);

    CHECK (p.state == rew::RunState::Done);
    CHECK (p.version.api == "0.9.9");
    CHECK (p.driver == rew::Driver::Java);
    CHECK (p.sweepsDone == 3);
    REQUIRE (p.results.size() == 3);
    for (const auto& r : p.results)
    {
        CHECK (r.status == rew::StepStatus::Done);
        CHECK (r.uuids.size() == 1);
    }
    CHECK (fake.numMeasurements() == 3);
    CHECK (fake.measurementTitle (1) == "L");
    CHECK (fake.measurementTitle (3) == "L+R");
    CHECK (started == std::vector<juce::String> { "L", "R", "L+R" });
    CHECK (finished == started);

    // Only set fields are sent: steps 2 and 3 do not touch the level
    CHECK (callsTo (fake, "/measure/level").size() == 2);   // step 1 + restore
    CHECK (callsTo (fake, "/measure/sweep/configuration", "PUT").empty());

    // Title exactly as entered
    const auto naming = callsTo (fake, "/measure/naming", "PUT");
    REQUIRE (naming.size() == 3);
    CHECK (naming[0].body.contains ("Use as entered"));

    // Blocking switched off for the run and back on; settings restored
    CHECK (indexOfCall (fake, "/application/blocking", "false") >= 0);
    CHECK ((bool) fake.value ("/application/blocking"));
    CHECK ((double) fake.value ("/measure/level").getProperty ("value", 0.0) == -14.0);
    CHECK (fake.value ("/measure/timing/reference").toString() == "None");
    CHECK (fake.value ("/audio/java/output-channel").getProperty ("channel", {}).toString() == "L");
    CHECK (fake.value ("/measure/naming").getProperty ("namingOption", {}).toString() == "Add number");
}

TEST_CASE ("REW sequencer: inputs, multi-input, loops, groups, Save all", "[rew]")
{
    FakeRewTransport fake;
    rew::Sequencer seq;
    auto q = threeSteps();
    q.steps.resize (1);
    q.steps[0].input = juce::String ("2");
    q.steps[0].lastInput = juce::String ("4");
    q.steps[0].group = "Left";
    q.loops = 2;
    q.saveMdatPath = "/x/run";

    const auto p = runToEnd (seq, fake, q);
    CHECK (p.state == rew::RunState::Done);
    REQUIRE (p.results.size() == 2);
    CHECK (p.results[0].uuids.size() == 3);   // inputs 2–4
    CHECK (p.results[1].loop == 1);
    CHECK (fake.numMeasurements() == 6);

    // last input before first input, then last again
    const int a = indexOfCall (fake, "/audio/java/last-input-channel", "4");
    const int b = indexOfCall (fake, "/audio/java/input-channel", "2");
    const int c = indexOfCall (fake, "/audio/java/last-input-channel", "4", b);
    CHECK (a >= 0);
    CHECK (b > a);
    CHECK (c > b);

    // group created once and every measurement added
    CHECK (callsTo (fake, "/groups").size() == 1);
    const auto list = fake.request ("GET", "/measurements").json();
    CHECK (list.getProperty ("6", {}).getProperty ("groupName", {}).toString() == "Left");

    CHECK (fake.savedFiles() == juce::StringArray ({ "/x/run.mdat" }));
    CHECK (p.savedMdat == "/x/run.mdat");

    // inputs restored
    CHECK ((int) fake.value ("/audio/java/input-channel").getProperty ("channel", 0) == 1);
    CHECK ((int) fake.value ("/audio/java/last-input-channel").getProperty ("channel", 0) == 1);
}

TEST_CASE ("REW sequencer: second output (ASIO) and invert, restored afterwards", "[rew]")
{
    FakeRewTransport fake;
    rew::Sequencer seq;

    SECTION ("ASIO: output, then enable, then invert")
    {
        auto* d = new juce::DynamicObject();
        d->setProperty ("driver", "ASIO");
        fake.setValue ("/audio/driver", juce::var (d));
        rew::Sequence q;
        q.betweenStepsSec = 0.0;
        rew::Step s;
        s.name = "Stereo";
        s.output = juce::String ("Out 1");
        s.secondOutput = rew::SecondOutput { true, "Out 3", true };
        q.steps = { s };

        fake.clearCalls();
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Done);
        const int a = indexOfCall (fake, "/audio/asio/secondary-output", "Out 3");
        const int b = indexOfCall (fake, "/audio/asio/secondary-output-enable", "true");
        const int c = indexOfCall (fake, "/measure/invert-second-output", "true");
        CHECK (a >= 0);
        CHECK (b > a);
        CHECK (c > b);
        // restored
        CHECK (fake.value ("/audio/asio/secondary-output").getProperty ("output", {}).toString() == "Out 2");
        CHECK_FALSE ((bool) fake.value ("/audio/asio/secondary-output-enable").getProperty ("enable", true));
        CHECK_FALSE ((bool) fake.value ("/measure/invert-second-output"));
    }

    SECTION ("Java: only invert is sent")
    {
        auto q = threeSteps();
        q.steps.resize (1);
        q.steps[0].output = juce::String ("L+R");
        q.steps[0].secondOutput = rew::SecondOutput { false, {}, true };
        fake.clearCalls();
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Done);
        CHECK (indexOfCall (fake, "/measure/invert-second-output", "true") >= 0);
        CHECK (callsTo (fake, "/audio/asio/secondary-output").empty());
        CHECK (callsTo (fake, "/audio/asio/secondary-output-enable").empty());
    }

    SECTION ("Java: second output switched on is refused before anything is written")
    {
        auto q = threeSteps();
        q.steps[0].secondOutput = rew::SecondOutput { true, "R", false };
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Failed);
        CHECK (p.message.contains ("ASIO"));
    }
}

TEST_CASE ("REW sequencer: errors follow the on-error policy", "[rew]")
{
    FakeRewTransport fake;
    rew::Sequencer seq;
    auto q = threeSteps();
    fake.sweepPolls = 1;

    SECTION ("stop")
    {
        fake.nextError = "Clipping detected";
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Failed);
        REQUIRE (p.results.size() == 1);
        CHECK (p.results[0].status == rew::StepStatus::Failed);
        CHECK (p.results[0].messages.joinIntoString (" ").contains ("Clipping"));
        CHECK (p.message.contains ("L failed"));
        CHECK ((bool) fake.value ("/application/blocking"));   // restored after a failure too
    }

    SECTION ("skip")
    {
        q.onError = rew::OnError::Skip;
        fake.nextError = "Clipping detected";
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Done);
        REQUIRE (p.results.size() == 3);
        CHECK (p.results[0].status == rew::StepStatus::Failed);
        CHECK (p.results[1].status == rew::StepStatus::Done);
        CHECK (fake.numMeasurements() == 2);
    }

    SECTION ("retry once")
    {
        q.onError = rew::OnError::RetryOnce;
        fake.nextError = "Clipping detected";
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Done);
        REQUIRE (p.results.size() == 3);
        CHECK (p.results[0].status == rew::StepStatus::Done);
        CHECK (p.results[0].attempts == 2);
        CHECK (p.results[0].messages.joinIntoString (" ").contains ("Clipping"));
        CHECK (fake.numMeasurements() == 3);
    }

    SECTION ("warning keeps the measurement")
    {
        fake.nextWarning = "Low SNR";
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Done);
        CHECK (p.results[0].status == rew::StepStatus::Warning);
        CHECK (p.results[0].uuids.size() == 1);
    }

    SECTION ("timeout sends Cancel")
    {
        fake.hangNextSweep = true;
        auto o = fastOptions();
        o.timeoutSec = 0.05;
        const auto p = runToEnd (seq, fake, q, o);
        CHECK (p.state == rew::RunState::Failed);
        CHECK (p.results[0].messages.joinIntoString (" ").contains ("timeout"));
        CHECK (indexOfCall (fake, "/measure/command", "Cancel") >= 0);
        CHECK_FALSE (fake.busy());
    }

    SECTION ("busy once: the trigger is retried")
    {
        fake.busyRejects = 1;
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Done);
        CHECK (fake.numMeasurements() == 3);
    }

    SECTION ("busy twice: no Pro / blocked → failed with a clear message")
    {
        fake.busyRejects = 2;
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Failed);
        CHECK (p.results[0].messages.joinIntoString (" ").contains ("REW Pro"));
    }

    SECTION ("REW stops answering mid-sweep")
    {
        fake.hangNextSweep = true;
        REQUIRE (seq.start (q, std::make_unique<SharedFake> (fake), fastOptions()));
        REQUIRE (waitFor (seq, rew::RunState::Measuring));
        fake.reachable = false;
        seq.waitUntilFinished (5000);
        const auto p = seq.progress();
        CHECK (p.state == rew::RunState::Failed);
        CHECK (p.results[0].messages.joinIntoString (" ").contains ("stopped responding"));
    }
}

TEST_CASE ("REW sequencer: preflight refuses before writing anything", "[rew]")
{
    FakeRewTransport fake;
    rew::Sequencer seq;

    SECTION ("REW not reachable")
    {
        fake.reachable = false;
        const auto p = runToEnd (seq, fake, threeSteps());
        CHECK (p.state == rew::RunState::Failed);
        CHECK (p.message.contains ("not reachable"));
    }

    SECTION ("audio not ready")
    {
        fake.setValue ("/audio/status", juce::JSON::parse ("{\"enabled\":true,\"ready\":false}"));
        const auto p = runToEnd (seq, fake, threeSteps());
        CHECK (p.state == rew::RunState::Failed);
        CHECK (p.message.contains ("audio"));
    }

    SECTION ("sequence does not fit REW (unknown output)")
    {
        auto q = threeSteps();
        q.steps[1].output = juce::String ("9");
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Failed);
        CHECK (p.message.contains ("REW has no output \"9\""));
    }

    for (const auto& c : fake.calls())
        CHECK (c.method == "GET");
}

TEST_CASE ("REW sequencer: wait for user, abort, pause, dry run", "[rew]")
{
    FakeRewTransport fake;
    rew::Sequencer seq;
    auto q = threeSteps();

    SECTION ("continue and skip at the prompt")
    {
        q.steps[0].waitForUser = true;
        q.steps[0].prompt = "Move the mic";
        q.steps[1].waitForUser = true;
        REQUIRE (seq.start (q, std::make_unique<SharedFake> (fake), fastOptions()));

        REQUIRE (waitFor (seq, rew::RunState::WaitingForUser));
        CHECK (seq.progress().prompt == "Move the mic");
        CHECK (fake.numMeasurements() == 0);
        seq.continueAfterPrompt();

        // the second prompt (the state reads "waiting" until the first reply is taken)
        const auto end = juce::Time::getMillisecondCounter() + 5000;
        while (! seq.progress().prompt.contains ("\"R\"") && juce::Time::getMillisecondCounter() < end)
            juce::Thread::sleep (1);
        REQUIRE (seq.progress().prompt.contains ("\"R\""));
        seq.skipPrompted();

        seq.waitUntilFinished (5000);
        const auto p = seq.progress();
        CHECK (p.state == rew::RunState::Done);
        REQUIRE (p.results.size() == 3);
        CHECK (p.results[1].status == rew::StepStatus::Skipped);
        CHECK (fake.numMeasurements() == 2);
    }

    SECTION ("abort mid-sweep cancels and restores")
    {
        fake.hangNextSweep = true;
        REQUIRE (seq.start (q, std::make_unique<SharedFake> (fake), fastOptions()));
        REQUIRE (waitFor (seq, rew::RunState::Measuring));
        CHECK_FALSE (seq.start (q, std::make_unique<SharedFake> (fake), fastOptions()));   // one run at a time
        seq.abort();
        seq.waitUntilFinished (5000);
        const auto p = seq.progress();
        CHECK (p.state == rew::RunState::Aborted);
        CHECK (p.results.back().status == rew::StepStatus::Cancelled);
        CHECK (indexOfCall (fake, "/measure/command", "Cancel") >= 0);
        CHECK ((double) fake.value ("/measure/level").getProperty ("value", 0.0) == -14.0);
        CHECK ((bool) fake.value ("/application/blocking"));
    }

    SECTION ("abort at the prompt")
    {
        q.steps[0].waitForUser = true;
        REQUIRE (seq.start (q, std::make_unique<SharedFake> (fake), fastOptions()));
        REQUIRE (waitFor (seq, rew::RunState::WaitingForUser));
        seq.abort();
        seq.waitUntilFinished (5000);
        CHECK (seq.progress().state == rew::RunState::Aborted);
        CHECK (fake.numMeasurements() == 0);
    }

    SECTION ("pause holds between steps")
    {
        q.betweenStepsSec = 0.05;
        REQUIRE (seq.start (q, std::make_unique<SharedFake> (fake), fastOptions()));
        seq.setPaused (true);
        REQUIRE (waitFor (seq, rew::RunState::Paused));
        const int during = fake.numMeasurements();
        juce::Thread::sleep (50);
        CHECK (fake.numMeasurements() == during);
        seq.setPaused (false);
        seq.waitUntilFinished (5000);
        CHECK (seq.progress().state == rew::RunState::Done);
        CHECK (fake.numMeasurements() == 3);
    }

    SECTION ("dry run sends settings but no sweep")
    {
        auto o = fastOptions();
        o.dryRun = true;
        const auto p = runToEnd (seq, fake, q, o);
        CHECK (p.state == rew::RunState::Done);
        CHECK (p.dryRun);
        CHECK (fake.numMeasurements() == 0);
        CHECK (indexOfCall (fake, "/measure/command", "SPL") < 0);
        CHECK (indexOfCall (fake, "/audio/java/output-channel", "L+R") >= 0);
    }

    SECTION ("a finished run can be started again")
    {
        runToEnd (seq, fake, q);
        const auto p = runToEnd (seq, fake, q);
        CHECK (p.state == rew::RunState::Done);
        CHECK (fake.numMeasurements() == 6);
    }
}

TEST_CASE ("REW sequencer: runFinished sees the final state and adds its note", "[rew]")
{
    FakeRewTransport fake;
    rew::Sequencer seq;
    rew::Hooks hooks;
    rew::RunState seen = rew::RunState::Idle;
    size_t seenResults = 0;
    hooks.runFinished = [&] (const rew::Progress& p)
    {
        seen = p.state;
        seenResults = p.results.size();
        return juce::String ("Results: /x.csv");
    };
    const auto p = runToEnd (seq, fake, threeSteps(), fastOptions(), hooks);
    CHECK (seen == rew::RunState::Done);
    CHECK (seenResults == 3);
    CHECK (p.message.contains ("Results: /x.csv"));
}

TEST_CASE ("REW sequencer: sweep length and expected duration", "[rew]")
{
    CHECK (rew::sweepSamples ("512k") == 524288.0);
    CHECK (rew::sweepSamples ("1M") == 1048576.0);
    CHECK (rew::sweepSamples ("4M") == 4194304.0);
}

// Against a running REW (not part of the normal run): ./SPLMeterTests "[.rewlive-dryrun]"
// A dry run that writes REW's current values back (no sweep), then checks that
// naming, notes and blocking are as before.
TEST_CASE ("REW sequencer: live dry run against REW on port 4735", "[.rewlive-dryrun]")
{
    rew::HttpTransport http;
    REQUIRE (rew::parseVersion (http.get ("/version")).valid());

    const auto ep = rew::channelEndpoints (rew::parseDriver (http.get ("/audio/driver")));
    const auto before = [&]
    {
        std::map<juce::String, juce::String> m;
        for (const auto& path : { ep.output, ep.input, ep.lastInput, juce::String ("/measure/level"),
                                  juce::String ("/measure/sweep/configuration"), juce::String ("/measure/timing/reference"),
                                  juce::String ("/measure/naming"), juce::String ("/measure/notes"),
                                  juce::String ("/application/blocking") })
            m[path] = http.get (path).body;
        return m;
    }();

    rew::Step s;
    s.name = "SPLMeter dry run";
    s.output = http.get (ep.output).json().getProperty (ep.outputKey, {}).toString();
    s.input = http.get (ep.input).json().getProperty (ep.inputKey, {}).toString();
    const auto level = http.get ("/measure/level").json();
    s.level = rew::Level { (double) level.getProperty ("value", -12.0), level.getProperty ("unit", "dBFS").toString() };
    s.timingRef = http.get ("/measure/timing/reference").json().toString();
    rew::Sequence q;
    q.steps = { s };

    rew::Sequencer seq;
    rew::RunOptions o;
    o.dryRun = true;
    REQUIRE (seq.start (q, std::make_unique<rew::HttpTransport>(), o));
    seq.waitUntilFinished (20000);
    const auto p = seq.progress();
    INFO (p.message);
    for (auto& r : p.results) INFO (r.messages.joinIntoString (" | "));
    CHECK (p.state == rew::RunState::Done);

    for (const auto& [path, body] : before)
    {
        INFO (path);
        CHECK (http.get (path).body == body);
    }
}
