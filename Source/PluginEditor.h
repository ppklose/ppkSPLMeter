#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MeterComponent.h"
#include "LogComponent.h"
#include "SettingsComponent.h"
#include "SpectrogramComponent.h"
#include "SoundDetectiveComponent.h"
#include "LFFTComponent.h"
#include "LongTermSPLComponent.h"
#include "ImpulseFidelityComponent.h"
#if SPLMETER_HAS_VISQOL || JUCE_MAC
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
// Pause / Play toggle button — draws ⏸ (two bars) or ▶ (triangle) depending on toggle state
class PauseButton : public juce::Button
{
public:
    PauseButton() : juce::Button ("Pause")
    {
        setClickingTogglesState (true);
        setToggleState (false, juce::dontSendNotification);  // false = running
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool /*isButtonDown*/) override
    {
        const bool paused = getToggleState();
        const auto b = getLocalBounds().toFloat();

        const juce::Colour bg = isMouseOver ? juce::Colour (0xff4a4a4e) : juce::Colour (0xff3a3a3c);
        g.setColour (bg);
        g.fillRoundedRectangle (b, 4.0f);

        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        const float h  = b.getHeight() * 0.42f;
        const float w  = h * 0.38f;

        g.setColour (juce::Colours::white);

        if (paused)
        {
            // Play triangle
            juce::Path tri;
            tri.startNewSubPath (cx - w * 0.8f, cy - h * 0.6f);
            tri.lineTo          (cx + w * 1.2f, cy);
            tri.lineTo          (cx - w * 0.8f, cy + h * 0.6f);
            tri.closeSubPath();
            g.fillPath (tri);
        }
        else
        {
            // Pause: two vertical bars
            const float gap = w * 0.5f;
            g.fillRoundedRectangle (cx - gap - w, cy - h * 0.5f, w, h, 2.0f);
            g.fillRoundedRectangle (cx + gap,      cy - h * 0.5f, w, h, 2.0f);
        }
    }
};

//==============================================================================
// Vertical L/R bargraph VU meter (peak, -60..0 dBFS) with green/yellow/red zones
class OutputVUMeter : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    OutputVUMeter() = default;

    // Linear peak values in [0, ~1+]; smoothed in setLevels() so callers can poll directly
    void setLevels (float linL, float linR) noexcept
    {
        levelL_ = applyBallistics (levelL_, linL);
        levelR_ = applyBallistics (levelR_, linR);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff1c1c1e));
        g.fillRoundedRectangle (b, 2.0f);

        const float gap   = 2.0f;
        const float barW  = (b.getWidth() - gap * 3.0f) * 0.5f;
        auto barL = juce::Rectangle<float> (b.getX() + gap, b.getY() + gap,
                                            barW, b.getHeight() - gap * 2.0f);
        auto barR = barL.withX (barL.getRight() + gap);

        drawBar (g, barL, levelL_);
        drawBar (g, barR, levelR_);
    }

private:
    static float linearToDb (float lin) noexcept
    {
        return juce::Decibels::gainToDecibels (juce::jmax (lin, 1.0e-6f), -120.0f);
    }

    static float dbToFraction (float dB) noexcept
    {
        // Map -60..+6 dBFS to 0..1 along the bar
        return juce::jlimit (0.0f, 1.0f, (dB + 60.0f) / 66.0f);
    }

    static float applyBallistics (float prev, float target) noexcept
    {
        // 30 Hz tick: fast attack (~one tick), exponential release (~250 ms 20-dB decay)
        constexpr float kRelease = 0.78f;
        return (target >= prev) ? target : prev * kRelease + target * (1.0f - kRelease);
    }

    void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float lin) const
    {
        g.setColour (juce::Colour (0xff0d0d0e));
        g.fillRect (r);

        const float frac = dbToFraction (linearToDb (lin));
        if (frac > 0.0f)
        {
            const float thresh18 = dbToFraction (-18.0f);
            const float thresh6  = dbToFraction (-6.0f);
            const float thresh0  = dbToFraction (0.0f);
            const float h = r.getHeight();

            auto fillSegment = [&] (float fracLo, float fracHi, juce::Colour col)
            {
                const float fHi = juce::jmin (fracHi, frac);
                if (fHi <= fracLo)
                    return;
                const float yLo = r.getBottom() - fracLo * h;
                const float yHi = r.getBottom() - fHi    * h;
                g.setColour (col);
                g.fillRect (juce::Rectangle<float> (r.getX(), yHi, r.getWidth(), yLo - yHi));
            };

            fillSegment (0.0f,     thresh18, juce::Colour (0xff34c759));  // green
            fillSegment (thresh18, thresh6,  juce::Colour (0xffffd60a));  // yellow
            fillSegment (thresh6,  thresh0,  juce::Colour (0xffff9500));  // orange
            fillSegment (thresh0,  1.0f,     juce::Colour (0xffff453a));  // red (clip)
        }

        // 0 dBFS reference tick
        const float y0 = r.getBottom() - dbToFraction (0.0f) * r.getHeight();
        g.setColour (juce::Colour (0x80ffffff));
        g.drawLine (r.getX(), y0, r.getRight(), y0, 1.0f);
    }

    float levelL_ = 0.0f;
    float levelR_ = 0.0f;
};

