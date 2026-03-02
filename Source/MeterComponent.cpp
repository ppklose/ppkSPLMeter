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
    g.fillAll (juce::Colour (0xff1c1c1e));

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
        // numeric readout ABOVE the bar
        g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
        g.setColour (juce::Colour (0xffaeaeb2));
        juce::String peakStr = juce::String (peakSPL_, 1) + " dB SPL  |  "
                             + juce::String (peakDBASPL_, 1) + " dBA SPL  |  "
                             + juce::String (peakDBCSPL_, 1) + " dBC SPL";
        g.drawText (peakStr, static_cast<int>(barAreaX), static_cast<int>(peakReadoutY),
                    static_cast<int>(barAreaW), static_cast<int>(readoutH),
                    juce::Justification::centredLeft, false);

        // background track
        g.setColour (juce::Colour (0xff2c2c2e));
        g.fillRoundedRectangle (barAreaX, peakY, barAreaW, barH, 4.0f);

        // bar fill — colour zones (all values in dB SPL):
        //   20– 40 : blue
        //   40– 80 : green
        //   80– 96 : yellow
        //   96–106 : orange
        //  106–130 : red
        float fillW = splToX (peakSPL_, barAreaW);
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
        float holdX = barAreaX + splToX (holdSPL_, barAreaW);
        g.setColour (juce::Colours::white);
        g.fillRect (holdX - 2.0f, peakY, 4.0f, barH);

        // ---- dB scale overlaid on bar ----
        g.setFont (juce::Font (juce::FontOptions().withHeight (16.0f)));
        for (float db = kMin; db <= kMax; db += 10.0f)
        {
            float x = barAreaX + splToX (db, barAreaW);
            // tick mark
            g.setColour (juce::Colour (0x99ffffff));
            g.drawVerticalLine (static_cast<int>(x), peakY, peakY + barH);
            // label centred on the bar
            g.setColour (juce::Colours::white);
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
        g.setColour (juce::Colours::white);
        g.drawText (label, static_cast<int>(x), static_cast<int>(y),
                    static_cast<int>(boldW), 28, juce::Justification::centredLeft, false);

        g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
        g.setColour (juce::Colour (0xffaeaeb2));
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
    if (peakSPL_ >= holdSPL_ || (now - holdTimestampMs_) > holdDurationMs_)
    {
        holdSPL_          = peakSPL_;
        holdTimestampMs_  = now;
    }

    repaint();
}
