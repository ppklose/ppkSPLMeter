#include "PluginEditor.h"
#include <map>

static juce::PropertiesFile::Options splmeterPropsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "SPLMeter";
    o.filenameSuffix      = "settings";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

static juce::File splmeterLastFolder()
{
    juce::PropertiesFile prefs (splmeterPropsOptions());
    const auto path = prefs.getValue ("lastUsedFolder");
    const juce::File f (path);
    return (path.isNotEmpty() && f.isDirectory())
               ? f
               : juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
}

static void splmeterSaveLastFolder (const juce::File& fileOrFolder)
{
    juce::PropertiesFile prefs (splmeterPropsOptions());
    prefs.setValue ("lastUsedFolder",
        (fileOrFolder.isDirectory() ? fileOrFolder
                                    : fileOrFolder.getParentDirectory()).getFullPathName());
}

void SPLMeterAudioProcessorEditor::saveSettings()
{
    juce::PropertiesFile prefs (splmeterPropsOptions());
    prefs.setValue ("basicMode", basicMode_);
    prefs.setValue ("lightMode", lightMode_);
    auto state = audioProcessor.apvts.copyState();
    if (auto xml = state.createXml())
        prefs.setValue ("apvtsState", xml->toString());
}

void SPLMeterAudioProcessorEditor::loadSettings()
{
    juce::PropertiesFile prefs (splmeterPropsOptions());
    basicMode_ = prefs.getBoolValue ("basicMode", true);
    lightMode_ = prefs.getBoolValue ("lightMode", false);

    auto xmlStr = prefs.getValue ("apvtsState");
    if (xmlStr.isNotEmpty())
        if (auto xml = juce::XmlDocument::parse (xmlStr))
            audioProcessor.apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
SPLMeterAudioProcessorEditor::SPLMeterAudioProcessorEditor (SPLMeterAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), log (p)
{
    addAndMakeVisible (meter);
    addAndMakeVisible (log);

    resetButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
    resetButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffff453a));
    resetButton.onClick = [this]
    {
        audioProcessor.resetPeak();
        audioProcessor.resetSessionPeak();
        audioProcessor.resetNoiseDose();
        audioProcessor.clearLog();
        meter.reset();
    };
    addAndMakeVisible (resetButton);

    saveMenuButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
    saveMenuButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    saveMenuButton.setTooltip ("Save CSV log, WAV recording, or screenshot");
    saveMenuButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Save CSV...");
        menu.addItem (2, "Save WAV...");
        menu.addItem (3, "Save Screenshot...");
        menu.addSeparator();
        menu.addItem (4, "Save All...");
        menu.addSeparator();
        menu.addItem (5, "Save Settings...");
        menu.addItem (6, "Load Settings...");
        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (saveMenuButton),
            [this] (int result)
            {
                switch (result)
                {
                    case 1: doSaveCsv(); break;
                    case 2: doSaveWav(); break;
                    case 3: doSaveJpg(); break;
                    case 4: doSaveAll(); break;
                    case 5: doSaveSettingsJson(); break;
                    case 6: doLoadSettingsJson(); break;
                    default: break;
                }
            });
    };
    addAndMakeVisible (saveMenuButton);

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
            "Open audio file", splmeterLastFolder(),
            "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File{}) return;
                splmeterSaveLastFolder (file);
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

    holdTimeLabel.setFont (juce::Font (juce::FontOptions().withHeight (15.0f)));
    holdTimeLabel.setJustificationType (juce::Justification::centredRight);
    holdTimeLabel.setColour (juce::Label::textColourId,          juce::Colour (0xffaeaeb2));
    holdTimeLabel.setColour (juce::Label::outlineWhenEditingColourId, juce::Colour (0xff5ac8fa));
    holdTimeLabel.setEditable (true, true);
    holdTimeLabel.setTooltip ("Peak hold time (s) - click to edit");
    holdTimeLabel.onEditorShow = [this]
    {
        if (auto* ed = holdTimeLabel.getCurrentTextEditor())
        {
            float val = audioProcessor.apvts.getRawParameterValue ("peakHoldTime")->load();
            ed->setText (juce::String (val, 1), false);
            ed->selectAll();
        }
    };
    holdTimeLabel.onTextChange = [this]
    {
        float val = holdTimeLabel.getText()
                        .trim()
                        .getFloatValue();
        val = juce::jlimit (0.1f, 5.0f, val);
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (
                audioProcessor.apvts.getParameter ("peakHoldTime")))
            rp->setValueNotifyingHost (rp->convertTo0to1 (val));
    };
    addAndMakeVisible (holdTimeLabel);

    // Pause / Play button
    pauseButton.setTooltip ("Pause / Resume (M)");
    pauseButton.onClick = [this]
    {
        const bool nowPaused = pauseButton.getToggleState();
        if (nowPaused) recordPauseStart();
        else           recordPauseEnd();
        audioProcessor.setPaused (nowPaused);
    };
    addAndMakeVisible (pauseButton);

    // DAW sync toggle — only relevant for AU / VST3 (not Standalone)
    const bool isPlugin = (audioProcessor.wrapperType != juce::AudioProcessor::wrapperType_Standalone);
    dawSyncToggle.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    dawSyncToggle.setTooltip ("Sync measurement to DAW transport: only logs when DAW is playing");
    dawSyncToggle.onClick = [this]
    {
        audioProcessor.setDawSync (dawSyncToggle.getToggleState());
    };
    dawSyncToggle.setVisible (isPlugin);
    addAndMakeVisible (dawSyncToggle);

    // Monitor (mute) button - default OFF (muted)
    monitorButton.onClick = [this]
    {
        audioProcessor.setMonitorEnabled (monitorButton.getToggleState());
    };
    addAndMakeVisible (monitorButton);

    // Monitor gain knob (volume, dB) — 270° rotary
    monitorGainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    monitorGainSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                           juce::MathConstants<float>::pi * 2.75f,
                                           true);
    monitorGainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    monitorGainSlider.setTextValueSuffix (" dB");
    monitorGainSlider.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff5ac8fa));
    monitorGainSlider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3a3c));
    monitorGainSlider.setColour (juce::Slider::thumbColourId,               juce::Colour (0xff5ac8fa));
    monitorGainSlider.setColour (juce::Slider::textBoxTextColourId,         juce::Colours::white);
    monitorGainSlider.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    monitorGainSlider.setColour (juce::Slider::textBoxHighlightColourId,    juce::Colour (0xff5ac8fa));
    monitorGainSlider.setTooltip ("Monitor output volume");
    addAndMakeVisible (monitorGainSlider);
    monitorGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "monitorGain", monitorGainSlider);

    outputVUMeter.setTooltip ("Monitor output level (peak, dBFS)");
    addAndMakeVisible (outputVUMeter);

    // Basic / Extended mode toggle
    basicModeButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xffff453a));  // red when Advanced
    basicModeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ac8fa));  // blue when Basic
    basicModeButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
    basicModeButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
    basicModeButton.setClickingTogglesState (true);
    basicModeButton.setTooltip ("You are currently in basic mode, click to go advanced mode.");
    basicModeButton.onClick = [this]
    {
        basicMode_ = basicModeButton.getToggleState();
        basicModeButton.setButtonText (basicMode_ ? "Basic Mode" : "Advanced Mode");
        basicModeButton.setTooltip (basicMode_
            ? "You are currently in basic mode, click to go advanced mode."
            : "You are currently in advanced mode, click to go to basic mode.");
        log.setVisible (!basicMode_);
        meter.setPsychoVisible (!basicMode_);
        meter.setDinVisible (!basicMode_);
        toolsMenuButton.setVisible (!basicMode_);
        markerButton.setVisible (!basicMode_);
        pauseButton.setVisible (!basicMode_);
        dawSyncToggle.setVisible (!basicMode_);
        if (basicMode_ && spectrogramWindow != nullptr)
            spectrogramWindow->setVisible (false);
        if (basicMode_)
        {
            extendedHeight_ = getHeight();
            const int basicH = 100 + 215 + 24;  // title + meter + build strip
            setResizeLimits (480, basicH, 3840, 2160);
            setSize (getWidth(), basicH);
        }
        else
        {
            setResizeLimits (480, 500, 3840, 2160);
            setSize (getWidth(), extendedHeight_);
        }

        saveSettings();
    };
    addAndMakeVisible (basicModeButton);

    // Settings button - opens the settings panel window
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

    toolsMenuButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
    toolsMenuButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    toolsMenuButton.setTooltip ("Spectrogram and analysis tools");
    toolsMenuButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Spectrogram");
        menu.addItem (4, "L_FFT");
        menu.addItem (3, "SoundDetective...");
        menu.addItem (5, "Long-term SPL");
        menu.addItem (6, "Impulse Fidelity");
