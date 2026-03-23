#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Custom channel mute button: shows label with red X + strikethrough when muted.
// Double-click to rename inline.
class ChannelMuteButton : public juce::Button
{
public:
    std::function<void(const juce::String&)> onNameChanged;

    ChannelMuteButton() : juce::Button ("")
    {
        setClickingTogglesState (true);

        editor_.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
        editor_.setColour (juce::TextEditor::backgroundColourId,     juce::Colour (0xff2a2a2c));
        editor_.setColour (juce::TextEditor::textColourId,           juce::Colours::white);
        editor_.setColour (juce::TextEditor::outlineColourId,        juce::Colour (0xff5ac8fa));
        editor_.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff5ac8fa));
        editor_.setJustification (juce::Justification::centred);
        editor_.setReturnKeyStartsNewLine (false);
        editor_.onReturnKey = [this] { commitEdit(); };
        editor_.onFocusLost = [this] { commitEdit(); };
        addChildComponent (editor_);  // starts hidden; addAndMakeVisible would override setVisible(false)
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool /*isButtonDown*/) override
    {
        const bool muted = getToggleState();
        const auto b = getLocalBounds().toFloat();

        // Background
        g.setColour (isMouseOver ? juce::Colour (0xff4a4a4e) : juce::Colour (0xff3a3a3c));
        g.fillRoundedRectangle (b, 4.0f);

        if (editor_.isVisible())
            return; // TextEditor paints itself on top

        const juce::String displayText = getButtonText();
        const juce::Colour textCol = muted ? juce::Colour (0xffff453a) : juce::Colours::white;
        g.setColour (textCol);
        const juce::Font font (juce::FontOptions().withHeight (13.0f));
        g.setFont (font);
        g.drawText (displayText, b.toNearestInt(), juce::Justification::centred, false);

        if (muted)
        {
            // Strikethrough across the label text
            const float textW = font.getStringWidthFloat (displayText);
            const float textX = b.getCentreX() - textW * 0.5f;
            const float midY  = b.getCentreY();
            g.setColour (juce::Colour (0xffff453a));
            g.drawLine (textX, midY, textX + textW, midY, 1.5f);

            // Red X - two diagonals filling the button area (inset a bit)
            const float m = 4.0f;
            g.drawLine (b.getX() + m, b.getY() + m, b.getRight() - m, b.getBottom() - m, 1.5f);
            g.drawLine (b.getRight() - m, b.getY() + m, b.getX() + m, b.getBottom() - m, 1.5f);
        }
    }

    void resized() override
    {
        editor_.setBounds (getLocalBounds().reduced (2));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            editor_.setText (getButtonText(), false);
            editor_.setVisible (true);
            editor_.grabKeyboardFocus();
            editor_.selectAll();
            repaint();
        }
        else
        {
            juce::Button::mouseDown (e);
        }
    }

private:
    void commitEdit()
    {
        if (!editor_.isVisible()) return;
        editor_.setVisible (false);
        const auto text = editor_.getText().trim();
        if (text.isNotEmpty())
            setButtonText (text);
        if (onNameChanged)
            onNameChanged (getButtonText());
        repaint();
    }

    juce::TextEditor editor_;
};

//==============================================================================
// Small panel shown in a CallOutBox when the FFT button is right-clicked
class FftRangePanel  : public juce::Component
{
public:
    explicit FftRangePanel (SPLMeterAudioProcessor& p)
    {
        auto setupSlider = [this] (juce::Slider& s, juce::Label& l, const juce::String& title)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
            s.setTextValueSuffix (" Hz");
            s.setColour (juce::Slider::trackColourId,          juce::Colour (0xff34c759));
            s.setColour (juce::Slider::thumbColourId,          juce::Colour (0xff34c759));
            s.setColour (juce::Slider::textBoxTextColourId,    juce::Colours::white);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            l.setText (title, juce::dontSendNotification);
            l.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
            l.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
            l.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (s);
            addAndMakeVisible (l);
        };

        setupSlider (lowerSlider_, lowerLabel_, "Lower");
        setupSlider (upperSlider_, upperLabel_, "Upper");

        lowerAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "fftLowerFreq", lowerSlider_);
        upperAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "fftUpperFreq", upperSlider_);

        setSize (300, 76);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);
        auto row1 = b.removeFromTop (28);
        b.removeFromTop (4);
        auto row2 = b.removeFromTop (28);
        lowerLabel_.setBounds (row1.removeFromLeft (52));
        lowerSlider_.setBounds (row1);
        upperLabel_.setBounds (row2.removeFromLeft (52));
        upperSlider_.setBounds (row2);
    }

private:
    juce::Slider lowerSlider_, upperSlider_;
    juce::Label  lowerLabel_, upperLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowerAttach_, upperAttach_;
};

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
            s.setSliderStyle (juce::Slider::LinearVertical);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
            s.setColour (juce::Slider::trackColourId,             juce::Colour (0xff5ac8fa));
            s.setColour (juce::Slider::thumbColourId,             juce::Colour (0xff5ac8fa));
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

        calSlider.addMouseListener       (this, false);
        holdSlider.addMouseListener      (this, false);
        fftGainSlider.addMouseListener   (this, false);
        fftEnableButton.addMouseListener (this, false);
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

        // 94 dB reference line toggle
        line94Button.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        line94Button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff453a));
        line94Button.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        line94Button.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        line94Button.setClickingTogglesState (true);
        line94Button.setTooltip ("Draw a dashed red reference line at 94 dB SPL across the plot");
        line94Button.onClick = [this, &p]
        {
            auto* param = p.apvts.getParameter ("line94Enabled");
            param->setValueNotifyingHost (line94Button.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (line94Button);

        // 20-20k Bandpass toggle
        bandpassButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        bandpassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff9f0a));
        bandpassButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        bandpassButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
        bandpassButton.setClickingTogglesState (true);
        bandpassButton.setTooltip ("8th-order Butterworth bandpass 20 Hz - 20 kHz (48 dB/oct)");
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

        bandRes11Button.setTooltip  ("1/1 Oct  -  10 bands");
        bandRes13Button.setTooltip  ("1/3 Oct  -  30 bands");
        bandRes16Button.setTooltip  ("1/6 Oct  -  60 bands");
        bandRes112Button.setTooltip ("1/12 Oct  -  120 bands");
        bandRes124Button.setTooltip ("1/24 Oct  -  240 bands");
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
        fftRTAButton.setTooltip ("+3 dB/oct slope applied to FFT bands - pink noise appears flat");
        fftRTAButton.onClick = [this, &p]
        {
            auto* param = p.apvts.getParameter ("fftRTAMode");
            param->setValueNotifyingHost (fftRTAButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (fftRTAButton);

        // Correction filter row
        correctionEnableButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        correctionEnableButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff34c759));
        correctionEnableButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        correctionEnableButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        correctionEnableButton.setClickingTogglesState (true);
        correctionEnableButton.setTooltip ("Enable / disable the loaded correction filter");
        correctionEnableButton.onClick = [this, &p]
        {
            auto* param = p.apvts.getParameter ("correctionEnabled");
            param->setValueNotifyingHost (correctionEnableButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (correctionEnableButton);

        correctionLoadButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
        correctionLoadButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        correctionLoadButton.setTooltip ("Load a correction filter (.txt: frequency Hz, SPL dB)");
        correctionLoadButton.onClick = [this, &p]
        {
            corrFileChooser_ = std::make_unique<juce::FileChooser> (
                "Load correction filter", lastUsedFolder(), "*.txt;*.csv");
            corrFileChooser_->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [&p] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File{}) return;
                    saveLastUsedFolder (file);
                    p.loadCorrectionFilter (file);
                });
        };
        addAndMakeVisible (correctionLoadButton);

        correctionClearButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
        correctionClearButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffff453a));
        correctionClearButton.onClick = [this, &p] { p.clearCorrectionFilter(); };
        addAndMakeVisible (correctionClearButton);

        // Graph overlay row
        graphOverlayEnableButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        graphOverlayEnableButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ac8fa));
        graphOverlayEnableButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        graphOverlayEnableButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
        graphOverlayEnableButton.setClickingTogglesState (true);
        graphOverlayEnableButton.setTooltip ("Enable / disable Graph Overlay 1");
        graphOverlayEnableButton.onClick = [this, &p]
        {
            auto* param = p.apvts.getParameter ("graphOverlayEnabled");
            param->setValueNotifyingHost (graphOverlayEnableButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (graphOverlayEnableButton);

        graphOverlayLoadButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
        graphOverlayLoadButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        graphOverlayLoadButton.setTooltip ("Load Graph Overlay 1 (.txt: frequency Hz, SPL dB) - plotted as dashed blue line in FFT view");
        graphOverlayLoadButton.onClick = [this, &p]
        {
            graphOverlayFileChooser_ = std::make_unique<juce::FileChooser> (
                "Load Graph Overlay 1", lastUsedFolder(), "*.txt;*.csv");
            graphOverlayFileChooser_->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [&p] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File{}) return;
                    saveLastUsedFolder (file);
                    p.loadGraphOverlay (file);
                });
        };
        addAndMakeVisible (graphOverlayLoadButton);

        graphOverlayClearButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
        graphOverlayClearButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffff453a));
        graphOverlayClearButton.onClick = [this, &p] { p.clearGraphOverlay(); };
        addAndMakeVisible (graphOverlayClearButton);

        // Graph overlay 2
        graphOverlay2EnableButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        graphOverlay2EnableButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff9f0a));
        graphOverlay2EnableButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        graphOverlay2EnableButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
        graphOverlay2EnableButton.setClickingTogglesState (true);
        graphOverlay2EnableButton.setTooltip ("Enable / disable Graph Overlay 2");
        graphOverlay2EnableButton.onClick = [this, &p]
        {
            auto* param = p.apvts.getParameter ("graphOverlay2Enabled");
            param->setValueNotifyingHost (graphOverlay2EnableButton.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (graphOverlay2EnableButton);

        graphOverlay2LoadButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
        graphOverlay2LoadButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        graphOverlay2LoadButton.setTooltip ("Load Graph Overlay 2 (.txt: frequency Hz, SPL dB) - plotted as dashed orange line in FFT view");
        graphOverlay2LoadButton.onClick = [this, &p]
        {
            graphOverlay2FileChooser_ = std::make_unique<juce::FileChooser> (
                "Load Graph Overlay 2", lastUsedFolder(), "*.txt;*.csv");
            graphOverlay2FileChooser_->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [&p] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File{}) return;
                    saveLastUsedFolder (file);
                    p.loadGraphOverlay2 (file);
                });
        };
        addAndMakeVisible (graphOverlay2LoadButton);

        graphOverlay2ClearButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
        graphOverlay2ClearButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffff453a));
        graphOverlay2ClearButton.onClick = [this, &p] { p.clearGraphOverlay2(); };
        addAndMakeVisible (graphOverlay2ClearButton);

        // Channel mute checkboxes IN01..IN32
        for (int i = 0; i < 32; ++i)
        {
            channelMuteButtons[i].setButtonText (p.getChannelName (i));
            channelMuteButtons[i].setTooltip ("Mute input channel " + juce::String (i + 1) + " — double-click to rename");
            channelMuteButtons[i].onClick = [this, &p, i]
            {
                auto* param = p.apvts.getParameter ("channelMute" + juce::String (i));
                param->setValueNotifyingHost (channelMuteButtons[i].getToggleState() ? 1.0f : 0.0f);
            };
            channelMuteButtons[i].onNameChanged = [&p, i] (const juce::String& name)
            {
                p.setChannelName (i, name);
            };
            addAndMakeVisible (channelMuteButtons[i]);
        }

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

        // Section header labels
        auto setupSectionLabel = [] (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (juce::Font (juce::FontOptions().withHeight (11.0f).withStyle ("Bold")));
            l.setColour (juce::Label::textColourId, juce::Colour (0xff8e8e93));
            l.setInterceptsMouseClicks (false, false);
        };
        setupSectionLabel (sectionGeneralLabel,  "GENERAL");
        setupSectionLabel (sectionFftLabel,       "FFT");
        setupSectionLabel (sectionAnalysisLabel,  "ANALYSIS");
        setupSectionLabel (sectionChannelsLabel,  "INPUT CHANNELS  (right click to rename)");
        addAndMakeVisible (sectionGeneralLabel);
        addAndMakeVisible (sectionFftLabel);
        addAndMakeVisible (sectionAnalysisLabel);
        addAndMakeVisible (sectionChannelsLabel);

        startTimerHz (10);
    }

    ~SettingsComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (lightMode_ ? juce::Colour (0xffe5e5ea) : juce::Colour (0xff2c2c2e));

        // Thin separator lines above each section except the first
        const juce::Colour sep = lightMode_ ? juce::Colour (0xffc7c7cc) : juce::Colour (0xff48484a);
        for (auto* lbl : { &sectionFftLabel, &sectionAnalysisLabel, &sectionChannelsLabel })
        {
            g.setColour (sep);
            g.fillRect (12, lbl->getY() - 7, getWidth() - 24, 1);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 12);

        auto row = [&] (int h = 28) { return area.removeFromTop (h); };
        auto gap = [&] (int g = 5)  { area.removeFromTop (g); };

        // === GENERAL ===
        sectionGeneralLabel.setBounds (row (16)); gap (5);
        {
            auto faderRow = row (100); gap (5);
            const int fw = faderRow.getWidth() / 2;
            auto calSect = faderRow.removeFromLeft (fw);
            calLabel.setBounds  (calSect.removeFromTop (22));
            calSlider.setBounds (calSect);
            holdLabel.setBounds  (faderRow.removeFromTop (22));
            holdSlider.setBounds (faderRow);
        }
        {
            auto r = row (28); gap (5);
            const int bw = r.getWidth() / 2;
            lightModeButton.setBounds  (r.removeFromLeft (bw).reduced (2, 0));
            fullscreenButton.setBounds (r.reduced (2, 0));
        }

        gap (10); // section gap

        // === FFT ===
        sectionFftLabel.setBounds (row (16)); gap (5);
        {
            auto r = row (28); gap (5);
            const int bw = r.getWidth() / 2;
            fftEnableButton.setBounds (r.removeFromLeft (bw).reduced (2, 0));
            bandpassButton.setBounds  (r.reduced (2, 0));
        }
        {
            auto faderRow = row (95); gap (5);
            const int fw = faderRow.getWidth() / 2;
            auto gainSect = faderRow.removeFromLeft (fw);
            fftGainLabel.setBounds    (gainSect.removeFromTop (22));
            fftGainSlider.setBounds   (gainSect);
            fftSmoothLabel.setBounds  (faderRow.removeFromTop (22));
            fftSmoothSlider.setBounds (faderRow);
        }
        {
            auto r = row (28); gap (5);
            const int bw = r.getWidth() / 5;
            bandRes11Button.setBounds  (r.removeFromLeft (bw).reduced (2, 0));
            bandRes13Button.setBounds  (r.removeFromLeft (bw).reduced (2, 0));
            bandRes16Button.setBounds  (r.removeFromLeft (bw).reduced (2, 0));
            bandRes112Button.setBounds (r.removeFromLeft (bw).reduced (2, 0));
            bandRes124Button.setBounds (r.reduced (2, 0));
        }
        {
            auto r = row (28); gap (5);
            const int bw = r.getWidth() / 5;
            dispBarsButton.setBounds    (r.removeFromLeft (bw).reduced (2, 0));
            dispAreaButton.setBounds    (r.removeFromLeft (bw).reduced (2, 0));
            dispPeakButton.setBounds    (r.removeFromLeft (bw).reduced (2, 0));
            fftPeakHoldButton.setBounds (r.removeFromLeft (bw).reduced (2, 0));
            fftRTAButton.setBounds      (r.reduced (2, 0));
        }
        {
            auto r = row (28); gap (5);
            const int bw = r.getWidth() / 5;
            winHannButton.setBounds    (r.removeFromLeft (bw).reduced (2, 0));
            winHammingButton.setBounds (r.removeFromLeft (bw).reduced (2, 0));
            winBlackButton.setBounds   (r.removeFromLeft (bw).reduced (2, 0));
            winFlatButton.setBounds    (r.removeFromLeft (bw).reduced (2, 0));
            winRectButton.setBounds    (r.reduced (2, 0));
        }
        {
            auto r = row (28); gap (5);
            const int bw = r.getWidth() / 4;
            ovlp0Button.setBounds  (r.removeFromLeft (bw).reduced (2, 0));
            ovlp25Button.setBounds (r.removeFromLeft (bw).reduced (2, 0));
            ovlp50Button.setBounds (r.removeFromLeft (bw).reduced (2, 0));
            ovlp75Button.setBounds (r.reduced (2, 0));
        }

        gap (10); // section gap

        // === ANALYSIS ===
        sectionAnalysisLabel.setBounds (row (16)); gap (5);
        {
            auto r = row (28); gap (5);
            line94Button.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2, 0));
        }
        {
            auto r = row (28); gap (5);
            correctionEnableButton.setBounds (r.removeFromLeft (70).reduced (2, 0));
            correctionClearButton.setBounds  (r.removeFromRight (60).reduced (2, 0));
            correctionLoadButton.setBounds   (r.reduced (2, 0));
        }
        {
            auto r = row (28); gap (5);
            graphOverlayEnableButton.setBounds (r.removeFromLeft (70).reduced (2, 0));
            graphOverlayClearButton.setBounds  (r.removeFromRight (60).reduced (2, 0));
            graphOverlayLoadButton.setBounds   (r.reduced (2, 0));
        }
        {
            auto r = row (28); gap (5);
            graphOverlay2EnableButton.setBounds (r.removeFromLeft (70).reduced (2, 0));
            graphOverlay2ClearButton.setBounds  (r.removeFromRight (60).reduced (2, 0));
            graphOverlay2LoadButton.setBounds   (r.reduced (2, 0));
        }

        gap (10); // section gap

        // === INPUT CHANNELS ===
        sectionChannelsLabel.setBounds (row (16)); gap (5);
        for (int i = 0; i < 4; ++i)
        {
            auto r = row (26);
            const int bw = r.getWidth() / 8;
            for (int j = 0; j < 8; ++j)
                channelMuteButtons[i * 8 + j].setBounds (r.removeFromLeft (bw));
            if (i < 3) gap (3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (!e.mods.isRightButtonDown()) return;

        if (e.eventComponent == &fftEnableButton)
        {
            auto panel = std::make_unique<FftRangePanel> (processor);
            juce::CallOutBox::launchAsynchronously (
                std::move (panel),
                fftEnableButton.getScreenBounds(),
                nullptr);
            return;
        }

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

    void refreshChannelNames()
    {
        for (int i = 0; i < 32; ++i)
            channelMuteButtons[i].setButtonText (processor.getChannelName (i));
    }

    static juce::File lastUsedFolder()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "SPLMeter";
        o.filenameSuffix      = "settings";
        o.osxLibrarySubFolder = "Application Support";
        juce::PropertiesFile prefs (o);
        const auto path = prefs.getValue ("lastUsedFolder");
        const juce::File f (path);
        return (path.isNotEmpty() && f.isDirectory())
                   ? f : juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
    }

    static void saveLastUsedFolder (const juce::File& fileOrFolder)
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "SPLMeter";
        o.filenameSuffix      = "settings";
        o.osxLibrarySubFolder = "Application Support";
        juce::PropertiesFile prefs (o);
        prefs.setValue ("lastUsedFolder",
            (fileOrFolder.isDirectory() ? fileOrFolder
                                        : fileOrFolder.getParentDirectory()).getFullPathName());
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
        styleBtn (line94Button,           juce::Colour (0xffff453a));
        styleBtn (correctionEnableButton,   juce::Colour (0xff34c759));
        correctionLoadButton.setColour  (juce::TextButton::buttonColourId,  bgBtn);
        correctionLoadButton.setColour  (juce::TextButton::textColourOffId, textOff);
        correctionClearButton.setColour (juce::TextButton::buttonColourId,  bgBtn);
        styleBtn (graphOverlayEnableButton, juce::Colour (0xff5ac8fa));
        graphOverlayLoadButton.setColour  (juce::TextButton::buttonColourId,  bgBtn);
        graphOverlayLoadButton.setColour  (juce::TextButton::textColourOffId, textOff);
        graphOverlayClearButton.setColour (juce::TextButton::buttonColourId,  bgBtn);
        styleBtn (graphOverlay2EnableButton, juce::Colour (0xffff9f0a));
        graphOverlay2LoadButton.setColour  (juce::TextButton::buttonColourId,  bgBtn);
        graphOverlay2LoadButton.setColour  (juce::TextButton::textColourOffId, textOff);
        graphOverlay2ClearButton.setColour (juce::TextButton::buttonColourId,  bgBtn);

        for (auto* label : { &calLabel, &holdLabel, &fftGainLabel, &fftSmoothLabel })
            label->setColour (juce::Label::textColourId, textLbl);

        const juce::Colour sectionCol = light ? juce::Colour (0xff6c6c70) : juce::Colour (0xff8e8e93);
        for (auto* lbl : { &sectionGeneralLabel, &sectionFftLabel, &sectionAnalysisLabel, &sectionChannelsLabel })
            lbl->setColour (juce::Label::textColourId, sectionCol);

        for (auto* s : { &calSlider, &holdSlider, &fftGainSlider, &fftSmoothSlider })
        {
            s->setColour (juce::Slider::textBoxTextColourId, textOff);
            s->setColour (juce::Slider::trackColourId,       juce::Colour (0xff5ac8fa));
            s->setColour (juce::Slider::thumbColourId,       juce::Colour (0xff5ac8fa));
        }

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

        bool l94On = processor.apvts.getRawParameterValue ("line94Enabled")->load() > 0.5f;
        line94Button.setToggleState (l94On, juce::dontSendNotification);

        bool corrOn = processor.apvts.getRawParameterValue ("correctionEnabled")->load() > 0.5f;
        correctionEnableButton.setToggleState (corrOn, juce::dontSendNotification);

        // Update load button text with filename (or prompt if none loaded)
        if (processor.isCorrectionLoaded())
            correctionLoadButton.setButtonText (processor.getCorrectionFileName());
        else
            correctionLoadButton.setButtonText ("Load Correction Filter");

        bool graphOn = processor.apvts.getRawParameterValue ("graphOverlayEnabled")->load() > 0.5f;
        graphOverlayEnableButton.setToggleState (graphOn, juce::dontSendNotification);

        if (processor.isGraphOverlayLoaded())
            graphOverlayLoadButton.setButtonText (processor.getGraphOverlayFileName());
        else
            graphOverlayLoadButton.setButtonText ("Load Graph Overlay 1");

        bool graph2On = processor.apvts.getRawParameterValue ("graphOverlay2Enabled")->load() > 0.5f;
        graphOverlay2EnableButton.setToggleState (graph2On, juce::dontSendNotification);

        if (processor.isGraphOverlay2Loaded())
            graphOverlay2LoadButton.setButtonText (processor.getGraphOverlay2FileName());
        else
            graphOverlay2LoadButton.setButtonText ("Load Graph Overlay 2");

        // Sync channel mute checkboxes
        for (int i = 0; i < 32; ++i)
        {
            bool muted = processor.apvts.getRawParameterValue ("channelMute" + juce::String (i))->load() > 0.5f;
            channelMuteButtons[i].setToggleState (muted, juce::dontSendNotification);
        }
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
    juce::TextButton fftRTAButton      { "RTA +3dB/Oct" };

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

    juce::TextButton fftEnableButton  { "FFT (right click for f limits)" };
    juce::TextButton lightModeButton  { "Light Mode" };
    juce::TextButton bandpassButton   { "20-20k BP" };
    juce::TextButton line94Button     { "94 dB Line" };
    juce::TextButton fullscreenButton { "Full Screen" };

    // Correction filter
    juce::TextButton correctionEnableButton { "Enable" };
    juce::TextButton correctionLoadButton   { "Load Correction Filter" };
    juce::TextButton correctionClearButton  { "Clear" };
    std::unique_ptr<juce::FileChooser> corrFileChooser_;

    // Graph overlay 1
    juce::TextButton graphOverlayEnableButton { "Enable" };
    juce::TextButton graphOverlayLoadButton   { "Load Graph Overlay 1" };
    juce::TextButton graphOverlayClearButton  { "Clear" };
    std::unique_ptr<juce::FileChooser> graphOverlayFileChooser_;

    // Graph overlay 2
    juce::TextButton graphOverlay2EnableButton { "Enable" };
    juce::TextButton graphOverlay2LoadButton   { "Load Graph Overlay 2" };
    juce::TextButton graphOverlay2ClearButton  { "Clear" };
    std::unique_ptr<juce::FileChooser> graphOverlay2FileChooser_;

    // Section header labels
    juce::Label sectionGeneralLabel, sectionFftLabel, sectionAnalysisLabel, sectionChannelsLabel;

    // Channel mute checkboxes
    ChannelMuteButton channelMuteButtons[32];

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
        content->setSize (620, 796);
        setContentOwned (content, true);
        setResizable (false, false);
    }

    void closeButtonPressed() override { setVisible (false); }

private:
    juce::TooltipWindow tooltipWindow { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};
