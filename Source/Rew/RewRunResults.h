#pragma once
#include "RewSequencer.h"
#include "../Vibration/Units.h"
#include <cmath>

namespace rew
{
//==============================================================================
// REW sequencer spec §6: what SPLMeter measured during each sweep. REW keeps the
// acoustic response; SPLMeter adds its own levels and, with an accelerometer,
// the vibration (REW cannot measure vibration).
struct StepLevels
{
    bool valid = false;               // the step was measured (audio was running)
    double seconds = 0.0;             // metered time from trigger to the measurement's arrival
    double pausedSeconds = 0.0;       // time SPLMeter was paused within the step

    // Microphone, dB SPL
    double laeq = 0.0, lceq = 0.0, lzeq = 0.0;
    double lafmax = 0.0, lcsmax = 0.0, lcpeak = 0.0;

    // Accelerometer (SI units, see vib::Quantity); X, Y, Z, Σ
    bool vibValid = false;
    int quantity = 0, axisMask = 0;
    bool uniaxial = false;
    double vibRms[4] {}, vibPeak[4] {};

    bool vibSlotPresent (int slot) const
    {
        int axes = 0;
        for (int a = 0; a < 3; ++a) axes += (axisMask >> a) & 1;
        if (slot == 3) return ! uniaxial && axes > 1;
        return ((axisMask >> slot) & 1) != 0;
    }
};

// Context written into the results header
struct RunInfo
{
    juce::String splmeterVersion;
    double calOffsetDb = 0.0;
    vib::DisplayUnit vibUnit = vib::DisplayUnit::Linear;
    juce::String vibProfile;          // profile key, e.g. "machinery"
    juce::StringArray vibCalibration; // "X: IN 2 151.5 dB (calibrator 2026-11-12)" …
    juce::Time finished;
};

namespace results
{
    inline juce::String csvField (const juce::String& s)
    {
        if (s.containsAnyOf (",\"\n\r"))
            return "\"" + s.replace ("\"", "\"\"") + "\"";
        return s;
    }

    inline juce::String num (double v, int decimals)
    {
        return std::isfinite (v) ? juce::String (v, decimals) : juce::String();
    }

    inline juce::String numG (double v)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.6g", v);
        return buf;
    }

    inline juce::String outputText (const Step& s)
    {
        return s.output ? *s.output : juce::String();
    }

    inline juce::String secondOutputText (const Step& s)
    {
        if (! s.secondOutput) return {};
        juce::String t = s.secondOutput->enabled ? s.secondOutput->output : juce::String ("off");
        if (s.secondOutput->invert) t << " (inverted)";
        return t;
    }

    inline juce::String inputText (const Step& s)
    {
        if (! s.input) return {};
        return s.lastInput ? *s.input + "-" + *s.lastInput : *s.input;
    }

    inline juce::String levelText (const Step& s)
    {
        return s.level ? juce::String (s.level->value, 1) + " " + s.level->unit : juce::String();
    }

    inline juce::String vibUnitLabel (const StepLevels& l, vib::DisplayUnit u)
    {
        return juce::String::fromUTF8 (vib::units::label (u, (vib::Quantity) l.quantity));
    }

    inline double vibDisplay (const StepLevels& l, double si, vib::DisplayUnit u)
    {
        return vib::units::convert (si, u, (vib::Quantity) l.quantity);
    }
}

