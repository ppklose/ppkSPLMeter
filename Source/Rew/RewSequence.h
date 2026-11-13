#pragma once
#include "RewTransport.h"
#include <cmath>
#include <optional>

namespace rew
{
//==============================================================================
// REW sequencer spec §3: a sequence of REW sweep measurements. Each per-step
// REW setting is optional: an unset field is not sent, so REW keeps the value
// the user set by hand ("—" in the table).

struct Level
{
    double value = -12.0;
    juce::String unit = "dBFS";       // dBFS | dBu | dBV | V
    bool operator== (const Level& o) const { return value == o.value && unit == o.unit; }
};

struct Sweep
{
    int startHz = 20, endHz = 20000;
    juce::String length = "512k";     // 64k … 4M
    bool operator== (const Sweep& o) const { return startHz == o.startHz && endHz == o.endHz && length == o.length; }
};

struct Protection                     // /measure/protection-options (spec Q1)
{
    bool clippingAbort = true;
    bool splLimitAbort = false;
    double dBSPLLimit = 100.0;
    bool operator== (const Protection& o) const
    {
        return clippingAbort == o.clippingAbort && splLimitAbort == o.splLimitAbort && dBSPLLimit == o.dBSPLLimit;
    }
};

// A second output driven with the first (REW: ASIO only), optionally inverted.
// With the Java driver two outputs come from the output choice "L+R"; only
// `invert` applies there.
struct SecondOutput
{
    bool enabled = false;
    juce::String output;              // ASIO output name
    bool invert = false;              // /measure/invert-second-output
    bool operator== (const SecondOutput& o) const
    {
        return enabled == o.enabled && output == o.output && invert == o.invert;
    }
};

struct Step
{
    bool enabled = true;
    juce::String name;                // REW title; "" = sequence name + step number
    juce::String notes;

    std::optional<juce::String> output;      // "L", "R", "L+R", "3" (Java) or an ASIO output name
    std::optional<SecondOutput> secondOutput;
    std::optional<juce::String> input;       // Java: "1", "2" … ; ASIO: input name
    std::optional<juce::String> lastInput;   // multi-input (REW Pro); unset = input only
    std::optional<Level> level;
    std::optional<Sweep> sweep;
    std::optional<int> repetitions;          // 1–16, averaged by REW
    std::optional<juce::String> timingRef;   // None | Loopback | Loopback cal | Acoustic | Wired
    std::optional<Protection> protection;

    juce::String group;               // REW group; "" = none, created if missing
    bool waitForUser = false;
    juce::String prompt;              // shown when waitForUser; "" = default text
    double preDelaySec = 0.0;

    bool operator== (const Step& o) const
    {
        return enabled == o.enabled && name == o.name && notes == o.notes && output == o.output
            && secondOutput == o.secondOutput
            && input == o.input && lastInput == o.lastInput && level == o.level && sweep == o.sweep
            && repetitions == o.repetitions && timingRef == o.timingRef && protection == o.protection
            && group == o.group && waitForUser == o.waitForUser && prompt == o.prompt
            && preDelaySec == o.preDelaySec;
    }
};

enum class OnError { Stop, Skip, RetryOnce };

struct Sequence
{
    juce::String name = "Sequence";
    int port = HttpTransport::kDefaultPort;
    std::vector<Step> steps;
    int loops = 1;
    double betweenStepsSec = 1.0;
    OnError onError = OnError::Stop;
    bool restoreRewSettings = true;
    juce::String saveMdatPath;        // "" = no Save all at the end
    juce::String resultsFolder;       // "" = default folder (Documents/SPLMeter/REW runs)
    bool addLogMarkers = true;

