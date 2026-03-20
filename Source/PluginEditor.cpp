#include "PluginEditor.h"

static juce::PropertiesFile::Options splmeterPropsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "SPLMeter";
    o.filenameSuffix      = "settings";
    o.osxLibrarySubFolder = "Application Support";
    return o;
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

    // Monitor (mute) button - default OFF (muted)
    monitorButton.onClick = [this]
    {
        audioProcessor.setMonitorEnabled (monitorButton.getToggleState());
    };
    addAndMakeVisible (monitorButton);

    // Monitor gain fader (volume, dB) - same style as calibration fader
    monitorGainSlider.setSliderStyle (juce::Slider::LinearVertical);
    monitorGainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    monitorGainSlider.setTextValueSuffix (" dB");
    monitorGainSlider.setColour (juce::Slider::trackColourId,            juce::Colour (0xff5ac8fa));
    monitorGainSlider.setColour (juce::Slider::thumbColourId,            juce::Colour (0xff5ac8fa));
    monitorGainSlider.setColour (juce::Slider::textBoxTextColourId,      juce::Colours::white);
    monitorGainSlider.setColour (juce::Slider::textBoxOutlineColourId,   juce::Colours::transparentBlack);
    monitorGainSlider.setColour (juce::Slider::textBoxHighlightColourId, juce::Colour (0xff5ac8fa));
    monitorGainSlider.setTooltip ("Monitor output volume");
    addAndMakeVisible (monitorGainSlider);
    monitorGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "monitorGain", monitorGainSlider);

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
        toolsMenuButton.setVisible (!basicMode_);
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
        menu.addItem (3, "SoundDetective...");
#if JUCE_MAC
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
                else if (result == 3)
                    openWindow (soundDetectiveWindow,
                                [this] { return std::make_unique<SoundDetectiveWindow> (audioProcessor); });
#if JUCE_MAC
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
    meter.setPsychoVisible (!basicMode_);
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
    g.drawText ("v2.5.0   Build: " + juce::String (__DATE__) + "  " + __TIME__,
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
    resetButton.setBounds     (tbR (160));
    basicModeButton.setBounds (tbR (160));
    {
        auto noteCol = titleBar.removeFromRight (juce::roundToInt (160 * ts)).reduced (4, 4);
        clockLabel.setBounds (noteCol.removeFromTop (20));
        noteField.setBounds  (noteCol);
    }

    // Real Time / File / Monitor buttons — centred below the title text
    const int modeBtnW = juce::roundToInt (110 * ts);
    const int modeBtnH = 30;
    const int modeY    = 57;
    realTimeButton.setBounds (getWidth() / 2 - modeBtnW - 2, modeY, modeBtnW, modeBtnH);
    fileButton.setBounds     (getWidth() / 2 + 2,            modeY, modeBtnW, modeBtnH);
    monitorButton.setBounds  (getWidth() / 2 + 2 + modeBtnW + 6, modeY, modeBtnH, modeBtnH);
    monitorGainSlider.setBounds (monitorButton.getRight() + 4, 2, 52, 96);

    const int meterHeight = 240;
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

    monitorGainSlider.setColour (juce::Slider::trackColourId,       juce::Colour (0xff5ac8fa));
    monitorGainSlider.setColour (juce::Slider::thumbColourId,       juce::Colour (0xff5ac8fa));
    monitorGainSlider.setColour (juce::Slider::textBoxTextColourId, textOff);
    monitorGainSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
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

#if JUCE_MAC
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
                                                  const std::vector<LogEntry>& rows)
{
    stream.writeText ("Timestamp,Peak dB SPL,Peak dBA SPL,Peak dBC SPL,"
                      "RMS dB SPL,RMS dBA SPL,RMS dBC SPL,"
                      "Roughness (%),Fluctuation (%),Sharpness (acum),"
                      "Specific Loudness (sone),Psychoacoustic Annoyance\n",
                      false, false, nullptr);
    for (const auto& e : rows)
    {
        juce::String line =
            juce::Time (e.timestampMs).toString (true, true, true, true) + ","
            + juce::String (e.peakSPL,         2) + ","
            + juce::String (e.peakDBASPL,       2) + ","
            + juce::String (e.peakDBCSPL,       2) + ","
            + juce::String (e.rmsSPL,           2) + ","
            + juce::String (e.rmsDBASPL,        2) + ","
            + juce::String (e.rmsDBCSPL,        2) + ","
            + juce::String (e.roughness,        2) + ","
            + juce::String (e.fluctuation,      2) + ","
            + juce::String (e.sharpness,        3) + ","
            + juce::String (e.loudnessSone,     3) + ","
            + juce::String (e.psychoAnnoyance,  3) + "\n";
        stream.writeText (line, false, false, nullptr);
    }
}

void SPLMeterAudioProcessorEditor::doSaveCsv()
{
    auto rows = audioProcessor.copyLog();
    if (rows.empty()) return;
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save log as CSV",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("SPLMeter.csv"),
        "*.csv");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [rows] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            juce::FileOutputStream stream (file);
            if (!stream.openedOk()) return;
            stream.setPosition (0); stream.truncate();
            writeCsvRows (stream, rows);
        });
}

void SPLMeterAudioProcessorEditor::doSaveWav()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save recording as WAV",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("SPLMeter.wav"),
        "*.wav");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            audioProcessor.saveWavToFile (file);
        });
}

void SPLMeterAudioProcessorEditor::doSaveJpg()
{
    auto snapshot = createComponentSnapshot (getLocalBounds());
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save snapshot as JPEG",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("SPLMeter.jpg"),
        "*.jpg");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [snapshot] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
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
    auto snapshot = createComponentSnapshot (getLocalBounds());
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose destination folder",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, rows, snapshot] (const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();
            if (folder == juce::File{} || !folder.isDirectory()) return;

            auto ts   = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
            auto base = folder.getChildFile ("SPLMeter_" + ts);

            // CSV
            if (!rows.empty())
            {
                juce::FileOutputStream csv (base.withFileExtension ("csv"));
                if (csv.openedOk()) { csv.setPosition (0); csv.truncate(); writeCsvRows (csv, rows); }
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

//==============================================================================
void SPLMeterAudioProcessorEditor::timerCallback()
{
    meter.setValues (audioProcessor.getPeakSPL(),
                     audioProcessor.getPeakDBASPL(),
                     audioProcessor.getPeakDBCSPL(),
                     audioProcessor.getRoughness(),
                     audioProcessor.getFluctuation(),
                     audioProcessor.getSharpness(),
                     audioProcessor.getLoudnessSone(),
                     audioProcessor.getPsychoAnnoyance(),
                     audioProcessor.getImpulsiveness(),
                     audioProcessor.getTonality());
    float holdSecs = audioProcessor.apvts.getRawParameterValue ("peakHoldTime")->load();
    meter.setHoldDuration (static_cast<double> (holdSecs) * 1000.0);

    // Update clock label once per second
    {
        auto now = juce::Time::getCurrentTime();
        if (now.getSeconds() != lastClockSecond_)
        {
            lastClockSecond_ = now.getSeconds();
            clockLabel.setText (now.toString (true, true, true, false),
                                juce::dontSendNotification);
        }
    }

    updateTimeWeightButtons();

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

    // Auto-return to Real Time when file playback finishes
    if (fileMode && !audioProcessor.isFileModeActive())
    {
        fileMode = false;
        updateModeButtons();
    }
}