#if SPLMETER_HAS_VISQOL || JUCE_MAC
        menu.addItem (2, "ViSQOL");
#endif
        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (toolsMenuButton),
            [this] (int result)
            {
                auto openWindow = [this] (auto& winPtr, auto makeWindow)
                {
                    if (winPtr == nullptr) winPtr = makeWindow();
                    if (winPtr->isVisible())
                    {
                        winPtr->setVisible (false);
                    }
                    else
                    {
                        auto pos = localPointToGlobal (toolsMenuButton.getBounds().getBottomLeft());
                        winPtr->setTopLeftPosition (pos);
                        winPtr->setVisible (true);
                        winPtr->toFront (true);
                    }
                };

                if (result == 1)
                    openWindow (spectrogramWindow,
                                [this] { return std::make_unique<SpectrogramWindow> (audioProcessor); });
                else if (result == 4)
                    openWindow (lfftWindow,
                                [this] { return std::make_unique<LFFTWindow> (audioProcessor); });
                else if (result == 3)
                {
                    openWindow (soundDetectiveWindow,
                                [this] { return std::make_unique<SoundDetectiveWindow> (audioProcessor); });
                    soundDetectiveWindow->onEventsCleared = [this]
                    {
                        allSoundEvents_.clear();
                        log.setSoundEvents ({});
                    };
                }
                else if (result == 5)
                    openWindow (longTermSPLWindow,
                                [this] { return std::make_unique<LongTermSPLWindow> (audioProcessor); });
                else if (result == 6)
                    openWindow (impulseFidelityWindow,
                                [this] { return std::make_unique<ImpulseFidelityWindow> (audioProcessor); });
#if SPLMETER_HAS_VISQOL || JUCE_MAC
                else if (result == 2)
                {
                    if (visqolWindow == nullptr)
                        visqolWindow = std::make_unique<VisqolWindow>();
                    if (visqolWindow->isVisible())
                    {
                        visqolWindow->setVisible (false);
                    }
                    else
                    {
                        auto pos = localPointToGlobal (toolsMenuButton.getBounds().getBottomLeft());
                        visqolWindow->setTopLeftPosition (pos);
                        visqolWindow->setVisible (true);
                        visqolWindow->toFront (true);
                    }
                }
#endif
            });
    };
    addAndMakeVisible (toolsMenuButton);

    // Clock label (line 1 - auto-updates every second)
    clockLabel.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (11.0f)));
    clockLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff2c2c2e));
    clockLabel.setColour (juce::Label::textColourId,       juce::Colour (0xffaeaeb2));
    clockLabel.setText (juce::Time::getCurrentTime().toString (true, true, true, false),
                        juce::dontSendNotification);
    addAndMakeVisible (clockLabel);

    // Note field (lines 2-4 - free text)
    noteField.setMultiLine (true, false);
    noteField.setReturnKeyStartsNewLine (true);
    noteField.setScrollbarsShown (false);
    noteField.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (11.0f)));
    noteField.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2c2c2e));
    noteField.setColour (juce::TextEditor::textColourId,       juce::Colours::white);
    noteField.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff48484a));
    noteField.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff5ac8fa));
    addAndMakeVisible (noteField);

    markerButton.setTooltip ("Add a marker to the timeline (Shift+M)");
    markerButton.onClick = [this] { triggerMarker(); };
    addAndMakeVisible (markerButton);

    setResizable (true, true);
    // Restore all persisted settings
    loadSettings();

    basicModeButton.setToggleState (basicMode_, juce::dontSendNotification);
    basicModeButton.setButtonText (basicMode_ ? "Basic Mode" : "Advanced Mode");
    basicModeButton.setTooltip (basicMode_
        ? "You are currently in basic mode, click to go advanced mode."
        : "You are currently in advanced mode, click to go to basic mode.");
    log.setVisible (!basicMode_);
    toolsMenuButton.setVisible (!basicMode_);
    markerButton.setVisible (!basicMode_);
    pauseButton.setVisible (!basicMode_);
    dawSyncToggle.setVisible (!basicMode_);
    meter.setPsychoVisible (!basicMode_);
    meter.setDinVisible (!basicMode_);
    const int basicH = 100 + 215 + 24;
    if (basicMode_)
    {
        setResizeLimits (480, basicH, 3840, 2160);
        setSize (1800, basicH);
    }
    else
    {
        setResizeLimits (480, 500, 3840, 2160);
        setSize (1800, extendedHeight_);
    }
    applyTheme (lightMode_);
    initialising_ = false;
    startTimerHz (30);
}