    bool operator== (const Sequence& o) const
    {
        return name == o.name && port == o.port && steps == o.steps && loops == o.loops
            && betweenStepsSec == o.betweenStepsSec && onError == o.onError
            && restoreRewSettings == o.restoreRewSettings && saveMdatPath == o.saveMdatPath
            && resultsFolder == o.resultsFolder && addLogMarkers == o.addLogMarkers;
    }
};

//==============================================================================
// Limits (shared by validation and the editor)
namespace limits
{
    constexpr int kMaxRepetitions = 16, kMaxLoops = 100, kMaxSteps = 500;
    constexpr double kMaxDelaySec = 600.0;
    constexpr int kMinHz = 1, kMaxHz = 96000;
}

inline const char* onErrorKey (OnError e)
{
    switch (e)
    {
        case OnError::Skip:      return "skip";
        case OnError::RetryOnce: return "retry";
        default:                 return "stop";
    }
}

inline OnError onErrorFromKey (const juce::String& k)
{
    if (k == "skip")  return OnError::Skip;
    if (k == "retry") return OnError::RetryOnce;
    return OnError::Stop;
}

// REW title of a step (index 0-based)
inline juce::String stepTitle (const Sequence& seq, int index)
{
    const auto& s = seq.steps[(size_t) index];
    return s.name.isNotEmpty() ? s.name : seq.name + " " + juce::String (index + 1);
}

inline juce::String stepPrompt (const Sequence& seq, int index)
{
    const auto& s = seq.steps[(size_t) index];
    return s.prompt.isNotEmpty() ? s.prompt
                                 : "Ready for \"" + stepTitle (seq, index) + "\"? Press Continue.";
}

//==============================================================================
// JSON (spec §3: *.rewseq.json and UIState/RewSequencer). Unset step fields
// are left out of the file; a missing field reads back as unset.
constexpr const char* kFormatId = "splmeter-rew-sequence";
constexpr int kFormatVersion = 1;

namespace detail
{
    inline juce::DynamicObject* obj (juce::var& v)
    {
        if (v.getDynamicObject() == nullptr) v = juce::var (new juce::DynamicObject());
        return v.getDynamicObject();
    }
    inline bool has (const juce::var& v, const char* key)
    {
        auto* o = v.getDynamicObject();
        return o != nullptr && o->hasProperty (key);
    }
    inline double num (const juce::var& v, const char* key, double fallback)
    {
        const auto x = v.getProperty (key, fallback);
        return x.isDouble() || x.isInt() || x.isInt64() ? (double) x : fallback;
    }
    inline bool flag (const juce::var& v, const char* key, bool fallback)
    {
        const auto x = v.getProperty (key, fallback);
        return x.isBool() || x.isInt() ? (bool) x : fallback;
    }
    inline juce::String str (const juce::var& v, const char* key, const juce::String& fallback = {})
    {
        const auto x = v.getProperty (key, fallback);
        return x.isString() ? x.toString() : fallback;
    }
    inline std::optional<juce::String> optStr (const juce::var& v, const char* key)
    {
        if (! has (v, key)) return {};
        return v.getProperty (key, {}).toString();   // numbers too (Java input written as 2)
    }
}

inline juce::var toVar (const Step& s)
{
    juce::var v;
    auto* o = detail::obj (v);
    o->setProperty ("enabled", s.enabled);
    o->setProperty ("name", s.name);
    if (s.notes.isNotEmpty())   o->setProperty ("notes", s.notes);
    if (s.output)    o->setProperty ("output", *s.output);
    if (s.secondOutput)
    {
        juce::var so;
        detail::obj (so)->setProperty ("enabled", s.secondOutput->enabled);
        so.getDynamicObject()->setProperty ("output", s.secondOutput->output);
        so.getDynamicObject()->setProperty ("invert", s.secondOutput->invert);
        o->setProperty ("secondOutput", so);
    }
    if (s.input)     o->setProperty ("input", *s.input);
    if (s.lastInput) o->setProperty ("lastInput", *s.lastInput);
    if (s.level)
    {
        juce::var l;
        detail::obj (l)->setProperty ("value", s.level->value);
        l.getDynamicObject()->setProperty ("unit", s.level->unit);
        o->setProperty ("level", l);
    }
    if (s.sweep)
    {
        juce::var w;
        detail::obj (w)->setProperty ("startHz", s.sweep->startHz);
        w.getDynamicObject()->setProperty ("endHz", s.sweep->endHz);
        w.getDynamicObject()->setProperty ("length", s.sweep->length);
        o->setProperty ("sweep", w);
    }
    if (s.repetitions) o->setProperty ("repetitions", *s.repetitions);
    if (s.timingRef)   o->setProperty ("timingRef", *s.timingRef);
    if (s.protection)
    {
        juce::var p;
        detail::obj (p)->setProperty ("clippingAbort", s.protection->clippingAbort);
        p.getDynamicObject()->setProperty ("splLimitAbort", s.protection->splLimitAbort);
        p.getDynamicObject()->setProperty ("dBSPLLimit", s.protection->dBSPLLimit);
        o->setProperty ("protection", p);
    }
    if (s.group.isNotEmpty()) o->setProperty ("group", s.group);
    if (s.waitForUser)        o->setProperty ("waitForUser", true);
    if (s.prompt.isNotEmpty())o->setProperty ("prompt", s.prompt);
    if (s.preDelaySec > 0.0)  o->setProperty ("preDelaySec", s.preDelaySec);
    return v;
}

inline Step stepFromVar (const juce::var& v)
{
    using namespace detail;
    Step s;
    s.enabled   = flag (v, "enabled", true);
    s.name      = str (v, "name");
    s.notes     = str (v, "notes");
    s.output    = optStr (v, "output");
    if (has (v, "secondOutput"))
    {
        const auto so = v.getProperty ("secondOutput", {});
        s.secondOutput = SecondOutput { flag (so, "enabled", false), str (so, "output"), flag (so, "invert", false) };
    }
    s.input     = optStr (v, "input");
    s.lastInput = optStr (v, "lastInput");
    if (has (v, "level"))
    {
        const auto l = v.getProperty ("level", {});
        Level lv;
        s.level = Level { num (l, "value", lv.value), str (l, "unit", lv.unit) };
    }
    if (has (v, "sweep"))
    {
        const auto w = v.getProperty ("sweep", {});
        Sweep sw;
        s.sweep = Sweep { (int) num (w, "startHz", sw.startHz), (int) num (w, "endHz", sw.endHz),
                          str (w, "length", sw.length) };
    }
    if (has (v, "repetitions")) s.repetitions = (int) num (v, "repetitions", 1);
    s.timingRef = optStr (v, "timingRef");
    if (has (v, "protection"))
    {
        const auto p = v.getProperty ("protection", {});
        Protection pr;
        s.protection = Protection { flag (p, "clippingAbort", pr.clippingAbort),
                                    flag (p, "splLimitAbort", pr.splLimitAbort),
                                    num (p, "dBSPLLimit", pr.dBSPLLimit) };
    }
    s.group       = str (v, "group");
    s.waitForUser = flag (v, "waitForUser", false);
    s.prompt      = str (v, "prompt");
    s.preDelaySec = num (v, "preDelaySec", 0.0);
    return s;
}

inline juce::var toVar (const Sequence& q)
{
    juce::var v;
    auto* o = detail::obj (v);
    o->setProperty ("format", kFormatId);
    o->setProperty ("version", kFormatVersion);
    o->setProperty ("name", q.name);
    o->setProperty ("port", q.port);
    o->setProperty ("loops", q.loops);
    o->setProperty ("betweenStepsSec", q.betweenStepsSec);
    o->setProperty ("onError", onErrorKey (q.onError));
    o->setProperty ("restoreRewSettings", q.restoreRewSettings);
    o->setProperty ("saveMdatPath", q.saveMdatPath);
    o->setProperty ("resultsFolder", q.resultsFolder);
    o->setProperty ("addLogMarkers", q.addLogMarkers);
    juce::Array<juce::var> steps;
    for (const auto& s : q.steps)
        steps.add (toVar (s));
    o->setProperty ("steps", steps);
    return v;
}

// Values are clamped to the limits; validate() reports the rest.
// Returns false (and leaves `out` untouched) if v is not a sequence.
inline bool sequenceFromVar (const juce::var& v, Sequence& out, juce::String* error = nullptr)
{
    using namespace detail;
    auto fail = [error] (const juce::String& e) { if (error) *error = e; return false; };
    if (v.getDynamicObject() == nullptr)
        return fail ("Not a JSON object.");
    if (str (v, "format") != kFormatId)
        return fail ("Not an SPLMeter REW sequence file.");
    if ((int) num (v, "version", 0) > kFormatVersion)
        return fail ("The file was written by a newer SPLMeter (format version "
                     + juce::String ((int) num (v, "version", 0)) + ").");

    Sequence q;
    q.name               = str (v, "name", q.name);
    q.port               = juce::jlimit (1024, 65535, (int) num (v, "port", q.port));
    q.loops              = juce::jlimit (1, limits::kMaxLoops, (int) num (v, "loops", q.loops));
    q.betweenStepsSec    = juce::jlimit (0.0, limits::kMaxDelaySec, num (v, "betweenStepsSec", q.betweenStepsSec));
    q.onError            = onErrorFromKey (str (v, "onError"));
    q.restoreRewSettings = flag (v, "restoreRewSettings", q.restoreRewSettings);
    q.saveMdatPath       = str (v, "saveMdatPath");
    q.resultsFolder      = str (v, "resultsFolder");
    q.addLogMarkers      = flag (v, "addLogMarkers", q.addLogMarkers);
    if (auto* arr = v.getProperty ("steps", {}).getArray())
        for (const auto& s : *arr)
            if ((int) q.steps.size() < limits::kMaxSteps)
                q.steps.push_back (stepFromVar (s));
    out = std::move (q);
    return true;
}

inline juce::String toJsonText (const Sequence& q)   { return juce::JSON::toString (toVar (q)); }

inline bool saveToFile (const Sequence& q, const juce::File& f)
{
    return f.replaceWithText (toJsonText (q));
}

inline bool loadFromFile (const juce::File& f, Sequence& out, juce::String& error)
{
    if (! f.existsAsFile())
    {
        error = "File not found.";
        return false;
    }
    juce::var v;
    const auto result = juce::JSON::parse (f.loadFileAsString(), v);
    if (result.failed())
    {
        error = "Invalid JSON: " + result.getErrorMessage();
        return false;
    }
    return sequenceFromVar (v, out, &error);
}

//==============================================================================
// Choices REW offers (spec §4). Built-in defaults until REW has been asked.
struct Choices
{
    Driver driver = Driver::Unknown;
    double sampleRate = 0.0;          // 0 = unknown
    juce::StringArray outputs, inputs;   // inputs: ASIO names only (Java inputs are numbers)
    juce::StringArray sweepLengths { "64k", "128k", "256k", "512k", "1M", "2M", "4M" };
    juce::StringArray timingRefs { "None", "Loopback", "Loopback cal", "Acoustic", "Wired" };
    juce::StringArray levelUnits { "dBFS", "dBu", "dBV", "V" };
    bool fromRew = false;             // true once read from a running REW
};

inline Choices offlineChoices()
{
    Choices c;
    c.outputs = { "L", "R", "L+R", "3", "4", "5", "6", "7", "8" };
    return c;
}

// Reads the choice lists from REW. Background thread only (HTTP).
inline Choices queryChoices (Transport& t)
{
    Choices c;
    c.driver = parseDriver (t.get ("/audio/driver"));
    if (c.driver == Driver::Unknown)
        return offlineChoices();
    const auto ep = channelEndpoints (c.driver);
    c.outputs = stringList (t.get (ep.outputs).json());
    if (c.driver == Driver::Asio)
        c.inputs = stringList (t.get ("/audio/asio/inputs").json());
    if (auto l = stringList (t.get ("/measure/sweep/configuration/sweep-lengths").json()); ! l.isEmpty())
        c.sweepLengths = l;
    if (auto l = stringList (t.get ("/measure/timing/reference/choices").json()); ! l.isEmpty())
        c.timingRefs = l;
    if (auto l = stringList (t.get ("/measure/level/units").json()); ! l.isEmpty())
        c.levelUnits = l;
    c.sampleRate = (double) t.get ("/audio/samplerate").json().getProperty ("value", 0.0);
    c.fromRew = true;
    return c;
}

//==============================================================================
// Validation (spec §5.2 preflight). Errors block the run; warnings do not.
struct Issue
{
    int step = -1;                    // 0-based; -1 = the sequence
    juce::String field, message;
    bool error = true;
};

inline std::vector<Issue> validate (const Sequence& q, const Choices& c)
{
    std::vector<Issue> out;
    auto err  = [&] (int i, const char* f, const juce::String& m) { out.push_back ({ i, f, m, true }); };
    auto warn = [&] (int i, const char* f, const juce::String& m) { out.push_back ({ i, f, m, false }); };

    int enabled = 0;
    for (const auto& s : q.steps) enabled += s.enabled ? 1 : 0;
    if (enabled == 0)
        err (-1, "steps", "No enabled steps.");
    if (q.loops < 1 || q.loops > limits::kMaxLoops)
        err (-1, "loops", "Loops must be 1–" + juce::String (limits::kMaxLoops) + ".");
    if (q.betweenStepsSec < 0.0 || q.betweenStepsSec > limits::kMaxDelaySec)
        err (-1, "betweenStepsSec", "Gap between steps must be 0–600 s.");

    // Only check against REW's lists once they came from REW: offline lists
    // are guesses, and the sequence must stay editable without REW.
    const bool strict = c.fromRew;
    const bool java = c.driver != Driver::Asio;
    juce::StringArray titles;

    for (int i = 0; i < (int) q.steps.size(); ++i)
    {
        const auto& s = q.steps[(size_t) i];
        if (! s.enabled)
            continue;

        if (s.output && s.output->isEmpty())
            err (i, "output", "Output is empty.");
        else if (s.output && strict && ! c.outputs.contains (*s.output))
            err (i, "output", "REW has no output \"" + *s.output + "\".");

        if (s.secondOutput && s.secondOutput->enabled)
        {
            if (strict && java)
                err (i, "secondOutput", "REW's second output needs the ASIO driver; with Java use the output L+R.");
            else if (s.secondOutput->output.isEmpty())
                err (i, "secondOutput", "Second output is empty.");
            else if (strict && ! c.outputs.contains (s.secondOutput->output))
                err (i, "secondOutput", "REW has no output \"" + s.secondOutput->output + "\".");
            else if (s.output && *s.output == s.secondOutput->output)
                err (i, "secondOutput", "Second output is the same as the output.");
        }

        if (s.lastInput && ! s.input)
            err (i, "lastInput", "Last input needs a first input.");
        if (s.input)
        {
            if (java)
            {
                const int first = s.input->getIntValue();
                if (! s.input->containsOnly ("0123456789") || first < 1)
                    err (i, "input", "Input must be a channel number (1, 2, …).");
                else if (s.lastInput && s.lastInput->getIntValue() < first)
                    err (i, "lastInput", "Last input is below the first input.");
            }
            else if (strict)
            {
                if (! c.inputs.contains (*s.input))
                    err (i, "input", "REW has no input \"" + *s.input + "\".");
                else if (s.lastInput && c.inputs.indexOf (*s.lastInput) < c.inputs.indexOf (*s.input))
                    err (i, "lastInput", "Last input must come after the first input.");
            }
        }

        if (s.level)
        {
            if (! c.levelUnits.contains (s.level->unit))
                err (i, "level", "Unknown level unit \"" + s.level->unit + "\".");
            else if (s.level->unit == "dBFS" && s.level->value > 0.0)
                err (i, "level", "Level above 0 dBFS.");
            else if (s.level->unit == "V" && s.level->value <= 0.0)
                err (i, "level", "Level in V must be above 0.");
        }

        if (s.sweep)
        {
            const auto& w = *s.sweep;
            if (w.startHz < limits::kMinHz || w.endHz > limits::kMaxHz || w.startHz >= w.endHz)
                err (i, "sweep", "Sweep range must be rising, " + juce::String (limits::kMinHz)
                                     + "–" + juce::String (limits::kMaxHz) + " Hz.");
            else if (c.sampleRate > 0.0 && w.endHz > c.sampleRate / 2.0)
                err (i, "sweep", "Sweep end above half the sample rate ("
                                     + juce::String (c.sampleRate / 2000.0, 1) + " kHz).");
            if (! c.sweepLengths.contains (w.length))
                err (i, "sweep", "Unknown sweep length \"" + w.length + "\".");
        }

        if (s.repetitions && (*s.repetitions < 1 || *s.repetitions > limits::kMaxRepetitions))
            err (i, "repetitions", "Repetitions must be 1–" + juce::String (limits::kMaxRepetitions) + ".");
        if (s.timingRef && ! c.timingRefs.contains (*s.timingRef))
            err (i, "timingRef", "Unknown timing reference \"" + *s.timingRef + "\".");
        if (s.protection && s.protection->splLimitAbort
            && (s.protection->dBSPLLimit < 40.0 || s.protection->dBSPLLimit > 160.0))
            err (i, "protection", "SPL limit must be 40–160 dB.");
        if (s.preDelaySec < 0.0 || s.preDelaySec > limits::kMaxDelaySec)
            err (i, "preDelaySec", "Delay must be 0–600 s.");

        const auto title = stepTitle (q, i);
        if (titles.contains (title))
            warn (i, "name", "Another step is also named \"" + title + "\".");
        titles.add (title);
    }
    return out;
}

inline bool hasErrors (const std::vector<Issue>& issues)
{
    return std::any_of (issues.begin(), issues.end(), [] (const Issue& i) { return i.error; });
}

//==============================================================================
// Editing helpers

// "Add positions…" (spec §3): the step at `index` becomes position 1 and n − 1
// copies follow it, each waiting for the user ("Move the microphone to position k").
inline void addPositions (Sequence& q, int index, int n)
{
    if (index < 0 || index >= (int) q.steps.size() || n < 1)
        return;
    n = std::min (n, limits::kMaxSteps - (int) q.steps.size() + 1);
    const Step base = q.steps[(size_t) index];
    const auto prefix = base.name.isNotEmpty() ? base.name + " " : juce::String();

    std::vector<Step> positions;
    for (int k = 1; k <= n; ++k)
    {
        Step s = base;
        s.name = prefix + "Pos " + juce::String (k);
        s.waitForUser = true;
        s.prompt = "Move the microphone to position " + juce::String (k) + ".";
        positions.push_back (std::move (s));
    }
    q.steps.erase (q.steps.begin() + index);
    q.steps.insert (q.steps.begin() + index, positions.begin(), positions.end());
}

inline void moveStep (Sequence& q, int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || to < 0 || index >= (int) q.steps.size() || to >= (int) q.steps.size())
        return;
    std::swap (q.steps[(size_t) index], q.steps[(size_t) to]);
}

// Number of sweeps a full run makes
inline int totalSweeps (const Sequence& q)
{
    int n = 0;
    for (const auto& s : q.steps) n += s.enabled ? 1 : 0;
    return n * q.loops;
}
} // namespace rew
