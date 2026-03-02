#pragma once
#include <JuceHeader.h>

//==============================================================================
/**
    Draws a horizontal Peak meter bar on a 20–130 dB SPL scale.
    Both the raw dB SPL and the A-weighted dBA SPL values are shown numerically.
*/
class MeterComponent  : public juce::Component
{
public:
    MeterComponent();

    void paint (juce::Graphics&) override;
    void resized() override {}

    void setValues (float peakSPL, float peakDBASPL, float peakDBCSPL,
                    float roughness, float fluctuation,
                    float sharpness, float sone);

    void setPsychoVisible (bool v) noexcept
    {
        if (psychoVisible_ != v) { psychoVisible_ = v; repaint(); }
    }

    void reset() noexcept
    {
        peakSPL_ = peakDBASPL_ = peakDBCSPL_ = kMin;
        roughness_ = fluctuation_ = sharpness_ = sone_ = 0.0f;
        holdSPL_ = kMin;
        holdTimestampMs_ = 0.0;
        repaint();
    }

private:
    float peakSPL_    = 0.0f;
    float peakDBASPL_ = 0.0f;
    float peakDBCSPL_ = 0.0f;
    float roughness_   = 0.0f;
    float fluctuation_ = 0.0f;
    float sharpness_  = 0.0f;
    float sone_       = 0.0f;

    bool  psychoVisible_      = true;
    float holdSPL_            = 0.0f;
    double holdTimestampMs_   = 0.0;
    double holdDurationMs_    = 2000.0;

public:
    void setHoldDuration (double ms) noexcept { holdDurationMs_ = ms; }

private:

    static constexpr float kMin = 20.0f;
    static constexpr float kMax = 130.0f;

    float splToX (float spl, float totalWidth) const noexcept;
    juce::Colour colourForSPL (float spl) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterComponent)
};