SPLMeterAudioProcessorEditor::~SPLMeterAudioProcessorEditor()
{
    stopTimer();
    saveSettings();
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
    g.drawText ("v3.3.0   Build: " + juce::String (__DATE__) + "  " + __TIME__,
                0, getHeight() - 22, getWidth(), 20,
                juce::Justification::centred, false);
}


void SPLMeterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto titleBar = area.removeFromTop (100);

    // Scale title-bar button widths proportionally so the layout fits on iPad
    // (reference width: 1800 px; minimum scale: 0.55 for 1024 pt iPad mini)
    const float ts = juce::jlimit (0.55f, 1.0f, (float) getWidth() / 1800.0f);
    auto tbL = [&] (int w) { return titleBar.removeFromLeft  (juce::roundToInt (w * ts)).reduced (10, 22); };
    auto tbR = [&] (int w) { return titleBar.removeFromRight (juce::roundToInt (w * ts)).reduced (10, 22); };

    settingsButton.setBounds  (tbL (160));
    saveMenuButton.setBounds  (tbL (140));
    toolsMenuButton.setBounds (tbL (140));
    pauseButton.setBounds     (tbL (50));
    dawSyncToggle.setBounds   (tbL (110));
    resetButton.setBounds     (tbR (160));
    basicModeButton.setBounds (tbR (160));
    {
        auto noteCol = titleBar.removeFromRight (juce::roundToInt (160 * ts)).reduced (4, 4);
        clockLabel.setBounds (noteCol.removeFromTop (20));
        noteField.setBounds  (noteCol);
        auto markerCol = titleBar.removeFromRight (juce::roundToInt (50 * ts)).reduced (4, 22);
        markerButton.setBounds (markerCol);
    }

    // Real Time / File / Monitor buttons — centred below the title text
    const int modeBtnW = juce::roundToInt (110 * ts);
    const int modeBtnH = 30;
    const int modeY    = 57;
    realTimeButton.setBounds (getWidth() / 2 - modeBtnW - 2, modeY, modeBtnW, modeBtnH);
    fileButton.setBounds     (getWidth() / 2 + 2,            modeY, modeBtnW, modeBtnH);
    monitorButton.setBounds  (getWidth() / 2 + 2 + modeBtnW + 6, modeY, modeBtnH, modeBtnH);
    monitorGainSlider.setBounds (monitorButton.getRight() + 4, 8, 60, 80);
    outputVUMeter.setBounds (monitorGainSlider.getRight() + 4, 4, 22, 84);

    const int meterHeight = 270;
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
    const int holdLabelW = 130;
    holdTimeLabel.setBounds (fastButton.getX() - holdLabelW - 4, btnY, holdLabelW, btnH);

    area.removeFromBottom (24);   // reserve strip for build time

    if (!basicMode_)
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
    for (auto* b : { &settingsButton, &toolsMenuButton, &saveMenuButton })
        styleBtn (*b);

    dawSyncToggle.setColour (juce::ToggleButton::textColourId, textOff);

    monitorGainSlider.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff5ac8fa));
    monitorGainSlider.setColour (juce::Slider::rotarySliderOutlineColourId,
                                 light ? juce::Colour (0xffc7c7cc) : juce::Colour (0xff3a3a3c));
    monitorGainSlider.setColour (juce::Slider::thumbColourId,               juce::Colour (0xff5ac8fa));
    monitorGainSlider.setColour (juce::Slider::textBoxTextColourId,         textOff);
    monitorGainSlider.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    // Reset button keeps red text regardless of theme
    resetButton.setColour (juce::TextButton::buttonColourId,  bgBtn);
    resetButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffff453a));

    // Mode + time-weight buttons keep their "on" accent colours; only restyle their off state
    for (auto* b : { &realTimeButton, &fileButton, &fastButton, &slowButton })
        styleBtn (*b);

    noteField.setColour (juce::TextEditor::backgroundColourId,
                         light ? juce::Colour (0xffe5e5ea) : juce::Colour (0xff2c2c2e));
    noteField.setColour (juce::TextEditor::textColourId,
                         light ? juce::Colour (0xff1c1c1e) : juce::Colours::white);
    noteField.setColour (juce::TextEditor::outlineColourId,
                         light ? juce::Colour (0xffb0b0b8) : juce::Colour (0xff48484a));
    noteField.repaint();

    meter.setLightMode (light);
    log.setLightMode   (light);

