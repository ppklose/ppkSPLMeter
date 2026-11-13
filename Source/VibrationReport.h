#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Vibration/Assessment.h"
#include "Vibration/ThirdOctave.h"
#include "Vibration/Units.h"

//==============================================================================
// Vibration report (vibration spec §9): one self-contained HTML file with inline
// CSS and SVG charts, no external resources, printable to PDF from a browser.
// generateHtml() is pure (no files, no processor), unit-tested in
// Tests/VibrationReportTests.cpp; the editor collects the Input.
namespace VibrationReport
{
//==============================================================================
// All visible text in one place, so a German version can be added (spec §9.1)
struct Text
{
    const char* title        = "Vibration measurement report";
    const char* measurement  = "Measurement";
    const char* setup        = "Setup";
    const char* results      = "Results";
    const char* assessment   = "Assessment";
    const char* timeHistory  = "Time history";
    const char* spectrum     = "1/3-octave spectrum (linear average over the measurement)";
    const char* ppvChart     = "PPV and dominant frequency (DIN 4150-3)";
    const char* events       = "Events";
    const char* micSummary   = "Sound level (microphone)";
    const char* disclaimer   = "Disclaimer";
    const char* project      = "Project";
    const char* location     = "Location / measurement point";
    const char* operatorName = "Operator";
    const char* mounting     = "Sensor mounting";
    const char* notes        = "Notes";
    const char* created      = "Report created";
    const char* measuredTime = "Measured time";
    const char* logWindow    = "Time history covers";
    const char* software     = "Software";
    const char* interface    = "Audio interface / host";
    const char* sampleRate   = "Sample rate";
    const char* sensor       = "Sensor serial";
    const char* usableRange  = "Usable range (datasheet)";
    const char* channels     = "Input channels";
    const char* profile      = "Profile";
    const char* quantity     = "Quantity";
    const char* highPass     = "High-pass";
    const char* weighting    = "Frequency weighting";
    const char* bandLimit    = "Band limit";
    const char* kFactors     = "k-factors";
    const char* timeConstant = "RMS time constant";
    const char* exposure     = "Daily exposure time T";
    const char* mainValue    = "Main value";
    const char* noAssessment = "No assessment for this profile.";
    const char* noEvents     = "No markers or pauses.";
    const char* limitsUsed   = "Reference values used";
    const char* verdict      = "Result";
    const char* outsideRange = "Shaded: outside the sensor's usable range or below the high-pass.";
    const char* pauseLabel   = "Pause";
    const char* markerLabel  = "Marker";
    const char* disclaimerText =
        "Measured with SPLMeter v%VERSION% using a general-purpose audio interface. The measurement chain "
        "has not been type-tested to ISO 8041-1 / DIN 45669-1. Results are indicative and do not replace a "
        "measurement with a certified instrument for legal or contractual assessments.";

    static const Text& english() { static const Text t; return t; }
};

struct Meta
{
    juce::String project, location, operatorName, mounting, notes;
};

struct ChannelInfo
{
    juce::String name;       // e.g. "IN 2"
    juce::String role;       // "Accel X", "Mic"
    double       calDb = 0.0;
    juce::String calInfo;    // "2026-11-12 · calibrator"
    bool         accel = false;
};

struct Event
{
    juce::int64  timeMs = 0;       // analysis time (as the log entries)
    juce::int64  durationMs = 0;   // pauses
    juce::String text;
    bool         pause = false;
};

struct Input
{
    Meta                     meta;
    juce::String             version;            // "3.6.0"
    juce::Time               created;
    juce::String             device;
    double                   sampleRate = 48000.0;
    juce::String             serial;
    double                   lowerLimitHz = 0.5, upperLimitHz = 10000.0;
    double                   exposureHours = 8.0;
    std::vector<ChannelInfo> channels;
    vib::Profile             profile = vib::Profile::Custom;
    juce::String             profileLabel, mainLabel;
    vib::VibrationSettings   settings;
    vib::DisplayUnit         unit = vib::DisplayUnit::Linear;
    vib::VibrationSnapshot   snap;
    vib::AssessmentLimits    limits;
    std::vector<LogEntry>    log;
    std::vector<Event>       events;
    bool                     hasMic = false;
    juce::String             timeWeighting = "F";   // F or S (LAFmax / LASmax)
};

//==============================================================================
namespace detail
{
    inline juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

    inline juce::String esc (const juce::String& s)
    {
        return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;");
    }

    inline juce::String num (double v, int sig = 3)
    {
        if (! std::isfinite (v)) return "--";
        const double a = std::abs (v);
        const int dec = a == 0.0 ? 0 : juce::jlimit (0, 6, sig - 1 - (int) std::floor (std::log10 (a)));
        return juce::String (v, dec);
    }

    inline vib::Quantity quantity (const Input& in) { return (vib::Quantity) in.snap.quantity; }

    inline juce::String unitLabel (const Input& in) { return U (vib::units::label (in.unit, quantity (in))); }

    inline juce::String value (const Input& in, double si)
    {
        return U (vib::units::format (si, in.unit, quantity (in)).c_str());
    }

    inline bool hasAxis (const vib::VibrationSnapshot& s, int a) { return (s.axisMask & (1 << a)) != 0; }

