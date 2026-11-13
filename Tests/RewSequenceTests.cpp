#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Rew/RewSequence.h"
#include "FakeRewTransport.h"

// REW sequencer spec §3 / step 2: sequence model, JSON, validation, editing

using Catch::Approx;

namespace
{
rew::Sequence sampleSequence()
{
    rew::Sequence q;
    q.name = "Room \"A\"";
    q.loops = 2;
    q.betweenStepsSec = 2.5;
    q.onError = rew::OnError::Skip;
    q.saveMdatPath = "/tmp/run.mdat";
    q.addLogMarkers = false;

    rew::Step a;
    a.name = "L";
    a.output = juce::String ("L");
    a.input = juce::String ("1");
    a.lastInput = juce::String ("3");
    a.level = rew::Level { -18.5, "dBFS" };
    a.sweep = rew::Sweep { 10, 24000, "1M" };
    a.repetitions = 2;
    a.timingRef = juce::String ("Acoustic");
    a.protection = rew::Protection { true, true, 105.0 };
    a.group = "Left";
    a.waitForUser = true;
    a.prompt = juce::String::fromUTF8 ("Mic → position 1");
    a.preDelaySec = 1.5;

    rew::Step b;                  // everything inherited
    b.name = "R";
    b.enabled = false;
    q.steps = { a, b };
    return q;
}
}

TEST_CASE ("REW sequence: JSON round trip keeps every field; unset stays unset", "[rew]")
{
    const auto q = sampleSequence();
    const auto text = rew::toJsonText (q);

    rew::Sequence back;
    juce::String error;
    REQUIRE (rew::sequenceFromVar (juce::JSON::parse (text), back, &error));
    CHECK (back == q);
    CHECK_FALSE (back.steps[1].output.has_value());
    CHECK_FALSE (back.steps[1].level.has_value());
    CHECK_FALSE (back.steps[1].protection.has_value());

    // Unset fields are not written at all
    const auto stepB = juce::JSON::parse (text).getProperty ("steps", {})[1];
    CHECK_FALSE (stepB.getDynamicObject()->hasProperty ("output"));
    CHECK_FALSE (stepB.getDynamicObject()->hasProperty ("sweep"));

    // File round trip
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rewseq_test.rewseq.json");
    REQUIRE (rew::saveToFile (q, f));
    rew::Sequence fromFile;
    CHECK (rew::loadFromFile (f, fromFile, error));
    CHECK (fromFile == q);
    f.deleteFile();
}

TEST_CASE ("REW sequence: reading rejects foreign files and clamps values", "[rew]")
{
    rew::Sequence q;
    juce::String error;
    CHECK_FALSE (rew::sequenceFromVar (juce::JSON::parse ("[1,2]"), q, &error));
    CHECK_FALSE (rew::sequenceFromVar (juce::JSON::parse ("{\"format\":\"other\"}"), q, &error));
    CHECK (error.contains ("REW sequence"));
    CHECK_FALSE (rew::sequenceFromVar (juce::JSON::parse ("{\"format\":\"splmeter-rew-sequence\",\"version\":99}"), q, &error));
    CHECK (error.contains ("newer"));

    const auto v = juce::JSON::parse (R"({"format":"splmeter-rew-sequence","version":1,
        "loops":1000,"port":80,"betweenStepsSec":-5,"onError":"what",
        "steps":[{"input":2,"name":"x"}]})");
    REQUIRE (rew::sequenceFromVar (v, q, &error));
    CHECK (q.loops == rew::limits::kMaxLoops);
    CHECK (q.port == 1024);
    CHECK (q.betweenStepsSec == Approx (0.0));
    CHECK (q.onError == rew::OnError::Stop);
    REQUIRE (q.steps.size() == 1);
    CHECK (q.steps[0].input == juce::String ("2"));   // a number reads as text

    juce::File missing ("/no/such/file.rewseq.json");
    CHECK_FALSE (rew::loadFromFile (missing, q, error));
}

