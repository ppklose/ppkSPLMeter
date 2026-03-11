#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MeterComponent.h"
#include "LogComponent.h"
#include "SettingsComponent.h"
#if JUCE_MAC
 #include "VisqolComponent.h"
#endif

//==============================================================================
// Stylised speaker button - toggle OFF = muted (red X), toggle ON = monitoring (waves)
class MonitorButton : public juce::Button
{
public:
    MonitorButton() : juce::Button ("Monitor")
    {
        setClickingTogglesState (true);
        setToggleState (false, juce::dontSendNotification);
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool /*isButtonDown*/) override
    {
        const bool on = getToggleState();
        const auto b  = getLocalBounds().toFloat();

        const juce::Colour bg = isMouseOver ? juce::Colour (0xff4a4a4e) : juce::Colour (0xff3a3a3c);
        g.setColour (bg);
        g.fillRoundedRectangle (b, 4.0f);

        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        const float r  = b.getHeight() * 0.28f;

        const juce::Colour iconCol = on ? juce::Colours::white : juce::Colour (0xffff453a);

        // Speaker body (rectangle, left side)
        juce::Path body;
        body.addRectangle (cx - r * 1.2f, cy - r * 0.5f, r * 0.7f, r);
        g.setColour (iconCol);
        g.fillPath (body);

        // Speaker horn (triangle pointing right)
        juce::Path horn;
        horn.startNewSubPath (cx - r * 0.5f, cy - r * 0.5f);
        horn.lineTo          (cx + r * 0.35f, cy - r);
        horn.lineTo          (cx + r * 0.35f, cy + r);
        horn.lineTo          (cx - r * 0.5f,  cy + r * 0.5f);
        horn.closeSubPath();
        g.fillPath (horn);

        if (on)
        {
            juce::Path waves;
            // Inner wave
            waves.addArc (cx + r * 0.3f,  cy - r * 0.55f, r * 0.8f, r * 1.1f,
                          -juce::MathConstants<float>::pi * 0.35f,
                           juce::MathConstants<float>::pi * 0.35f, true);
            // Outer wave
            waves.addArc (cx + r * 0.45f, cy - r * 0.9f,  r * 1.3f, r * 1.8f,
                          -juce::MathConstants<float>::pi * 0.35f,
                           juce::MathConstants<float>::pi * 0.35f, true);
            g.strokePath (waves, juce::PathStrokeType (1.5f));
        }
        else
        {
            // Red X cross over the right side
            const float x1 = cx + r * 0.4f;
            const float x2 = cx + r * 1.15f;
            const float yt = cy - r * 0.65f;
            const float yb = cy + r * 0.65f;
            g.setColour (juce::Colour (0xffff453a));
            g.drawLine (x1, yt, x2, yb, 2.0f);
            g.drawLine (x1, yb, x2, yt, 2.0f);
        }
    }
};

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
    void saveSettings();
    void loadSettings();

    SPLMeterAudioProcessor& audioProcessor;

    MeterComponent       meter;
    LogComponent         log;

    juce::TooltipWindow tooltipWindow { this, 400 };  // 400 ms delay
    juce::TextButton settingsButton  { "Settings" };
    juce::TextButton resetButton     { "Reset" };
    juce::TextButton saveButton      { "Save JPG" };
    juce::TextButton saveCsvButton   { "Save CSV" };
    juce::TextButton saveWavButton   { "Save WAV" };
    juce::TextButton basicModeButton { "Advanced Mode" };
    juce::TextButton fastButton      { "FAST" };
    juce::TextButton slowButton      { "SLOW" };
    bool basicMode_      = true;
    int  extendedHeight_ = 900;
    MonitorButton    monitorButton;
    juce::Slider     monitorGainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> monitorGainAttachment;

    juce::TextButton realTimeButton { "Real Time" };
    juce::TextButton fileButton     { "File" };
    bool fileMode = false;
    void updateModeButtons();
    std::unique_ptr<juce::FileChooser> fileChooser;

    void updateTimeWeightButtons();
    void applyTheme (bool light);

    bool lightMode_     = false;
    bool initialising_  = true;
    juce::Label       clockLabel;
    int               lastClockSecond_ = -1;
    juce::TextEditor  noteField;
    std::unique_ptr<SettingsWindow> settingsWindow;
#if JUCE_MAC
    std::unique_ptr<VisqolWindow>   visqolWindow;
    juce::TextButton visqolButton { "ViSQOL" };
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPLMeterAudioProcessorEditor)
};
