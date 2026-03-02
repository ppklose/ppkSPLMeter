#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class LogComponent  : public juce::Component,
                      private juce::Timer
{
public:
    explicit LogComponent (SPLMeterAudioProcessor&);
    ~LogComponent() override;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isOff()           const noexcept { return selectedMetric == PsychoMetric::Off; }
    bool isSpectroEnabled() const noexcept { return spectroEnableButton.getToggleState(); }

private:
    void timerCallback() override;
    void drawLegend (juce::Graphics&, const juce::Rectangle<float>& strip);

    SPLMeterAudioProcessor& processor;

    juce::Label  durationLabel;
    juce::Slider durationSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> durationAttachment;

    juce::Label        gainLabel;
    juce::Slider       gainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    juce::ToggleButton spectroEnableButton { "Spectrogram" };

    std::vector<LogEntry> rows;

    // Selected psychoacoustic metric
    enum class PsychoMetric { Roughness = 0, Fluctuation, Sharpness, Loudness, Off };
    PsychoMetric selectedMetric = PsychoMetric::Roughness;


    // Hit-test rects for metric selectors (set during paint, read in mouseDown)
    juce::Rectangle<float> metricSelectRects[5];

    // Graph constants
    static constexpr float kYMin = 20.0f;
    static constexpr float kYMax = 130.0f;

    // Colours — SPL series
    static const juce::Colour colPeakSPL;
    static const juce::Colour colRmsSPL;
    static const juce::Colour colPeakDBA;
    static const juce::Colour colRmsDBA;
    static const juce::Colour colPeakDBC;
    static const juce::Colour colRmsDBC;

    // Colours — psychoacoustic series
    static const juce::Colour colRoughness;
    static const juce::Colour colFluctuation;
    static const juce::Colour colSharpness;
    static const juce::Colour colLoudness;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogComponent)
};
