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

    bool isOff()        const noexcept { return selectedMetric == PsychoMetric::Off; }
    void setFftEnabled (bool e) noexcept { fftEnabled_ = e; repaint(); }

private:
    void timerCallback() override;
    void computeFftBands();
    void drawFftOverlay (juce::Graphics&, const juce::Rectangle<float>& plot);

    SPLMeterAudioProcessor& processor;

    juce::Label  durationLabel;
    juce::Slider durationSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> durationAttachment;

    juce::ToggleButton splVisButton  { "dB SPL"  };
    juce::ToggleButton dbaVisButton  { "dBA SPL" };
    juce::ToggleButton dbcVisButton  { "dBC SPL" };

    // 1/3-octave FFT
    static constexpr int   kFftOrder   = 13;           // 2^13 = 8192
    static constexpr int   kFftSize    = 1 << kFftOrder;
    static constexpr int   kNumFftBands = 31;
    static const float     kFftBandCenters[kNumFftBands];

    juce::dsp::FFT                  fft_        { kFftOrder };
    std::array<float, kFftSize>     hannWindow_ {};
    std::array<float, kFftSize * 2> fftBuffer_  {};
    float                           fftBands_[kNumFftBands] {};
    bool                            fftEnabled_ = false;

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