    inline bool hasSum (const vib::VibrationSnapshot& s)
    {
        int n = 0;
        for (int a = 0; a < 3; ++a) n += hasAxis (s, a) ? 1 : 0;
        return n > 1 && ! s.uniaxial;
    }

    inline juce::String axisName (const vib::VibrationSnapshot& s, int slot)
    {
        if (s.uniaxial) return "Axis";
        if (slot == vib::AxisSum) return U ("\xce\xa3");
        const char* n[3] = { "X", "Y", "Z" };
        return n[slot];
    }

    // Axis colours for print (darker than the app's, same hues)
    inline const char* axisColour (int slot)
    {
        static const char* c[4] = { "#8a6d45", "#2fa889", "#5878a8", "#222222" };
        return c[juce::jlimit (0, 3, slot)];
    }

    inline juce::String weightingName (int w)
    {
        const char* names[] = { "Unweighted", "Band limit", "Wb", "Wc", "Wd", "We", "Wf", "Wh", "Wj", "Wk", "Wm", "KB" };
        return (w >= 0 && w < (int) std::size (names)) ? juce::String (names[w]) : juce::String();
    }

    inline juce::String duration (double seconds)
    {
        if (seconds < 60.0) return juce::String (seconds, 1) + " s";
        const int s = (int) std::lround (seconds);
        if (s < 3600) return juce::String (s / 60) + " min " + juce::String (s % 60) + " s";
        return juce::String (s / 3600) + " h " + juce::String ((s % 3600) / 60) + " min";
    }

    inline juce::String row (const juce::String& k, const juce::String& v)
    {
        return "<tr><th>" + k + "</th><td>" + v + "</td></tr>\n";
    }

    // "Nice" tick step for about n ticks over a range
    inline double niceStep (double range, int n)
    {
        if (range <= 0.0) return 1.0;
        const double raw = range / std::max (1, n);
        const double mag = std::pow (10.0, std::floor (std::log10 (raw)));
        const double r = raw / mag;
        return (r < 1.5 ? 1.0 : r < 3.5 ? 2.0 : r < 7.5 ? 5.0 : 10.0) * mag;
    }

    // Tick label: as many decimals as the step needs, no trailing zeros
    inline juce::String tick (double v, double step)
    {
        const int dec = juce::jlimit (0, 6, (int) std::ceil (-std::log10 (step) - 1.0e-9));
        return juce::String (std::abs (v) < step * 1.0e-6 ? 0.0 : v, dec);
    }

    //==========================================================================
    // Minimal SVG plot: frame, ticks, polylines
    struct Plot
    {
        double x0, x1, y0, y1;
        bool   logX = false, logY = false;
        int    w = 900, h = 320, L = 72, R = 16, T = 14, B = 44;
        juce::String svg;

        double px (double x) const
        {
            const double t = logX ? (std::log10 (x) - std::log10 (x0)) / (std::log10 (x1) - std::log10 (x0))
                                  : (x - x0) / (x1 - x0);
            return L + t * (w - L - R);
        }
        double py (double y) const
        {
            const double t = logY ? (std::log10 (std::max (y, 1.0e-12)) - std::log10 (y0)) / (std::log10 (y1) - std::log10 (y0))
                                  : (y - y0) / (y1 - y0);
            return h - B - juce::jlimit (-0.02, 1.02, t) * (h - T - B);
        }

        void begin (const juce::String& xLabel, const juce::String& yLabel)
        {
            svg << "<svg viewBox=\"0 0 " << w << " " << h << "\" xmlns=\"http://www.w3.org/2000/svg\" class=\"chart\">\n"
                << "<rect x=\"" << L << "\" y=\"" << T << "\" width=\"" << (w - L - R) << "\" height=\"" << (h - T - B)
                << "\" fill=\"#fff\" stroke=\"#999\"/>\n";
            // X ticks
            std::vector<double> xt;
            if (logX)
            {
                for (double d = std::pow (10.0, std::floor (std::log10 (x0))); d <= x1 * 1.0001; d *= 10.0)
                    for (double m : { 1.0, 2.0, 5.0 })
                        if (d * m >= x0 * 0.9999 && d * m <= x1 * 1.0001) xt.push_back (d * m);
            }
            else
            {
                const double st = niceStep (x1 - x0, 8);
                for (double v = std::ceil (x0 / st) * st; v <= x1 + 1e-9; v += st) xt.push_back (v);
            }
            for (double v : xt)
            {
                const double x = px (v);
                svg << "<line x1=\"" << x << "\" y1=\"" << T << "\" x2=\"" << x << "\" y2=\"" << (h - B)
                    << "\" stroke=\"#e2e2e2\"/>\n<text x=\"" << x << "\" y=\"" << (h - B + 16)
                    << "\" text-anchor=\"middle\">" << tick (v, logX ? v : niceStep (x1 - x0, 8)) << "</text>\n";
            }
            // Y ticks
            std::vector<double> yt;
            if (logY)
            {
                for (double d = std::pow (10.0, std::floor (std::log10 (y0))); d <= y1 * 1.0001; d *= 10.0)
                    for (double m : { 1.0, 2.0, 5.0 })
                        if (d * m >= y0 * 0.9999 && d * m <= y1 * 1.0001) yt.push_back (d * m);
            }
            else
            {
                const double st = niceStep (y1 - y0, 6);
                for (double v = std::ceil (y0 / st) * st; v <= y1 + 1e-9; v += st) yt.push_back (v);
            }
            for (double v : yt)
            {
                const double y = py (v);
                svg << "<line x1=\"" << L << "\" y1=\"" << y << "\" x2=\"" << (w - R) << "\" y2=\"" << y
                    << "\" stroke=\"#e2e2e2\"/>\n<text x=\"" << (L - 6) << "\" y=\"" << (y + 4)
                    << "\" text-anchor=\"end\">" << tick (v, logY ? v : niceStep (y1 - y0, 6)) << "</text>\n";
            }
            svg << "<text x=\"" << (L + (w - L - R) / 2) << "\" y=\"" << (h - 6) << "\" text-anchor=\"middle\">"
                << esc (xLabel) << "</text>\n"
                << "<text transform=\"translate(14 " << (T + (h - T - B) / 2) << ") rotate(-90)\" text-anchor=\"middle\">"
                << esc (yLabel) << "</text>\n";
        }

