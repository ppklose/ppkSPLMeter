#pragma once
#include <JuceHeader.h>
#include "Vibration/Detectors.h"
#include "Vibration/Units.h"
#include "Vibration/Profiles.h"

//==============================================================================
// Vibration readout below the SPL meter (vibration spec §7.2): one row per axis
// plus the vector sum Σ, each with a log-scaled RMS bar, the interval peak tick,
// the maximum since reset, and the readout in the selected display unit.
class VibrationPanel  : public juce::Component
{
public:
    struct State
    {
        enum class Kind { Hidden, Message, Running };
        Kind              kind = Kind::Hidden;
        juce::String      message;          // Kind::Message: why there is no readout
        bool              messageIsError = false;
        juce::String      note;             // shown in the header, e.g. file mode
        vib::VibrationSnapshot snap;
        vib::DisplayUnit  unit = vib::DisplayUnit::Linear;
        double            tauSec = 1.0;
        juce::String      profileLabel;     // "Whole-body health (ISO 2631-1)", "Custom (based on …)"
        juce::String      mainLabel;        // symbol of snap.main, e.g. "a_w"
        double            k[3] { 1.0, 1.0, 1.0 };
        double            exposureHours = 8.0;   // for A(8)
    };

    std::function<void (vib::DisplayUnit)> onUnitChange;

    VibrationPanel()
    {
        unitCombo_.onChange = [this]
        {
            if (onUnitChange && unitCombo_.getSelectedId() > 0)
                onUnitChange ((vib::DisplayUnit) (unitCombo_.getSelectedId() - 1));
        };
        unitCombo_.setTooltip ("Display unit of the vibration readout");
        addAndMakeVisible (unitCombo_);
    }

    // Height the panel needs for the current state (0 when hidden)
    int preferredHeight() const noexcept
    {
        switch (state_.kind)
        {
            case State::Kind::Hidden:  return 0;
            case State::Kind::Message: return kPad * 2 + kHeaderH + kRowH;
            case State::Kind::Running: break;
        }
        return kPad * 2 + kHeaderH + kResultsH + numRows() * kRowH;
    }

    void setState (const State& s)
    {
        const bool unitItemsChanged = s.snap.quantity != state_.snap.quantity || ! unitItemsBuilt_;
        state_ = s;
        if (unitItemsChanged)
            rebuildUnitItems();
        if (! unitCombo_.isPopupActive())
            unitCombo_.setSelectedId ((int) vib::units::effective (s.unit, quantity()) + 1, juce::dontSendNotification);
        unitCombo_.setVisible (s.kind == State::Kind::Running);
        repaint();
    }

    void setLightMode (bool light)
    {
        light_ = light;
        unitCombo_.setColour (juce::ComboBox::backgroundColourId, light ? juce::Colour (0xffd1d1d6) : juce::Colour (0xff3a3a3c));
        unitCombo_.setColour (juce::ComboBox::textColourId,       light ? juce::Colour (0xff1c1c1e) : juce::Colours::white);
        unitCombo_.setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
        unitCombo_.setColour (juce::ComboBox::arrowColourId,      light ? juce::Colour (0xff6c6c70) : juce::Colour (0xffaeaeb2));
        repaint();
    }

