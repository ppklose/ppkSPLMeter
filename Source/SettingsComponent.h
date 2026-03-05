#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class SettingsComponent  : public juce::Component,
                           private juce::Timer
{
public:
    explicit SettingsComponent (SPLMeterAudioProcessor& p,
                                juce::AudioProcessorEditor& editor,
                                std::function<void(bool)> onFftToggle,
                                std::function<void(bool)> onThemeToggle)
        : processor (p), editor_ (editor),
          onFftToggle_ (std::move (onFftToggle)),
          onThemeToggle_ (std::move (onThemeToggle))
    {
        auto setupKnob = [] (juce::Slider& s, juce::Label& l, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::Rotary);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
            s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
            s.setColour (juce::Slider::rotarySliderFillColourId,  juce::Colour (0xff5ac8fa));
            s.setColour (juce::Slider::textBoxTextColourId,       juce::Colours::white);
            s.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
            s.setColour (juce::Slider::textBoxHighlightColourId,  juce::Colour (0xff5ac8fa));

            l.setText (text, juce::dontSendNotification);
            l.setFont (juce::Font (juce::FontOptions().withHeight (16.0f)));
            l.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
            l.setJustificationType (juce::Justification::centred);
        };

        setupKnob (calSlider,        calLabel,        "Calibration");
        setupKnob (holdSlider,       holdLabel,       "Hold Time");
        setupKnob (fftGainSlider,    fftGainLabel,    "FFT Gain");
        setupKnob (fftSmoothSlider,  fftSmoothLabel,  "FFT Smooth");

        calSlider.addMouseListener     (this, false);
        holdSlider.addMouseListener    (this, false);
        fftGainSlider.addMouseListener (this, false);
        holdSlider.setTooltip ("Hold time in seconds");
        fftSmoothSlider.setTooltip ("Spectral smoothing (0 = off, 0.95 = heavy)");

        addAndMakeVisible (calSlider);        addAndMakeVisible (calLabel);
        addAndMakeVisible (holdSlider);       addAndMakeVisible (holdLabel);
        addAndMakeVisible (fftGainSlider);    addAndMakeVisible (fftGainLabel);
        addAndMakeVisible (fftSmoothSlider);  addAndMakeVisible (fftSmoothLabel);

        // FFT enable toggle
        fftEnableButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        fftEnableButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff34c759));
        fftEnableButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        fftEnableButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        fftEnableButton.setClickingTogglesState (true);
        fftEnableButton.onClick = [this]
        {
            if (onFftToggle_) onFftToggle_ (fftEnableButton.getToggleState());
        };
        addAndMakeVisible (fftEnableButton);

        // 20-20k Bandpass toggle
        bandpassButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        bandpassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff9f0a));
        bandpassButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        bandpassButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
        bandpassButton.setClickingTogglesState (true);
        bandpassButton.setTooltip ("8th-order Butterworth bandpass 20 Hz – 20 kHz (48 dB/oct)");
        bandpassButton.onClick = [this, &p]
        {
            auto* param = p.apvts.getParameter ("bandpassEnabled");
            param->setValueNotifyingHost (bandpassButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (bandpassButton);

        // Light mode toggle
        lightModeButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        lightModeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffd60a));
        lightModeButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        lightModeButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
        lightModeButton.setClickingTogglesState (true);
        lightModeButton.onClick = [this]
        {
            bool light = lightModeButton.getToggleState();
            applyTheme (light);
            if (onThemeToggle_) onThemeToggle_ (light);
        };
        addAndMakeVisible (lightModeButton);

        calAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "calOffset", calSlider);
        holdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "peakHoldTime", holdSlider);
        fftGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "fftGain", fftGainSlider);
        fftSmoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "fftSmoothing", fftSmoothSlider);

        // Band resolution + display mode radio buttons
        auto setupRadioBtn = [this] (juce::TextButton& btn, juce::Colour onColour)
        {
            btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
            btn.setColour (juce::TextButton::buttonOnColourId, onColour);
            btn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            btn.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
            btn.setClickingTogglesState (false);
            addAndMakeVisible (btn);
        };
        setupRadioBtn (bandRes11Button,  juce::Colour (0xff5ac8fa));
        setupRadioBtn (bandRes13Button,  juce::Colour (0xff5ac8fa));
        setupRadioBtn (bandRes16Button,  juce::Colour (0xff5ac8fa));
        setupRadioBtn (bandRes112Button, juce::Colour (0xff5ac8fa));
        setupRadioBtn (bandRes124Button, juce::Colour (0xff5ac8fa));
        setupRadioBtn (dispBarsButton,  juce::Colour (0xff34c759));
        setupRadioBtn (dispAreaButton,  juce::Colour (0xff34c759));
        setupRadioBtn (dispPeakButton,  juce::Colour (0xff34c759));

        // Window function buttons
        setupRadioBtn (winHannButton,    juce::Colour (0xffbf5af2));
        setupRadioBtn (winHammingButton, juce::Colour (0xffbf5af2));
        setupRadioBtn (winBlackButton,   juce::Colour (0xffbf5af2));
        setupRadioBtn (winFlatButton,    juce::Colour (0xffbf5af2));
        setupRadioBtn (winRectButton,    juce::Colour (0xffbf5af2));
        winHannButton.onClick    = [this, &p] { setChoiceParam (p, "fftWindowType", 0); };
        winHammingButton.onClick = [this, &p] { setChoiceParam (p, "fftWindowType", 1); };
        winBlackButton.onClick   = [this, &p] { setChoiceParam (p, "fftWindowType", 2); };
        winFlatButton.onClick    = [this, &p] { setChoiceParam (p, "fftWindowType", 3); };
        winRectButton.onClick    = [this, &p] { setChoiceParam (p, "fftWindowType", 4); };

        // Overlap buttons
        setupRadioBtn (ovlp0Button,  juce::Colour (0xffff9f0a));
        setupRadioBtn (ovlp25Button, juce::Colour (0xffff9f0a));
        setupRadioBtn (ovlp50Button, juce::Colour (0xffff9f0a));
        setupRadioBtn (ovlp75Button, juce::Colour (0xffff9f0a));
        ovlp0Button.onClick  = [this, &p] { setChoiceParam (p, "fftOverlap", 0); };
        ovlp25Button.onClick = [this, &p] { setChoiceParam (p, "fftOverlap", 1); };
        ovlp50Button.onClick = [this, &p] { setChoiceParam (p, "fftOverlap", 2); };
        ovlp75Button.onClick = [this, &p] { setChoiceParam (p, "fftOverlap", 3); };

        bandRes11Button.onClick  = [this, &p] { setChoiceParam (p, "fftBandRes", 0); };
        bandRes13Button.onClick  = [this, &p] { setChoiceParam (p, "fftBandRes", 1); };
        bandRes16Button.onClick  = [this, &p] { setChoiceParam (p, "fftBandRes", 2); };
        bandRes112Button.onClick = [this, &p] { setChoiceParam (p, "fftBandRes", 3); };
        bandRes124Button.onClick = [this, &p] { setChoiceParam (p, "fftBandRes", 4); };

        bandRes11Button.setTooltip  ("1/1 Oct  —  10 bands");
        bandRes13Button.setTooltip  ("1/3 Oct  —  30 bands");
        bandRes16Button.setTooltip  ("1/6 Oct  —  60 bands");
        bandRes112Button.setTooltip ("1/12 Oct  —  120 bands");
        bandRes124Button.setTooltip ("1/24 Oct  —  240 bands");
        dispBarsButton.onClick  = [this, &p] { setChoiceParam (p, "fftDisplayMode", 0); };
        dispAreaButton.onClick  = [this, &p] { setChoiceParam (p, "fftDisplayMode", 1); };
        dispPeakButton.onClick  = [this, &p] { setChoiceParam (p, "fftDisplayMode", 2); };

        // FFT Peak Hold toggle
        fftPeakHoldButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        fftPeakHoldButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcc00));
        fftPeakHoldButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        fftPeakHoldButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
        fftPeakHoldButton.setClickingTogglesState (true);
        fftPeakHoldButton.onClick = [this, &p]
        {
            auto* param = p.apvts.getParameter ("fftPeakHold");
            param->setValueNotifyingHost (fftPeakHoldButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (fftPeakHoldButton);

        fftRTAButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        fftRTAButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff6b35));
        fftRTAButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        fftRTAButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        fftRTAButton.setClickingTogglesState (true);
        fftRTAButton.setTooltip ("+3 dB/oct slope applied to FFT bands — pink noise appears flat");
        fftRTAButton.onClick = [this, &p]
        {
            auto* param = p.apvts.getParameter ("fftRTAMode");
            param->setValueNotifyingHost (fftRTAButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (fftRTAButton);

        // Full Screen toggle
        fullscreenButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        fullscreenButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ac8fa));
        fullscreenButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        fullscreenButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        fullscreenButton.setClickingTogglesState (true);
        fullscreenButton.onClick = [this]
        {
            if (auto* peer = editor_.getPeer())
            {
                bool goFull = fullscreenButton.getToggleState();
                peer->setFullScreen (goFull);
                fullscreenButton.setButtonText (goFull ? "Exit Full Screen" : "Full Screen");
            }
        };
        addAndMakeVisible (fullscreenButton);

        startTimerHz (10);
    }

    ~SettingsComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (lightMode_ ? juce::Colour (0xffe5e5ea) : juce::Colour (0xff2c2c2e));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 16);

        // Full Screen button (bottom)
        fullscreenButton.setBounds (area.removeFromBottom (32).reduced (0, 2));
        area.removeFromBottom (6);

        // Light Mode | 20-20k BP | FFT enable
        {
            auto row = area.removeFromBottom (32);
            const int bw = row.getWidth() / 3;
            lightModeButton.setBounds (row.removeFromLeft (bw).reduced (2, 0));
            bandpassButton.setBounds  (row.removeFromLeft (bw).reduced (2, 0));
            fftEnableButton.setBounds (row.reduced (2, 0));
        }
        area.removeFromBottom (6);

        // Display mode: [Bars] [Area] [Bars+Peak] [Peak Hold] [RTA +3dB/oct]
        {
            auto row = area.removeFromBottom (32);
            const int btnW = row.getWidth() / 5;
            dispBarsButton.setBounds    (row.removeFromLeft (btnW).reduced (2, 0));
            dispAreaButton.setBounds    (row.removeFromLeft (btnW).reduced (2, 0));
            dispPeakButton.setBounds    (row.removeFromLeft (btnW).reduced (2, 0));
            fftPeakHoldButton.setBounds (row.removeFromLeft (btnW).reduced (2, 0));
            fftRTAButton.setBounds      (row.reduced (2, 0));
        }
        area.removeFromBottom (6);

        // Band resolution: [1/1] [1/3] [1/6] [1/12] [1/24]
        {
            auto row = area.removeFromBottom (32);
            const int bw = row.getWidth() / 5;
            bandRes11Button.setBounds  (row.removeFromLeft (bw).reduced (2, 0));
            bandRes13Button.setBounds  (row.removeFromLeft (bw).reduced (2, 0));
            bandRes16Button.setBounds  (row.removeFromLeft (bw).reduced (2, 0));
            bandRes112Button.setBounds (row.removeFromLeft (bw).reduced (2, 0));
            bandRes124Button.setBounds (row.reduced (2, 0));
        }
        area.removeFromBottom (6);

        // Overlap: [0%] [25%] [50%] [75%]
        {
            auto row = area.removeFromBottom (32);
            const int bw = row.getWidth() / 4;
            ovlp0Button.setBounds  (row.removeFromLeft (bw).reduced (2, 0));
            ovlp25Button.setBounds (row.removeFromLeft (bw).reduced (2, 0));
            ovlp50Button.setBounds (row.removeFromLeft (bw).reduced (2, 0));
            ovlp75Button.setBounds (row.reduced (2, 0));
        }
        area.removeFromBottom (6);

        // Window function: [Hann] [Hamming] [Blackman] [Flat-top] [Rect]
        {
            auto row = area.removeFromBottom (32);
            const int bw = row.getWidth() / 5;
            winHannButton.setBounds    (row.removeFromLeft (bw).reduced (2, 0));
            winHammingButton.setBounds (row.removeFromLeft (bw).reduced (2, 0));
            winBlackButton.setBounds   (row.removeFromLeft (bw).reduced (2, 0));
            winFlatButton.setBounds    (row.removeFromLeft (bw).reduced (2, 0));
            winRectButton.setBounds    (row.reduced (2, 0));
        }
        area.removeFromBottom (6);

        // Four knobs: Calibration | Hold Time | FFT Gain | FFT Smooth
        const int sectionW = area.getWidth() / 4;
        struct { juce::Slider* s; juce::Label* l; } knobs[] = {
            { &calSlider, &calLabel }, { &holdSlider, &holdLabel },
            { &fftGainSlider, &fftGainLabel }, { &fftSmoothSlider, &fftSmoothLabel }
        };
        for (auto& k : knobs)
        {
            auto section = area.removeFromLeft (sectionW);
            k.l->setBounds (section.removeFromTop (22));
            k.s->setBounds (section.withSizeKeepingCentre (80, 90));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (!e.mods.isRightButtonDown()) return;

        int paramIdx = -1;
        if      (e.eventComponent == &calSlider)     paramIdx = 0;
        else if (e.eventComponent == &holdSlider)    paramIdx = 1;
        else if (e.eventComponent == &fftGainSlider) paramIdx = 2;
        else return;

        juce::PopupMenu menu;
        menu.addItem (1, "MIDI Learn");
        int cc = processor.getMidiCC (paramIdx);
        menu.addItem (2, cc >= 0 ? "Clear MIDI Mapping (CC " + juce::String (cc) + ")" : "Clear MIDI Mapping", cc >= 0);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (e.eventComponent),
            [this, paramIdx] (int result)
            {
                if      (result == 1) processor.startMidiLearn (paramIdx);
                else if (result == 2) processor.clearMidiCC (paramIdx);
            });
    }