        void polyline (const std::vector<std::pair<double, double>>& pts, const char* colour, double width = 1.5,
                       const char* dash = nullptr)
        {
            if (pts.size() < 2) return;
            svg << "<polyline fill=\"none\" stroke=\"" << colour << "\" stroke-width=\"" << width << "\"";
            if (dash != nullptr) svg << " stroke-dasharray=\"" << dash << "\"";
            svg << " points=\"";
            for (const auto& p : pts)
                svg << juce::String (px (p.first), 1) << "," << juce::String (py (p.second), 1) << " ";
            svg << "\"/>\n";
        }

        void legend (const std::vector<std::pair<juce::String, const char*>>& items)
        {
            double x = L + 10;
            for (const auto& it : items)
            {
                svg << "<rect x=\"" << x << "\" y=\"" << (T + 8) << "\" width=\"14\" height=\"4\" fill=\"" << it.second << "\"/>"
                    << "<text x=\"" << (x + 18) << "\" y=\"" << (T + 14) << "\">" << esc (it.first) << "</text>\n";
                x += 30 + 8 * it.first.length();
            }
        }

        juce::String end() { return svg + "</svg>\n"; }
    };

    //==========================================================================
    inline juce::String timeHistoryChart (const Input& in)
    {
        const auto q = quantity (in);
        std::vector<const LogEntry*> rows;
        for (const auto& e : in.log)
            if (e.vib.valid) rows.push_back (&e);
        if (rows.size() < 2)
            return {};
        const auto t0 = rows.front()->timestampMs;
        const double tEnd = (rows.back()->timestampMs - t0) / 1000.0;

        const int slots = hasSum (in.snap) ? 4 : 3;
        double lo = 1e300, hi = -1e300;
        for (auto* e : rows)
            for (int s = 0; s < slots; ++s)
                if (s == vib::AxisSum || hasAxis (in.snap, s))
                {
                    const double v = vib::units::convert (e->vib.rms[s], in.unit, q);
                    lo = std::min (lo, v); hi = std::max (hi, v);
                }
        const bool db = vib::units::effective (in.unit, q) == vib::DisplayUnit::Db;
        if (db) { lo = std::floor (lo / 10.0) * 10.0; hi = std::max (lo + 20.0, std::ceil (hi / 10.0) * 10.0); }
        else    { lo = 0.0; hi = hi > 0.0 ? hi * 1.1 : 1.0; }

        Plot p { 0.0, std::max (tEnd, 1.0), lo, hi };
        p.begin ("Time (s)", "RMS (" + unitLabel (in) + ")");

        // Events: pauses (grey) and markers (red) as vertical lines
        for (const auto& ev : in.events)
        {
            const double t = (ev.timeMs - t0) / 1000.0;
            if (t < 0.0 || t > tEnd) continue;
            const double x = p.px (t);
            p.svg << "<line x1=\"" << x << "\" y1=\"" << p.T << "\" x2=\"" << x << "\" y2=\"" << (p.h - p.B)
                  << "\" stroke=\"" << (ev.pause ? "#aaa" : "#d70015") << "\" stroke-dasharray=\"4 3\"/>\n";
            const auto label = ev.pause ? juce::String (Text::english().pauseLabel) : ev.text;
            if (label.isNotEmpty())
                p.svg << "<text x=\"" << (x + 3) << "\" y=\"" << (p.T + 26) << "\" fill=\""
                      << (ev.pause ? "#777" : "#d70015") << "\">" << esc (label) << "</text>\n";
        }

        std::vector<std::pair<juce::String, const char*>> legend;
        for (int s = 0; s < slots; ++s)
        {
            if (s != vib::AxisSum && ! hasAxis (in.snap, s)) continue;
            std::vector<std::pair<double, double>> pts;
            for (auto* e : rows)
                pts.push_back ({ (e->timestampMs - t0) / 1000.0, vib::units::convert (e->vib.rms[s], in.unit, q) });
            p.polyline (pts, axisColour (s), s == vib::AxisSum ? 1.2 : 1.5, s == vib::AxisSum ? "5 3" : nullptr);
            legend.push_back ({ axisName (in.snap, s), axisColour (s) });
        }
        p.legend (legend);
        return p.end();
    }

