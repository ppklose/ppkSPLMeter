#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MeterComponent.h"
#include "LogComponent.h"
#include "SpectrogramComponent.h"

//==============================================================================
class SPLMeterAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit SPLMeterAudioProcessorEditor (SPLMeterAudioProcessor&);
    ~SPLMeterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SPLMeterAudioProcessor& audioProcessor;

    MeterComponent       meter;
    LogComponent         log;
    SpectrogramComponent spectrogram;


    juce::TooltipWindow tooltipWindow { this, 400 };  // 400 ms delay
    juce::TextButton resetButton { "Reset" };
    juce::TextButton saveButton  { "Save JPG" };
    juce::TextButton fastButton  { "FAST" };
    juce::TextButton slowButton  { "SLOW" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    void updateTimeWeightButtons();

    juce::Slider  calSlider;
    juce::Label   calLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> calAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPLMeterAudioProcessorEditor)
};