//==============================================================================
// Marker button - draws a red diamond icon
class MarkerButton : public juce::Button
{
public:
    MarkerButton() : juce::Button ("Marker") {}

    void paintButton (juce::Graphics& g, bool isMouseOver, bool /*isButtonDown*/) override
    {
        const auto b = getLocalBounds().toFloat();

        const juce::Colour bg = isMouseOver ? juce::Colour (0xff4a4a4e) : juce::Colour (0xff3a3a3c);
        g.setColour (bg);
        g.fillRoundedRectangle (b, 4.0f);

        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        const float r  = b.getHeight() * 0.3f;

        juce::Path diamond;
        diamond.startNewSubPath (cx, cy - r);
        diamond.lineTo (cx + r * 0.7f, cy);
        diamond.lineTo (cx, cy + r);
        diamond.lineTo (cx - r * 0.7f, cy);
        diamond.closeSubPath();

        g.setColour (juce::Colour (0xffff453a));
        g.fillPath (diamond);
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
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void saveSettings();
    void loadSettings();

    void doSaveCsv();
    void doSaveWav();
    void doSaveJpg();
    void doSaveAll();
    void doSaveSettingsJson();
    void doLoadSettingsJson();
    static void writeCsvRows (juce::OutputStream&, const std::vector<LogEntry>&,
                              const std::vector<LogComponent::MarkerEvent>&);

    SPLMeterAudioProcessor& audioProcessor;

    MeterComponent       meter;
    LogComponent         log;

    juce::TooltipWindow tooltipWindow { this, 400 };  // 400 ms delay
    juce::TextButton settingsButton      { "Settings..." };
    juce::TextButton toolsMenuButton    { "Tools..." };
    juce::TextButton resetButton     { "Reset" };
    juce::TextButton saveMenuButton  { "Ex-/Import..." };
    juce::TextButton basicModeButton { "Advanced Mode" };
    juce::TextButton fastButton      { "FAST" };
    juce::TextButton slowButton      { "SLOW" };
    juce::Label      holdTimeLabel;
    PauseButton      pauseButton;
    juce::ToggleButton dawSyncToggle { "DAW sync" };
    bool basicMode_      = true;
    int  extendedHeight_ = 900;
    MonitorButton    monitorButton;
    juce::Slider     monitorGainSlider;
    OutputVUMeter    outputVUMeter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> monitorGainAttachment;

    juce::TextButton realTimeButton { "Real Time" };
    juce::TextButton fileButton     { "File" };
    bool fileMode = false;
    void updateModeButtons();

    // Pause event tracking for timeline markers
    std::vector<LogComponent::PauseEvent> pauseEvents_;
    juce::int64 pendingPauseWallMs_     = 0;
    juce::int64 pendingPauseAnalysisMs_ = 0;
    void recordPauseStart();
    void recordPauseEnd();
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> settingsJsonChooser_;

    void updateTimeWeightButtons();
    void applyTheme (bool light);

    bool lightMode_     = false;
    bool initialising_  = true;
    juce::Label       clockLabel;
    int               lastClockSecond_ = -1;
    juce::TextEditor  noteField;
    MarkerButton      markerButton;
    std::vector<LogComponent::MarkerEvent> markerEvents_;
    void triggerMarker();
    std::unique_ptr<SettingsWindow>       settingsWindow;
    std::unique_ptr<SpectrogramWindow>    spectrogramWindow;
    std::unique_ptr<SoundDetectiveWindow> soundDetectiveWindow;
    std::unique_ptr<LFFTWindow>           lfftWindow;
    std::unique_ptr<LongTermSPLWindow>    longTermSPLWindow;
    std::unique_ptr<ImpulseFidelityWindow> impulseFidelityWindow;
    std::vector<SoundEvent>               allSoundEvents_;
#if SPLMETER_HAS_VISQOL || JUCE_MAC
    std::unique_ptr<VisqolWindow>   visqolWindow;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPLMeterAudioProcessorEditor)
};
