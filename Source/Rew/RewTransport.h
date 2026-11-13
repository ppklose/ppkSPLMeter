#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

namespace rew
{
//==============================================================================
// Transport to the REW REST API (REW sequencer spec §5.1). The sequencer talks
// to REW only through this interface, so the tests can run it against a
// simulated REW (Tests/FakeRewTransport.h).
struct Response
{
    int status = 0;          // HTTP status; 0 = REW not reachable
    juce::String body;

    bool connected() const noexcept { return status > 0; }
    bool ok() const noexcept        { return status >= 200 && status < 300; }
    bool busy() const noexcept      { return status == 400; }   // a command is still running
    juce::var json() const          { return juce::JSON::fromString (body); }   // any value, also a bare string
};

class Transport
{
public:
    virtual ~Transport() = default;

    // method: GET / POST / PUT / DELETE; path starts with '/'; body is JSON text
    virtual Response request (const juce::String& method, const juce::String& path,
                              const juce::String& body = {}) = 0;

    Response get (const juce::String& path)                       { return request ("GET", path); }
    Response post (const juce::String& path, const juce::var& v)  { return request ("POST", path, toJson (v)); }
    Response put (const juce::String& path, const juce::var& v)   { return request ("PUT", path, toJson (v)); }
    Response del (const juce::String& path)                       { return request ("DELETE", path); }

    // Compact JSON; strings are quoted and escaped ("Acoustic" → "\"Acoustic\"")
    static juce::String toJson (const juce::var& v) { return juce::JSON::toString (v, true); }
};

//==============================================================================
// HTTP to REW on this machine. REW's API only listens on the local machine,
// so the host is fixed. Blocking: call from a background thread only.
class HttpTransport : public Transport
{
public:
    static constexpr int kDefaultPort = 4735;

    explicit HttpTransport (int port = kDefaultPort, int timeoutMs = 3000)
        : port_ (port), timeoutMs_ (timeoutMs) {}

    int port() const noexcept { return port_; }

    Response request (const juce::String& method, const juce::String& path,
                      const juce::String& body = {}) override
    {
        auto url = juce::URL ("http://127.0.0.1:" + juce::String (port_) + path);
        const bool hasBody = method != "GET" && method != "DELETE";
        if (hasBody)
            url = url.withPOSTData (body);

        // WebInputStream directly: URL::createInputStream drops the body and
        // status of 4xx replies, and REW reports "busy" as 400
        juce::WebInputStream in (url, hasBody);
        in.withCustomRequestCommand (method)
          .withConnectionTimeout (timeoutMs_)
          .withNumRedirectsToFollow (0);
        if (hasBody)
            in.withExtraHeaders ("Content-Type: application/json");

        Response r;
        if (! in.connect (nullptr))
        {
            r.status = in.getStatusCode();   // 0 when nothing answered
            return r;
        }
        r.status = in.getStatusCode();
        r.body = in.readEntireStreamAsString();
        return r;
    }

private:
    int port_, timeoutMs_;
};

//==============================================================================
// Small read helpers shared by the sequencer and the window

// "5.40 Beta 147 API 0.9.9" → { "5.40 Beta 147", "0.9.9" }
struct Version { juce::String rew, api; bool valid() const { return api.isNotEmpty(); } };

inline Version parseVersion (const Response& r)
{
    Version v;
    if (! r.ok()) return v;
    const auto msg = r.json().getProperty ("message", {}).toString();
    const int i = msg.indexOf (" API ");
    if (i < 0) return v;
    v.rew = msg.substring (0, i).trim();
    v.api = msg.substring (i + 5).trim();
    return v;
}

// GET /measurements returns an object keyed by index ({"1":{…,"uuid":…}}),
// {} when empty. Returns the UUIDs in index order.
inline juce::StringArray measurementUuids (const juce::var& list)
{
    std::vector<std::pair<int, juce::String>> items;
    if (auto* obj = list.getDynamicObject())
        for (const auto& p : obj->getProperties())
            items.emplace_back (p.name.toString().getIntValue(),
                                p.value.getProperty ("uuid", {}).toString());
    else if (auto* arr = list.getArray())   // tolerate an array, should REW change it
        for (int i = 0; i < arr->size(); ++i)
            items.emplace_back (i + 1, (*arr)[i].getProperty ("uuid", {}).toString());

    std::sort (items.begin(), items.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    juce::StringArray out;
    for (auto& [index, uuid] : items)
        if (uuid.isNotEmpty())
            out.add (uuid);
    return out;
}

// A JSON array of strings ("…/choices", "/audio/java/outputs") → StringArray
inline juce::StringArray stringList (const juce::var& v)
{
    juce::StringArray out;
    if (auto* arr = v.getArray())
        for (const auto& s : *arr)
            out.add (s.toString());
    return out;
}

// REW's audio driver decides which channel endpoints apply (spec §2, Q5)
enum class Driver { Unknown, Java, Asio };

inline Driver parseDriver (const Response& r)
{
    if (! r.ok()) return Driver::Unknown;
    const auto d = r.json().getProperty ("driver", {}).toString();
    if (d.equalsIgnoreCase ("Java")) return Driver::Java;
    if (d.equalsIgnoreCase ("ASIO")) return Driver::Asio;
    return Driver::Unknown;
}

struct ChannelEndpoints
{
    juce::String output, outputs;       // {"output"|"channel": "L"}, list of choices
    juce::String outputKey;             // property name in the output body
    juce::String input, lastInput;      // {"channel": n} (Java, 1-based) / {"input": "name"} (ASIO)
    juce::String inputKey;
    bool inputIsNumber = true;
};

inline ChannelEndpoints channelEndpoints (Driver d)
{
    if (d == Driver::Asio)
        return { "/audio/asio/output", "/audio/asio/outputs", "output",
                 "/audio/asio/input", "/audio/asio/last-input", "input", false };
    return { "/audio/java/output-channel", "/audio/java/output-channels", "channel",
             "/audio/java/input-channel", "/audio/java/last-input-channel", "channel", true };
}
} // namespace rew