#if SPLMETER_HAS_VISQOL || JUCE_MAC
    if (visqolWindow != nullptr)
        visqolWindow->setLightMode (light);
#endif

    repaint();
    if (!initialising_) saveSettings();
}

//==============================================================================
// Save helpers
//==============================================================================
void SPLMeterAudioProcessorEditor::writeCsvRows (juce::OutputStream& stream,
                                                  const std::vector<LogEntry>& rows,
                                                  const std::vector<LogComponent::MarkerEvent>& markers)
{
    stream.writeText ("Timestamp,Peak dB SPL,Peak dBA SPL,Peak dBC SPL,"
                      "RMS dB SPL,RMS dBA SPL,RMS dBC SPL,"
                      "LAeq,LCeq,"
                      "Roughness (%),Fluctuation (%),Sharpness (acum),"
                      "Specific Loudness (sone),Psychoacoustic Annoyance,Marker\n",
                      false, false, nullptr);

    // Build a lookup: for each marker, find the closest log entry timestamp
    // (within 200 ms) and assign the marker text to it.
    std::map<juce::int64, juce::String> markerByTs;
    for (const auto& mk : markers)
    {
        juce::int64 bestTs = 0;
        juce::int64 bestDist = 200; // max 200 ms snap distance
        for (const auto& e : rows)
        {
            juce::int64 dist = std::abs (e.timestampMs - mk.timestampMs);
            if (dist < bestDist) { bestDist = dist; bestTs = e.timestampMs; }
        }
        if (bestTs != 0)
            markerByTs[bestTs] = mk.text.isEmpty() ? juce::String ("*") : mk.text;
    }

    // Running energy sums for LAeq / LCeq
    double cumSumA = 0.0, cumSumC = 0.0;
    int cumCount = 0;

    for (const auto& e : rows)
    {
        cumSumA += std::pow (10.0, static_cast<double> (e.rmsDBASPL) / 10.0);
        cumSumC += std::pow (10.0, static_cast<double> (e.rmsDBCSPL) / 10.0);
        ++cumCount;
        float laeq = static_cast<float> (10.0 * std::log10 (cumSumA / cumCount));
        float lceq = static_cast<float> (10.0 * std::log10 (cumSumC / cumCount));

        auto it = markerByTs.find (e.timestampMs);
        juce::String markerText = (it != markerByTs.end()) ? it->second : juce::String();

        // Escape CSV: if marker text contains commas or quotes, wrap in quotes
        if (markerText.containsAnyOf (",\""))
            markerText = "\"" + markerText.replace ("\"", "\"\"") + "\"";

        juce::String line =
            juce::Time (e.timestampMs).toString (true, true, true, true) + ","
            + juce::String (e.peakSPL,         2) + ","
            + juce::String (e.peakDBASPL,       2) + ","
            + juce::String (e.peakDBCSPL,       2) + ","
            + juce::String (e.rmsSPL,           2) + ","
            + juce::String (e.rmsDBASPL,        2) + ","
            + juce::String (e.rmsDBCSPL,        2) + ","
            + juce::String (laeq,               2) + ","
            + juce::String (lceq,               2) + ","
            + juce::String (e.roughness,        2) + ","
            + juce::String (e.fluctuation,      2) + ","
            + juce::String (e.sharpness,        3) + ","
            + juce::String (e.loudnessSone,     3) + ","
            + juce::String (e.psychoAnnoyance,  3) + ","
            + markerText + "\n";
        stream.writeText (line, false, false, nullptr);
    }
}