    inline juce::String spectrumChart (const Input& in)
    {
        const auto& s = in.snap;
        const int nb = std::min (s.numBands, vib::kNumThirdOctaveBands);
        if (nb < 2) return {};
        const auto q = quantity (in);
        double lo = 1e300, hi = -1e300;
        for (int a = 0; a < 3; ++a)
            if (hasAxis (s, a))
                for (int b = 0; b < nb; ++b)
                    if (s.bandRms[a][b] > 0.0f)
                    {
                        const double d = vib::units::levelDb (s.bandRms[a][b], q);
                        lo = std::min (lo, d); hi = std::max (hi, d);
                    }
        if (hi < lo) return {};
        hi = std::ceil (hi / 10.0) * 10.0;
        lo = std::max (std::floor (lo / 10.0) * 10.0, hi - 80.0);
        if (hi - lo < 20.0) lo = hi - 20.0;

        const double f0 = vib::ThirdOctaveBank::exactCentre (0) / 1.12, f1 = vib::ThirdOctaveBank::exactCentre (nb - 1) * 1.12;
        Plot p { f0, f1, lo, hi };
        p.logX = true;
        p.begin ("Frequency (Hz)", U (vib::units::label (vib::DisplayUnit::Db, q)));

        // Shade below max (high-pass, lower usable limit) and above the upper usable limit
        auto shade = [&] (double fa, double fb)
        {
            fa = std::max (fa, f0); fb = std::min (fb, f1);
            if (fb <= fa) return;
            p.svg << "<rect x=\"" << p.px (fa) << "\" y=\"" << p.T << "\" width=\"" << (p.px (fb) - p.px (fa))
                  << "\" height=\"" << (p.h - p.T - p.B) << "\" fill=\"#000\" fill-opacity=\"0.06\"/>\n";
        };
        shade (f0, std::max (in.settings.highPassHz, in.lowerLimitHz));
        shade (in.upperLimitHz, f1);

        std::vector<std::pair<juce::String, const char*>> legend;
        for (int a = 0; a < 3; ++a)
        {
            if (! hasAxis (s, a)) continue;
            std::vector<std::pair<double, double>> pts;
            for (int b = 0; b < nb; ++b)
                pts.push_back ({ vib::ThirdOctaveBank::exactCentre (b), vib::units::levelDb (s.bandRms[a][b], q) });
            p.polyline (pts, axisColour (a));
            for (const auto& pt : pts)
                p.svg << "<circle cx=\"" << juce::String (p.px (pt.first), 1) << "\" cy=\"" << juce::String (p.py (pt.second), 1)
                      << "\" r=\"2.2\" fill=\"" << axisColour (a) << "\"/>\n";
            legend.push_back ({ axisName (s, a), axisColour (a) });
        }
        p.legend (legend);
        return p.end();
    }

    inline juce::String ppvChart (const Input& in)
    {
        const auto& s = in.snap;
        Plot p { 1.0, 300.0, 1.0, 100.0 };
        p.logX = p.logY = true;
        p.begin ("Dominant frequency (Hz)", "PPV (mm/s)");
        // Foundation lines 1–3 of DIN 4150-3 Table 1 (dashed), the values used (solid)
        for (int line = 0; line < 3; ++line)
        {
            vib::AssessmentLimits l;
            vib::applyDin4150_3Preset (l, (vib::Din4150Line) line, vib::Din4150Location::Foundation, false);
            std::vector<std::pair<double, double>> pts;
            for (double f : { 1.0, 10.0, 50.0, 100.0, 300.0 })
                pts.push_back ({ f, vib::din4150_3FoundationLimit (l, f) });
            p.polyline (pts, "#bbb", 1.0, "5 4");
            p.svg << "<text x=\"" << (p.px (300.0) - 4) << "\" y=\"" << (p.py (pts.back().second) - 4)
                  << "\" text-anchor=\"end\" fill=\"#999\">Line " << (line + 1) << "</text>\n";
        }
        std::vector<std::pair<double, double>> used;
        if (in.limits.din3Location == vib::Din4150Location::Foundation)
            for (double f : { 1.0, 10.0, 50.0, 100.0, 300.0 })
                used.push_back ({ f, vib::din4150_3FoundationLimit (in.limits, f) });
        else
            used = { { 1.0, in.limits.din3Single }, { 300.0, in.limits.din3Single } };
        p.polyline (used, "#d70015", 2.0);

        std::vector<std::pair<juce::String, const char*>> legend;
        for (int a = 0; a < 3; ++a)
        {
            if (! hasAxis (s, a) || s.axis[a].peakMax <= 0.0f) continue;
            const double f = s.axis[a].fDom > 0.0f ? (double) s.axis[a].fDom : 1.0;
            p.svg << "<circle cx=\"" << juce::String (p.px (juce::jlimit (1.0, 300.0, f)), 1) << "\" cy=\""
                  << juce::String (p.py (s.axis[a].peakMax * 1000.0), 1) << "\" r=\"5\" fill=\"" << axisColour (a) << "\"/>\n";
            legend.push_back ({ axisName (s, a), axisColour (a) });
        }
        legend.push_back ({ "Values used", "#d70015" });
        p.legend (legend);
        return p.end();
    }