//==============================================================================
// CSV: one row per step result (spec §6.2). Header lines start with '#'.
inline juce::String toCsv (const Sequence& seq, const Progress& p, const std::vector<StepLevels>& levels,
                           const RunInfo& info)
{
    using namespace results;
    juce::StringArray lines;
    lines.add ("# SPLMeter " + info.splmeterVersion + " - REW measurement sequencer run");
    lines.add ("# Sequence: " + seq.name);
    if (p.version.valid())
        lines.add ("# REW: " + p.version.rew + ", API " + p.version.api);
    lines.add ("# Result: " + juce::String (toString (p.state)) + (p.dryRun ? " (dry run)" : ""));
    lines.add ("# Finished: " + info.finished.toISO8601 (true));
    lines.add ("# Microphone calibration offset: " + juce::String (info.calOffsetDb, 2) + " dB");
    if (info.vibProfile.isNotEmpty())
        lines.add ("# Vibration profile: " + info.vibProfile);
    for (const auto& c : info.vibCalibration)
        lines.add ("# Accelerometer " + c);
    if (p.savedMdat.isNotEmpty())
        lines.add ("# REW measurements saved to: " + p.savedMdat);

    lines.add ("step,loop,name,status,start,duration_s,messages,rew_uuids,output,second_output,input,level,"
               "LAeq,LCeq,LZeq,LAFmax,LCSmax,LCpeak,metered_s,paused_s,"
               "vib_unit,vibX_rms,vibY_rms,vibZ_rms,vibSum_rms,vibX_pk,vibY_pk,vibZ_pk,vibSum_pk");

    for (size_t i = 0; i < p.results.size(); ++i)
    {
        const auto& r = p.results[i];
        const auto& s = seq.steps[(size_t) r.step];
        const StepLevels l = i < levels.size() ? levels[i] : StepLevels {};

        juce::StringArray f;
        f.add (juce::String (r.step + 1));
        f.add (juce::String (r.loop + 1));
        f.add (csvField (r.title));
        f.add (toString (r.status));
        f.add (r.start != juce::Time() ? r.start.toISO8601 (true) : juce::String());
        f.add (num (r.durationSec, 2));
        f.add (csvField (r.messages.joinIntoString (" | ")));
        f.add (csvField (r.uuids.joinIntoString (" ")));
        f.add (csvField (outputText (s)));
        f.add (csvField (secondOutputText (s)));
        f.add (csvField (inputText (s)));
        f.add (csvField (levelText (s)));
        if (l.valid)
        {
            for (double v : { l.laeq, l.lceq, l.lzeq, l.lafmax, l.lcsmax, l.lcpeak })
                f.add (num (v, 1));
            f.add (num (l.seconds, 2));
            f.add (num (l.pausedSeconds, 2));
        }
        else
        {
            for (int k = 0; k < 8; ++k) f.add ({});
        }
        if (l.valid && l.vibValid)
        {
            f.add (csvField (vibUnitLabel (l, info.vibUnit)));
            for (const double* values : { l.vibRms, l.vibPeak })
                for (int slot = 0; slot < 4; ++slot)
                    f.add (l.vibSlotPresent (slot) ? numG (vibDisplay (l, values[slot], info.vibUnit)) : juce::String());
        }
        else
        {
            for (int k = 0; k < 9; ++k) f.add ({});
        }
        lines.add (f.joinIntoString (","));
    }
    return lines.joinIntoString ("\n") + "\n";
}

