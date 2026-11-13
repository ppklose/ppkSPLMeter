#pragma once
#include "RewSequence.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace rew
{
//==============================================================================
// REW sequencer spec §5: runs a Sequence against REW on its own thread.
// Preflight → per step: [wait for user] → configure → trigger → wait for the
// new measurement(s) → group → next; at the end Save all and restore REW's
// settings and blocking mode, also after an abort or error.

enum class StepStatus { Pending, Running, Done, Warning, Failed, Skipped, Cancelled };
enum class RunState { Idle, Preflight, WaitingForUser, Configuring, Measuring, Between, Paused,
                      Finishing, Done, Aborted, Failed };

inline const char* toString (StepStatus s)
{
    switch (s)
    {
        case StepStatus::Pending:   return "pending";
        case StepStatus::Running:   return "running";
        case StepStatus::Done:      return "ok";
        case StepStatus::Warning:   return "warning";
        case StepStatus::Failed:    return "failed";
        case StepStatus::Skipped:   return "skipped";
        case StepStatus::Cancelled: return "cancelled";
    }
    return "";
}

inline const char* toString (RunState s)
{
    switch (s)
    {
        case RunState::Idle:           return "Idle";
        case RunState::Preflight:      return "Checking REW";
        case RunState::WaitingForUser: return "Waiting for you";
        case RunState::Configuring:    return "Configuring REW";
        case RunState::Measuring:      return "Measuring";
        case RunState::Between:        return "Next step";
        case RunState::Paused:         return "Paused";
        case RunState::Finishing:      return "Finishing";
        case RunState::Done:           return "Done";
        case RunState::Aborted:        return "Aborted";
        case RunState::Failed:         return "Failed";
    }
    return "";
}

struct StepResult
{
    int step = -1, loop = 0;          // 0-based step index, 0-based loop
    juce::String title;
    StepStatus status = StepStatus::Pending;
    juce::StringArray messages;       // REW errors and warnings, refused settings
    juce::StringArray uuids;          // new REW measurements
    juce::Time start;
    double durationSec = 0.0;
    int attempts = 0;
};

struct Progress
{
    RunState state = RunState::Idle;
    int step = -1, loop = 0;          // current position
    int sweepsDone = 0, totalSweeps = 0;
    juce::String title, prompt, message;   // prompt: WaitingForUser; message: last error / summary
    double stepElapsedSec = 0.0, stepExpectedSec = 0.0;
    Version version;
    Driver driver = Driver::Unknown;
    bool dryRun = false;
    juce::String savedMdat;
    std::vector<StepResult> results;

    bool running() const
    {
        return state != RunState::Idle && state != RunState::Done
            && state != RunState::Aborted && state != RunState::Failed;
    }
};

struct RunOptions
{
    bool dryRun = false;              // send settings, skip the sweeps
    int pollIntervalMs = 500;
    int retryWaitMs = 1000;           // after a 400 "busy"
    int quietMs = 2000;               // multi-input: no further measurement for this long → done
    double timeoutMarginSec = 15.0;
    double timeoutSec = 0.0;          // > 0 replaces the computed sweep timeout (tests)
    int maxUnreachablePolls = 10;     // consecutive failed polls before giving up
};

// Called on the sequencer thread (spec §6: markers, per-step integrator)
struct Hooks
{
    std::function<void (const StepResult&)> stepStarted;    // just before the trigger
    std::function<void (const StepResult&)> stepFinished;
    // After the end of the run (REW restored), with the final state; the text
    // returned is added to the run's message (e.g. where the results went)
    std::function<juce::String (const Progress&)> runFinished;
};

// Sweep length text → samples ("512k" = 524288, "1M" = 1048576)
inline double sweepSamples (const juce::String& length)
{
    const auto t = length.trim();
    const double n = t.getDoubleValue();
    if (t.endsWithIgnoreCase ("M")) return n * 1048576.0;
    if (t.endsWithIgnoreCase ("k")) return n * 1024.0;
    return n;
}

//==============================================================================
class Sequencer : private juce::Thread
{
public:
    Sequencer() : juce::Thread ("REW sequencer") {}
    ~Sequencer() override { abort(); stopThread (30000); }

    // Starts a run; false if one is running. The transport is used only by the
    // sequencer thread until the run ends.
    bool start (const Sequence& seq, std::unique_ptr<Transport> transport,
                RunOptions options = {}, Hooks hooks = {})
    {
        if (isThreadRunning())
        {
            if (progress().running())
                return false;
            stopThread (1000);
        }
        seq_ = seq;
        transport_ = std::move (transport);
        options_ = options;
        hooks_ = std::move (hooks);
        abort_ = false;
        pause_ = false;
        userReply_ = 0;
        {
            std::lock_guard<std::mutex> l (m_);
            progress_ = {};
            progress_.state = RunState::Preflight;
            progress_.dryRun = options.dryRun;
            progress_.totalSweeps = totalSweeps (seq);
        }
        startThread();
        return true;
    }

    void abort()                     { abort_ = true; wake_.signal(); }
    void setPaused (bool p)          { pause_ = p; wake_.signal(); }   // takes effect between steps
    bool isPaused() const            { return pause_; }
    void continueAfterPrompt()       { userReply_ = 1; wake_.signal(); }
    void skipPrompted()              { userReply_ = 2; wake_.signal(); }
    bool isRunning() const           { return progress().running(); }
    void waitUntilFinished (int timeoutMs = -1) { waitForThreadToExit (timeoutMs); }

    Progress progress() const
    {
        std::lock_guard<std::mutex> l (m_);
        auto p = progress_;
        if (p.state == RunState::Measuring && stepStartMs_ > 0.0)
            p.stepElapsedSec = (juce::Time::getMillisecondCounterHiRes() - stepStartMs_) / 1000.0;
        return p;
    }

private:
    //==========================================================================
    void run() override
    {
        const auto endState = runSequence();
        finish();
        const auto finalState = abort_ && endState != RunState::Failed ? RunState::Aborted : endState;
        if (hooks_.runFinished)
        {
            auto p = progress();
            p.state = finalState;
            p.prompt.clear();
            const auto note = hooks_.runFinished (p);
            if (note.isNotEmpty())
                setMessage (p.message.isNotEmpty() ? p.message + "\n" + note : note);
        }
        std::lock_guard<std::mutex> l (m_);
        progress_.state = finalState;
        progress_.prompt.clear();
    }

    RunState runSequence()
    {
        if (! preflight())
            return RunState::Failed;

        for (int loop = 0; loop < seq_.loops; ++loop)
            for (int i = 0; i < (int) seq_.steps.size(); ++i)
            {
                if (! seq_.steps[(size_t) i].enabled)
                    continue;
                if (abort_)
                    return RunState::Aborted;
                if (! waitWhilePaused())
                    return RunState::Aborted;

                auto result = runStepWithPolicy (i, loop);
                if (abort_)
                    return RunState::Aborted;
                if (result.status == StepStatus::Failed && seq_.onError == OnError::Stop)
                {
                    setMessage ("Stopped: " + result.title + " failed. " + result.messages.joinIntoString (" "));
                    return RunState::Failed;
                }
                if (! isLastSweep (i, loop) && seq_.betweenStepsSec > 0.0)
                {
                    setState (RunState::Between);
                    if (! sleep (seq_.betweenStepsSec * 1000.0))
                        return RunState::Aborted;
                }
            }
        return RunState::Done;
    }

    bool isLastSweep (int i, int loop) const
    {
        if (loop + 1 < seq_.loops) return false;
        for (int j = i + 1; j < (int) seq_.steps.size(); ++j)
            if (seq_.steps[(size_t) j].enabled) return false;
        return true;
    }

    //==========================================================================
    // Preflight (spec §5.2): reachable, audio ready, choices, validation; then
    // blocking off and the snapshot. Nothing is written before validation passes.
    bool preflight()
    {
        auto& t = *transport_;
        const auto version = parseVersion (t.get ("/version"));
        if (! version.valid())
            return fail ("REW is not reachable on port " + juce::String (seq_.port)
                         + ". Start REW and enable its API (Preferences → API).");
        const auto audio = t.get ("/audio/status").json();
        if (! (bool) audio.getProperty ("ready", false))
            return fail ("REW's audio is not ready. Check REW's soundcard settings.");

        choices_ = queryChoices (t);
        {
            std::lock_guard<std::mutex> l (m_);
            progress_.version = version;
            progress_.driver = choices_.driver;
        }
        endpoints_ = channelEndpoints (choices_.driver);

        const auto issues = validate (seq_, choices_);
        if (hasErrors (issues))
        {
            juce::StringArray lines;
            for (const auto& is : issues)
                if (is.error)
                    lines.add ((is.step >= 0 ? stepTitle (seq_, is.step) + ": " : juce::String()) + is.message);
            return fail ("The sequence does not fit this REW setup:\n" + lines.joinIntoString ("\n"));
        }

        // Blocking calls time out after 10 s and the setting persists in REW
        const auto blocking = t.get ("/application/blocking");
        if (blocking.ok() && blocking.json().isBool() && (bool) blocking.json())
        {
            if (! t.post ("/application/blocking", false).ok())
                return fail ("Could not switch off REW's blocking mode.");
            restoreBlocking_ = true;
        }

        snapshot_.clear();
        touched_.clear();
        groupUuids_.clear();
        if (seq_.restoreRewSettings)
            for (const auto& path : settingPaths())
                if (auto r = t.get (path); r.ok())
                    snapshot_[path] = r.body;
        return true;
    }

    juce::StringArray settingPaths() const
    {
        return { endpoints_.output, "/audio/asio/secondary-output", "/audio/asio/secondary-output-enable",
                 "/measure/invert-second-output", endpoints_.lastInput, endpoints_.input,
                 "/measure/level", "/measure/sweep/configuration", "/measure/sweep/repetitions",
                 "/measure/timing/reference", "/measure/protection-options",
                 "/measure/naming", "/measure/notes" };
    }

    //==========================================================================
    StepResult runStepWithPolicy (int i, int loop)
    {
        auto result = runStep (i, loop, 1);
        if (result.status == StepStatus::Failed && seq_.onError == OnError::RetryOnce && ! abort_)
        {
            auto retry = runStep (i, loop, 2);
            auto messages = result.messages;   // keep the first attempt's errors
            messages.addArray (retry.messages);
            retry.messages = messages;
            result = retry;
        }
        // "retry once" falls back to skipping; "skip" continues anyway
        publishResult (result, true);
        if (hooks_.stepFinished)
            hooks_.stepFinished (result);
        return result;
    }

    StepResult runStep (int i, int loop, int attempt)
    {
        const auto& s = seq_.steps[(size_t) i];
        StepResult r;
        r.step = i;
        r.loop = loop;
        r.title = stepTitle (seq_, i);
        r.attempts = attempt;
        r.status = StepStatus::Running;
        {
            std::lock_guard<std::mutex> l (m_);
            progress_.step = i;
            progress_.loop = loop;
            progress_.title = r.title;
            progress_.stepElapsedSec = 0.0;
        }

        if (s.waitForUser && attempt == 1)
        {
            userReply_ = 0;
            {
                std::lock_guard<std::mutex> l (m_);
                progress_.state = RunState::WaitingForUser;
                progress_.prompt = stepPrompt (seq_, i);
            }
            while (userReply_ == 0 && ! abort_)
                wake_.wait (100);
            { std::lock_guard<std::mutex> l (m_); progress_.prompt.clear(); }
            if (abort_)                { r.status = StepStatus::Cancelled; return r; }
            if (userReply_ == 2)       { r.status = StepStatus::Skipped;   return r; }
        }

        setState (RunState::Configuring);
        juce::String refused;
        if (! configure (s, r.title, refused))
        {
            r.status = StepStatus::Failed;
            r.messages.add (refused);
            return r;
        }
        if (s.preDelaySec > 0.0 && ! sleep (s.preDelaySec * 1000.0))
        {
            r.status = StepStatus::Cancelled;
            return r;
        }

        r.start = juce::Time::getCurrentTime();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        if (options_.dryRun)
        {
            r.status = StepStatus::Done;
            r.messages.add ("Dry run: settings sent, no sweep.");
            return r;
        }
        measure (s, r);
        r.durationSec = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

        if (r.status == StepStatus::Done && s.group.isNotEmpty() && ! r.uuids.isEmpty())
            if (! assignGroup (s.group, r.uuids))
                r.messages.add ("Could not add the measurement to group \"" + s.group + "\".");
        return r;
    }

    //==========================================================================
    // Configure (spec §5.2): only the fields the step sets, plus title and notes
    bool configure (const Step& s, const juce::String& title, juce::String& refused)
    {
        auto obj = [] (std::initializer_list<std::pair<const char*, juce::var>> props)
        {
            auto* o = new juce::DynamicObject();
            for (auto& [k, v] : props) o->setProperty (k, v);
            return juce::var (o);
        };
        auto channelValue = [this] (const juce::String& ch)
        {
            return endpoints_.inputIsNumber ? juce::var (ch.getIntValue()) : juce::var (ch);
        };

        struct Write { const char* method; juce::String path; juce::var body; };
        std::vector<Write> writes;
        if (s.output)
            writes.push_back ({ "POST", endpoints_.output, obj ({ { endpoints_.outputKey.toRawUTF8(), *s.output } }) });
        if (s.secondOutput)
        {
            if (! endpoints_.inputIsNumber)   // ASIO: output first, then the switch
            {
                if (s.secondOutput->enabled)
                    writes.push_back ({ "POST", "/audio/asio/secondary-output", obj ({ { "output", s.secondOutput->output } }) });
                writes.push_back ({ "POST", "/audio/asio/secondary-output-enable", obj ({ { "enable", s.secondOutput->enabled } }) });
            }
            writes.push_back ({ "POST", "/measure/invert-second-output", s.secondOutput->invert });
        }
        if (s.input)
        {
            // last before first, or REW clamps the range; then last again
            const auto last = s.lastInput ? *s.lastInput : *s.input;
            writes.push_back ({ "POST", endpoints_.lastInput, obj ({ { endpoints_.inputKey.toRawUTF8(), channelValue (last) } }) });
            writes.push_back ({ "POST", endpoints_.input,     obj ({ { endpoints_.inputKey.toRawUTF8(), channelValue (*s.input) } }) });
            if (last != *s.input)
                writes.push_back ({ "POST", endpoints_.lastInput, obj ({ { endpoints_.inputKey.toRawUTF8(), channelValue (last) } }) });
        }
        if (s.level)
            writes.push_back ({ "POST", "/measure/level", obj ({ { "value", s.level->value }, { "unit", s.level->unit } }) });
        if (s.sweep)
            writes.push_back ({ "PUT", "/measure/sweep/configuration",
                                obj ({ { "startFrequency", s.sweep->startHz }, { "endFrequency", s.sweep->endHz },
                                       { "length", s.sweep->length } }) });
        if (s.repetitions)
            writes.push_back ({ "POST", "/measure/sweep/repetitions", *s.repetitions });
        if (s.timingRef)
            writes.push_back ({ "POST", "/measure/timing/reference", *s.timingRef });
        if (s.protection)
            writes.push_back ({ "PUT", "/measure/protection-options",
                                obj ({ { "clippingAbort", s.protection->clippingAbort },
                                       { "splLimitAbort", s.protection->splLimitAbort },
                                       { "dBSPLLimit", s.protection->dBSPLLimit } }) });
        // The title exactly as entered: no number, date, output prefix or level suffix
        writes.push_back ({ "PUT", "/measure/naming",
                            obj ({ { "title", title }, { "namingOption", "Use as entered" },
                                   { "prefixMeasNameWithOutput", false }, { "appendLevelToMeasName", false } }) });
        writes.push_back ({ "POST", "/measure/notes", s.notes });   // always, so notes do not carry over

        for (const auto& w : writes)
        {
            if (abort_)
                return false;
            touched_.insert (w.path);
            auto r = transport_->request (w.method, w.path, Transport::toJson (w.body));
            if (r.busy() && sleep (options_.retryWaitMs))
                r = transport_->request (w.method, w.path, Transport::toJson (w.body));
            if (! r.ok())
            {
                refused = "REW refused " + w.path + " (" + describe (r) + ").";
                return false;
            }
        }
        return true;
    }

    //==========================================================================
    // Trigger and wait (spec §5.3): new UUIDs, new errors/warnings, timeout
    void measure (const Step& s, StepResult& r)
    {
        auto& t = *transport_;
        const auto before = measurementUuids (t.get ("/measurements").json());
        const int errorsBefore = t.get ("/application/errors").json().size();
        const int warningsBefore = t.get ("/application/warnings").json().size();
        const double expectedSec = expectedDuration();
        const double timeoutSec = options_.timeoutSec > 0.0 ? options_.timeoutSec
                                                             : expectedSec + options_.timeoutMarginSec;
        const int expectedCount = expectedMeasurements (s);

        {
            std::lock_guard<std::mutex> l (m_);
            progress_.state = RunState::Measuring;
            progress_.stepExpectedSec = expectedSec;
            stepStartMs_ = juce::Time::getMillisecondCounterHiRes();
        }
        if (hooks_.stepStarted)
            hooks_.stepStarted (r);

        auto trigger = [&] { return t.post ("/measure/command", juce::JSON::parse ("{\"command\":\"SPL\"}")); };
        auto resp = trigger();
        if (resp.busy() && sleep (options_.retryWaitMs))
            resp = trigger();
        if (! resp.ok())
        {
            r.status = StepStatus::Failed;
            r.messages.add ("REW did not start the measurement (" + describe (resp)
                            + "). Automated sweeps need a REW Pro licence.");
            return;
        }

        const double t0 = juce::Time::getMillisecondCounterHiRes();
        double lastNewMs = 0.0;
        int unreachable = 0;
        for (;;)
        {
            if (! sleep (options_.pollIntervalMs))
            {
                t.post ("/measure/command", juce::JSON::parse ("{\"command\":\"Cancel\"}"));
                r.status = StepStatus::Cancelled;
                return;
            }
            const double now = juce::Time::getMillisecondCounterHiRes();

            const auto list = t.get ("/measurements");
            if (! list.connected())
            {
                if (++unreachable >= options_.maxUnreachablePolls)
                {
                    r.status = StepStatus::Failed;
                    r.messages.add ("REW stopped responding.");
                    return;
                }
                continue;
            }
            unreachable = 0;

            const auto newErrors = newMessages ("/application/errors", errorsBefore);
            if (! newErrors.isEmpty())
            {
                r.status = StepStatus::Failed;
                r.messages.addArray (newErrors);
                return;
            }

            int added = 0;
            for (const auto& u : measurementUuids (list.json()))
                if (! before.contains (u) && ! r.uuids.contains (u))
                {
                    r.uuids.add (u);
                    ++added;
                }
            if (added > 0)
                lastNewMs = now;

            const bool complete = r.uuids.size() >= expectedCount
                                  && (expectedCount == 1 || now - lastNewMs >= options_.quietMs);
            if (complete)
            {
                const auto warnings = newMessages ("/application/warnings", warningsBefore);
                r.messages.addArray (warnings);
                r.status = warnings.isEmpty() ? StepStatus::Done : StepStatus::Warning;
                return;
            }
            if ((now - t0) / 1000.0 > timeoutSec)
            {
                t.post ("/measure/command", juce::JSON::parse ("{\"command\":\"Cancel\"}"));
                r.status = r.uuids.isEmpty() ? StepStatus::Failed : StepStatus::Warning;
                r.messages.add (r.uuids.isEmpty()
                                    ? "No measurement after " + juce::String (timeoutSec, 0) + " s (timeout)."
                                    : "Expected " + juce::String (expectedCount) + " measurements, got "
                                          + juce::String (r.uuids.size()) + ".");
                return;
            }
        }
    }

    juce::StringArray newMessages (const juce::String& path, int countBefore)
    {
        juce::StringArray out;
        const auto list = transport_->get (path).json();
        if (auto* arr = list.getArray())
            for (int k = countBefore; k < arr->size(); ++k)
            {
                const auto& e = (*arr)[k];
                auto title = e.getProperty ("title", {}).toString();
                auto msg = e.getProperty ("message", {}).toString();
                out.add (title.isNotEmpty() && title != msg ? title + ": " + msg : msg);
            }
        return out;
    }

    // REW's current settings after configure: start delay + repetitions × (sweep + 1 s)
    double expectedDuration()
    {
        auto& t = *transport_;
        const double fs = (double) t.get ("/audio/samplerate").json().getProperty ("value", 48000.0);
        const auto sweep = t.get ("/measure/sweep/configuration").json();
        const double samples = sweepSamples (sweep.getProperty ("length", "512k").toString());
        const auto repsV = t.get ("/measure/sweep/repetitions").json();
        const int reps = juce::jmax (1, repsV.isVoid() ? 1 : (int) repsV);
        const auto delayV = t.get ("/measure/start-delay").json();
        const double startDelay = delayV.isVoid() ? 2.0 : (double) delayV;
        return startDelay + reps * (samples / juce::jmax (8000.0, fs) + 1.0);
    }

    // Java multi-input: one measurement per input in the range
    int expectedMeasurements (const Step& s) const
    {
        if (! s.input || ! s.lastInput)
            return 1;
        if (endpoints_.inputIsNumber)
            return juce::jmax (1, s.lastInput->getIntValue() - s.input->getIntValue() + 1);
        return juce::jmax (1, choices_.inputs.indexOf (*s.lastInput) - choices_.inputs.indexOf (*s.input) + 1);
    }

    bool assignGroup (const juce::String& name, const juce::StringArray& uuids)
    {
        auto& t = *transport_;
        auto findGroup = [&]
        {
            const auto groups = t.get ("/groups").json();   // keep the var alive for getArray()
            if (auto* arr = groups.getArray())
                for (const auto& g : *arr)
                    if (g.getProperty ("name", {}).toString() == name)
                        return g.getProperty ("uuid", {}).toString();
            return juce::String();
        };
        auto uuid = groupUuids_.count (name) ? groupUuids_[name] : findGroup();
        if (uuid.isEmpty())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", name);
            t.post ("/groups", juce::var (o));
            uuid = findGroup();
        }
        if (uuid.isEmpty())
            return false;
        groupUuids_[name] = uuid;
        bool ok = true;
        for (const auto& m : uuids)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("uuid", m);
            ok = t.post ("/groups/" + uuid + "/measurements", juce::var (o)).ok() && ok;
        }
        return ok;
    }

    //==========================================================================
    // End of run (also after abort/error): Save all, restore settings, blocking
    void finish()
    {
        if (transport_ == nullptr)
            return;
        auto& t = *transport_;
        setState (RunState::Finishing);

        int measured = 0;
        {
            std::lock_guard<std::mutex> l (m_);
            for (const auto& r : progress_.results) measured += r.uuids.size();
        }
        if (seq_.saveMdatPath.isNotEmpty() && measured > 0)
        {
            auto path = seq_.saveMdatPath.replaceCharacter ('\\', '/');
            if (! path.endsWithIgnoreCase (".mdat"))
                path << ".mdat";
            auto* o = new juce::DynamicObject();
            o->setProperty ("command", "Save all");
            o->setProperty ("parameters", juce::Array<juce::var> { path, "SPLMeter REW sequence \"" + seq_.name + "\"" });
            const auto r = t.post ("/measurements/command", juce::var (o));
            std::lock_guard<std::mutex> l (m_);
            if (r.ok()) progress_.savedMdat = path;
            else        progress_.message << (progress_.message.isEmpty() ? "" : "\n") << "Save all failed (" << describe (r) << ").";
        }

        // Restore what the run changed, with the full objects read before it, in
        // the order of settingPaths() (last input before input, then last again)
        for (const auto& path : settingPaths())
            if (touched_.count (path) && snapshot_.count (path))
                t.request ("POST", path, snapshot_[path]);
        if (touched_.count (endpoints_.lastInput) && snapshot_.count (endpoints_.lastInput))
            t.request ("POST", endpoints_.lastInput, snapshot_[endpoints_.lastInput]);
        if (restoreBlocking_)
            t.post ("/application/blocking", true);
        restoreBlocking_ = false;
    }

    //==========================================================================
    bool sleep (double ms)   // false when aborted
    {
        const double end = juce::Time::getMillisecondCounterHiRes() + ms;
        while (! abort_)
        {
            const double left = end - juce::Time::getMillisecondCounterHiRes();
            if (left <= 0.0)
                return true;
            wake_.wait ((int) std::ceil (std::min (left, 100.0)));
        }
        return false;
    }

    bool waitWhilePaused()
    {
        if (! pause_) return true;
        setState (RunState::Paused);
        while (pause_ && ! abort_)
            wake_.wait (100);
        return ! abort_;
    }

    static juce::String describe (const Response& r)
    {
        if (! r.connected()) return "no answer";
        auto msg = r.json().getProperty ("message", {}).toString();
        return "HTTP " + juce::String (r.status) + (msg.isNotEmpty() ? ": " + msg : juce::String());
    }

    bool fail (const juce::String& message)
    {
        setMessage (message);
        return false;
    }

    void setState (RunState s)
    {
        std::lock_guard<std::mutex> l (m_);
        progress_.state = s;
        if (s != RunState::Measuring)
            stepStartMs_ = 0.0;
    }

    void setMessage (const juce::String& message)
    {
        std::lock_guard<std::mutex> l (m_);
        progress_.message = message;
    }

    void publishResult (const StepResult& r, bool countSweep)
    {
        std::lock_guard<std::mutex> l (m_);
        progress_.results.push_back (r);
        if (countSweep) ++progress_.sweepsDone;
    }

    //==========================================================================
    Sequence seq_;
    std::unique_ptr<Transport> transport_;
    RunOptions options_;
    Hooks hooks_;
    Choices choices_;
    ChannelEndpoints endpoints_;

    std::map<juce::String, juce::String> snapshot_;   // path → body before the run
    std::set<juce::String> touched_;
    std::map<juce::String, juce::String> groupUuids_;
    bool restoreBlocking_ = false;

    std::atomic<bool> abort_ { false }, pause_ { false };
    std::atomic<int> userReply_ { 0 };
    juce::WaitableEvent wake_;

    mutable std::mutex m_;
    Progress progress_;
    double stepStartMs_ = 0.0;
};
} // namespace rew