    //==========================================================================
    inline juce::String resultsTable (const Input& in)
    {
        const auto& s = in.snap;
        const auto q = quantity (in);
        std::vector<int> slots;
        for (int a = 0; a < 3; ++a) if (hasAxis (s, a)) slots.push_back (a);
        if (hasSum (s)) slots.push_back (vib::AxisSum);

        bool kb = false;
        for (int a = 0; a < 3; ++a) kb = kb || (hasAxis (s, a) && s.weighting[a] == (int) vib::Weighting::KB);
        const auto unit = unitLabel (in);

        juce::String h = "<table class=\"grid\"><tr><th></th>";
        for (int sl : slots) h << "<th>" << axisName (s, sl) << "</th>";
        h << "<th>Unit</th></tr>\n";
        auto line = [&] (const juce::String& name, const juce::String& u, std::function<juce::String (int)> f)
        {
            h << "<tr><th>" << name << "</th>";
            for (int sl : slots) h << "<td>" << f (sl) << "</td>";
            h << "<td>" << u << "</td></tr>\n";
        };
        line ("RMS over the measurement", unit, [&] (int sl) { return value (in, s.axis[sl].awRms); });
        line (U ("Running RMS at the end (\xcf\x84 ") + num (in.settings.tauSec) + " s)", unit,
              [&] (int sl) { return value (in, s.axis[sl].rms); });
        line (q == vib::Quantity::Velocity ? "Peak since reset (PPV)" : "Peak since reset", unit,
              [&] (int sl) { return value (in, s.axis[sl].peakMax); });
        line ("Crest factor", "", [&] (int sl) { return num (s.axis[sl].crest); });
        if (q == vib::Quantity::Acceleration)
        {
            line ("MTVV", unit, [&] (int sl) { return value (in, s.axis[sl].mtvv); });
            line ("VDV", U ("m/s<sup>1.75</sup>"), [&] (int sl) { return num (s.axis[sl].vdv); });
        }
        if (q == vib::Quantity::Velocity)
            line ("Dominant frequency at the peak", "Hz", [&] (int sl)
                  { return sl == vib::AxisSum || s.axis[sl].fDom <= 0.0f ? juce::String ("--") : num (s.axis[sl].fDom); });
        if (kb)
        {
            line ("KB<sub>Fmax</sub>", "", [&] (int sl) { return sl == vib::AxisSum ? juce::String ("--") : num (s.axis[sl].kbFmax * 1000.0); });
            line ("KB<sub>FTm</sub>", "", [&] (int sl) { return sl == vib::AxisSum ? juce::String ("--") : num (s.axis[sl].kbFTm * 1000.0); });
            line ("KB<sub>FTr</sub>", "", [&] (int sl)
                  { return sl == vib::AxisSum ? juce::String ("--") : num (vib::kbFTr (s.axis[sl].kbFTm * 1000.0, in.limits)); });
        }
        h << "</table>\n";
        if (in.profileLabel.isNotEmpty() && in.mainLabel.isNotEmpty())
            h << "<p>" << Text::english().mainValue << " (" << esc (in.profileLabel) << "): <b>" << esc (in.mainLabel)
              << " = " << value (in, s.main) << " " << unit << "</b></p>\n";
        return h;
    }

    inline const char* verdictClass (vib::Verdict v)
    {
        switch (v)
        {
            case vib::Verdict::Ok:       return "ok";
            case vib::Verdict::Warning:  return "warn";
            case vib::Verdict::Exceeded: return "bad";
            case vib::Verdict::Info:     return "info";
            case vib::Verdict::None:     break;
        }
        return "none";
    }

