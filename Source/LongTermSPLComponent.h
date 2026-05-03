#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "AWeightingFilter.h"
#include "CWeightingFilter.h"
#include <vector>
#include <cmath>

//==============================================================================
class LongTermSPLWindow : public juce::DocumentWindow
{
    //--------------------------------------------------------------------------
    struct AnalysisEntry
    {
        double timeSeconds = 0.0;
        float  lafDB       = -999.0f;
        float  lcfDB       = -999.0f;
        float  lzfDB       = -999.0f;
    };

    //--------------------------------------------------------------------------
    // CallOutBox panel for Y-axis range (dB SPL min/max)
    class YAxisPanel : public juce::Component
    {
    public:
        YAxisPanel (float& minRef, float& maxRef, std::function<void()> onChange)
            : minVal (minRef), maxVal (maxRef), onChange_ (std::move (onChange))
        {
            auto setup = [this] (juce::Slider& s, juce::Label& l,
                                 const juce::String& title, float val, float lo, float hi)
            {
                s.setSliderStyle (juce::Slider::LinearHorizontal);
                s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
                s.setTextValueSuffix (" dB");
                s.setRange (lo, hi, 1.0);
                s.setValue (val, juce::dontSendNotification);
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

            setup (minSlider, minLabel, "Min", minVal, -20.0f, 120.0f);
            setup (maxSlider, maxLabel, "Max", maxVal,  10.0f, 160.0f);

            minSlider.onValueChange = [this] { minVal = (float) minSlider.getValue(); if (onChange_) onChange_(); };
            maxSlider.onValueChange = [this] { maxVal = (float) maxSlider.getValue(); if (onChange_) onChange_(); };

            setSize (300, 76);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (8, 6);
            auto row1 = b.removeFromTop (28); b.removeFromTop (4);
            auto row2 = b.removeFromTop (28);
            minLabel.setBounds (row1.removeFromLeft (52)); minSlider.setBounds (row1);
            maxLabel.setBounds (row2.removeFromLeft (52)); maxSlider.setBounds (row2);
        }

    private:
        float& minVal;
        float& maxVal;
        std::function<void()> onChange_;
        juce::Slider minSlider, maxSlider;
        juce::Label  minLabel,  maxLabel;
    };

    //--------------------------------------------------------------------------
    // CallOutBox panel for X-axis range (time window in seconds)
    class XAxisPanel : public juce::Component
    {
    public:
        XAxisPanel (double& startRef, double& endRef, double fileLen,
                    std::function<void()> onChange)
            : startVal (startRef), endVal (endRef), onChange_ (std::move (onChange))
        {
            auto setup = [this] (juce::Slider& s, juce::Label& l,
                                 const juce::String& title, double val, double lo, double hi)
            {
                s.setSliderStyle (juce::Slider::LinearHorizontal);
                s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
                s.setTextValueSuffix (" s");
                s.setRange (lo, hi, 0.01);
                s.setValue (val, juce::dontSendNotification);
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

            setup (startSlider, startLabel, "Start", startVal, 0.0, fileLen);
            setup (endSlider,   endLabel,   "End",   endVal,   0.0, fileLen);

            resetButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            resetButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            resetButton.onClick = [this, fileLen]
            {
                startSlider.setValue (0.0, juce::sendNotificationSync);
                endSlider.setValue (fileLen, juce::sendNotificationSync);
            };
            addAndMakeVisible (resetButton);

            startSlider.onValueChange = [this] { startVal = startSlider.getValue(); if (onChange_) onChange_(); };
            endSlider.onValueChange   = [this] { endVal   = endSlider.getValue();   if (onChange_) onChange_(); };

            setSize (300, 108);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (8, 6);
            auto row1 = b.removeFromTop (28); b.removeFromTop (4);
            auto row2 = b.removeFromTop (28); b.removeFromTop (4);
            auto row3 = b.removeFromTop (28);
            startLabel.setBounds (row1.removeFromLeft (52)); startSlider.setBounds (row1);
            endLabel.setBounds   (row2.removeFromLeft (52)); endSlider.setBounds   (row2);
            resetButton.setBounds (row3.removeFromLeft (120));
        }

    private:
        double& startVal;
        double& endVal;
        std::function<void()> onChange_;
        juce::Slider startSlider, endSlider;
        juce::Label  startLabel,  endLabel;
        juce::TextButton resetButton { "Reset to full" };
    };

    //--------------------------------------------------------------------------
    // Speaker/mute button (mirrors MonitorButton from PluginEditor.h)
    class SpeakerButton : public juce::Button
    {
    public:
        SpeakerButton() : juce::Button ("Speaker")
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

            juce::Path body;
            body.addRectangle (cx - r * 1.2f, cy - r * 0.5f, r * 0.7f, r);
            g.setColour (iconCol);
            g.fillPath (body);

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
                waves.addArc (cx + r * 0.3f,  cy - r * 0.55f, r * 0.8f, r * 1.1f,
                              -juce::MathConstants<float>::pi * 0.35f,
                               juce::MathConstants<float>::pi * 0.35f, true);
                waves.addArc (cx + r * 0.45f, cy - r * 0.9f,  r * 1.3f, r * 1.8f,
                              -juce::MathConstants<float>::pi * 0.35f,
                               juce::MathConstants<float>::pi * 0.35f, true);
                g.strokePath (waves, juce::PathStrokeType (1.5f));
            }
            else
            {
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

    //--------------------------------------------------------------------------
    class Content : public juce::Component,
                    private juce::Timer
    {
    public:
        explicit Content (SPLMeterAudioProcessor& p)
            : processor (p)
        {
            formatManager.registerBasicFormats();

            // ---- Transport bar ----
            // Play/Pause button
            playPauseButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
            playPauseButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff34c759));
            playPauseButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            playPauseButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
            playPauseButton.setClickingTogglesState (true);
            playPauseButton.onClick = [this]
            {
                if (!fileLoaded_) return;
                if (playPauseButton.getToggleState())
                {
                    processor.setMonitorEnabled (true);
                    speakerButton.setToggleState (true, juce::dontSendNotification);
                    processor.startPlayback();
                }
                else
                    processor.stopPlayback();
            };
            addAndMakeVisible (playPauseButton);

            // Stop button
            stopButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            stopButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            stopButton.onClick = [this]
            {
                if (!fileLoaded_) return;
                processor.stopPlayback();
                processor.seekTo (0.0);
                playPauseButton.setToggleState (false, juce::dontSendNotification);
            };
            addAndMakeVisible (stopButton);

            // Position label
            posLabel.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (13.0f)));
            posLabel.setColour (juce::Label::textColourId, juce::Colours::white);
            posLabel.setJustificationType (juce::Justification::centred);
            posLabel.setText ("0:00 / 0:00", juce::dontSendNotification);
            addAndMakeVisible (posLabel);