void SPLMeterAudioProcessorEditor::doSaveCsv()
{
    auto rows = audioProcessor.copyLog();
    if (rows.empty()) return;
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save log as CSV",
        splmeterLastFolder().getChildFile ("SPLMeter.csv"),
        "*.csv");
    auto markers = markerEvents_;   // snapshot for async lambda
    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [rows, markers] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            splmeterSaveLastFolder (file);
            juce::FileOutputStream stream (file);
            if (!stream.openedOk()) return;
            stream.setPosition (0); stream.truncate();
            writeCsvRows (stream, rows, markers);
        });
}

void SPLMeterAudioProcessorEditor::doSaveWav()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save recording as WAV",
        splmeterLastFolder().getChildFile ("SPLMeter.wav"),
        "*.wav");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            splmeterSaveLastFolder (file);
            audioProcessor.saveWavToFile (file);
        });
}

void SPLMeterAudioProcessorEditor::doSaveJpg()
{
    auto snapshot = createComponentSnapshot (getLocalBounds());
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save snapshot as JPEG",
        splmeterLastFolder().getChildFile ("SPLMeter.jpg"),
        "*.jpg");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [snapshot] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            splmeterSaveLastFolder (file);
            juce::FileOutputStream stream (file);
            if (!stream.openedOk()) return;
            stream.setPosition (0); stream.truncate();
            juce::JPEGImageFormat jpeg;
            jpeg.setQuality (0.95f);
            jpeg.writeImageToStream (snapshot, stream);
        });
}

void SPLMeterAudioProcessorEditor::doSaveAll()
{
    auto rows     = audioProcessor.copyLog();
    auto markers  = markerEvents_;
    auto snapshot = createComponentSnapshot (getLocalBounds());
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose destination folder", splmeterLastFolder());
    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, rows, markers, snapshot] (const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();
            if (folder == juce::File{} || !folder.isDirectory()) return;
            splmeterSaveLastFolder (folder);

            auto ts   = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
            auto base = folder.getChildFile ("SPLMeter_" + ts);

            // CSV
            if (!rows.empty())
            {
                juce::FileOutputStream csv (base.withFileExtension ("csv"));
                if (csv.openedOk()) { csv.setPosition (0); csv.truncate(); writeCsvRows (csv, rows, markers); }
            }

            // WAV
            audioProcessor.saveWavToFile (base.withFileExtension ("wav"));

            // JPG
            juce::FileOutputStream jpg (base.withFileExtension ("jpg"));
            if (jpg.openedOk())
            {
                jpg.setPosition (0); jpg.truncate();
                juce::JPEGImageFormat jpeg;
                jpeg.setQuality (0.95f);
                jpeg.writeImageToStream (snapshot, jpg);
            }
        });
}

