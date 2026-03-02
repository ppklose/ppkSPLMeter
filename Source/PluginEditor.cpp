#include "PluginEditor.h"

//==============================================================================
SPLMeterAudioProcessorEditor::SPLMeterAudioProcessorEditor (SPLMeterAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), log (p), spectrogram (p)
{
    addAndMakeVisible (meter);
    addAndMakeVisible (spectrogram);   // spectrogram behind log
    addAndMakeVisible (log);

    resetButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
    resetButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
    resetButton.onClick = [this]
    {
        audioProcessor.resetPeak();
        audioProcessor.clearLog();
        meter.reset();
    };
    addAndMakeVisible (resetButton);

    saveButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
    saveButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    saveButton.onClick = [this]
    {
        auto snapshot = createComponentSnapshot (getLocalBounds());
        fileChooser = std::make_unique<juce::FileChooser> (
            "Save snapshot as JPEG",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("SPLMeter.jpg"),
            "*.jpg");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [snapshot] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File{}) return;
                juce::FileOutputStream stream (file);
                if (stream.openedOk())
                {
                    stream.setPosition (0);
                    stream.truncate();
                    juce::JPEGImageFormat jpeg;
                    jpeg.setQuality (0.95f);
                    jpeg.writeImageToStream (snapshot, stream);
                }
            });
    };
    addAndMakeVisible (saveButton);

    // Real Time / File mode buttons
    auto setupModeBtn = [this] (juce::TextButton& btn)
    {
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ac8fa));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        btn.setClickingTogglesState (false);
        addAndMakeVisible (btn);
    };
    setupModeBtn (realTimeButton);
    setupModeBtn (fileButton);

    realTimeButton.onClick = [this]
    {
        audioProcessor.setFileMode (false);
        fileMode = false;
        updateModeButtons();
    };

    fileButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Open audio file",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory),
            "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File{}) return;
                audioProcessor.loadFile (file);
                fileMode = true;
                updateModeButtons();
            });
    };

    updateModeButtons();

    // FAST / SLOW time-weight toggle buttons
    auto setupTimeBtn = [this] (juce::TextButton& btn, int index)
    {
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ac8fa));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        btn.setClickingTogglesState (false);
        btn.onClick = [this, index]
        {
            auto* p = audioProcessor.apvts.getParameter ("splTimeWeight");
            p->setValueNotifyingHost (index == 0 ? 0.0f : 1.0f);
            audioProcessor.resetForModeSwitch();
            meter.reset();
            updateTimeWeightButtons();
        };
        addAndMakeVisible (btn);
    };
    setupTimeBtn (fastButton, 0);
    setupTimeBtn (slowButton, 1);
    updateTimeWeightButtons();

    // Calibration rotary
    calSlider.setSliderStyle (juce::Slider::Rotary);
    calSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    calSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f,
                                   true);
    calSlider.setColour (juce::Slider::rotarySliderFillColourId,   juce::Colour (0xff5ac8fa));
    calSlider.setColour (juce::Slider::textBoxTextColourId,        juce::Colours::white);
    calSlider.setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
    calSlider.setColour (juce::Slider::textBoxHighlightColourId,   juce::Colour (0xff5ac8fa));
    addAndMakeVisible (calSlider);

    calLabel.setText ("Calibration", juce::dontSendNotification);
    calLabel.setFont (juce::Font (juce::FontOptions().withHeight (20.0f)));
    calLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
    calLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (calLabel);

    calAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "calOffset", calSlider);

    // Peak hold rotary
    holdSlider.setSliderStyle (juce::Slider::Rotary);
    holdSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    holdSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f,
                                    true);
    holdSlider.setColour (juce::Slider::rotarySliderFillColourId,   juce::Colour (0xff5ac8fa));
    holdSlider.setColour (juce::Slider::textBoxTextColourId,        juce::Colours::white);
    holdSlider.setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
    holdSlider.setColour (juce::Slider::textBoxHighlightColourId,   juce::Colour (0xff5ac8fa));
    addAndMakeVisible (holdSlider);

    holdLabel.setText ("Hold Time", juce::dontSendNotification);
    holdLabel.setFont (juce::Font (juce::FontOptions().withHeight (20.0f)));
    holdLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
    holdLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (holdLabel);

    holdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "peakHoldTime", holdSlider);
    holdSlider.setTooltip ("Hold time in seconds");

    setSize (960, 1080);
    startTimerHz (30);
}

