#include <catch2/catch_test_macros.hpp>
#include "Rew/RewTransport.h"
#include "FakeRewTransport.h"

// REW sequencer spec §5.1 / step 1: transport, read helpers, simulated REW

namespace
{
rew::Response reply (const juce::String& body) { return { 200, body }; }
}

TEST_CASE ("REW transport: JSON bodies are quoted and escaped", "[rew]")
{
    CHECK (rew::Transport::toJson ("Acoustic") == "\"Acoustic\"");
    CHECK (rew::Transport::toJson (false) == "false");
    CHECK (rew::Transport::toJson (juce::String ("say \"hi\"")) == "\"say \\\"hi\\\"\"");

    auto* o = new juce::DynamicObject();
    o->setProperty ("channel", "L+R");
    const auto json = rew::Transport::toJson (juce::var (o));
    CHECK (juce::JSON::parse (json).getProperty ("channel", {}).toString() == "L+R");
}

TEST_CASE ("REW transport: version, driver and measurement list parsing", "[rew]")
{
    const auto v = rew::parseVersion (reply ("{\"message\":\"5.40 Beta 147 API 0.9.9\"}"));
    CHECK (v.valid());
    CHECK (v.rew == "5.40 Beta 147");
    CHECK (v.api == "0.9.9");
    CHECK_FALSE (rew::parseVersion ({}).valid());
    CHECK_FALSE (rew::parseVersion (reply ("{\"message\":\"hello\"}")).valid());

    CHECK (rew::parseDriver (reply ("{\"driver\":\"Java\"}")) == rew::Driver::Java);
    CHECK (rew::parseDriver (reply ("{\"driver\":\"ASIO\"}")) == rew::Driver::Asio);
    CHECK (rew::parseDriver ({}) == rew::Driver::Unknown);

    // Object keyed by index; numeric order, not text order ("10" after "2")
    const auto list = juce::JSON::parse (
        "{\"10\":{\"uuid\":\"c\"},\"2\":{\"uuid\":\"b\"},\"1\":{\"uuid\":\"a\"}}");
    CHECK (rew::measurementUuids (list) == juce::StringArray ({ "a", "b", "c" }));
    CHECK (rew::measurementUuids (juce::JSON::parse ("{}")).isEmpty());

    CHECK (rew::stringList (juce::JSON::parse ("[\"64k\",\"4M\"]")) == juce::StringArray ({ "64k", "4M" }));

    const auto java = rew::channelEndpoints (rew::Driver::Java);
    CHECK (java.output == "/audio/java/output-channel");
    CHECK (java.inputIsNumber);
    const auto asio = rew::channelEndpoints (rew::Driver::Asio);
    CHECK (asio.lastInput == "/audio/asio/last-input");
    CHECK (asio.inputKey == "input");
    CHECK_FALSE (asio.inputIsNumber);
}

TEST_CASE ("REW transport: unreachable REW reports status 0 quickly", "[rew]")
{
    rew::HttpTransport http (47391, 1000);   // nothing listens here
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto r = http.get ("/version");
    CHECK_FALSE (r.connected());
    CHECK (juce::Time::getMillisecondCounterHiRes() - t0 < 3000.0);
}