void SPLMeterAudioProcessorEditor::doSaveSettingsJson()
{
    settingsJsonChooser_ = std::make_unique<juce::FileChooser> (
        "Save Settings",
        splmeterLastFolder().getChildFile ("SPLMeter_settings.json"),
        "*.json");

    settingsJsonChooser_->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension ("json");

            auto getRaw = [&] (const char* id)
            {
                return audioProcessor.apvts.getRawParameterValue (id)->load();
            };
            auto getBool = [&] (const char* id) -> bool { return getRaw (id) > 0.5f; };
            auto getInt  = [&] (const char* id) -> int  { return static_cast<int> (getRaw (id)); };

            // Root
            juce::var root (new juce::DynamicObject());
            auto* r = root.getDynamicObject();
            r->setProperty ("version", juce::var ("2.8"));

            // UI state
            juce::var ui (new juce::DynamicObject());
            ui.getDynamicObject()->setProperty ("basicMode", basicMode_);
            ui.getDynamicObject()->setProperty ("lightMode",  lightMode_);
            ui.getDynamicObject()->setProperty ("noteField",  noteField.getText());
            r->setProperty ("ui", ui);

            // APVTS scalar parameters
            juce::var params (new juce::DynamicObject());
            auto* p = params.getDynamicObject();
            p->setProperty ("calOffset",           getRaw ("calOffset"));
            p->setProperty ("peakHoldTime",         getRaw ("peakHoldTime"));
            p->setProperty ("splTimeWeight",         getInt ("splTimeWeight"));
            p->setProperty ("logDuration",           getRaw ("logDuration"));
            p->setProperty ("fftGain",               getRaw ("fftGain"));
            p->setProperty ("fftSmoothing",          getRaw ("fftSmoothing"));
            p->setProperty ("monitorGain",           getRaw ("monitorGain"));
            p->setProperty ("spectroGain",           getRaw ("spectroGain"));
            p->setProperty ("fftPeakHold",           getBool ("fftPeakHold"));
            p->setProperty ("fftDisplayMode",        getInt  ("fftDisplayMode"));
            p->setProperty ("fftBandRes",            getInt  ("fftBandRes"));
            p->setProperty ("fftWindowType",         getInt  ("fftWindowType"));
            p->setProperty ("fftOverlap",            getInt  ("fftOverlap"));
            p->setProperty ("fftRTAMode",            getBool ("fftRTAMode"));
            p->setProperty ("bandpassEnabled",       getBool ("bandpassEnabled"));
            p->setProperty ("line94Enabled",         getBool ("line94Enabled"));
            p->setProperty ("correctionEnabled",     getBool ("correctionEnabled"));
            p->setProperty ("graphOverlayEnabled",   getBool ("graphOverlayEnabled"));
            p->setProperty ("graphOverlay2Enabled",  getBool ("graphOverlay2Enabled"));
            p->setProperty ("fftLowerFreq",          getRaw ("fftLowerFreq"));
            p->setProperty ("fftUpperFreq",          getRaw ("fftUpperFreq"));
            p->setProperty ("splYMin",               getRaw ("splYMin"));
            p->setProperty ("splYMax",               getRaw ("splYMax"));
            r->setProperty ("parameters", params);

            // Channel mutes & names
            juce::Array<juce::var> mutes, names;
            for (int i = 0; i < 32; ++i)
            {
                bool muted = audioProcessor.apvts.getRawParameterValue (
                    "channelMute" + juce::String (i))->load() > 0.5f;
                mutes.add (juce::var (muted));
                names.add (juce::var (audioProcessor.getChannelName (i)));
            }
            r->setProperty ("channelMutes", juce::var (mutes));
            r->setProperty ("channelNames", juce::var (names));

            // MIDI CCs
            juce::var midi (new juce::DynamicObject());
            for (int i = 0; i < SPLMeterAudioProcessor::kNumMidiParams; ++i)
                midi.getDynamicObject()->setProperty (
                    SPLMeterAudioProcessor::kMidiParamIds[i],
                    audioProcessor.getMidiCC (i));
            r->setProperty ("midiCC", midi);

            splmeterSaveLastFolder (file);
            file.replaceWithText (juce::JSON::toString (root, true));
        });
}

void SPLMeterAudioProcessorEditor::doLoadSettingsJson()
{
    settingsJsonChooser_ = std::make_unique<juce::FileChooser> (
        "Load Settings", splmeterLastFolder(), "*.json");

    settingsJsonChooser_->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;

            auto parsed = juce::JSON::parse (file.loadFileAsString());
            if (!parsed.isObject()) return;

            // Helper: set an APVTS parameter by denormalised value
            auto setParam = [&] (const juce::String& id, float denorm)
            {
                if (auto* param = audioProcessor.apvts.getParameter (id))
                    param->setValueNotifyingHost (param->convertTo0to1 (denorm));
            };

            // UI state
            if (auto* ui = parsed["ui"].getDynamicObject())
            {
                noteField.setText (ui->getProperty ("noteField").toString(), false);
                applyTheme (static_cast<bool> (ui->getProperty ("lightMode")));
                basicModeButton.setToggleState (
                    static_cast<bool> (ui->getProperty ("basicMode")),
                    juce::sendNotification);
            }

            // APVTS scalar parameters
            if (auto* p = parsed["parameters"].getDynamicObject())
            {
                auto applyF = [&] (const char* id)
                {
                    if (p->hasProperty (id))
                        setParam (id, (float)(double) p->getProperty (id));
                };
                auto applyI = [&] (const char* id)
                {
                    if (p->hasProperty (id))
                        setParam (id, (float)(int) p->getProperty (id));
                };
                auto applyB = [&] (const char* id)
                {
                    if (p->hasProperty (id))
                        setParam (id, static_cast<bool> (p->getProperty (id)) ? 1.0f : 0.0f);
                };

                applyF ("calOffset");         applyF ("peakHoldTime");
                applyI ("splTimeWeight");      applyF ("logDuration");
                applyF ("fftGain");            applyF ("fftSmoothing");
                applyF ("monitorGain");        applyF ("spectroGain");
                applyB ("fftPeakHold");        applyI ("fftDisplayMode");
                applyI ("fftBandRes");         applyI ("fftWindowType");
                applyI ("fftOverlap");         applyB ("fftRTAMode");
                applyB ("bandpassEnabled");    applyB ("line94Enabled");
                applyB ("correctionEnabled");  applyB ("graphOverlayEnabled");
                applyB ("graphOverlay2Enabled");
                applyF ("fftLowerFreq");       applyF ("fftUpperFreq");
                applyF ("splYMin");            applyF ("splYMax");
            }

            // Channel mutes
            if (auto* mutes = parsed["channelMutes"].getArray())
                for (int i = 0; i < juce::jmin (32, mutes->size()); ++i)
                    setParam ("channelMute" + juce::String (i),
                              static_cast<bool> ((*mutes)[i]) ? 1.0f : 0.0f);

            // Channel names
            if (auto* names = parsed["channelNames"].getArray())
                for (int i = 0; i < juce::jmin (32, names->size()); ++i)
                    audioProcessor.setChannelName (i, (*names)[i].toString());

            // MIDI CCs
            if (auto* midi = parsed["midiCC"].getDynamicObject())
                for (int i = 0; i < SPLMeterAudioProcessor::kNumMidiParams; ++i)
                {
                    const char* id = SPLMeterAudioProcessor::kMidiParamIds[i];
                    if (midi->hasProperty (id))
                        audioProcessor.setMidiCC (i, (int) midi->getProperty (id));
                }

            // Refresh settings window channel names if open
            if (settingsWindow != nullptr)
                if (auto* sc = dynamic_cast<SettingsComponent*> (
                        settingsWindow->getContentComponent()))
                    sc->refreshChannelNames();

            splmeterSaveLastFolder (file);
            updateTimeWeightButtons();
            saveSettings();  // persist to disk
        });
}