    inline juce::String assessmentSection (const Input& in)
    {
        const auto& t = Text::english();
        const auto& s = in.snap;
        const auto& l = in.limits;
        const auto a = vib::assess (in.profile, s, in.settings.k, in.exposureHours, l);
        juce::String h;
        juce::String standard, params, values;

        switch (in.profile)
        {
            case vib::Profile::WholeBodyHealth:
            {
                standard = l.wbPreset == vib::WholeBodyPreset::Germany
                         ? U ("LärmVibrationsArbSchV \xc2\xa7" "9 (Germany), ISO 2631-1") : "Directive 2002/44/EC, ISO 2631-1";
                params = U ("Criterion: ") + (l.wbCriterion == vib::WholeBodyCriterion::A8 ? "A(8)" : "VDV")
                       + U (" \xc2\xb7 T = ") + num (in.exposureHours) + " h";
                if (l.wbCriterion == vib::WholeBodyCriterion::A8)
                {
                    for (int ax = 0; ax < 3; ++ax)
                        if (hasAxis (s, ax))
                            values << row ("A(8) " + axisName (s, ax) + U (" (k = ") + num (in.settings.k[ax]) + ")",
                                           num (vib::dailyExposureA8 (in.settings.k[ax] * s.axis[ax].awRms, in.exposureHours))
                                           + U (" m/s\xc2\xb2"));
                    values << row ("Action value", num (l.wbAction) + U (" m/s\xc2\xb2"))
                           << row ("Limit value X/Y", num (l.wbLimitXY) + U (" m/s\xc2\xb2"))
                           << row ("Limit value Z", num (l.wbLimitZ) + U (" m/s\xc2\xb2"));
                }
                else
                {
                    double vdv = 0.0;
                    for (int ax = 0; ax < 3; ++ax)
                        if (hasAxis (s, ax)) vdv = std::max (vdv, in.settings.k[ax] * s.axis[ax].vdv);
                    values << row ("VDV measured (max k" + U ("\xc2\xb7") + "VDV)", num (vdv) + U (" m/s<sup>1.75</sup>"))
                           << row ("VDV daily exposure", num (vib::dailyVdv (vdv, s.seconds, in.exposureHours)) + U (" m/s<sup>1.75</sup>"))
                           << row ("Action value", num (l.wbVdvAction) + U (" m/s<sup>1.75</sup>"))
                           << row ("Limit value", num (l.wbVdvLimit) + U (" m/s<sup>1.75</sup>"));
                }
                break;
            }
            case vib::Profile::HandArm:
            {
                standard = "Directive 2002/44/EC, ISO 5349-1";
                const double ahv = hasSum (s) ? s.axis[vib::AxisSum].awRms : s.axis[0].awRms;
                params = U ("T = ") + num (in.exposureHours) + " h";
                values << row ("a<sub>hv</sub>", num (ahv) + U (" m/s\xc2\xb2"))
                       << row ("A(8)", num (vib::dailyExposureA8 (ahv, in.exposureHours)) + U (" m/s\xc2\xb2"))
                       << row ("Action value", num (l.haAction) + U (" m/s\xc2\xb2"))
                       << row ("Limit value", num (l.haLimit) + U (" m/s\xc2\xb2"));
                break;
            }
            case vib::Profile::WholeBodyComfort:
            {
                standard = "ISO 2631-1 Annex C (informative)";
                const double av = hasSum (s) ? s.axis[vib::AxisSum].awRms : s.axis[0].awRms;
                values << row ("a<sub>v</sub>", num (av) + U (" m/s\xc2\xb2"))
                       << row ("Comfort scale", U ("< 0.315 not uncomfortable \xc2\xb7 0.315\xe2\x80\x93" "0.63 a little \xc2\xb7 "
                                                   "0.5\xe2\x80\x93" "1 fairly \xc2\xb7 0.8\xe2\x80\x93" "1.6 uncomfortable \xc2\xb7 "
                                                   "1.25\xe2\x80\x93" "2.5 very \xc2\xb7 > 2 m/s\xc2\xb2 extremely uncomfortable"));
                break;
            }
            case vib::Profile::MachineryIso20816:
            case vib::Profile::MachineryLowSpeed:
            {
                standard = "ISO 10816-3 / ISO 20816-3";
                params = juce::String (l.machGroup == vib::MachineGroup::Group1 ? "Group 1" : "Group 2")
                       + U (" \xc2\xb7 ") + (l.machFoundation == vib::Foundation::Rigid ? "rigid" : "flexible") + " foundation";
                double v = 0.0;
                for (int ax = 0; ax < 3; ++ax) if (hasAxis (s, ax)) v = std::max (v, (double) s.axis[ax].awRms);
                values << row ("v<sub>rms</sub> (largest axis, over the measurement)", num (v * 1000.0) + " mm/s")
                       << row ("Zone boundaries A/B, B/C, C/D", num (l.machAB) + " / " + num (l.machBC) + " / " + num (l.machCD) + " mm/s");
                break;
            }
            case vib::Profile::BuildingPerception:
            {
                standard = "DIN 4150-2";
                const char* zones[5] = { "Industrial (GI)", "Commercial (GE)", "Mixed (MI, MD, MK)", "Residential (WR, WA)", "Hospitals, spa (SO)" };
                const double tr = vib::din4150_2RatingHours (l.din2Night);
                params = juce::String (zones[juce::jlimit (0, 4, (int) l.din2Zone)]) + U (" \xc2\xb7 ")
                       + (l.din2Night ? "night" : "day") + U (" \xc2\xb7 T_r = ") + num (tr) + U (" h \xc2\xb7 T_e = ")
                       + num (std::min (l.din2ExposureHours, tr)) + " h";
                double fmax = 0.0, ftm = 0.0;
                for (int ax = 0; ax < 3; ++ax)
                    if (hasAxis (s, ax)) { fmax = std::max (fmax, (double) s.axis[ax].kbFmax); ftm = std::max (ftm, (double) s.axis[ax].kbFTm); }
                values << row ("KB<sub>Fmax</sub>", num (fmax * 1000.0))
                       << row ("KB<sub>FTm</sub>", num (ftm * 1000.0))
                       << row (U ("KB<sub>FTr</sub> = KB<sub>FTm</sub> \xc2\xb7 \xe2\x88\x9a(T<sub>e</sub>/T<sub>r</sub>)"),
                               num (vib::kbFTr (ftm * 1000.0, l)))
                       << row ("A<sub>u</sub> / A<sub>o</sub> / A<sub>r</sub>", num (l.din2Au) + " / " + num (l.din2Ao) + " / " + num (l.din2Ar));
                break;
            }
            case vib::Profile::BuildingStructural:
            {
                standard = l.din3LongTerm ? "DIN 4150-3, long-term vibration (Table 3)" : "DIN 4150-3, short-term vibration (Table 1)";
                const char* locs[3] = { "foundation, all directions", "top floor, horizontal", "floor slab, vertical" };
                params = "Line " + juce::String ((int) l.din3Line + 1) + U (" \xc2\xb7 ") + locs[juce::jlimit (0, 2, (int) l.din3Location)];
                for (int ax = 0; ax < 3; ++ax)
                    if (hasAxis (s, ax))
                    {
                        const double f = s.axis[ax].fDom;
                        const double lim = l.din3Location == vib::Din4150Location::Foundation
                                         ? vib::din4150_3FoundationLimit (l, f) : l.din3Single;
                        values << row ("PPV " + axisName (s, ax),
                                       num (s.axis[ax].peakMax * 1000.0) + " mm/s at " + (f > 0.0 ? num (f) + " Hz" : juce::String ("--"))
                                       + U (" \xc2\xb7 value ") + num (lim) + " mm/s");
                    }
                if (l.din3Location == vib::Din4150Location::Foundation)
                    values << row (U ("Values 1\xe2\x80\x93" "10 Hz / 50 Hz / 100 Hz"),
                                   num (l.din3At10) + " / " + num (l.din3At50) + " / " + num (l.din3At100) + " mm/s (linear between)");
                else
                    values << row ("Value, all frequencies", num (l.din3Single) + " mm/s");
                break;
            }
            case vib::Profile::Custom:
            case vib::Profile::StructureBorne:
            case vib::Profile::NumProfiles:
                return "<p>" + juce::String (t.noAssessment) + "</p>\n";
        }

        h << "<table class=\"kv\">" << row ("Standard", esc (standard));
        if (params.isNotEmpty()) h << row ("Parameters", params);
        h << "</table>\n<h3>" << t.limitsUsed << "</h3>\n<table class=\"kv\">" << values << "</table>\n";
        h << "<p class=\"verdict " << verdictClass (a.verdict) << "\"><span class=\"dot\"></span>" << t.verdict << ": <b>"
          << esc (a.text.empty() ? juce::String ("--") : U (a.text.c_str())) << "</b></p>\n";
        return h;
    }