SPLMeterAudioProcessorEditor::~SPLMeterAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SPLMeterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1c1e));

    // Title bar
    g.setColour (juce::Colour (0xff2c2c2e));
    g.fillRect (0, 0, getWidth(), 100);

    g.setFont (juce::Font (juce::FontOptions().withHeight (28.0f).withStyle ("Bold")));
    g.setColour (juce::Colours::white);
    g.drawText ("PPK's SPLMeter", 0, 0, getWidth(), 52, juce::Justification::centred, false);

    // Build info strip at the bottom (not covered by any child)
    g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    g.setColour (juce::Colour (0xff555558));
    g.drawText (juce::String (JucePlugin_VersionString) + "   Build: " + __DATE__ + "  " + __TIME__,
                0, getHeight() - 22, getWidth(), 20,
                juce::Justification::centred, false);

}


void SPLMeterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto titleBar = area.removeFromTop (100);
    saveButton.setBounds  (titleBar.removeFromLeft  (160).reduced (10, 22));
    resetButton.setBounds (titleBar.removeFromRight (160).reduced (10, 22));

    // Hold Time section — right side, immediately left of Reset
    auto holdSection = titleBar.removeFromRight (160);
    holdLabel.setBounds  (holdSection.removeFromTop (22));
    holdSlider.setBounds (holdSection.withSizeKeepingCentre (80, 76));

    // Calibration section in title bar — left of centre
    auto calSection = titleBar.removeFromLeft (240);
    calLabel.setBounds  (calSection.removeFromTop (22));
    calSlider.setBounds (calSection.withSizeKeepingCentre (80, 76));

    // Real Time / File buttons — centred below the title text
    const int modeBtnW = 110;
    const int modeBtnH = 30;
    const int modeY    = 57;
    realTimeButton.setBounds (getWidth() / 2 - modeBtnW - 2, modeY, modeBtnW, modeBtnH);
    fileButton.setBounds     (getWidth() / 2 + 2,            modeY, modeBtnW, modeBtnH);

    const int meterHeight = 215;
    auto meterArea = area.removeFromTop (meterHeight);
    meter.setBounds (meterArea);

    // FAST/SLOW buttons right-aligned in the numeric readout row of the meter
    // MeterComponent: margin=20, peakReadoutY=20, readoutH=40
    const int btnMargin = 20;
    const int btnW      = 70;
    const int btnH      = 32;
    const int btnY      = meterArea.getY() + 20 + (40 - btnH) / 2;
    slowButton.setBounds (meterArea.getRight() - btnMargin - btnW,       btnY, btnW, btnH);
    fastButton.setBounds (meterArea.getRight() - btnMargin - btnW * 2 - 4, btnY, btnW, btnH);

    area.removeFromBottom (24);   // reserve strip for build time

    // Overlay: spectrogram and log share the same bounds
    log.setBounds         (area);
    spectrogram.setBounds (area);
}

//==============================================================================
void SPLMeterAudioProcessorEditor::updateModeButtons()
{
    realTimeButton.setToggleState (!fileMode, juce::dontSendNotification);
    fileButton.setToggleState     ( fileMode, juce::dontSendNotification);
}

void SPLMeterAudioProcessorEditor::updateTimeWeightButtons()
{
    int idx = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("splTimeWeight")->load());
    fastButton.setToggleState (idx == 0, juce::dontSendNotification);
    slowButton.setToggleState (idx == 1, juce::dontSendNotification);
}

void SPLMeterAudioProcessorEditor::timerCallback()
{
    meter.setValues (audioProcessor.getPeakSPL(),
                     audioProcessor.getPeakDBASPL(),
                     audioProcessor.getPeakDBCSPL(),
                     audioProcessor.getRoughness(),
                     audioProcessor.getFluctuation(),
                     audioProcessor.getSharpness(),
                     audioProcessor.getLoudnessSone());
    meter.setPsychoVisible (!log.isOff());
    spectrogram.setVisible (log.isSpectroEnabled());

    float calOffset = audioProcessor.apvts.getRawParameterValue ("calOffset")->load();
    float dbfs = 94.0f - calOffset;
    calSlider.setTooltip ("94 dB SPL = " + juce::String (dbfs, 1) + " dBFS");

    float holdSecs = audioProcessor.apvts.getRawParameterValue ("peakHoldTime")->load();
    meter.setHoldDuration (static_cast<double> (holdSecs) * 1000.0);

    updateTimeWeightButtons();

    // Auto-return to Real Time when file playback finishes
    if (fileMode && !audioProcessor.isFileModeActive())
    {
        fileMode = false;
        updateModeButtons();
    }
}
