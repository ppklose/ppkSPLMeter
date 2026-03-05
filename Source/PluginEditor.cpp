#include "PluginEditor.h"

//==============================================================================
SPLMeterAudioProcessorEditor::SPLMeterAudioProcessorEditor (SPLMeterAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), log (p)
{
    addAndMakeVisible (meter);
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

    saveCsvButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
    saveCsvButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    saveCsvButton.onClick = [this]
    {
        auto rows = audioProcessor.copyLog();
        if (rows.empty()) return;

        fileChooser = std::make_unique<juce::FileChooser> (
            "Save log as CSV",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("SPLMeter.csv"),
            "*.csv");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [rows] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File{}) return;
                juce::FileOutputStream stream (file);
                if (!stream.openedOk()) return;
                stream.setPosition (0);
                stream.truncate();

                stream.writeText ("Timestamp,Peak dB SPL,Peak dBA SPL,Peak dBC SPL,"
                                  "RMS dB SPL,RMS dBA SPL,RMS dBC SPL,"
                                  "Roughness (%),Fluctuation (%),Sharpness (acum),Loudness (sone)\n",
                                  false, false, nullptr);

                for (const auto& e : rows)
                {
                    juce::String line =
                        juce::Time (e.timestampMs).toString (true, true, true, true) + ","
                        + juce::String (e.peakSPL,      2) + ","
                        + juce::String (e.peakDBASPL,   2) + ","
                        + juce::String (e.peakDBCSPL,   2) + ","
                        + juce::String (e.rmsSPL,       2) + ","
                        + juce::String (e.rmsDBASPL,    2) + ","
                        + juce::String (e.rmsDBCSPL,    2) + ","
                        + juce::String (e.roughness,    2) + ","
                        + juce::String (e.fluctuation,  2) + ","
                        + juce::String (e.sharpness,    3) + ","
                        + juce::String (e.loudnessSone, 3) + "\n";
                    stream.writeText (line, false, false, nullptr);
                }
            });
    };
    addAndMakeVisible (saveCsvButton);

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

    // Monitor (mute) button — default OFF (muted)
    monitorButton.onClick = [this]
    {
        audioProcessor.setMonitorEnabled (monitorButton.getToggleState());
    };
    addAndMakeVisible (monitorButton);

    // Settings button — opens the settings panel window
    settingsButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
    settingsButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    settingsButton.onClick = [this]
    {
        if (settingsWindow == nullptr)
            settingsWindow = std::make_unique<SettingsWindow> (audioProcessor, *this,
                [this] (bool e)     { log.setFftEnabled (e); },
                [this] (bool light) { applyTheme (light); });

        if (settingsWindow->isVisible())
        {
            settingsWindow->setVisible (false);
        }
        else
        {
            // Position below the settings button
            auto pos = localPointToGlobal (settingsButton.getBounds().getBottomLeft());
            settingsWindow->setTopLeftPosition (pos);
            settingsWindow->setVisible (true);
            settingsWindow->toFront (true);
        }
    };
    addAndMakeVisible (settingsButton);

    setResizable (true, true);
    setResizeLimits (480, 500, 3840, 2160);
    setSize (1400, 900);
    startTimerHz (30);
}

SPLMeterAudioProcessorEditor::~SPLMeterAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SPLMeterAudioProcessorEditor::paint (juce::Graphics& g)
{
    const juce::Colour bgMain  = lightMode_ ? juce::Colour (0xfff2f2f7) : juce::Colour (0xff1c1c1e);
    const juce::Colour bgBar   = lightMode_ ? juce::Colour (0xffe5e5ea) : juce::Colour (0xff2c2c2e);
    const juce::Colour textPri = lightMode_ ? juce::Colour (0xff1c1c1e) : juce::Colours::white;
    const juce::Colour textFnt = lightMode_ ? juce::Colour (0xff8e8e93) : juce::Colour (0xff555558);

    g.fillAll (bgMain);

    // Title bar
    g.setColour (bgBar);
    g.fillRect (0, 0, getWidth(), 100);

    g.setFont (juce::Font (juce::FontOptions().withHeight (28.0f).withStyle ("Bold")));
    g.setColour (textPri);
    g.drawText ("SPLMeter", 0, 0, getWidth(), 52, juce::Justification::centred, false);

    // Build info strip at the bottom
    g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    g.setColour (textFnt);
    g.drawText (juce::String (JucePlugin_VersionString) + "   Build: " + __DATE__ + "  " + __TIME__,
                0, getHeight() - 22, getWidth(), 20,
                juce::Justification::centred, false);
}


void SPLMeterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto titleBar = area.removeFromTop (100);
    settingsButton.setBounds (titleBar.removeFromLeft (160).reduced (10, 22));
    saveButton.setBounds     (titleBar.removeFromLeft (160).reduced (10, 22));
    saveCsvButton.setBounds  (titleBar.removeFromLeft (160).reduced (10, 22));
    resetButton.setBounds (titleBar.removeFromRight (160).reduced (10, 22));

    // Real Time / File buttons — centred below the title text
    const int modeBtnW = 110;
    const int modeBtnH = 30;
    const int modeY    = 57;
    realTimeButton.setBounds (getWidth() / 2 - modeBtnW - 2, modeY, modeBtnW, modeBtnH);
    fileButton.setBounds     (getWidth() / 2 + 2,            modeY, modeBtnW, modeBtnH);
    monitorButton.setBounds  (getWidth() / 2 + 2 + modeBtnW + 6, modeY, modeBtnH, modeBtnH);

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

    log.setBounds (area);
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

void SPLMeterAudioProcessorEditor::applyTheme (bool light)
{
    lightMode_ = light;

    const juce::Colour bgBtn   = light ? juce::Colour (0xffd1d1d6) : juce::Colour (0xff3a3a3c);
    const juce::Colour textOff = light ? juce::Colour (0xff1c1c1e) : juce::Colours::white;

    auto styleBtn = [&] (juce::TextButton& btn)
    {
        btn.setColour (juce::TextButton::buttonColourId,  bgBtn);
        btn.setColour (juce::TextButton::textColourOffId, textOff);
    };
    for (auto* b : { &settingsButton, &resetButton, &saveButton, &saveCsvButton })
        styleBtn (*b);

    // Mode + time-weight buttons keep their "on" accent colours; only restyle their off state
    for (auto* b : { &realTimeButton, &fileButton, &fastButton, &slowButton })
        styleBtn (*b);

    meter.setLightMode (light);
    log.setLightMode   (light);

    repaint();
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