    inline juce::String micSection (const Input& in)
    {
        double sumA = 0.0, sumC = 0.0, maxA = -999.0;
        int n = 0;
        for (const auto& e : in.log)
        {
            sumA += std::pow (10.0, e.rmsDBASPL / 10.0);
            sumC += std::pow (10.0, e.rmsDBCSPL / 10.0);
            maxA = std::max (maxA, (double) e.rmsDBASPL);
            ++n;
        }
        if (n == 0) return {};
        juce::String h = "<table class=\"kv\">";
        h << row ("L<sub>Aeq</sub>", juce::String (10.0 * std::log10 (sumA / n), 1) + " dB")
          << row ("L<sub>Ceq</sub>", juce::String (10.0 * std::log10 (sumC / n), 1) + " dB")
          << row ("L<sub>A" + in.timeWeighting + "max</sub>", juce::String (maxA, 1) + " dB")
          << "</table>\n<p class=\"small\">Over the time history period.</p>\n";
        return h;
    }

    inline const char* css()
    {
        return
            "body{font-family:-apple-system,Segoe UI,Helvetica,Arial,sans-serif;color:#1c1c1e;max-width:960px;margin:24px auto;padding:0 16px;font-size:14px}"
            "h1{font-size:24px;margin-bottom:4px}h2{font-size:18px;border-bottom:1px solid #ccc;padding-bottom:4px;margin-top:28px}"
            "h3{font-size:15px;margin:14px 0 6px}"
            "table{border-collapse:collapse;margin:6px 0}th,td{padding:4px 10px;text-align:left;vertical-align:top}"
            "table.kv th{font-weight:600;color:#555;width:260px}"
            "table.grid th,table.grid td{border:1px solid #ddd}table.grid td{text-align:right;font-variant-numeric:tabular-nums}"
            ".chart{width:100%;height:auto;font-size:11px;fill:#333}"
            ".verdict{font-size:16px;padding:8px 12px;border-radius:6px;background:#f2f2f7}"
            ".dot{display:inline-block;width:12px;height:12px;border-radius:6px;margin-right:8px;background:#999}"
            ".ok .dot{background:#248a3d}.warn .dot{background:#c93400}.bad .dot{background:#d70015}.info .dot{background:#007aff}"
            ".small{color:#666;font-size:12px}.disclaimer{color:#555;font-size:12px;border-top:1px solid #ccc;padding-top:8px}"
            "@media print{body{margin:0;max-width:none}h2{break-after:avoid}svg,table{break-inside:avoid}}";
    }
}

//==============================================================================
inline juce::String generateHtml (const Input& in)
{
    using namespace detail;
    const auto& t = Text::english();
    const auto& s = in.snap;
    const auto q = quantity (in);
    const char* qNames[3] = { "Acceleration", "Velocity", "Displacement" };

    juce::String h;
    h << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n<title>" << t.title;
    if (in.meta.project.isNotEmpty()) h << " - " << esc (in.meta.project);
    h << "</title>\n<style>" << css() << "</style></head><body>\n";
    h << "<h1>" << t.title << "</h1>\n";

    // 1. Header
    h << "<h2>" << t.measurement << "</h2>\n<table class=\"kv\">"
      << row (t.project, esc (in.meta.project)) << row (t.location, esc (in.meta.location))
      << row (t.operatorName, esc (in.meta.operatorName))
      << row (t.created, in.created.formatted ("%Y-%m-%d %H:%M:%S"))
      << row (t.measuredTime, duration (s.seconds));
    {
        juce::int64 first = 0, last = 0;
        for (const auto& e : in.log)
            if (e.vib.valid) { if (first == 0) first = e.timestampMs; last = e.timestampMs; }
        if (last > first)
            h << row (t.logWindow, juce::Time (first).formatted ("%H:%M:%S") + U (" \xe2\x80\x93 ")
                                   + juce::Time (last).formatted ("%H:%M:%S") + " (" + duration ((last - first) / 1000.0) + ")");
    }
    h << row (t.software, "SPLMeter v" + esc (in.version)) << "</table>\n";

    // 2. Setup
    h << "<h2>" << t.setup << "</h2>\n<table class=\"kv\">"
      << row (t.interface, esc (in.device)) << row (t.sampleRate, juce::String (in.sampleRate, 0) + " Hz")
      << row (t.sensor, esc (in.serial)) << row (t.mounting, esc (in.meta.mounting))
      << row (t.usableRange, num (in.lowerLimitHz) + U ("\xe2\x80\x93") + num (in.upperLimitHz) + " Hz")
      << row (t.profile, esc (in.profileLabel))
      << row (t.quantity, qNames[juce::jlimit (0, 2, (int) q)])
      << row (t.highPass, num (in.settings.highPassHz) + " Hz");
    {
        juce::StringArray w;
        for (int a = 0; a < 3; ++a)
            if (hasAxis (s, a)) w.add (axisName (s, a) + ": " + weightingName ((int) in.settings.weighting[a]));
        h << row (t.weighting, w.joinIntoString (U (" \xc2\xb7 ")));
        bool band = false;
        for (int a = 0; a < 3; ++a) band = band || (hasAxis (s, a) && in.settings.weighting[a] == vib::Weighting::BandLimit);
        if (band) h << row (t.bandLimit, num (in.settings.bandLowHz) + U ("\xe2\x80\x93") + num (in.settings.bandHighHz) + " Hz");
        if (hasSum (s))
            h << row (t.kFactors, "x " + num (in.settings.k[0]) + ", y " + num (in.settings.k[1]) + ", z " + num (in.settings.k[2]));
    }
    h << row (t.timeConstant, num (in.settings.tauSec) + " s") << row (t.exposure, num (in.exposureHours) + " h");
    if (in.meta.notes.isNotEmpty()) h << row (t.notes, esc (in.meta.notes).replace ("\n", "<br>"));
    h << "</table>\n<h3>" << t.channels << "</h3>\n<table class=\"grid\"><tr><th>Input</th><th>Role</th>"
      << U ("<th>Cal (dB re 1 \xc2\xb5m/s\xc2\xb2 at 0 dBFS)</th><th>Calibration</th></tr>\n");
    for (const auto& c : in.channels)
        h << "<tr><th>" << esc (c.name) << "</th><td style=\"text-align:left\">" << esc (c.role) << "</td><td>"
          << (c.accel ? juce::String (c.calDb, 2) : juce::String ("--")) << "</td><td style=\"text-align:left\">"
          << esc (c.calInfo.isNotEmpty() ? c.calInfo : juce::String (c.accel ? "not recorded" : "")) << "</td></tr>\n";
    h << "</table>\n";

    // 3. Results, 4. Assessment
    h << "<h2>" << t.results << "</h2>\n" << resultsTable (in);
    h << "<h2>" << t.assessment << "</h2>\n" << assessmentSection (in);

    // 5. Charts
    const auto th = timeHistoryChart (in);
    if (th.isNotEmpty()) h << "<h2>" << t.timeHistory << "</h2>\n" << th;
    const auto sp = spectrumChart (in);
    if (sp.isNotEmpty()) h << "<h2>" << t.spectrum << "</h2>\n" << sp << "<p class=\"small\">" << t.outsideRange << "</p>\n";
    if (in.profile == vib::Profile::BuildingStructural && q == vib::Quantity::Velocity)
        h << "<h2>" << t.ppvChart << "</h2>\n" << ppvChart (in);

    // 6. Events
    h << "<h2>" << t.events << "</h2>\n";
    if (in.events.empty())
        h << "<p>" << t.noEvents << "</p>\n";
    else
    {
        auto evs = in.events;
        std::sort (evs.begin(), evs.end(), [] (const Event& a, const Event& b) { return a.timeMs < b.timeMs; });
        h << "<table class=\"grid\"><tr><th>Time</th><th>Event</th></tr>\n";
        for (const auto& e : evs)
            h << "<tr><td>" << juce::Time (e.timeMs).formatted ("%H:%M:%S") << "</td><td style=\"text-align:left\">"
              << (e.pause ? juce::String (t.pauseLabel) + " (" + duration (e.durationMs / 1000.0) + ")"
                          : juce::String (t.markerLabel) + (e.text.isNotEmpty() ? ": " + esc (e.text) : juce::String()))
              << "</td></tr>\n";
        h << "</table>\n";
    }

    // 7. Mic summary
    if (in.hasMic)
    {
        const auto mic = micSection (in);
        if (mic.isNotEmpty()) h << "<h2>" << t.micSummary << "</h2>\n" << mic;
    }

    // 8. Disclaimer
    h << "<h2>" << t.disclaimer << "</h2>\n<p class=\"disclaimer\">"
      << juce::String (t.disclaimerText).replace ("%VERSION%", esc (in.version)) << "</p>\n";
    h << "</body></html>\n";
    return h;
}

inline juce::String suggestedFileName (const Input& in)
{
    auto name = in.meta.project.trim().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_")
                  .replaceCharacter (' ', '_');
    return "SPLMeter_Vibration_" + (name.isNotEmpty() ? name + "_" : juce::String())
         + in.created.formatted ("%Y-%m-%d_%H-%M") + ".html";
}
} // namespace VibrationReport