//==============================================================================
// JSON: the same data plus the full sequence, so a run can be traced later
inline juce::var toJson (const Sequence& seq, const Progress& p, const std::vector<StepLevels>& levels,
                         const RunInfo& info)
{
    using namespace results;
    auto obj = [] { return new juce::DynamicObject(); };

    auto* root = obj();
    root->setProperty ("format", "splmeter-rew-run");
    root->setProperty ("version", 1);
    root->setProperty ("splmeter", info.splmeterVersion);
    if (p.version.valid())
        root->setProperty ("rew", p.version.rew + " (API " + p.version.api + ")");
    root->setProperty ("result", toString (p.state));
    root->setProperty ("dryRun", p.dryRun);
    root->setProperty ("finished", info.finished.toISO8601 (true));
    root->setProperty ("message", p.message);
    root->setProperty ("savedMdat", p.savedMdat);
    root->setProperty ("calOffsetDb", info.calOffsetDb);
    root->setProperty ("vibProfile", info.vibProfile);
    root->setProperty ("vibUnit", (int) info.vibUnit);
    juce::Array<juce::var> cal;
    for (const auto& c : info.vibCalibration) cal.add (c);
    root->setProperty ("vibCalibration", cal);
    root->setProperty ("sequence", toVar (seq));

    juce::Array<juce::var> steps;
    for (size_t i = 0; i < p.results.size(); ++i)
    {
        const auto& r = p.results[i];
        const StepLevels l = i < levels.size() ? levels[i] : StepLevels {};
        auto* o = obj();
        o->setProperty ("step", r.step + 1);
        o->setProperty ("loop", r.loop + 1);
        o->setProperty ("name", r.title);
        o->setProperty ("status", toString (r.status));
        o->setProperty ("start", r.start != juce::Time() ? r.start.toISO8601 (true) : juce::String());
        o->setProperty ("durationSec", r.durationSec);
        o->setProperty ("attempts", r.attempts);
        juce::Array<juce::var> msgs, uuids;
        for (auto& m : r.messages) msgs.add (m);
        for (auto& u : r.uuids) uuids.add (u);
        o->setProperty ("messages", msgs);
        o->setProperty ("rewUuids", uuids);
        if (l.valid)
        {
            auto* m = obj();
            m->setProperty ("LAeq", l.laeq);
            m->setProperty ("LCeq", l.lceq);
            m->setProperty ("LZeq", l.lzeq);
            m->setProperty ("LAFmax", l.lafmax);
            m->setProperty ("LCSmax", l.lcsmax);
            m->setProperty ("LCpeak", l.lcpeak);
            m->setProperty ("meteredSec", l.seconds);
            m->setProperty ("pausedSec", l.pausedSeconds);
            o->setProperty ("sound", juce::var (m));
        }
        if (l.valid && l.vibValid)
        {
            static const char* names[4] = { "X", "Y", "Z", "Sum" };
            auto* v = obj();
            v->setProperty ("unit", vibUnitLabel (l, info.vibUnit));
            for (int slot = 0; slot < 4; ++slot)
                if (l.vibSlotPresent (slot))
                {
                    auto* a = obj();
                    a->setProperty ("rms", vibDisplay (l, l.vibRms[slot], info.vibUnit));
                    a->setProperty ("peak", vibDisplay (l, l.vibPeak[slot], info.vibUnit));
                    v->setProperty (names[slot], juce::var (a));
                }
            o->setProperty ("vibration", juce::var (v));
        }
        steps.add (juce::var (o));
    }
    root->setProperty ("steps", steps);
    return juce::var (root);
}

inline juce::File defaultResultsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("SPLMeter").getChildFile ("REW runs");
}

// Writes <sequence>_<date>_<time>.csv and .json; returns the CSV (or {} on failure)
inline juce::File writeResults (const Sequence& seq, const Progress& p, const std::vector<StepLevels>& levels,
                                const RunInfo& info, juce::String& error)
{
    const auto folder = seq.resultsFolder.isNotEmpty() ? juce::File (seq.resultsFolder) : defaultResultsFolder();
    if (! folder.createDirectory())
    {
        error = "Could not create " + folder.getFullPathName();
        return {};
    }
    const auto base = juce::File::createLegalFileName (seq.name.isNotEmpty() ? seq.name : juce::String ("Sequence"))
                      + "_" + info.finished.formatted ("%Y-%m-%d_%H-%M-%S");
    const auto csv = folder.getNonexistentChildFile (base, ".csv", false);
    const auto json = csv.withFileExtension ("json");
    if (! csv.replaceWithText (toCsv (seq, p, levels, info))
        || ! json.replaceWithText (juce::JSON::toString (toJson (seq, p, levels, info))))
    {
        error = "Could not write the results to " + folder.getFullPathName();
        return {};
    }
    return csv;
}
} // namespace rew