//==============================================================================
void SPLMeterAudioProcessorEditor::recordPauseStart()
{
    pendingPauseWallMs_    = juce::Time::currentTimeMillis();
    pendingPauseAnalysisMs_ = pendingPauseWallMs_ - audioProcessor.getPauseOffsetMs();
}

void SPLMeterAudioProcessorEditor::recordPauseEnd()
{
    if (pendingPauseWallMs_ == 0) return;
    const juce::int64 dur = juce::Time::currentTimeMillis() - pendingPauseWallMs_;
    pauseEvents_.push_back (LogComponent::PauseEvent { pendingPauseAnalysisMs_, dur });
    pendingPauseWallMs_ = 0;
}

void SPLMeterAudioProcessorEditor::triggerMarker()
{
    const juce::int64 nowMs = juce::Time::currentTimeMillis() - audioProcessor.getPauseOffsetMs();

    auto* aw = new juce::AlertWindow ("Add Marker", "Enter marker text (or leave empty):",
                                       juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("markerText", "", "Label:");
    aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, nowMs, aw] (int result)
        {
            juce::String text;
            if (result == 1)
                text = aw->getTextEditorContents ("markerText").trim();

            markerEvents_.push_back (LogComponent::MarkerEvent { nowMs, text });
            log.setMarkerEvents (markerEvents_);
            delete aw;
        }), false);

    // Defer focus until after the window is on screen
    juce::Timer::callAfterDelay (50, [aw]
    {
        if (auto* te = aw->getTextEditor ("markerText"))
            te->grabKeyboardFocus();
    });
}

bool SPLMeterAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    // Ctrl/Cmd+M → toggle monitor (output) mute
    if (key.getKeyCode() == 'M'
        && (mods.isCtrlDown() || mods.isCommandDown())
        && ! mods.isShiftDown())
    {
        const bool newOn = ! audioProcessor.isMonitorEnabled();
        audioProcessor.setMonitorEnabled (newOn);
        monitorButton.setToggleState (newOn, juce::dontSendNotification);
        return true;
    }

    // Shift+M → add marker
    if (key.getTextCharacter() == 'M' && mods.isShiftDown())
    {
        triggerMarker();
        return true;
    }

    // m (without shift / ctrl / cmd) → toggle pause
    if (key.getTextCharacter() == 'm' && ! mods.isShiftDown()
        && ! mods.isCtrlDown() && ! mods.isCommandDown())
    {
        const bool nowPaused = !audioProcessor.isPaused();
        if (nowPaused) recordPauseStart();
        else           recordPauseEnd();
        audioProcessor.setPaused (nowPaused);
        pauseButton.setToggleState (nowPaused, juce::dontSendNotification);
        return true;
    }
    return false;
}

