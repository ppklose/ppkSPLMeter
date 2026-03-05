#include "MeterComponent.h"

MeterComponent::MeterComponent()
{
    setOpaque (true);
}

//==============================================================================
float MeterComponent::splToX (float spl, float totalWidth) const noexcept
{
    float t = (spl - kMin) / (kMax - kMin);
    t = juce::jlimit (0.0f, 1.0f, t);
    return t * totalWidth;
}

juce::Colour MeterComponent::colourForSPL (float spl) const noexcept
{
    if (spl >= 100.0f) return juce::Colour (0xffff3b30);   // red
    if (spl >= 85.0f)  return juce::Colour (0xffff9500);   // amber
    return juce::Colour (0xff34c759);                        // green
}

//==============================================================================
void MeterComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    const juce::Colour bgMain      = lightMode_ ? juce::Colour (0xfff2f2f7) : juce::Colour (0xff1c1c1e);
    const juce::Colour bgBar       = lightMode_ ? juce::Colour (0xffcecece) : juce::Colour (0xff2c2c2e);
    const juce::Colour textPrimary = lightMode_ ? juce::Colour (0xff1c1c1e) : juce::Colours::white;
    const juce::Colour textSecond  = lightMode_ ? juce::Colour (0xff6c6c70) : juce::Colour (0xffaeaeb2);
    const juce::Colour tickColour  = lightMode_ ? juce::Colour (0x99000000) : juce::Colour (0x99ffffff);

    g.fillAll (bgMain);

    const float margin   = 20.0f;
    const float barH     = 56.0f;
    const float readoutH = 40.0f;
    const float labelW   = 112.0f;

    const float barAreaX = margin;
    const float barAreaW = bounds.getWidth() - margin * 2.0f;

    // ---- Readout + bar ----
    const float peakReadoutY = margin;
    const float peakY        = peakReadoutY + readoutH + 4.0f;
    {
        // Three clickable readout segments (dB SPL / dBA SPL / dBC SPL)
        // Fixed compact width so the values sit close together
        const int   segW    = 160;
        const int   sepW    = 24;   // width of "  |  " separator
        struct { const char* unit; float value; Band band; } segs[3] = {
            { "dB SPL",  peakSPL_,    Band::SPL },
            { "dBA SPL", peakDBASPL_, Band::DBA },
            { "dBC SPL", peakDBCSPL_, Band::DBC },
        };
        int curX = static_cast<int> (barAreaX);
        for (int i = 0; i < 3; ++i)
        {
            bool sel = (selectedBand_ == segs[i].band);
            auto rect = juce::Rectangle<int> (curX, static_cast<int> (peakReadoutY),
                                              segW,  static_cast<int> (readoutH));
            readoutRects_[i] = rect;

            auto fo = juce::FontOptions().withHeight (22.0f);
            if (sel) fo = fo.withStyle ("Bold");
            g.setFont (juce::Font (fo));
            g.setColour (sel ? textPrimary : textSecond);
            g.drawText (juce::String (segs[i].value, 1) + " " + segs[i].unit,
                        rect, juce::Justification::centredLeft, false);

            curX += segW;

            // separator between segments
            if (i < 2)
            {
                g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
                g.setColour (juce::Colour (0xff48484a));
                g.drawText ("  |  ", curX, static_cast<int> (peakReadoutY),
                            sepW, static_cast<int> (readoutH), juce::Justification::centred, false);
                curX += sepW;
            }
        }

        // background track
        g.setColour (bgBar);
        g.fillRoundedRectangle (barAreaX, peakY, barAreaW, barH, 4.0f);

        // bar fill — driven by selected band
        float fillW = splToX (selectedValue(), barAreaW);
        if (fillW > 0.0f)
        {
            const float z1 = splToX ( 40.0f, barAreaW);   // blue   → green
            const float z2 = splToX ( 80.0f, barAreaW);   // green  → yellow
            const float z3 = splToX ( 96.0f, barAreaW);   // yellow → orange
            const float z4 = splToX (106.0f, barAreaW);   // orange → red

            struct Zone { float start, end; juce::Colour colour; };
            const Zone zones[] = {
                { 0.0f, z1, juce::Colour (0xff5ac8fa) },   // blue
                { z1,   z2, juce::Colour (0xff34c759) },   // green
                { z2,   z3, juce::Colour (0xffffd60a) },   // yellow
                { z3,   z4, juce::Colour (0xffff9500) },   // orange
                { z4,   barAreaW, juce::Colour (0xffff3b30) }, // red
            };

            bool firstSegment = true;
            for (const auto& zone : zones)
            {
                if (fillW <= zone.start) break;
                float segW = std::min (fillW, zone.end) - zone.start;
                if (segW <= 0.0f) continue;
                g.setColour (zone.colour);
                if (firstSegment)
                {
                    g.fillRoundedRectangle (barAreaX + zone.start, peakY, segW, barH, 4.0f);
                    firstSegment = false;
                }
                else
                {
                    g.fillRect (barAreaX + zone.start, peakY, segW, barH);
                }
            }
        }

        // ---- Peak hold indicator ----
        float holdX = barAreaX + splToX (holdVal_, barAreaW);
        g.setColour (textPrimary);
        g.fillRect (holdX - 2.0f, peakY, 4.0f, barH);

        // ---- dB scale overlaid on bar ----
        g.setFont (juce::Font (juce::FontOptions().withHeight (16.0f)));
        for (float db = kMin; db <= kMax; db += 10.0f)
        {
            float x = barAreaX + splToX (db, barAreaW);
            // tick mark
            g.setColour (tickColour);
            g.drawVerticalLine (static_cast<int>(x), peakY, peakY + barH);
            // label centred on the bar
            g.setColour (textPrimary);
            g.drawText (juce::String (static_cast<int>(db)),
                        static_cast<int>(x) - 24, static_cast<int>(peakY),
                        48, static_cast<int>(barH),
                        juce::Justification::centred, false);
        }

        // ---- 94 dB SPL reference marker ----
        {
            float x94 = barAreaX + splToX (94.0f, barAreaW);
            g.setColour (juce::Colour (0xffff3b30));
            g.drawVerticalLine (static_cast<int>(x94), peakY, peakY + barH);
        }
    }

    // ---- Psychoacoustic rows (left-aligned label + value) ----
    const float roughnessY  = peakY + barH + 10.0f;
    const float sharpnessY  = roughnessY + 34.0f;
    const float col2X       = bounds.getWidth() / 2.0f;
    const float boldW       = 130.0f;   // width reserved for bold label
    const float valW        = col2X - margin - boldW - 8.0f;

    auto drawPsychoRow = [&] (float x, float y,
                               const juce::String& label,
                               const juce::String& value)
    {
        g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f).withStyle ("Bold")));
        g.setColour (textPrimary);
        g.drawText (label, static_cast<int>(x), static_cast<int>(y),
                    static_cast<int>(boldW), 28, juce::Justification::centredLeft, false);

        g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
        g.setColour (textSecond);
        g.drawText (value, static_cast<int>(x + boldW + 8.0f), static_cast<int>(y),
                    static_cast<int>(valW), 28, juce::Justification::centredLeft, false);
    };

    if (psychoVisible_)
    {
        drawPsychoRow (margin,  roughnessY, "Roughness",   juce::String (roughness_,  1) + " %  (15\xe2\x80\x93" "300 Hz)");
        drawPsychoRow (col2X,   roughnessY, "Fluctuation", juce::String (fluctuation_, 1) + " %  (0.5\xe2\x80\x93" "20 Hz)");
        drawPsychoRow (margin,  sharpnessY, "Sharpness",   juce::String (sharpness_,  2) + " acum (approx.)");
        drawPsychoRow (col2X,   sharpnessY, "Loudness",    juce::String (sone_,       2) + " sone");
    }

}

//==============================================================================
void MeterComponent::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 3; ++i)
    {
        if (readoutRects_[i].contains (e.getPosition()))
        {
            selectedBand_    = static_cast<Band> (i);
            holdVal_         = kMin;
            holdTimestampMs_ = 0.0;
            repaint();
            return;
        }
    }
}

//==============================================================================
void MeterComponent::setValues (float peakSPL, float peakDBASPL, float peakDBCSPL,
                                 float roughness, float fluctuation,
                                 float sharpness, float sone)
{
    peakSPL_     = peakSPL;
    peakDBASPL_  = peakDBASPL;
    peakDBCSPL_  = peakDBCSPL;
    roughness_   = roughness;
    fluctuation_ = fluctuation;
    sharpness_   = sharpness;
    sone_        = sone;

    double now = juce::Time::getMillisecondCounterHiRes();
    float selVal = selectedValue();
    if (selVal >= holdVal_ || (now - holdTimestampMs_) > holdDurationMs_)
    {
        holdVal_         = selVal;
        holdTimestampMs_ = now;
    }

    repaint();
}
