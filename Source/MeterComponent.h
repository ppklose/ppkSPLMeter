#pragma once
#include <JuceHeader.h>

//==============================================================================
/**
    Draws a horizontal peak meter bar on a 20-130 dB SPL scale.
    Clicking one of the three readout labels (dB SPL / dBA SPL / dBC SPL)
    selects which value drives the bar and hold indicator.
*/
class MeterComponent  : public juce::Component
{
public:
    enum class Band { SPL = 0, DBA, DBC };

    MeterComponent();

    void paint   (juce::Graphics&) override;
    void resized () override {}
    void mouseDown (const juce::MouseEvent&) override;

    void setValues (float peakSPL, float peakDBASPL, float peakDBCSPL,
                    float roughness, float fluctuation,
                    float sharpness, float sone, float psychoAnnoyance,
                    float impulsiveness, float tonality);

    void setPsychoVisible (bool v) noexcept
    {
        if (psychoVisible_ != v) { psychoVisible_ = v; repaint(); }
    }

    void reset() noexcept
    {
        peakSPL_ = peakDBASPL_ = peakDBCSPL_ = kMin;
        roughness_ = fluctuation_ = sharpness_ = sone_ = psychoAnnoyance_ = 0.0f;
        impulsiveness_ = tonality_ = 0.0f;
        holdVal_ = kMin;
        holdTimestampMs_ = 0.0;
        repaint();
    }

    void setHoldDuration (double ms) noexcept { holdDurationMs_ = ms; }

    void setLightMode (bool light) noexcept
    {
        if (lightMode_ != light) { lightMode_ = light; repaint(); }
    }

private:
    float peakSPL_    = 0.0f;
    float peakDBASPL_ = 0.0f;
    float peakDBCSPL_ = 0.0f;
    float roughness_        = 0.0f;
    float fluctuation_      = 0.0f;
    float sharpness_        = 0.0f;
    float sone_             = 0.0f;
    float psychoAnnoyance_  = 0.0f;
    float impulsiveness_    = 0.0f;
    float tonality_         = 0.0f;

    bool  lightMode_        = false;
    bool  psychoVisible_    = true;
    Band  selectedBand_     = Band::SPL;
    float holdVal_          = 0.0f;
    double holdTimestampMs_ = 0.0;
    double holdDurationMs_  = 2000.0;

    // hit-test rects for the three readout labels (set during paint)
    juce::Rectangle<int> readoutRects_[3];

    static constexpr float kMin = 20.0f;
    static constexpr float kMax = 130.0f;

    float selectedValue() const noexcept
    {
        switch (selectedBand_)
        {
            case Band::DBA: return peakDBASPL_;
            case Band::DBC: return peakDBCSPL_;
            default:        return peakSPL_;
        }
    }

    float splToX (float spl, float totalWidth) const noexcept;
    juce::Colour colourForSPL (float spl) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterComponent)
};
