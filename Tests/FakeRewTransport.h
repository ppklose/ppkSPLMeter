#pragma once
#include "Rew/RewTransport.h"
#include <map>
#include <mutex>

//==============================================================================
// Simulated REW (REW sequencer spec §7). It answers like REW 5.40 / API 0.9.9:
// settings endpoints store what is POSTed (PUT merges), choice lists are fixed,
// a measure command makes the sweep "run" for a number of GET /measurements
// polls, and then adds the measurement(s), or an error if one is scripted.
// Every request is logged so the tests can check the order of calls.
class FakeRewTransport : public rew::Transport
{
public:
    struct Call { juce::String method, path, body; };

    FakeRewTransport()
    {
        auto obj = [] (std::initializer_list<std::pair<const char*, juce::var>> props)
        {
            auto* o = new juce::DynamicObject();
            for (auto& [k, v] : props) o->setProperty (k, v);
            return juce::var (o);
        };
        auto list = [] (std::initializer_list<const char*> items)
        {
            juce::Array<juce::var> a;
            for (auto* s : items) a.add (juce::String (s));
            return juce::var (a);
        };

        values_ = {
            { "/version",                 obj ({ { "message", "5.40 Beta 147 API 0.9.9" } }) },
            { "/audio/status",            obj ({ { "enabled", true }, { "ready", true } }) },
            { "/audio/driver",            obj ({ { "driver", "Java" } }) },
            { "/audio/samplerate",        obj ({ { "value", 48000.0 }, { "unit", "Hz" } }) },
            { "/application/blocking",    true },
            { "/audio/java/output-channel",     obj ({ { "channel", "L" } }) },
            { "/audio/java/input-channel",      obj ({ { "channel", 1 } }) },
            { "/audio/java/last-input-channel", obj ({ { "channel", 1 } }) },
            { "/audio/asio/output",       obj ({ { "output", "Out 1" } }) },
            { "/audio/asio/input",        obj ({ { "input", "In 1" } }) },
            { "/audio/asio/last-input",   obj ({ { "input", "In 1" } }) },
            { "/measure/level",           obj ({ { "value", -14.0 }, { "unit", "dBFS" } }) },
            { "/measure/sweep/configuration",
              obj ({ { "startFrequency", 20 }, { "endFrequency", 20000 }, { "length", "512k" },
                     { "fillSilenceWithDither", false } }) },
            { "/measure/sweep/repetitions", 1 },
            { "/measure/timing/reference",  "None" },
            { "/measure/start-delay",       2 },
            { "/measure/naming",
              obj ({ { "title", "Meas" }, { "namingOption", "Add number" }, { "nextNumber", 1 },
                     { "numberIncrement", 1 }, { "dateTimeFormat", "H:mm:ss" },
                     { "prefixMeasNameWithOutput", false }, { "appendLevelToMeasName", false } }) },
            { "/measure/notes",             "" },
            { "/measure/protection-options",
              obj ({ { "splLimitAbort", false }, { "clippingAbort", false }, { "dBSPLLimit", 100 },
                     { "warnForLowLevels", false }, { "warnForHighDistortion", false },
                     { "warnForLowSNR", false } }) },
        };
        choices_ = {
            { "/audio/java/output-channels", list ({ "L", "R", "L+R", "3", "4" }) },
            { "/audio/asio/outputs",         list ({ "Out 1", "Out 2", "Out 3" }) },
            { "/audio/asio/inputs",          list ({ "In 1", "In 2", "In 3", "In 4" }) },
            { "/audio/driver-types",         list ({ "Java" }) },
            { "/measure/level/units",        list ({ "dBu", "dBV", "V", "dBFS" }) },
            { "/measure/sweep/configuration/sweep-lengths",
              list ({ "64k", "128k", "256k", "512k", "1M", "2M", "4M" }) },
            { "/measure/timing/reference/choices",
              list ({ "None", "Loopback", "Loopback cal", "Acoustic", "Wired" }) },
            { "/measure/commands",
              list ({ "SPL", "Filtered SPL", "Impedance", "Check levels", "Cancel" }) },
        };
    }