TEST_CASE ("REW sequence: validation", "[rew]")
{
    auto q = sampleSequence();
    auto c = rew::offlineChoices();

    auto fieldsOf = [] (const std::vector<rew::Issue>& issues)
    {
        juce::StringArray f;
        for (auto& i : issues) if (i.error) f.add (i.field);
        return f;
    };

    SECTION ("the sample is valid offline")
    {
        CHECK_FALSE (rew::hasErrors (rew::validate (q, c)));
    }

    SECTION ("no enabled step")
    {
        q.steps[0].enabled = false;
        CHECK (fieldsOf (rew::validate (q, c)).contains ("steps"));
    }

    SECTION ("field errors")
    {
        auto& s = q.steps[0];
        s.level = rew::Level { 3.0, "dBFS" };
        s.sweep = rew::Sweep { 2000, 20, "3M" };
        s.repetitions = 0;
        s.timingRef = juce::String ("Laser");
        s.input = juce::String ("2");
        s.lastInput = juce::String ("1");
        s.protection = rew::Protection { false, true, 200.0 };
        s.preDelaySec = -1.0;
        const auto f = fieldsOf (rew::validate (q, c));
        for (auto* field : { "level", "sweep", "repetitions", "timingRef", "lastInput", "protection", "preDelaySec" })
            CHECK (f.contains (field));
    }

    SECTION ("Java input must be a number")
    {
        q.steps[0].input = juce::String ("In 1");
        CHECK (fieldsOf (rew::validate (q, c)).contains ("input"));
    }

    SECTION ("REW's lists apply once they come from REW")
    {
        q.steps[0].output = juce::String ("9");   // offline: accepted
        CHECK_FALSE (rew::hasErrors (rew::validate (q, c)));

        FakeRewTransport fake;
        c = rew::queryChoices (fake);
        CHECK (c.fromRew);
        CHECK (c.sampleRate == Approx (48000.0));
        CHECK (fieldsOf (rew::validate (q, c)).contains ("output"));   // fake REW has L, R, L+R, 3, 4

        q.steps[0].output = juce::String ("L+R");
        q.steps[0].sweep = rew::Sweep { 20, 30000, "512k" };            // above 24 kHz
        CHECK (fieldsOf (rew::validate (q, c)).contains ("sweep"));
    }

    SECTION ("ASIO inputs are names from REW")
    {
        FakeRewTransport fake;
        auto* d = new juce::DynamicObject();
        d->setProperty ("driver", "ASIO");
        fake.setValue ("/audio/driver", juce::var (d));
        c = rew::queryChoices (fake);
        CHECK (c.driver == rew::Driver::Asio);
        CHECK (c.outputs.contains ("Out 2"));

        q.steps[0].output = juce::String ("Out 1");
        q.steps[0].input = juce::String ("In 2");
        q.steps[0].lastInput = juce::String ("In 4");
        CHECK_FALSE (rew::hasErrors (rew::validate (q, c)));
        q.steps[0].lastInput = juce::String ("In 1");
        CHECK (fieldsOf (rew::validate (q, c)).contains ("lastInput"));
        q.steps[0].input = juce::String ("In 9");
        CHECK (fieldsOf (rew::validate (q, c)).contains ("input"));
    }

    SECTION ("unreachable REW gives the offline lists")
    {
        FakeRewTransport fake;
        fake.reachable = false;
        c = rew::queryChoices (fake);
        CHECK_FALSE (c.fromRew);
        CHECK (c.outputs.contains ("L+R"));
    }

    SECTION ("duplicate titles are a warning only")
    {
        q.steps[1].enabled = true;
        q.steps[1].name = "L";
        const auto issues = rew::validate (q, c);
        CHECK_FALSE (rew::hasErrors (issues));
        CHECK (std::any_of (issues.begin(), issues.end(), [] (auto& i) { return i.field == "name" && ! i.error; }));
    }
}

TEST_CASE ("REW sequence: titles, prompts, positions and moving steps", "[rew]")
{
    rew::Sequence q;
    q.name = "Room";
    rew::Step s;
    s.output = juce::String ("L");
    q.steps = { s, s };
    q.steps[1].name = "Sub";

    CHECK (rew::stepTitle (q, 0) == "Room 1");
    CHECK (rew::stepTitle (q, 1) == "Sub");
    CHECK (rew::stepPrompt (q, 1).contains ("\"Sub\""));

    rew::addPositions (q, 1, 3);
    REQUIRE (q.steps.size() == 4);
    CHECK (q.steps[1].name == "Sub Pos 1");
    CHECK (q.steps[3].name == "Sub Pos 3");
    CHECK (q.steps[3].waitForUser);
    CHECK (q.steps[3].prompt.contains ("position 3"));
    CHECK (q.steps[3].output == juce::String ("L"));
    CHECK (rew::stepPrompt (q, 2) == "Move the microphone to position 2.");

    rew::moveStep (q, 0, 1);
    CHECK (q.steps[1].name.isEmpty());
    rew::moveStep (q, 3, 1);                     // out of range: no change
    CHECK (q.steps[3].name == "Sub Pos 3");

    q.loops = 3;
    q.steps[0].enabled = false;
    CHECK (rew::totalSweeps (q) == 9);
}