private:
    void applyTheme (bool light)
    {
        lightMode_ = light;

        juce::Colour bgBtn    = light ? juce::Colour (0xffd1d1d6) : juce::Colour (0xff3a3a3c);
        juce::Colour textOff  = light ? juce::Colour (0xff1c1c1e) : juce::Colours::white;
        juce::Colour textLbl  = light ? juce::Colour (0xff6c6c70) : juce::Colour (0xffaeaeb2);

        auto styleBtn = [&] (juce::TextButton& btn, juce::Colour onColour)
        {
            btn.setColour (juce::TextButton::buttonColourId,   bgBtn);
            btn.setColour (juce::TextButton::textColourOffId,  textOff);
            btn.setColour (juce::TextButton::buttonOnColourId, onColour);
        };
        styleBtn (fftEnableButton,   juce::Colour (0xff34c759));
        styleBtn (fullscreenButton,  juce::Colour (0xff5ac8fa));
        styleBtn (winHannButton,    juce::Colour (0xffbf5af2));
        styleBtn (winHammingButton, juce::Colour (0xffbf5af2));
        styleBtn (winBlackButton,   juce::Colour (0xffbf5af2));
        styleBtn (winFlatButton,    juce::Colour (0xffbf5af2));
        styleBtn (winRectButton,    juce::Colour (0xffbf5af2));
        styleBtn (ovlp0Button,      juce::Colour (0xffff9f0a));
        styleBtn (ovlp25Button,     juce::Colour (0xffff9f0a));
        styleBtn (ovlp50Button,     juce::Colour (0xffff9f0a));
        styleBtn (ovlp75Button,     juce::Colour (0xffff9f0a));
        styleBtn (bandRes11Button,   juce::Colour (0xff5ac8fa));
        styleBtn (bandRes13Button,   juce::Colour (0xff5ac8fa));
        styleBtn (bandRes16Button,   juce::Colour (0xff5ac8fa));
        styleBtn (bandRes112Button,  juce::Colour (0xff5ac8fa));
        styleBtn (bandRes124Button,  juce::Colour (0xff5ac8fa));
        styleBtn (dispBarsButton,    juce::Colour (0xff34c759));
        styleBtn (dispAreaButton,    juce::Colour (0xff34c759));
        styleBtn (dispPeakButton,    juce::Colour (0xff34c759));
        styleBtn (fftPeakHoldButton, juce::Colour (0xffffcc00));
        styleBtn (fftRTAButton,      juce::Colour (0xffff6b35));
        styleBtn (bandpassButton,    juce::Colour (0xffff9f0a));

        for (auto* label : { &calLabel, &holdLabel, &fftGainLabel, &fftSmoothLabel })
            label->setColour (juce::Label::textColourId, textLbl);

        for (auto* s : { &calSlider, &holdSlider, &fftGainSlider, &fftSmoothSlider })
            s->setColour (juce::Slider::textBoxTextColourId, textOff);

        repaint();
    }

    void timerCallback() override
    {
        float calOffset = processor.apvts.getRawParameterValue ("calOffset")->load();
        float dbfs = 94.0f - calOffset;
        calSlider.setTooltip ("94 dB SPL = " + juce::String (dbfs, 1) + " dBFS");

        struct { juce::Label* label; const char* baseName; int idx; } entries[] = {
            { &calLabel,     "Calibration", 0 },
            { &holdLabel,    "Hold Time",   1 },
            { &fftGainLabel, "FFT Gain",    2 },
        };
        for (auto& e : entries)
        {
            if (processor.isMidiLearning (e.idx))
                e.label->setText (juce::String (e.baseName) + " [Learning...]", juce::dontSendNotification);
            else if (int cc = processor.getMidiCC (e.idx); cc >= 0)
                e.label->setText (juce::String (e.baseName) + " [CC " + juce::String (cc) + "]", juce::dontSendNotification);
            else
                e.label->setText (e.baseName, juce::dontSendNotification);
        }

        // Sync radio buttons to current param values
        int bandRes  = static_cast<int> (processor.apvts.getRawParameterValue ("fftBandRes")->load());
        int dispMode = static_cast<int> (processor.apvts.getRawParameterValue ("fftDisplayMode")->load());
        bool peakOn  = processor.apvts.getRawParameterValue ("fftPeakHold")->load() > 0.5f;

        int winType  = static_cast<int> (processor.apvts.getRawParameterValue ("fftWindowType")->load());
        int overlapI = static_cast<int> (processor.apvts.getRawParameterValue ("fftOverlap")->load());
        winHannButton.setToggleState    (winType  == 0, juce::dontSendNotification);
        winHammingButton.setToggleState (winType  == 1, juce::dontSendNotification);
        winBlackButton.setToggleState   (winType  == 2, juce::dontSendNotification);
        winFlatButton.setToggleState    (winType  == 3, juce::dontSendNotification);
        winRectButton.setToggleState    (winType  == 4, juce::dontSendNotification);
        ovlp0Button.setToggleState  (overlapI == 0, juce::dontSendNotification);
        ovlp25Button.setToggleState (overlapI == 1, juce::dontSendNotification);
        ovlp50Button.setToggleState (overlapI == 2, juce::dontSendNotification);
        ovlp75Button.setToggleState (overlapI == 3, juce::dontSendNotification);

        bandRes11Button.setToggleState  (bandRes == 0, juce::dontSendNotification);
        bandRes13Button.setToggleState  (bandRes == 1, juce::dontSendNotification);
        bandRes16Button.setToggleState  (bandRes == 2, juce::dontSendNotification);
        bandRes112Button.setToggleState (bandRes == 3, juce::dontSendNotification);
        bandRes124Button.setToggleState (bandRes == 4, juce::dontSendNotification);
        dispBarsButton.setToggleState  (dispMode == 0, juce::dontSendNotification);
        dispAreaButton.setToggleState  (dispMode == 1, juce::dontSendNotification);
        dispPeakButton.setToggleState  (dispMode == 2, juce::dontSendNotification);
        fftPeakHoldButton.setToggleState (peakOn, juce::dontSendNotification);
        bool rtaOn = processor.apvts.getRawParameterValue ("fftRTAMode")->load() > 0.5f;
        fftRTAButton.setToggleState (rtaOn, juce::dontSendNotification);

        bool bpOn = processor.apvts.getRawParameterValue ("bandpassEnabled")->load() > 0.5f;
        bandpassButton.setToggleState (bpOn, juce::dontSendNotification);
    }

    static void setChoiceParam (SPLMeterAudioProcessor& p, const char* id, int index)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (index)));
    }

    SPLMeterAudioProcessor&    processor;
    juce::AudioProcessorEditor& editor_;
    std::function<void(bool)>  onFftToggle_;
    std::function<void(bool)>  onThemeToggle_;
    bool                       lightMode_ = false;

    juce::Slider calSlider, holdSlider, fftGainSlider, fftSmoothSlider;
    juce::Label  calLabel,  holdLabel,  fftGainLabel,  fftSmoothLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        calAttachment, holdAttachment, fftGainAttachment, fftSmoothAttachment;

    // Band resolution
    juce::TextButton bandRes11Button  { "1/1 Oct"  };
    juce::TextButton bandRes13Button  { "1/3 Oct"  };
    juce::TextButton bandRes16Button  { "1/6 Oct"  };
    juce::TextButton bandRes112Button { "1/12 Oct" };
    juce::TextButton bandRes124Button { "1/24 Oct" };

    // Display mode (Bars | Area | Bars+Peak)
    juce::TextButton dispBarsButton { "Bars" };
    juce::TextButton dispAreaButton { "Area" };
    juce::TextButton dispPeakButton { "Bars+Peak" };

    // FFT Peak Hold + RTA
    juce::TextButton fftPeakHoldButton { "Peak Hold"   };
    juce::TextButton fftRTAButton      { "RTA +3dB/oct" };

    // Window function
    juce::TextButton winHannButton    { "Hann"     };
    juce::TextButton winHammingButton { "Hamming"  };
    juce::TextButton winBlackButton   { "Blackman" };
    juce::TextButton winFlatButton    { "Flat-top" };
    juce::TextButton winRectButton    { "Rect"     };

    // Overlap
    juce::TextButton ovlp0Button  { "0%"  };
    juce::TextButton ovlp25Button { "25%" };
    juce::TextButton ovlp50Button { "50%" };
    juce::TextButton ovlp75Button { "75%" };

    juce::TextButton fftEnableButton  { "FFT" };
    juce::TextButton lightModeButton  { "Light Mode" };
    juce::TextButton bandpassButton   { "20-20k BP" };
    juce::TextButton fullscreenButton { "Full Screen" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsComponent)
};

//==============================================================================
class SettingsWindow  : public juce::DocumentWindow
{
public:
    explicit SettingsWindow (SPLMeterAudioProcessor& p, juce::AudioProcessorEditor& editor,
                             std::function<void(bool)> onFftToggle,
                             std::function<void(bool)> onThemeToggle)
        : juce::DocumentWindow ("Settings",
                                juce::Colour (0xff1c1c1e),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        auto* content = new SettingsComponent (p, editor,
                                               std::move (onFftToggle),
                                               std::move (onThemeToggle));
        content->setSize (620, 430);
        setContentOwned (content, true);
        setResizable (false, false);
    }

    void closeButtonPressed() override { setVisible (false); }

private:
    juce::TooltipWindow tooltipWindow { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};