    //==========================================================================
    // Test controls
    bool reachable = true;           // false: every request returns status 0
    int sweepPolls = 2;              // GET /measurements polls until a sweep finishes
    juce::String nextError;          // the next sweep ends with this error, no measurement
    juce::String nextWarning;        // the next sweep adds this warning and the measurement
    bool hangNextSweep = false;      // the next sweep never finishes (until Cancel)
    int busyRejects = 0;             // the next n measure commands get 400 "busy"

    std::vector<Call> calls() const               { std::lock_guard<std::mutex> l (m_); return log_; }
    void clearCalls()                             { std::lock_guard<std::mutex> l (m_); log_.clear(); }
    bool busy() const                             { std::lock_guard<std::mutex> l (m_); return busy_; }
    int numMeasurements() const                   { std::lock_guard<std::mutex> l (m_); return (int) meas_.size(); }
    juce::var value (const juce::String& path) const
    {
        std::lock_guard<std::mutex> l (m_);
        auto it = values_.find (path);
        return it != values_.end() ? it->second : juce::var();
    }
    void setValue (const juce::String& path, const juce::var& v) { std::lock_guard<std::mutex> l (m_); values_[path] = v; }
    juce::String measurementTitle (int index1) const
    {
        std::lock_guard<std::mutex> l (m_);
        return meas_[(size_t) index1 - 1].title;
    }
    juce::StringArray savedFiles() const          { std::lock_guard<std::mutex> l (m_); return saved_; }

    //==========================================================================
    rew::Response request (const juce::String& method, const juce::String& path,
                           const juce::String& body = {}) override
    {
        std::lock_guard<std::mutex> l (m_);
        log_.push_back ({ method, path, body });
        if (! reachable)
            return {};

        const auto in = juce::JSON::fromString (body);

        // Measurement list and the sweep that fills it
        if (path == "/measurements" && method == "GET")
        {
            advanceSweep();
            auto* o = new juce::DynamicObject();
            for (size_t i = 0; i < meas_.size(); ++i)
                o->setProperty (juce::String ((int) i + 1), summary (meas_[i]));
            return ok (juce::var (o));
        }
        if (path.startsWith ("/measurements/") && method == "DELETE")
        {
            const int idx = path.fromLastOccurrenceOf ("/", false, false).getIntValue();
            if (idx < 1 || idx > (int) meas_.size()) return { 404, "{}" };
            meas_.erase (meas_.begin() + idx - 1);
            return ok ("Deleted");
        }
        if (path == "/measure/command" && method == "POST")
        {
            const auto cmd = in.getProperty ("command", {}).toString();
            if (cmd == "Cancel")
            {
                busy_ = false;
                return ok ("Cancelled");
            }
            if (busy_ || busyRejects > 0)
            {
                busyRejects = std::max (0, busyRejects - 1);
                return { 400, "{\"message\":\"Command in progress\"}" };
            }
            if (! choiceContains ("/measure/commands", cmd))
                return { 400, "{\"message\":\"Unknown command\"}" };
            busy_ = true;
            pollsLeft_ = hangNextSweep ? -1 : sweepPolls;
            hangNextSweep = false;
            return { 202, "{\"message\":\"Measurement started\"}" };
        }
        if (path == "/measurements/command" && method == "POST")
        {
            if (in.getProperty ("command", {}).toString() == "Save all")
                if (auto* p = in.getProperty ("parameters", {}).getArray(); p != nullptr && ! p->isEmpty())
                    saved_.add ((*p)[0].toString());
            return ok ("Saved");
        }
        if (path == "/application/errors" && method == "GET")   return ok (messages (errors_));
        if (path == "/application/warnings" && method == "GET") return ok (messages (warnings_));

        // Groups
        if (path == "/groups")
        {
            if (method == "GET")
            {
                juce::Array<juce::var> a;
                for (auto& [uuid, name] : groups_)
                {
                    auto* o = new juce::DynamicObject();
                    o->setProperty ("name", name);
                    o->setProperty ("uuid", uuid);
                    a.add (juce::var (o));
                }
                return ok (juce::var (a));
            }
            if (method == "POST")
            {
                groups_.emplace_back ("group-" + juce::String ((int) groups_.size() + 1),
                                      in.getProperty ("name", {}).toString());
                return ok ("Created");
            }
        }
        if (path.startsWith ("/groups/") && path.endsWith ("/measurements") && method == "POST")
        {
            const auto uuid = path.fromFirstOccurrenceOf ("/groups/", false, false).upToFirstOccurrenceOf ("/", false, false);
            const auto mUuid = in.getProperty ("uuid", {}).toString();
            for (auto& m : meas_)
                if (m.uuid == mUuid)
                    for (auto& [gUuid, name] : groups_)
                        if (gUuid == uuid) { m.group = name; return ok ("Added"); }
            return { 400, "{\"message\":\"Not found\"}" };
        }

        // Choice lists (read-only)
        if (auto it = choices_.find (path); it != choices_.end())
            return method == "GET" ? ok (it->second) : rew::Response { 405, {} };

        // Settings: GET returns, POST replaces, PUT merges
        if (auto it = values_.find (path); it != values_.end())
        {
            if (method == "GET")
                return ok (it->second);
            if (path == "/version" || path == "/audio/status" || path == "/audio/samplerate")
                return { 405, {} };
            if (busy_)
                return { 400, "{\"message\":\"Command in progress\"}" };
            if (method == "POST")
                it->second = in;
            else if (method == "PUT")
            {
                if (auto* dst = it->second.getDynamicObject(); dst != nullptr)
                    if (auto* src = in.getDynamicObject())
                        for (auto& p : src->getProperties())
                            dst->setProperty (p.name, p.value);
            }
            return ok ("Updated");
        }
        return { 404, "{\"message\":\"Not found\"}" };
    }

private:
    struct Meas { juce::String uuid, title, notes, group; };

