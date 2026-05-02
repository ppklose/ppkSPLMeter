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

        // ---- LAeq / LCeq after a double-pipe separator ----
        {
            const int dpW = 32;   // width of "  ‖  " separator
            g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
            g.setColour (juce::Colour (0xff48484a));
            g.drawText (juce::String::charToString (0x2016),  // ‖
                        curX, static_cast<int> (peakReadoutY),
                        dpW, static_cast<int> (readoutH), juce::Justification::centred, false);
            curX += dpW;

            const int leqW = 170;
            auto drawLeq = [&] (const char* unit, float value)
            {
                g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
                g.setColour (textSecond);
                g.drawText (juce::String (value, 1) + " " + unit,
                            curX, static_cast<int> (peakReadoutY),
                            leqW, static_cast<int> (readoutH),
                            juce::Justification::centredLeft, false);
                curX += leqW;
            };

            drawLeq ("LAeq", laeq_);

            // thin pipe between LAeq and LCeq
            g.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
            g.setColour (juce::Colour (0xff48484a));
            g.drawText ("  |  ", curX, static_cast<int> (peakReadoutY),
                        sepW, static_cast<int> (readoutH), juce::Justification::centred, false);
            curX += sepW;

            drawLeq ("LCeq", lceq_);
        }

        // background track
        g.setColour (bgBar);
        g.fillRoundedRectangle (barAreaX, peakY, barAreaW, barH, 4.0f);

        // bar fill - driven by selected band
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

    // ---- DIN 15905-5 strip: LAeq(30min) + LCpeak with compliance status ----
    // Anchored to the bottom of the meter, below the psychoacoustic table and
    // above the log component's axis pickers. Hidden in Basic mode.
    const float dinH = 22.0f;
    const float dinY = bounds.getBottom() - dinH - 6.0f;
    if (dinVisible_)
    {
        const bool laeqValid   = laeq30Min_ > -100.0f;
        const bool lcpeakValid = lcPeak_    > -100.0f;

        // Threshold colours per DIN 15905-5 (LAeq,30min ≤ 99, LCpeak ≤ 135)
        auto laeqCol = [&] () -> juce::Colour
        {
            if (! laeqValid)               return textSecond;
            if (laeq30Min_ >= 99.0f)        return juce::Colour (0xffff3b30); // red
            if (laeq30Min_ >= 95.0f)        return juce::Colour (0xffffd60a); // yellow
            return juce::Colour (0xff34c759);                                  // green
        }();
        auto lcpkCol = [&] () -> juce::Colour
        {
            if (! lcpeakValid)             return textSecond;
            if (lcPeak_   >= 135.0f)        return juce::Colour (0xffff3b30);
            if (lcPeak_   >= 130.0f)        return juce::Colour (0xffffd60a);
            return juce::Colour (0xff34c759);
        }();

        const bool overLimit = (laeqValid   && laeq30Min_ >= 99.0f)
                            || (lcpeakValid && lcPeak_    >= 135.0f);
        const bool warn      = ! overLimit
                            && ((laeqValid   && laeq30Min_ >= 95.0f)
                             || (lcpeakValid && lcPeak_    >= 130.0f));
        const juce::Colour statusCol = overLimit ? juce::Colour (0xffff3b30)
                                     : warn      ? juce::Colour (0xffffd60a)
                                                 : juce::Colour (0xff34c759);
        const char* statusText = overLimit ? "LIMIT" : warn ? "WARN" : "OK";

        // Header label
        g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f).withStyle ("Bold")));
        g.setColour (textSecond);
        int curX = static_cast<int> (barAreaX);
        const int hdrW = 110;
        g.drawText ("DIN 15905-5",
                    curX, static_cast<int> (dinY),
                    hdrW, static_cast<int> (dinH),
                    juce::Justification::centredLeft, false);
        curX += hdrW;

        // LAeq,30min readout
        const int laeqW = 240;
        g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
        g.setColour (textSecond);
        g.drawText ("LAeq(30min):",
                    curX, static_cast<int> (dinY),
                    104, static_cast<int> (dinH),
                    juce::Justification::centredLeft, false);
        g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f).withStyle ("Bold")));
        g.setColour (laeqCol);
        g.drawText (laeqValid ? juce::String (laeq30Min_, 1) + " dB(A)" : juce::String ("-- dB(A)"),
                    curX + 104, static_cast<int> (dinY),
                    laeqW - 104, static_cast<int> (dinH),
                    juce::Justification::centredLeft, false);
        curX += laeqW;

        // LCpeak readout
        const int lcpkW = 220;
        g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
        g.setColour (textSecond);
        g.drawText ("LCpeak:",
                    curX, static_cast<int> (dinY),
                    72, static_cast<int> (dinH),
                    juce::Justification::centredLeft, false);
        g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f).withStyle ("Bold")));
        g.setColour (lcpkCol);
        g.drawText (lcpeakValid ? juce::String (lcPeak_, 1) + " dB(C)" : juce::String ("-- dB(C)"),
                    curX + 72, static_cast<int> (dinY),
                    lcpkW - 72, static_cast<int> (dinH),
                    juce::Justification::centredLeft, false);
        curX += lcpkW;

        // Compliance status pill
        const int pillW = 64;
        auto pill = juce::Rectangle<float> (static_cast<float> (curX),
                                            dinY + 2.0f,
                                            static_cast<float> (pillW),
                                            dinH - 4.0f);
        g.setColour (statusCol);
        g.fillRoundedRectangle (pill, 4.0f);
        g.setColour (lightMode_ ? juce::Colour (0xff1c1c1e) : juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f).withStyle ("Bold")));
        g.drawText (statusText, pill.toNearestInt(),
                    juce::Justification::centred, false);
        curX += pillW;

        // ---- NIOSH REL section ----
        // Double-pipe separator before NIOSH
        const int dpW = 28;
        g.setFont (juce::Font (juce::FontOptions().withHeight (16.0f)));
        g.setColour (juce::Colour (0xff48484a));
        g.drawText (juce::String::charToString (0x2016),
                    curX, static_cast<int> (dinY),
                    dpW, static_cast<int> (dinH),
                    juce::Justification::centred, false);
        curX += dpW;

        // NIOSH header
        g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f).withStyle ("Bold")));
        g.setColour (textSecond);
        const int niHdrW = 56;
        g.drawText ("NIOSH",
                    curX, static_cast<int> (dinY),
                    niHdrW, static_cast<int> (dinH),
                    juce::Justification::centredLeft, false);
        curX += niHdrW;

        // Dose colour (NIOSH thresholds: 50 % = WARN, 100 % = LIMIT)
        const float dose = noiseDosePct_;
        const juce::Colour doseCol = (dose >= 100.0f) ? juce::Colour (0xffff3b30)
                                   : (dose >= 50.0f)  ? juce::Colour (0xffffd60a)
                                                      : juce::Colour (0xff34c759);

        const int doseW = 140;
        g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
        g.setColour (textSecond);
        g.drawText ("Dose:",
                    curX, static_cast<int> (dinY),
                    44, static_cast<int> (dinH),
                    juce::Justification::centredLeft, false);
        g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f).withStyle ("Bold")));
        g.setColour (doseCol);
        g.drawText (juce::String (dose, dose < 10.0f ? 2 : (dose < 100.0f ? 1 : 0)) + " %",
                    curX + 44, static_cast<int> (dinY),
                    doseW - 44, static_cast<int> (dinH),
                    juce::Justification::centredLeft, false);
        curX += doseW;

        // NIOSH compliance pill: worst of dose ≥ 100 % or LCpeak ≥ 140 dB(C)
        const bool nioshOver = (dose >= 100.0f) || (lcpeakValid && lcPeak_ >= 140.0f);
        const bool nioshWarn = ! nioshOver
                            && ((dose >= 50.0f) || (lcpeakValid && lcPeak_ >= 135.0f));
        const juce::Colour niStatusCol = nioshOver ? juce::Colour (0xffff3b30)
                                       : nioshWarn ? juce::Colour (0xffffd60a)
                                                   : juce::Colour (0xff34c759);
        const char* niStatusText = nioshOver ? "LIMIT" : nioshWarn ? "WARN" : "OK";

        auto niPill = juce::Rectangle<float> (static_cast<float> (curX),
                                              dinY + 2.0f,
                                              static_cast<float> (pillW),
                                              dinH - 4.0f);
        g.setColour (niStatusCol);
        g.fillRoundedRectangle (niPill, 4.0f);
        g.setColour (lightMode_ ? juce::Colour (0xff1c1c1e) : juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f).withStyle ("Bold")));
        g.drawText (niStatusText, niPill.toNearestInt(),
                    juce::Justification::centred, false);
    }

    // ---- Psychoacoustic table (2-column grid, 4 rows) ----
    const float tableY   = peakY + barH + 12.0f;
    const float rowH     = 20.0f;
    const float rowGap   = 6.0f;
    const float colW     = barAreaW / 2.0f;
    const float lblW     = 118.0f;

    // Unified cell renderer: col 0 or 1, row index
    auto drawCell = [&] (int col, int row,
                          const juce::String& label,
                          const juce::String& value)
    {
        const float x  = margin + col * colW;
        const float y  = tableY + row * (rowH + rowGap);
        const float vW = colW - lblW - 4.0f;

        g.setFont (juce::Font (juce::FontOptions().withHeight (15.0f).withStyle ("Bold")));
        g.setColour (textSecond);
        g.drawText (label, static_cast<int>(x), static_cast<int>(y),
                    static_cast<int>(lblW), static_cast<int>(rowH),
                    juce::Justification::centredLeft, false);

        g.setFont (juce::Font (juce::FontOptions().withHeight (15.0f)));
        g.setColour (textPrimary);
        g.drawText (value, static_cast<int>(x + lblW), static_cast<int>(y),
                    static_cast<int>(vW), static_cast<int>(rowH),
                    juce::Justification::centredLeft, false);
    };

    if (psychoVisible_)
    {
        drawCell (0, 0, "Roughness",      juce::String (roughness_,      1) + " %");
        drawCell (1, 0, "Fluctuation",    juce::String (fluctuation_,    1) + " %");
        drawCell (0, 1, "Sharpness",      juce::String (sharpness_,      2) + " acum");
        drawCell (1, 1, "Loudness",       juce::String (sone_,           2) + " sone");
        drawCell (0, 2, "Impulsiveness",  juce::String (impulsiveness_,  1) + " dB");
        drawCell (1, 2, "Tonality",       juce::String (tonality_,       1) + " %");
        drawCell (0, 3, "Annoyance",      juce::String (psychoAnnoyance_, 2));
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
                                 float sharpness, float sone, float psychoAnnoyance,
                                 float impulsiveness, float tonality)
{
    peakSPL_          = peakSPL;
    peakDBASPL_       = peakDBASPL;
    peakDBCSPL_       = peakDBCSPL;
    roughness_        = roughness;
    fluctuation_      = fluctuation;
    sharpness_        = sharpness;
    sone_             = sone;
    psychoAnnoyance_  = psychoAnnoyance;
    impulsiveness_    = impulsiveness;
    tonality_         = tonality;

    double now = juce::Time::getMillisecondCounterHiRes();
    float selVal = selectedValue();
    if (selVal >= holdVal_ || (now - holdTimestampMs_) > holdDurationMs_)
    {
        holdVal_         = selVal;
        holdTimestampMs_ = now;
    }

    repaint();
}