    void resized() override
    {
        auto header = getLocalBounds().reduced (kMargin, 0).withTrimmedTop (kPad).withHeight (kHeaderH);
        unitCombo_.setBounds (header.removeFromRight (150).reduced (0, 2));
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour bg      = light_ ? juce::Colour (0xfff2f2f7) : juce::Colour (0xff1c1c1e);
        const juce::Colour card    = light_ ? juce::Colour (0xffe5e5ea) : juce::Colour (0xff2c2c2e);
        const juce::Colour barBg   = light_ ? juce::Colour (0xffcecece) : juce::Colour (0xff3a3a3c);
        const juce::Colour textPri = light_ ? juce::Colour (0xff1c1c1e) : juce::Colours::white;
        const juce::Colour textSec = light_ ? juce::Colour (0xff6c6c70) : juce::Colour (0xffaeaeb2);
        const juce::Colour bad     = juce::Colour (0xffff453a);

        g.fillAll (bg);
        if (state_.kind == State::Kind::Hidden)
            return;

        auto area = getLocalBounds().reduced (kMargin / 2, 2);
        g.setColour (card);
        g.fillRoundedRectangle (area.toFloat(), 8.0f);

        auto content = getLocalBounds().reduced (kMargin, 0).withTrimmedTop (kPad).withTrimmedBottom (kPad);
        auto header = content.removeFromTop (kHeaderH);
        if (unitCombo_.isVisible())
            header.removeFromRight (160);

        // The profile's main value, right-aligned in the header
        if (state_.kind == State::Kind::Running && state_.snap.mainKind != 0)
        {
            const auto q = quantity();
            auto mainArea = header.removeFromRight (300);
            const auto unitText = juce::String::fromUTF8 (vib::units::label (state_.unit, q));
            g.setColour (textSec);
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
            g.drawText (unitText, mainArea.removeFromRight (100), juce::Justification::centredLeft, true);
            const juce::Font valueFont (juce::FontOptions().withHeight (19.0f).withStyle ("Bold"));
            const auto valueText = (state_.snap.valid ? juce::String (vib::units::format (state_.snap.main, state_.unit, q))
                                                      : juce::String ("--")) + " ";
            const int valueW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (valueFont, valueText));
            g.setColour (textPri);
            g.setFont (valueFont);
            g.drawText (valueText, mainArea.removeFromRight (valueW), juce::Justification::centredRight, false);
            mainArea.removeFromRight (8);
            g.setColour (textSec);
            g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f).withStyle ("Bold")));
            g.drawText (state_.mainLabel, mainArea, juce::Justification::centredRight, true);
        }

        g.setColour (textSec);
        g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f).withStyle ("Bold")));
        g.drawText ("VIBRATION", header.removeFromLeft (90), juce::Justification::centredLeft, false);
        g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
        g.drawText (headerSummary(), header, juce::Justification::centredLeft, true);

        if (state_.kind == State::Kind::Running)
        {
            g.setColour (textSec);
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
            g.drawText (resultsLine(), content.removeFromTop (kResultsH).withTrimmedLeft (90),
                        juce::Justification::centredLeft, true);
        }

        if (state_.kind == State::Kind::Message)
        {
            g.setColour (state_.messageIsError ? bad : textSec);
            g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
            g.drawText (state_.message, content, juce::Justification::centredLeft, true);
            return;
        }

        // Columns: label | bar | RMS readout | pk max | p-p
        const auto q = quantity();
        double lo, hi;
        vib::units::barRangeDb (q, lo, hi);
        const bool wide = content.getWidth() > 900;
        for (const auto& row : rows())
        {
            auto r = content.removeFromTop (kRowH);
            const auto& v = state_.snap.axis[row.slot];

            g.setColour (row.colour);
            g.setFont (juce::Font (juce::FontOptions().withHeight (16.0f).withStyle ("Bold")));
            g.drawText (row.label, r.removeFromLeft (36), juce::Justification::centredLeft, false);

            auto p2pCol  = wide ? r.removeFromRight (170) : juce::Rectangle<int>();
            auto pkCol   = r.removeFromRight (wide ? 170 : 150);
            auto rmsCol  = r.removeFromRight (wide ? 230 : 190);
            auto bar     = r.reduced (0, 7).toFloat();

            // Bar: log scale, RMS fill + interval peak tick + max-since-reset marker
            g.setColour (barBg);
            g.fillRoundedRectangle (bar, 3.0f);
            auto toX = [&] (float si)
            {
                const double db = vib::units::levelDb (si, q);
                return bar.getX() + bar.getWidth() * (float) juce::jlimit (0.0, 1.0, (db - lo) / (hi - lo));
            };
            if (state_.snap.valid)
            {
                g.setColour (row.colour.withAlpha (0.85f));
                g.fillRoundedRectangle (bar.withRight (toX (v.rms)), 3.0f);
                g.setColour (row.colour);
                if (v.peak > 0.0f)
                    g.fillRect (juce::Rectangle<float> (toX (v.peak) - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
                if (v.peakMax > 0.0f)
                {
                    const float x = toX (v.peakMax);
                    juce::Path tri;
                    tri.addTriangle (x - 4.0f, bar.getY() - 1.0f, x + 4.0f, bar.getY() - 1.0f, x, bar.getY() + 5.0f);
                    g.setColour (textPri);
                    g.fillPath (tri);
                }
            }

            const auto unit = juce::String::fromUTF8 (vib::units::label (state_.unit, q));
            auto fmt = [&] (float si) { return state_.snap.valid ? juce::String (vib::units::format (si, state_.unit, q))
                                                                 : juce::String ("--"); };
            g.setColour (textPri);
            g.setFont (juce::Font (juce::FontOptions().withHeight (20.0f).withStyle ("Bold")));
            g.drawText (fmt (v.rms), rmsCol.removeFromLeft (rmsCol.getWidth() - (wide ? 110 : 90)),
                        juce::Justification::centredRight, false);
            g.setColour (textSec);
            g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
            g.drawText (" " + unit, rmsCol, juce::Justification::centredLeft, true);

            g.drawText ("max " + fmt (v.peakMax), pkCol.withTrimmedLeft (12), juce::Justification::centredLeft, true);
            if (wide && row.slot != vib::AxisSum)
                g.drawText ("p-p " + fmt (v.p2p), p2pCol, juce::Justification::centredLeft, true);
        }
    }

private:
    static constexpr int kMargin = 20, kPad = 8, kHeaderH = 24, kResultsH = 20, kRowH = 28;

    // Accumulated values for the profile's main value (spec §5.3, §6)
    juce::String resultsLine() const
    {
        const auto& s = state_.snap;
        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        juce::String text = "Measured " + juce::String (s.seconds, 1) + " s";
        if (! s.valid)
            return text;
        const auto q = quantity();
        const auto unit = juce::String::fromUTF8 (vib::units::label (state_.unit, q));
        auto value = [&] (float si) { return juce::String (vib::units::format (si, state_.unit, q)) + " " + unit; };
        const auto m = (vib::MainValue) s.mainKind;
        const auto sum = vib::summarize (s, m, state_.k);

        if (m == vib::MainValue::MaxAxisKAw || m == vib::MainValue::SumAw)
        {
            text << dot << "A(8) " << value ((float) vib::dailyExposureA8 (s.main, state_.exposureHours))
                 << " (T = " << juce::String (state_.exposureHours, state_.exposureHours < 10.0 ? 2 : 1) << " h)";
            if (q == vib::Quantity::Acceleration)
                text << dot << "VDV " << juce::String (sum.vdv, 3) << " m/s^1.75";
            text << dot << "MTVV " << value (sum.mtvv)
                 << dot << "crest factor " << juce::String (sum.crest, 1);
        }
        else if (m == vib::MainValue::MaxAxisKbFmax)
        {
            text << dot << "KB_FTr " << value (sum.kbFTr);
        }
        else if (m == vib::MainValue::MaxAxisPeakMax && sum.ppvAxis >= 0)
        {
            const char* axis[3] = { "X", "Y", "Z" };
            text << dot << "on " << (s.uniaxial ? "the axis" : axis[sum.ppvAxis])
                 << dot << "f_dom " << (sum.fDom > 0.0f ? juce::String (sum.fDom, 1) + " Hz" : juce::String ("--"));
        }
        return text;
    }

    struct Row { int slot; juce::String label; juce::Colour colour; };

    vib::Quantity quantity() const noexcept { return (vib::Quantity) state_.snap.quantity; }

    // Axis colours as in the timeline (spec §7.3): X brown, Y mint, Z slate, Σ neutral (keep in sync with LogComponent::colVib)
    std::vector<Row> rows() const
    {
        std::vector<Row> r;
        const auto& s = state_.snap;
        if (s.uniaxial)
        {
            const char* sym[3] = { "a", "v", "d" };
            r.push_back ({ vib::AxisX, sym[juce::jlimit (0, 2, s.quantity)], juce::Colour (0xffac8e68) });
            return r;
        }
        const juce::Colour cols[3] = { juce::Colour (0xffac8e68), juce::Colour (0xff63e6be), juce::Colour (0xff8fa8c8) };
        const char* names[3] = { "X", "Y", "Z" };
        int axes = 0;
        for (int a = 0; a < 3; ++a)
            if (s.axisMask & (1 << a)) { r.push_back ({ a, names[a], cols[a] }); ++axes; }
        if (axes > 1)
            r.push_back ({ vib::AxisSum, juce::String::fromUTF8 ("\xce\xa3"),
                           light_ ? juce::Colour (0xff1c1c1e) : juce::Colours::white });
        return r;
    }

    int numRows() const { return juce::jmax (1, (int) rows().size()); }

    juce::String headerSummary() const
    {
        if (state_.kind != State::Kind::Running)
            return state_.note;
        const auto& s = state_.snap;
        const char* qNames[3] = { "Acceleration", "Velocity", "Displacement" };
        juce::String text;
        if (state_.profileLabel.isNotEmpty())
            text << state_.profileLabel << juce::String::fromUTF8 ("  \xc2\xb7  ");
        text << qNames[juce::jlimit (0, 2, s.quantity)];

        juce::StringArray w;
        for (int a = 0; a < 3; ++a)
            if (s.axisMask & (1 << a))
                w.add (weightingShort (s.weighting[a]));
        w.removeDuplicates (false);
        text << juce::String::fromUTF8 ("  \xc2\xb7  ") << w.joinIntoString (" / ")
             << juce::String::fromUTF8 ("  \xc2\xb7  RMS \xcf\x84 ") << juce::String (state_.tauSec, state_.tauSec < 1.0 ? 3 : 0) << " s";
        if (state_.note.isNotEmpty())
            text << juce::String::fromUTF8 ("  \xc2\xb7  ") << state_.note;
        return text;
    }

    static juce::String weightingShort (int w)
    {
        const char* names[] = { "unweighted", "band limit", "Wb", "Wc", "Wd", "We", "Wf", "Wh", "Wj", "Wk", "Wm", "KB" };
        return (w >= 0 && w < (int) std::size (names)) ? juce::String (names[w]) : juce::String();
    }

    void rebuildUnitItems()
    {
        unitItemsBuilt_ = true;
        const auto q = quantity();
        unitCombo_.clear (juce::dontSendNotification);
        unitCombo_.addItem (juce::String::fromUTF8 (vib::units::label (vib::DisplayUnit::Linear, q)), (int) vib::DisplayUnit::Linear + 1);
        if (q == vib::Quantity::Acceleration)
            unitCombo_.addItem ("g", (int) vib::DisplayUnit::G + 1);
        unitCombo_.addItem (juce::String::fromUTF8 (vib::units::label (vib::DisplayUnit::Db, q)), (int) vib::DisplayUnit::Db + 1);
    }

    State state_;
    bool light_ = false;
    bool unitItemsBuilt_ = false;
    juce::ComboBox unitCombo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VibrationPanel)
};