    static rew::Response ok (const juce::var& v)          { return { 200, juce::JSON::toString (v, true) }; }
    static rew::Response ok (const char* message)         { return { 200, "{\"message\":\"" + juce::String (message) + "\"}" }; }

    juce::var summary (const Meas& m) const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("title", m.title);
        o->setProperty ("notes", m.notes);
        o->setProperty ("uuid", m.uuid);
        o->setProperty ("groupName", m.group);
        return juce::var (o);
    }

    static juce::var messages (const juce::StringArray& list)
    {
        juce::Array<juce::var> a;
        for (auto& s : list)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("time", "12:00:00");
            o->setProperty ("title", "REW");
            o->setProperty ("message", s);
            a.add (juce::var (o));
        }
        return juce::var (a);
    }

    bool choiceContains (const juce::String& path, const juce::String& s) const
    {
        auto it = choices_.find (path);
        if (auto* a = it != choices_.end() ? it->second.getArray() : nullptr)
            return a->contains (s);
        return false;
    }

    void advanceSweep()
    {
        if (! busy_ || pollsLeft_ < 0 || --pollsLeft_ > 0)
            return;
        busy_ = false;
        if (nextError.isNotEmpty())
        {
            errors_.add (nextError);
            nextError.clear();
            return;
        }
        if (nextWarning.isNotEmpty())
        {
            warnings_.add (nextWarning);
            nextWarning.clear();
        }
        // One measurement per input in the range (Java multi-input)
        const int first = (int) values_["/audio/java/input-channel"].getProperty ("channel", 1);
        const int last  = (int) values_["/audio/java/last-input-channel"].getProperty ("channel", 1);
        const auto title = values_["/measure/naming"].getProperty ("title", "").toString();
        for (int ch = first; ch <= std::max (first, last); ++ch)
            meas_.push_back ({ "uuid-" + juce::String (++uuidCounter_),
                               last > first ? title + " in " + juce::String (ch) : title,
                               values_["/measure/notes"].toString(), {} });
    }

    mutable std::mutex m_;
    std::vector<Call> log_;
    std::map<juce::String, juce::var> values_, choices_;
    std::vector<Meas> meas_;
    std::vector<std::pair<juce::String, juce::String>> groups_;   // uuid, name
    juce::StringArray errors_, warnings_, saved_;
    bool busy_ = false;
    int pollsLeft_ = 0, uuidCounter_ = 0;
};