//==============================================================================
void SPLMeterAudioProcessorEditor::timerCallback()
{
    // IEC 61672 SPL = time-weighted RMS (FAST/SLOW). The bargraph follows this;
    // the in-meter hold tick captures the maximum recent reading (Lmax-style).
    meter.setValues (audioProcessor.getRMSSPL(),
                     audioProcessor.getRMSDBASPL(),
                     audioProcessor.getRMSDBCSPL(),
                     audioProcessor.getRoughness(),
                     audioProcessor.getFluctuation(),
                     audioProcessor.getSharpness(),
                     audioProcessor.getLoudnessSone(),
                     audioProcessor.getPsychoAnnoyance(),
                     audioProcessor.getImpulsiveness(),
                     audioProcessor.getTonality());

    // Compute LAeq / LCeq from log entries (energy average over logDuration)
    // and DIN 15905-5 LAeq,30min (sliding 30-min window over the available log)
    {
        auto logEntries = audioProcessor.copyLog();
        float laeq = 0.0f, lceq = 0.0f;
        float laeq30 = -999.0f;
        if (!logEntries.empty())
        {
            double sumA = 0.0, sumC = 0.0;
            for (const auto& e : logEntries)
            {
                sumA += std::pow (10.0, static_cast<double> (e.rmsDBASPL) / 10.0);
                sumC += std::pow (10.0, static_cast<double> (e.rmsDBCSPL) / 10.0);
            }
            double n = static_cast<double> (logEntries.size());
            laeq = static_cast<float> (10.0 * std::log10 (sumA / n));
            lceq = static_cast<float> (10.0 * std::log10 (sumC / n));

            // DIN 15905-5: LAeq over the last 30 min (1 800 000 ms)
            const juce::int64 latestMs = logEntries.back().timestampMs;
            constexpr juce::int64 kWindowMs = 30 * 60 * 1000;
            double sumA30 = 0.0;
            int    n30    = 0;
            for (auto it = logEntries.rbegin(); it != logEntries.rend(); ++it)
            {
                if (latestMs - it->timestampMs > kWindowMs)
                    break;
                sumA30 += std::pow (10.0, static_cast<double> (it->rmsDBASPL) / 10.0);
                ++n30;
            }
            if (n30 > 0)
                laeq30 = static_cast<float> (10.0 * std::log10 (sumA30 / n30));
        }
        meter.setLeq (laeq, lceq);
        meter.setDinValues (laeq30, audioProcessor.getSessionPeakDBCSPL());
        meter.setNioshDose (audioProcessor.getNoiseDosePct());
    }

    float holdSecs = audioProcessor.apvts.getRawParameterValue ("peakHoldTime")->load();
    meter.setHoldDuration (static_cast<double> (holdSecs) * 1000.0);
    if (!holdTimeLabel.isBeingEdited())
        holdTimeLabel.setText ("Hold time: " + juce::String (holdSecs, 1) + " s", juce::dontSendNotification);

    // Update clock label — show wall-clock time of last recorded data point
    {
        const juce::int64 wallMs = audioProcessor.getLastEntryWallMs();
        const auto t = wallMs > 0 ? juce::Time (wallMs) : juce::Time::getCurrentTime();
        if (t.getSeconds() != lastClockSecond_)
        {
            lastClockSecond_ = t.getSeconds();
            clockLabel.setText (t.toString (true, true, true, false),
                                juce::dontSendNotification);
        }
    }

    updateTimeWeightButtons();

    // Pause button: keep toggle state in sync with processor (may be changed externally)
    pauseButton.setToggleState (audioProcessor.isPaused(), juce::dontSendNotification);

    // Monitor button: keep in sync with processor (may be toggled from Long-term SPL window)
    monitorButton.setToggleState (audioProcessor.isMonitorEnabled(), juce::dontSendNotification);

    // Real Time / File buttons: reflect the processor's file-mode state. This picks
    // up Long-term SPL playback start/stop, window close, and end-of-file auto-stop.
    {
        const bool procFileMode = audioProcessor.isFileModeActive();
        if (fileMode != procFileMode)
        {
            fileMode = procFileMode;
            updateModeButtons();
        }
    }

    // Output VU meter: linear peak per channel from audio thread
    outputVUMeter.setLevels (audioProcessor.getOutputPeakL(),
                             audioProcessor.getOutputPeakR());

    // DAW sync indicator: dim the dawSyncToggle when synced but DAW is stopped
    if (audioProcessor.isDawSync())
        dawSyncToggle.setAlpha (audioProcessor.isDawPlaying() ? 1.0f : 0.5f);
    else
        dawSyncToggle.setAlpha (1.0f);

    // Apply correction file metadata (Sens Factor → calOffset, SERNO → noteField)
    if (audioProcessor.correctionMetaReady_.exchange (false))
    {
        if (audioProcessor.correctionSerno_.isNotEmpty())
            noteField.setText ("SERNO: " + audioProcessor.correctionSerno_,
                               juce::dontSendNotification);
    }

    // Poll sound detection events and forward to log and detective window
    {
        auto newEvents = audioProcessor.popSoundEvents();
        if (!newEvents.empty())
        {
            allSoundEvents_.insert (allSoundEvents_.end(), newEvents.begin(), newEvents.end());
            // Prune to keep max 1000 events
            if (allSoundEvents_.size() > 1000)
                allSoundEvents_.erase (allSoundEvents_.begin(),
                                       allSoundEvents_.begin() + 200);
            log.setSoundEvents (allSoundEvents_);
            if (soundDetectiveWindow != nullptr && soundDetectiveWindow->isVisible())
                soundDetectiveWindow->addEvents (newEvents);
        }
    }

    // Pass pause events to log (trim those older than the visible window)
    {
        const float logDuration = audioProcessor.apvts.getRawParameterValue ("logDuration")->load();
        const juce::int64 nowAnalysis = juce::Time::currentTimeMillis() - audioProcessor.getPauseOffsetMs();
        const juce::int64 cutoff = nowAnalysis - static_cast<juce::int64> (logDuration * 1000.0f);
        pauseEvents_.erase (std::remove_if (pauseEvents_.begin(), pauseEvents_.end(),
            [cutoff] (const LogComponent::PauseEvent& e) { return e.startMs < cutoff; }),
            pauseEvents_.end());
        log.setPauseEvents (pauseEvents_);
        markerEvents_.erase (std::remove_if (markerEvents_.begin(), markerEvents_.end(),
            [cutoff] (const LogComponent::MarkerEvent& e) { return e.timestampMs < cutoff; }),
            markerEvents_.end());
        log.setMarkerEvents (markerEvents_);
    }

    // Auto-return to Real Time when file playback finishes
    if (fileMode && !audioProcessor.isFileModeActive())
    {
        fileMode = false;
        updateModeButtons();
    }
}

