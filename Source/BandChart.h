#pragma once

#include <JuceHeader.h>
#include <vector>

/**
 * Lightweight bar-chart component that visualises per-band NSIM scores.
 * Colours follow SPLMeter's dark / light theme.
 */
class BandChart : public juce::Component
{
public:
    BandChart() { setOpaque (false); }

    void setBands (const std::vector<double>& fvnsim,
                   const std::vector<double>& centerFreqs)
    {
        jassert (fvnsim.size() == centerFreqs.size());
        fvnsim_      = fvnsim;
        centerFreqs_ = centerFreqs;
        repaint();
    }

    void clear()
    {
        fvnsim_.clear();
        centerFreqs_.clear();
        repaint();
    }

    void setLightMode (bool light)
    {
        lightMode_ = light;
        repaint();
    }

    // -----------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        // SPLMeter palette
        const juce::Colour bg       = lightMode_ ? juce::Colour (0xffe5e5ea) : juce::Colour (0xff2c2c2e);
        const juce::Colour gridLine = lightMode_ ? juce::Colour (0xffc7c7cc) : juce::Colour (0xff48484a);
        const juce::Colour textCol  = lightMode_ ? juce::Colour (0xff6c6c70) : juce::Colour (0xffaeaeb2);
        const juce::Colour axisCol  = lightMode_ ? juce::Colour (0xff1c1c1e) : juce::Colours::white;

        auto bounds = getLocalBounds().toFloat();

        g.setColour (bg);
        g.fillRoundedRectangle (bounds, 6.0f);

        if (fvnsim_.empty())
        {
            g.setColour (textCol);
            g.setFont (14.0f);
            g.drawText ("Run an analysis to see per-band scores",
                        bounds, juce::Justification::centred, true);
            return;
        }

        const int   n         = static_cast<int> (fvnsim_.size());
        const float padLeft   = 44.0f;
        const float padRight  = 10.0f;
        const float padTop    = 10.0f;
        const float padBottom = 36.0f;
        const float chartW    = bounds.getWidth()  - padLeft - padRight;
        const float chartH    = bounds.getHeight() - padTop  - padBottom;
        const float barW      = chartW / static_cast<float> (n);
        const float barGap    = barW * 0.15f;

        // Y-axis grid lines (0, 0.25, 0.5, 0.75, 1.0)
        for (int tick = 0; tick <= 4; ++tick)
        {
            float frac = tick / 4.0f;
            float y    = padTop + chartH * (1.0f - frac);

            g.setColour (gridLine);
            g.drawHorizontalLine (static_cast<int> (y), padLeft, padLeft + chartW);

            g.setColour (textCol);
            g.setFont (10.0f);
            g.drawText (juce::String (frac, 2),
                        0, static_cast<int> (y) - 6,
                        static_cast<int> (padLeft) - 4, 12,
                        juce::Justification::centredRight);
        }

        // Bars — use SPLMeter's green / orange / red accent colours
        for (int i = 0; i < n; ++i)
        {
            float val  = static_cast<float> (juce::jlimit (0.0, 1.0, fvnsim_[i]));
            float barH = chartH * val;
            float x    = padLeft + i * barW + barGap * 0.5f;
            float y    = padTop + chartH - barH;
            float w    = barW - barGap;

            juce::Colour barColour;
            if      (val >= 0.75f) barColour = juce::Colour (0xff34c759); // green
            else if (val >= 0.5f)  barColour = juce::Colour (0xffff9f0a); // orange
            else                   barColour = juce::Colour (0xffff453a); // red

            g.setColour (barColour);
            g.fillRoundedRectangle (x, y, w, barH, 2.0f);

            if (i < static_cast<int> (centerFreqs_.size()))
            {
                double freq = centerFreqs_[i];
                juce::String label = freq >= 1000.0
                    ? juce::String (freq / 1000.0, 1) + "k"
                    : juce::String (static_cast<int> (freq));

                g.setColour (textCol);
                g.setFont (9.0f);
                g.drawText (label,
                            static_cast<int> (x),
                            static_cast<int> (padTop + chartH + 4),
                            static_cast<int> (w), 14,
                            juce::Justification::centred);
            }
        }

        // Axis labels
        g.setColour (axisCol);
        g.setFont (11.0f);
        g.drawText ("NSIM",
                    0, static_cast<int> (padTop + chartH * 0.5f) - 20,
                    static_cast<int> (padLeft) - 4, 40,
                    juce::Justification::centredRight);
        g.drawText ("Frequency (Hz)",
                    static_cast<int> (padLeft),
                    static_cast<int> (padTop + chartH + 20),
                    static_cast<int> (chartW), 14,
                    juce::Justification::centred);
    }

private:
    std::vector<double> fvnsim_;
    std::vector<double> centerFreqs_;
    bool                lightMode_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandChart)
};