            // Monitor/speaker button (mirrors MonitorButton)
            speakerButton.setToggleState (processor.isMonitorEnabled(), juce::dontSendNotification);
            speakerButton.onClick = [this]
            {
                processor.setMonitorEnabled (speakerButton.getToggleState());
            };
            addAndMakeVisible (speakerButton);

            // Volume slider (mirrors monitorGainSlider from main editor)
            volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
            volumeSlider.setTextValueSuffix (" dB");
            volumeSlider.setColour (juce::Slider::trackColourId,            juce::Colour (0xff5ac8fa));
            volumeSlider.setColour (juce::Slider::thumbColourId,            juce::Colour (0xff5ac8fa));
            volumeSlider.setColour (juce::Slider::textBoxTextColourId,      juce::Colours::white);
            volumeSlider.setColour (juce::Slider::textBoxOutlineColourId,   juce::Colours::transparentBlack);
            volumeSlider.setColour (juce::Slider::textBoxHighlightColourId, juce::Colour (0xff5ac8fa));
            addAndMakeVisible (volumeSlider);
            volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.apvts, "monitorGain", volumeSlider);

            // ---- File / analysis controls ----
            loadButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            loadButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            loadButton.onClick = [this] { doLoadFile(); };
            addAndMakeVisible (loadButton);

            refreshButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            refreshButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            refreshButton.setTooltip ("Re-run the analysis with the current Calibration value.");
            refreshButton.onClick = [this]
            {
                if (loadedFile_ != juce::File{} && loadedFile_.existsAsFile())
                    analyseFile (loadedFile_);
            };
            refreshButton.setEnabled (false);
            addAndMakeVisible (refreshButton);

            calToInputButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            calToInputButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            calToInputButton.setTooltip ("Calibrate to a 94 dB reference: adjusts the Calibration "
                                         "offset so the loaded file's unweighted (Z) Leq reads "
                                         "94 dB SPL, then re-runs the analysis. Use with a "
                                         "94 dB / 1 kHz calibrator recording. Matches the "
                                         "Settings panel's Cal to Input behaviour.");
            calToInputButton.onClick = [this]
            {
                if (loadedFile_ == juce::File{} || ! loadedFile_.existsAsFile() || data.empty())
                {
                    juce::NativeMessageBox::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "Cal to Input",
                        "Load an audio file first.");
                    return;
                }

                // Energy-average the unweighted (Z) FAST level across the file, matching
                // the Settings panel's Cal to Input which references rmsSPL (Z-weighted).
                const int skip = std::min (4, static_cast<int> (data.size()) - 1);
                double sumZ = 0.0;
                int    count = 0;
                for (int i = skip; i < static_cast<int> (data.size()); ++i)
                {
                    if (data[i].lzfDB > -100.0f)
                    {
                        sumZ += std::pow (10.0, data[i].lzfDB / 10.0);
                        ++count;
                    }
                }