TEST_CASE ("Fake REW: settings, sweep lifecycle, errors and groups", "[rew]")
{
    FakeRewTransport rew;

    SECTION ("settings: POST replaces, PUT merges, choices are read-only")
    {
        CHECK (rew.post ("/measure/timing/reference", "Acoustic").ok());
        CHECK (rew.get ("/measure/timing/reference").json().toString() == "Acoustic");

        auto* o = new juce::DynamicObject();
        o->setProperty ("length", "1M");
        CHECK (rew.put ("/measure/sweep/configuration", juce::var (o)).ok());
        const auto sweep = rew.get ("/measure/sweep/configuration").json();
        CHECK (sweep.getProperty ("length", {}).toString() == "1M");
        CHECK ((int) sweep.getProperty ("endFrequency", 0) == 20000);

        CHECK (rew.post ("/audio/java/output-channels", "L").status == 405);
        CHECK (rew.get ("/no/such/endpoint").status == 404);
    }

    SECTION ("sweep: 202, busy while running, then the measurement appears")
    {
        rew.sweepPolls = 2;
        auto* naming = new juce::DynamicObject();
        naming->setProperty ("title", "Pos 1");
        rew.put ("/measure/naming", juce::var (naming));

        auto cmd = [] (const char* c) { auto* o = new juce::DynamicObject(); o->setProperty ("command", c); return juce::var (o); };
        CHECK (rew.post ("/measure/command", cmd ("SPL")).status == 202);
        CHECK (rew.post ("/measure/command", cmd ("SPL")).busy());
        CHECK (rew.post ("/measure/level", juce::var()).busy());   // settings refused while measuring

        CHECK (rew::measurementUuids (rew.get ("/measurements").json()).isEmpty());   // poll 1
        const auto after = rew::measurementUuids (rew.get ("/measurements").json());  // poll 2
        REQUIRE (after.size() == 1);
        CHECK (rew.measurementTitle (1) == "Pos 1");
        CHECK_FALSE (rew.busy());
    }

    SECTION ("multi-input: one measurement per input")
    {
        auto ch = [] (int c) { auto* o = new juce::DynamicObject(); o->setProperty ("channel", c); return juce::var (o); };
        rew.post ("/audio/java/last-input-channel", ch (3));
        rew.post ("/audio/java/input-channel", ch (1));
        rew.sweepPolls = 1;
        rew.post ("/measure/command", juce::JSON::parse ("{\"command\":\"SPL\"}"));
        CHECK (rew::measurementUuids (rew.get ("/measurements").json()).size() == 3);
    }

    SECTION ("scripted error: no measurement, error listed")
    {
        rew.nextError = "Clipping detected";
        rew.sweepPolls = 1;
        rew.post ("/measure/command", juce::JSON::parse ("{\"command\":\"SPL\"}"));
        CHECK (rew::measurementUuids (rew.get ("/measurements").json()).isEmpty());
        const auto errors = rew.get ("/application/errors").json();
        REQUIRE (errors.size() == 1);
        CHECK (errors[0].getProperty ("message", {}).toString() == "Clipping detected");
    }

    SECTION ("hanging sweep ends with Cancel")
    {
        rew.hangNextSweep = true;
        rew.post ("/measure/command", juce::JSON::parse ("{\"command\":\"SPL\"}"));
        for (int i = 0; i < 5; ++i) rew.get ("/measurements");
        CHECK (rew.busy());
        CHECK (rew.post ("/measure/command", juce::JSON::parse ("{\"command\":\"Cancel\"}")).ok());
        CHECK_FALSE (rew.busy());
        CHECK (rew.numMeasurements() == 0);
    }

    SECTION ("groups and Save all")
    {
        rew.sweepPolls = 1;
        rew.post ("/measure/command", juce::JSON::parse ("{\"command\":\"SPL\"}"));
        const auto uuid = rew::measurementUuids (rew.get ("/measurements").json())[0];

        rew.post ("/groups", juce::JSON::parse ("{\"name\":\"Left\"}"));
        const auto groups = rew.get ("/groups").json();
        REQUIRE (groups.size() == 1);
        const auto gUuid = groups[0].getProperty ("uuid", {}).toString();
        CHECK (rew.post ("/groups/" + gUuid + "/measurements",
                         juce::JSON::parse ("{\"uuid\":\"" + uuid + "\"}")).ok());
        CHECK (rew.get ("/measurements").json().getProperty ("1", {}).getProperty ("groupName", {}).toString() == "Left");

        rew.post ("/measurements/command", juce::JSON::parse ("{\"command\":\"Save all\",\"parameters\":[\"/x/run.mdat\"]}"));
        CHECK (rew.savedFiles() == juce::StringArray ({ "/x/run.mdat" }));
    }

    SECTION ("unreachable and call log")
    {
        rew.clearCalls();
        rew.get ("/version");
        rew.reachable = false;
        CHECK_FALSE (rew.get ("/version").connected());
        const auto calls = rew.calls();
        REQUIRE (calls.size() == 2);
        CHECK (calls[0].method == "GET");
        CHECK (calls[0].path == "/version");
    }
}

// Against a running REW (not part of the normal run): ./SPLMeterTests "[.rewlive]"
// Reads only, and writes the measurement notes back unchanged.
TEST_CASE ("REW transport: live REW on port 4735", "[.rewlive]")
{
    rew::HttpTransport http;
    const auto v = rew::parseVersion (http.get ("/version"));
    REQUIRE (v.valid());
    INFO ("REW " << v.rew << ", API " << v.api);

    CHECK (rew::parseDriver (http.get ("/audio/driver")) != rew::Driver::Unknown);
    CHECK (rew::stringList (http.get ("/measure/sweep/configuration/sweep-lengths").json()).contains ("512k"));

    const auto missing = http.get ("/no/such/endpoint");
    CHECK (missing.status == 404);

    const auto notes = http.get ("/measure/notes");
    REQUIRE (notes.ok());
    const auto r = http.post ("/measure/notes", notes.json());
    CHECK (r.ok());
    CHECK (http.get ("/measure/notes").body == notes.body);

    // measurement list parses (object keyed by index)
    const auto list = http.get ("/measurements");
    REQUIRE (list.ok());
    INFO ("measurements: " << rew::measurementUuids (list.json()).size());
}