                if (count == 0)
                {
                    juce::NativeMessageBox::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "Cal to Input",
                        "No usable level in the file.");
                    return;
                }

                const float lzeq = static_cast<float> (10.0 * std::log10 (sumZ / count));
                const float currentOffset = processor.apvts.getRawParameterValue ("calOffset")->load();
                const float delta         = 94.0f - lzeq;
                const float newOffset     = juce::jlimit (80.0f, 180.0f, currentOffset + delta);

                if (auto* param = processor.apvts.getParameter ("calOffset"))
                {
                    const auto& range = param->getNormalisableRange();
                    param->setValueNotifyingHost (range.convertTo0to1 (newOffset));
                }

                // Re-analyse so all displayed values pick up the new offset
                analyseFile (loadedFile_);
            };
            calToInputButton.setEnabled (false);
            addAndMakeVisible (calToInputButton);

            exportCsvButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            exportCsvButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            exportCsvButton.onClick = [this] { doExportCsv(); };
            exportCsvButton.setEnabled (false);
            addAndMakeVisible (exportCsvButton);

            fileInfoLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            fileInfoLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
            fileInfoLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (fileInfoLabel);

            auto setupToggle = [this] (juce::ToggleButton& tb, const juce::String& name,
                                       juce::Colour col, bool defaultOn)
            {
                tb.setButtonText (name);
                tb.setToggleState (defaultOn, juce::dontSendNotification);
                tb.setColour (juce::ToggleButton::textColourId, col);
                tb.setColour (juce::ToggleButton::tickColourId, col);
                tb.onClick = [this] { repaint(); };
                addAndMakeVisible (tb);
            };
            setupToggle (lafToggle, "LAF", juce::Colour (0xffbf5af2), true);
            setupToggle (lcfToggle, "LCF", juce::Colour (0xffffe620), false);
            setupToggle (lzfToggle, "LZF", juce::Colour (0xff34c759), false);

            statsLabel.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (12.0f)));
            statsLabel.setColour (juce::Label::textColourId, juce::Colours::white);
            statsLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (statsLabel);

            dinStatsLabel.setFont (juce::Font (juce::FontOptions().withName ("Courier New")
                                                                  .withHeight (12.0f).withStyle ("Bold")));
            dinStatsLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8e8e93));
            dinStatsLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (dinStatsLabel);

            statusLabel.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8e8e93));
            statusLabel.setJustificationType (juce::Justification::centredLeft);
            statusLabel.setText ("Load an audio file to begin analysis.", juce::dontSendNotification);
            addAndMakeVisible (statusLabel);

            // Y-axis zoom button
            yZoomButton_.setButtonText ("");
            yZoomButton_.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
            yZoomButton_.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
            yZoomButton_.onClick = [this]
            {
                auto panel = std::make_unique<YAxisPanel> (yMin_, yMax_, [this] { repaint(); });
                juce::CallOutBox::launchAsynchronously (
                    std::move (panel), yZoomButton_.getScreenBounds(), nullptr);
            };
            addAndMakeVisible (yZoomButton_);

            // X-axis zoom button
            xZoomButton_.setButtonText ("");
            xZoomButton_.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
            xZoomButton_.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
            xZoomButton_.onClick = [this]
            {
                if (fileDuration <= 0.0) return;
                auto panel = std::make_unique<XAxisPanel> (xViewStart_, xViewEnd_, fileDuration,
                                                           [this] { repaint(); });
                juce::CallOutBox::launchAsynchronously (
                    std::move (panel), xZoomButton_.getScreenBounds(), nullptr);
            };
            addAndMakeVisible (xZoomButton_);

            setWantsKeyboardFocus (true);
            // Keep focus on the Content so spacebar always toggles play/pause
            for (juce::Component* c : { (juce::Component*) &playPauseButton,
                                        (juce::Component*) &stopButton,
                                        (juce::Component*) &loadButton,
                                        (juce::Component*) &refreshButton,
                                        (juce::Component*) &exportCsvButton,
                                        (juce::Component*) &speakerButton,
                                        (juce::Component*) &yZoomButton_,
                                        (juce::Component*) &xZoomButton_ })
                c->setMouseClickGrabsKeyboardFocus (false);

            startTimerHz (30);
        }

        ~Content() override { stopTimer(); }

        bool keyPressed (const juce::KeyPress& key) override
        {
            // Spacebar → toggle play / pause
            if (key == juce::KeyPress::spaceKey)
            {
                if (! fileLoaded_) return true;
                playPauseButton.triggerClick();
                return true;
            }
            return false;
        }

        void visibilityChanged() override
        {
            if (isVisible())
                grabKeyboardFocus();
        }

        //----------------------------------------------------------------------
        void resized() override
        {
            auto r = getLocalBounds().reduced (6);

            // Transport bar (top row)
            auto transport = r.removeFromTop (28).reduced (0, 2);
            playPauseButton.setBounds (transport.removeFromLeft (60));
            transport.removeFromLeft (4);
            stopButton.setBounds (transport.removeFromLeft (46));
            transport.removeFromLeft (8);
            posLabel.setBounds (transport.removeFromLeft (170));
            transport.removeFromLeft (8);

            // Volume controls on the right side of transport
            auto volArea = transport.removeFromRight (180);
            speakerButton.setBounds (volArea.removeFromLeft (28));
            volArea.removeFromLeft (4);
            volumeSlider.setBounds (volArea);

            r.removeFromTop (4);

            // Row 2: load + refresh + cal + export + file info
            auto row2 = r.removeFromTop (28).reduced (0, 2);
            loadButton.setBounds        (row2.removeFromLeft (160));
            row2.removeFromLeft (8);
            refreshButton.setBounds     (row2.removeFromLeft (90));
            row2.removeFromLeft (8);
            calToInputButton.setBounds  (row2.removeFromLeft (110));
            row2.removeFromLeft (8);
            exportCsvButton.setBounds   (row2.removeFromLeft (120));
            row2.removeFromLeft (8);
            fileInfoLabel.setBounds     (row2);
            r.removeFromTop (4);

            // Row 3: toggles + stats
            auto row3 = r.removeFromTop (24).reduced (0, 2);
            lafToggle.setBounds (row3.removeFromLeft (60));
            lcfToggle.setBounds (row3.removeFromLeft (60));
            lzfToggle.setBounds (row3.removeFromLeft (60));
            row3.removeFromLeft (8);
            statsLabel.setBounds (row3);

            // Row 3b: DIN 15905-5 stats
            auto row3b = r.removeFromTop (20).reduced (0, 1);
            row3b.removeFromLeft (188);  // align with stats text (after toggle buttons + gap)
            dinStatsLabel.setBounds (row3b);
            r.removeFromTop (4);

            // Bottom: status
            auto rowB = r.removeFromBottom (20);
            statusLabel.setBounds (rowB);
            r.removeFromBottom (4);

            plotArea = r;

            // Position zoom buttons relative to plot area
            const float leftMargin  = 46.0f;
            const float botMargin   = 24.0f;
            auto pa = plotArea.toFloat();
            float plotCentreY = pa.getY() + 8.0f + (pa.getHeight() - 8.0f - botMargin) * 0.5f;
            float plotCentreX = pa.getX() + leftMargin
                                + (pa.getWidth() - leftMargin - 12.0f) * 0.5f;

            yZoomButton_.setBounds (static_cast<int> (pa.getX()),
                                    static_cast<int> (plotCentreY + 40.0f),
                                    28, 28);
            xZoomButton_.setBounds (static_cast<int> (plotCentreX - 14.0f),
                                    static_cast<int> (pa.getBottom() - botMargin + 2.0f),
                                    28, 28);
        }

        //----------------------------------------------------------------------
        void timerCallback() override
        {
            if (!fileLoaded_) return;

            // Sync play/pause button state with processor
            bool playing = processor.isFilePlaying();
            if (playing != playPauseButton.getToggleState())
                playPauseButton.setToggleState (playing, juce::dontSendNotification);

            // Sync speaker button state
            bool monitoring = processor.isMonitorEnabled();
            if (monitoring != speakerButton.getToggleState())
                speakerButton.setToggleState (monitoring, juce::dontSendNotification);

            // Update position label
            double pos = processor.getFilePosition();
            if (bwfOffsetSec_ > 0.0)
                posLabel.setText (formatTimecode (pos + bwfOffsetSec_) + " / "
                                  + formatTimecode (fileDuration + bwfOffsetSec_),
                                  juce::dontSendNotification);
            else
                posLabel.setText (formatDuration (pos) + " / " + formatDuration (fileDuration),
                                  juce::dontSendNotification);

            // Repaint for playhead
            if (playing)
                repaint();
        }

        //----------------------------------------------------------------------
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (!fileLoaded_ || plotArea.isEmpty()) return;
            seekFromMouse (event);
        }

        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (!fileLoaded_ || plotArea.isEmpty()) return;
            seekFromMouse (event);
        }

        void seekFromMouse (const juce::MouseEvent& event)
        {
            const float leftMargin = 46.0f;
            const float rightMargin = 12.0f;
            auto pa = plotArea.toFloat();
            float plotL = pa.getX() + leftMargin;
            float plotW = pa.getWidth() - leftMargin - rightMargin;
            if (plotW < 10.0f) return;

            // Only act if click is within the plot region vertically
            if (event.y < pa.getY() || event.y > pa.getBottom()) return;
            if (event.x < plotL || event.x > plotL + plotW) return;

            float frac = (event.x - plotL) / plotW;
            double seekTime = xViewStart_ + frac * (xViewEnd_ - xViewStart_);
            seekTime = juce::jlimit (0.0, fileDuration, seekTime);
            processor.seekTo (seekTime);
            repaint();
        }

        //----------------------------------------------------------------------
        void mouseWheelMove (const juce::MouseEvent& event,
                             const juce::MouseWheelDetails& wheel) override
        {
            if (data.empty() || fileDuration <= 0.0)
                return;

            if (!plotArea.contains (event.getPosition()))
                return;

            const float leftMargin = 46.0f;
            const float rightMargin = 12.0f;
            auto pa = plotArea.toFloat();
            float plotL = pa.getX() + leftMargin;
            float plotW = pa.getWidth() - leftMargin - rightMargin;
            if (plotW < 10.0f) return;

            float frac = juce::jlimit (0.0f, 1.0f,
                (static_cast<float> (event.getPosition().x) - plotL) / plotW);

            double viewLen = xViewEnd_ - xViewStart_;
            double cursorTime = xViewStart_ + frac * viewLen;

            double zoomFactor = (wheel.deltaY > 0) ? 0.8 : 1.25;
            double newLen = juce::jlimit (0.01, fileDuration, viewLen * zoomFactor);

            double newStart = cursorTime - frac * newLen;
            double newEnd   = newStart + newLen;

            if (newStart < 0.0)        { newStart = 0.0; newEnd = newLen; }
            if (newEnd > fileDuration) { newEnd = fileDuration; newStart = newEnd - newLen; }
            newStart = std::max (0.0, newStart);

            xViewStart_ = newStart;
            xViewEnd_   = newEnd;
            repaint();
        }

        //----------------------------------------------------------------------
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1c1c1e));

            // Transport bar background
            auto transportBg = getLocalBounds().reduced (6).removeFromTop (28);
            g.setColour (juce::Colour (0xff2c2c2e));
            g.fillRoundedRectangle (transportBg.toFloat(), 4.0f);

            if (data.empty() || plotArea.isEmpty())
                return;

            const float leftMargin  = 46.0f;
            const float rightMargin = 12.0f;
            const float topMargin   = 8.0f;
            const float botMargin   = 24.0f;

            const auto pa = plotArea.toFloat();
            const float plotL = pa.getX() + leftMargin;
            const float plotR = pa.getRight() - rightMargin;
            const float plotT = pa.getY() + topMargin;
            const float plotB = pa.getBottom() - botMargin;
            const float plotW = plotR - plotL;
            const float plotH = plotB - plotT;

            if (plotW < 10.0f || plotH < 10.0f)
                return;

            const float yRange = yMax_ - yMin_;

            auto yToScreen = [&] (float db) -> float
            {
                return plotB - ((db - yMin_) / yRange) * plotH;
            };

            const double xViewLen = xViewEnd_ - xViewStart_;
            auto xToScreen = [&] (double t) -> float
            {
                return plotL + static_cast<float> (((t - xViewStart_) / xViewLen) * plotW);
            };

            // Grid -- Y
            g.setColour (juce::Colour (0xff3a3a3c));
            float gridStep = (yRange > 60.0f) ? 20.0f : 10.0f;
            float firstGrid = std::ceil (yMin_ / gridStep) * gridStep;
            for (float db = firstGrid; db <= yMax_; db += gridStep)
            {
                float y = yToScreen (db);
                g.drawHorizontalLine (static_cast<int> (y), plotL, plotR);
            }

            // Y-axis labels
            g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
            g.setColour (juce::Colour (0xffaeaeb2));
            for (float db = firstGrid; db <= yMax_; db += gridStep)
            {
                float y = yToScreen (db);
                g.drawText (juce::String (static_cast<int> (db)),
                            static_cast<int> (pa.getX()), static_cast<int> (y - 6),
                            static_cast<int> (leftMargin - 4), 12,
                            juce::Justification::centredRight);
            }

            // Grid -- X
            const int numXTicks = juce::jlimit (3, 8, static_cast<int> (plotW / 90));
            const bool hasBwf = (bwfOffsetSec_ > 0.0);
            const int xLabelW = hasBwf ? 72 : 48;
            for (int i = 0; i <= numXTicks; ++i)
            {
                double t = xViewStart_ + xViewLen * i / numXTicks;
                float x = xToScreen (t);
                g.setColour (juce::Colour (0xff3a3a3c));
                g.drawVerticalLine (static_cast<int> (x), plotT, plotB);

                g.setColour (juce::Colour (0xffaeaeb2));
                g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
                juce::String label = hasBwf ? formatTimecode (t + bwfOffsetSec_)
                                            : formatDuration (t);
                g.drawText (label,
                            static_cast<int> (x - xLabelW / 2), static_cast<int> (plotB + 2),
                            xLabelW, 16, juce::Justification::centred);
            }

            // Plot border
            g.setColour (juce::Colour (0xff48484a));
            g.drawRect (plotL, plotT, plotW, plotH, 1.0f);

            // Clip to plot area
            g.saveState();
            g.reduceClipRegion (static_cast<int> (plotL), static_cast<int> (plotT),
                                static_cast<int> (plotW), static_cast<int> (plotH));

            // Waveform overview (background)
            if (!waveformOverview.empty() && waveformPeak > 1e-10f)
            {
                const float centreY = (plotT + plotB) * 0.5f;
                const float halfH   = plotH * 0.45f;
                const int   numBuckets = static_cast<int> (waveformOverview.size());

                juce::Path waveTop, waveBot;
                bool started = false;
                for (int i = 0; i < numBuckets; ++i)
                {
                    double bucketTime = fileDuration * i / numBuckets;
                    if (bucketTime < xViewStart_ || bucketTime > xViewEnd_) continue;

                    float x = xToScreen (bucketTime);
                    float yTop = centreY - (waveformOverview[i].maxVal / waveformPeak) * halfH;
                    float yBot = centreY - (waveformOverview[i].minVal / waveformPeak) * halfH;
                    if (!started)
                    {
                        waveTop.startNewSubPath (x, yTop);
                        waveBot.startNewSubPath (x, yBot);
                        started = true;
                    }
                    else
                    {
                        waveTop.lineTo (x, yTop);
                        waveBot.lineTo (x, yBot);
                    }
                }

                if (started)
                {
                    juce::Path waveFill;
                    waveFill.addPath (waveTop);
                    juce::Path waveBotReversed;
                    bool revStarted = false;
                    for (int i = numBuckets - 1; i >= 0; --i)
                    {
                        double bucketTime = fileDuration * i / numBuckets;
                        if (bucketTime < xViewStart_ || bucketTime > xViewEnd_) continue;
                        float x = xToScreen (bucketTime);
                        float yBot = centreY - (waveformOverview[i].minVal / waveformPeak) * halfH;
                        if (!revStarted) { waveBotReversed.startNewSubPath (x, yBot); revStarted = true; }
                        else              waveBotReversed.lineTo (x, yBot);
                    }
                    waveFill.addPath (waveBotReversed);
                    waveFill.closeSubPath();

                    g.setColour (juce::Colour (0xff5ac8fa).withAlpha (0.12f));
                    g.fillPath (waveFill);
                    g.setColour (juce::Colour (0xff5ac8fa).withAlpha (0.25f));
                    g.strokePath (waveTop, juce::PathStrokeType (0.5f));
                    g.strokePath (waveBot, juce::PathStrokeType (0.5f));
                }
            }

            // Draw series
            auto drawSeries = [&] (bool visible, juce::Colour col,
                                   float (AnalysisEntry::*member))
            {
                if (!visible || data.empty()) return;
                juce::Path path;
                bool started = false;
                for (const auto& e : data)
                {
                    if (e.timeSeconds < xViewStart_ || e.timeSeconds > xViewEnd_) continue;
                    float val = e.*member;
                    if (val < -900.0f) continue;
                    float x = xToScreen (e.timeSeconds);
                    float y = yToScreen (val);
                    if (!started) { path.startNewSubPath (x, y); started = true; }
                    else          path.lineTo (x, y);
                }
                g.setColour (col);
                g.strokePath (path, juce::PathStrokeType (1.5f));
            };

            drawSeries (lzfToggle.getToggleState(), juce::Colour (0xff34c759), &AnalysisEntry::lzfDB);
            drawSeries (lcfToggle.getToggleState(), juce::Colour (0xffffe620), &AnalysisEntry::lcfDB);
            drawSeries (lafToggle.getToggleState(), juce::Colour (0xffbf5af2), &AnalysisEntry::lafDB);

            // Playhead
            if (fileLoaded_)
            {
                double pos = processor.getFilePosition();
                if (pos >= xViewStart_ && pos <= xViewEnd_)
                {
                    float px = xToScreen (pos);
                    g.setColour (juce::Colour (0xffff453a));
                    g.drawVerticalLine (static_cast<int> (px), plotT, plotB);

                    // Small triangle at top
                    juce::Path tri;
                    tri.addTriangle (px - 5.0f, plotT, px + 5.0f, plotT, px, plotT + 7.0f);
                    g.fillPath (tri);
                }
            }

            g.restoreState();

            // Draw magnifying glass icons
            drawMagnifyingGlass (g, yZoomButton_, juce::Colour (0xffaeaeb2));
            drawMagnifyingGlass (g, xZoomButton_, juce::Colour (0xffaeaeb2));
        }

    private:
        //----------------------------------------------------------------------
        static void drawMagnifyingGlass (juce::Graphics& g, const juce::TextButton& btn,
                                         juce::Colour col)
        {
            auto ib = btn.getBounds().toFloat().reduced (3.0f);
            const float r     = ib.getWidth() * 0.32f;
            const float cx    = ib.getCentreX() - r * 0.15f;
            const float cy    = ib.getCentreY() - r * 0.15f;
            const float angle = juce::MathConstants<float>::pi * 0.785f;
            g.setColour (col);
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
            g.drawLine (cx + std::cos (angle) * r, cy + std::sin (angle) * r,
                        cx + std::cos (angle) * r * 1.9f, cy + std::sin (angle) * r * 1.9f, 1.5f);
        }

        //----------------------------------------------------------------------
        static float linearToDBFS (float linear)
        {
            return 20.0f * std::log10 (std::max (linear, 1e-10f));
        }

        //----------------------------------------------------------------------
        void doLoadFile()
        {
            fileChooser = std::make_unique<juce::FileChooser> (
                "Load audio file for Long-term SPL analysis",
                juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
                formatManager.getWildcardForAllFormats());

            fileChooser->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File{}) return;
                    analyseFile (file);
                });
        }

        //----------------------------------------------------------------------
        void analyseFile (const juce::File& file)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
            if (reader == nullptr)
            {
                statusLabel.setText ("Error: could not read file.", juce::dontSendNotification);
                return;
            }

            const double sr      = reader->sampleRate;
            const int    numCh   = static_cast<int> (reader->numChannels);
            const juce::int64 totalSamples = reader->lengthInSamples;

            fileDuration = static_cast<double> (totalSamples) / sr;

            // BWF time reference (sample offset from bext chunk)
            bwfOffsetSec_ = 0.0;
            juce::String bwfTimeRef = reader->metadataValues.getValue ("bwav time reference", "");
            if (bwfTimeRef.isNotEmpty())
            {
                juce::int64 bwfSamples = bwfTimeRef.getLargeIntValue();
                if (bwfSamples > 0)
                    bwfOffsetSec_ = static_cast<double> (bwfSamples) / sr;
            }

            // Reset view to full file
            xViewStart_ = 0.0;
            xViewEnd_   = fileDuration;

            // File info
            juce::String infoText = file.getFileName()
                + "  |  " + juce::String (sr / 1000.0, 1) + " kHz"
                + "  |  " + juce::String (reader->bitsPerSample) + " bit"
                + "  |  " + juce::String (numCh) + " ch"
                + "  |  " + formatDuration (fileDuration);
            if (bwfOffsetSec_ > 0.0)
                infoText += "  |  TC " + formatTimecode (bwfOffsetSec_);
            fileInfoLabel.setText (infoText, juce::dontSendNotification);

            // Read calOffset
            const float calOffset = processor.apvts.getRawParameterValue ("calOffset")->load();

            // Prepare filters
            AWeightingFilter aWeight;
            CWeightingFilter cWeight;
            aWeight.prepare (sr);
            cWeight.prepare (sr);

            // IEC 61672 FAST time constant
            const double tau   = 0.125;
            const double alpha = 1.0 - std::exp (-1.0 / (tau * sr));

            double rmsA = 0.0, rmsC = 0.0, rmsZ = 0.0;
            double maxAbsC = 0.0;   // for DIN 15905-5 LCpeak (overall max of |C-weighted sample|)

            const int logInterval = std::max (1, static_cast<int> (0.125 * sr));
            int sampleCounter = 0;

            data.clear();

            // Waveform overview: ~2000 buckets for the whole file
            const int numBuckets = 2000;
            waveformOverview.clear();
            waveformOverview.resize (static_cast<size_t> (numBuckets));
            waveformPeak = 0.0f;
            const double samplesPerBucket = static_cast<double> (totalSamples) / numBuckets;

            juce::int64 samplesRead = 0;
            const int blockSize = 8192;
            juce::AudioBuffer<float> buffer (numCh, blockSize);

            while (samplesRead < totalSamples)
            {
                const int toRead = static_cast<int> (
                    std::min (static_cast<juce::int64> (blockSize), totalSamples - samplesRead));
                reader->read (&buffer, 0, toRead, samplesRead, true, numCh > 1);

                for (int s = 0; s < toRead; ++s)
                {
                    float raw = 0.0f;
                    for (int ch = 0; ch < numCh; ++ch)
                        raw += buffer.getReadPointer (ch)[s];
                    raw /= static_cast<float> (numCh);

                    int bucket = static_cast<int> ((samplesRead + s) / samplesPerBucket);
                    bucket = juce::jlimit (0, numBuckets - 1, bucket);
                    auto& wb = waveformOverview[static_cast<size_t> (bucket)];
                    wb.minVal = std::min (wb.minVal, raw);
                    wb.maxVal = std::max (wb.maxVal, raw);

                    float aSample = aWeight.processSample (raw);
                    float cSample = cWeight.processSample (raw);

                    const double absC = std::fabs (static_cast<double> (cSample));
                    if (absC > maxAbsC) maxAbsC = absC;

                    rmsA += alpha * (static_cast<double> (aSample) * aSample - rmsA);
                    rmsC += alpha * (static_cast<double> (cSample) * cSample - rmsC);
                    rmsZ += alpha * (static_cast<double> (raw)     * raw     - rmsZ);

                    if (++sampleCounter >= logInterval)
                    {
                        sampleCounter = 0;
                        double t = static_cast<double> (samplesRead + s) / sr;

                        AnalysisEntry e;
                        e.timeSeconds = t;
                        e.lafDB = linearToDBFS (static_cast<float> (std::sqrt (rmsA))) + calOffset;
                        e.lcfDB = linearToDBFS (static_cast<float> (std::sqrt (rmsC))) + calOffset;
                        e.lzfDB = linearToDBFS (static_cast<float> (std::sqrt (rmsZ))) + calOffset;
                        data.push_back (e);
                    }
                }
                samplesRead += toRead;
            }

            for (const auto& wb : waveformOverview)
                waveformPeak = std::max (waveformPeak,
                                         std::max (std::fabs (wb.minVal), std::fabs (wb.maxVal)));

            // DIN 15905-5: LCpeak = 20·log10(max|x_C|) + calOffset
            lcPeak_ = (maxAbsC > 0.0)
                      ? static_cast<float> (20.0 * std::log10 (maxAbsC)) + calOffset
                      : -999.0f;

            computeStats();

            // Load file into the processor's transport for playback
            processor.loadFile (file);
            processor.stopPlayback();
            processor.seekTo (0.0);
            fileLoaded_ = true;
            playPauseButton.setToggleState (false, juce::dontSendNotification);

            exportCsvButton.setEnabled (true);
            refreshButton.setEnabled (true);
            calToInputButton.setEnabled (true);
            loadedFile_ = file;
            statusLabel.setText ("Analysis complete: " + juce::String (static_cast<int> (data.size()))
                                 + " data points over " + formatDuration (fileDuration),
                                 juce::dontSendNotification);
            repaint();
        }

        //----------------------------------------------------------------------
        void computeStats()
        {
            if (data.empty()) return;

            const int skip = std::min (4, static_cast<int> (data.size()) - 1);

            double sumA = 0.0, sumC = 0.0;
            float  maxA = -999.0f, minA = 999.0f;
            float  maxC = -999.0f, minC = -999.0f;
            int    count = 0;

            for (int i = skip; i < static_cast<int> (data.size()); ++i)
            {
                const auto& e = data[i];
                sumA += std::pow (10.0, e.lafDB / 10.0);
                sumC += std::pow (10.0, e.lcfDB / 10.0);
                maxA = std::max (maxA, e.lafDB);
                minA = std::min (minA, e.lafDB);
                maxC = std::max (maxC, e.lcfDB);
                minC = std::min (minC, e.lcfDB);
                ++count;
            }

            if (count > 0)
            {
                laeq   = static_cast<float> (10.0 * std::log10 (sumA / count));
                lceq   = static_cast<float> (10.0 * std::log10 (sumC / count));
                lafMax = maxA;
                lafMin = minA;
                lcfMax = maxC;
                lcfMin = minC;
            }

            // DIN 15905-5: max sliding 30-min LAeq across the file (or full-file LAeq
            // if shorter than 30 min, per the standard's wording).
            maxLAeq30_ = -999.0f;
            {
                constexpr double kWindow = 30.0 * 60.0;
                double sumA30 = 0.0;
                int    head   = skip;
                for (int i = skip; i < static_cast<int> (data.size()); ++i)
                {
                    sumA30 += std::pow (10.0, data[i].lafDB / 10.0);
                    while (head < i && (data[i].timeSeconds - data[head].timeSeconds) > kWindow)
                    {
                        sumA30 -= std::pow (10.0, data[head].lafDB / 10.0);
                        ++head;
                    }
                    const int n = i - head + 1;
                    if (n > 0)
                    {
                        const float leq = static_cast<float> (10.0 * std::log10 (sumA30 / n));
                        if (leq > maxLAeq30_) maxLAeq30_ = leq;
                    }
                }
            }

            const bool overLimit = (maxLAeq30_ >= 99.0f) || (lcPeak_ >= 135.0f);
            const bool warn      = ! overLimit
                                && ((maxLAeq30_ >= 95.0f) || (lcPeak_ >= 130.0f));
            const juce::String dinStatus = overLimit ? "LIMIT" : warn ? "WARN" : "OK";

            // NIOSH REL: integrate dose across the file (3 dB exchange rate, 80 dB(A)
            // threshold, 85 dB(A) for 8 h = 100 % criterion).
            double nioshDose = 0.0;
            if (data.size() > 1)
            {
                for (int i = skip; i < static_cast<int> (data.size()); ++i)
                {
                    const float laeqA = data[i].lafDB;
                    if (laeqA < 80.0f) continue;
                    const double prevT = (i > 0) ? data[i - 1].timeSeconds : 0.0;
                    const double dt    = juce::jlimit (0.0, 1.0, data[i].timeSeconds - prevT);
                    const double tAllowed = 28800.0
                                          / std::pow (2.0, (static_cast<double> (laeqA) - 85.0) / 3.0);
                    nioshDose += (dt / tAllowed) * 100.0;
                }
            }
            const bool nioshOver = (nioshDose >= 100.0) || (lcPeak_ >= 140.0f);
            const bool nioshWarn = ! nioshOver
                                && ((nioshDose >= 50.0)  || (lcPeak_ >= 135.0f));
            const juce::String nioshStatus = nioshOver ? "LIMIT" : nioshWarn ? "WARN" : "OK";

            statsLabel.setText (
                "LAeq=" + juce::String (laeq, 1)
                + "  LAFmax=" + juce::String (lafMax, 1)
                + "  LAFmin=" + juce::String (lafMin, 1)
                + "  |  LCeq=" + juce::String (lceq, 1)
                + "  LCFmax=" + juce::String (lcfMax, 1),
                juce::dontSendNotification);

            dinStatsLabel.setText (
                "DIN 15905-5: LAeq(30min) max="
                + (maxLAeq30_ > -100.0f ? juce::String (maxLAeq30_, 1) : juce::String ("--"))
                + " dB(A)  LCpeak="
                + (lcPeak_    > -100.0f ? juce::String (lcPeak_,    1) : juce::String ("--"))
                + " dB(C)  [" + dinStatus + "]"
                + "    \xe2\x80\x96    NIOSH: Dose="
                + juce::String (nioshDose, nioshDose < 10.0 ? 2 : 1) + " %  ["
                + nioshStatus + "]",
                juce::dontSendNotification);
            const juce::Colour okCol   (0xff34c759);
            const juce::Colour warnCol (0xffffd60a);
            const juce::Colour limCol  (0xffff3b30);
            // Pick the worst status colour across the two standards
            const bool worstOver = overLimit || nioshOver;
            const bool worstWarn = warn      || nioshWarn;
            dinStatsLabel.setColour (juce::Label::textColourId,
                                     worstOver ? limCol : worstWarn ? warnCol : okCol);
        }

        //----------------------------------------------------------------------
        void doExportCsv()
        {
            if (data.empty()) return;

            fileChooser = std::make_unique<juce::FileChooser> (
                "Export Long-term SPL as CSV",
                juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                    .getChildFile ("LongTermSPL.csv"),
                "*.csv");

            fileChooser->launchAsync (
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File{}) return;

                    juce::FileOutputStream stream (file);
                    if (!stream.openedOk()) return;
                    stream.setPosition (0);
                    stream.truncate();

                    stream.writeText ("Time(s),LAF(dB),LCF(dB),LZF(dB)\n", false, false, nullptr);
                    for (const auto& e : data)
                    {
                        stream.writeText (
                            juce::String (e.timeSeconds, 3)
                            + "," + juce::String (e.lafDB, 1)
                            + "," + juce::String (e.lcfDB, 1)
                            + "," + juce::String (e.lzfDB, 1) + "\n",
                            false, false, nullptr);
                    }

                    stream.writeText ("# LAeq=" + juce::String (laeq, 1)
                                      + ",LAFmax=" + juce::String (lafMax, 1)
                                      + ",LAFmin=" + juce::String (lafMin, 1)
                                      + ",LCeq=" + juce::String (lceq, 1)
                                      + ",LCFmax=" + juce::String (lcfMax, 1)
                                      + ",LCFmin=" + juce::String (lcfMin, 1)
                                      + "\n",
                                      false, false, nullptr);

                    statusLabel.setText ("CSV exported to " + file.getFileName(),
                                         juce::dontSendNotification);
                });
        }

        //----------------------------------------------------------------------
        static juce::String formatDuration (double seconds)
        {
            int total = static_cast<int> (seconds);
            int h = total / 3600;
            int m = (total % 3600) / 60;
            int s = total % 60;
            if (h > 0)
                return juce::String::formatted ("%d:%02d:%02d", h, m, s);
            return juce::String::formatted ("%d:%02d", m, s);
        }

        // HH:MM:SS timecode format (always shows hours)
        static juce::String formatTimecode (double seconds)
        {
            int total = static_cast<int> (seconds);
            int h = total / 3600;
            int m = (total % 3600) / 60;
            int s = total % 60;
            return juce::String::formatted ("%02d:%02d:%02d", h, m, s);
        }

        //----------------------------------------------------------------------
        SPLMeterAudioProcessor& processor;
        juce::AudioFormatManager formatManager;

        // Transport bar
        juce::TextButton playPauseButton { "Play" };
        juce::TextButton stopButton      { "Stop" };
        juce::Label      posLabel;
        SpeakerButton    speakerButton;
        juce::Slider     volumeSlider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;

        // File / analysis controls
        juce::TextButton   loadButton       { "Load Audio File..." };
        juce::TextButton   refreshButton    { "Refresh" };
        juce::TextButton   calToInputButton { "Cal to Input" };
        juce::TextButton   exportCsvButton  { "Export CSV..." };
        juce::File         loadedFile_;
        juce::Label        fileInfoLabel;
        juce::ToggleButton lafToggle;
        juce::ToggleButton lcfToggle;
        juce::ToggleButton lzfToggle;
        juce::Label        statsLabel;
        juce::Label        dinStatsLabel;
        juce::Label        statusLabel;

        juce::TextButton   yZoomButton_;
        juce::TextButton   xZoomButton_;

        std::unique_ptr<juce::FileChooser> fileChooser;

        std::vector<AnalysisEntry> data;
        double fileDuration = 0.0;
        bool   fileLoaded_  = false;
        float  laeq   = 0.0f, lceq   = 0.0f;
        float  lafMax  = 0.0f, lafMin  = 0.0f;
        float  lcfMax  = 0.0f, lcfMin  = 0.0f;
        // DIN 15905-5 results
        float  lcPeak_     = -999.0f;   // overall C-weighted peak (max |x_C|), dB SPL
        float  maxLAeq30_  = -999.0f;   // worst-case sliding 30-min LAeq, dB(A)

        // Axis ranges
        float  yMin_ = 20.0f, yMax_ = 130.0f;
        double xViewStart_ = 0.0, xViewEnd_ = 0.0;

        // BWF timecode offset (seconds from "bwav time reference" sample count)
        double bwfOffsetSec_ = 0.0;

        // Waveform overview
        struct WaveformBucket { float minVal = 0.0f; float maxVal = 0.0f; };
        std::vector<WaveformBucket> waveformOverview;
        float waveformPeak = 0.0f;

        juce::Rectangle<int> plotArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

public:
    //--------------------------------------------------------------------------
    explicit LongTermSPLWindow (SPLMeterAudioProcessor& p)
        : juce::DocumentWindow ("Long-term SPL",
                                juce::Colour (0xff1c1c1e),
                                juce::DocumentWindow::closeButton),
          processor_ (p)
    {
        setUsingNativeTitleBar (false);
        auto* c = new Content (p);
        c->setSize (750, 540);
        setContentOwned (c, true);
        setResizable (true, false);
    }

    void closeButtonPressed() override
    {
        // Closing the window: drop file mode so the meter analyses the live
        // input again. setFileMode(false) also stops the transport.
        processor_.setFileMode (false);
        setVisible (false);
    }

private:
    SPLMeterAudioProcessor& processor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LongTermSPLWindow)
};
