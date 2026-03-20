#pragma once
#include <JuceHeader.h>
#include "SoundDetective.h"
#include "PluginProcessor.h"
#include <vector>

//==============================================================================
class SoundDetectiveWindow : public juce::DocumentWindow
{
    //==========================================================================
    class Content : public juce::Component
    {
    public:
        explicit Content (SPLMeterAudioProcessor& p) : processor (p)
        {
            // ---- Enable button ----
            enableButton.setClickingTogglesState (true);
            enableButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
            enableButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff34c759));
            enableButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            enableButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
            enableButton.onClick = [this]
            {
                processor.setSoundDetectiveEnabled (enableButton.getToggleState());
                updateStatus();
            };
            addAndMakeVisible (enableButton);

            // ---- Confidence threshold slider ----
            thresholdLabel.setText ("Min confidence:", juce::dontSendNotification);
            thresholdLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            thresholdLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
            addAndMakeVisible (thresholdLabel);

            thresholdSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            thresholdSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
            thresholdSlider.setRange (0.1, 0.9, 0.05);
            thresholdSlider.setValue (0.35, juce::dontSendNotification);
            thresholdSlider.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
            thresholdSlider.setColour (juce::Slider::thumbColourId,          juce::Colour (0xff5ac8fa));
            thresholdSlider.setColour (juce::Slider::textBoxTextColourId,    juce::Colours::white);
            thresholdSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            thresholdSlider.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff3a3a3c));
            thresholdSlider.onValueChange = [this]
            {
                processor.getSoundDetective().setThreshold ((float) thresholdSlider.getValue());
            };
            addAndMakeVisible (thresholdSlider);

            // ---- Event log ----
            eventLog.setMultiLine (true, false);
            eventLog.setReadOnly (true);
            eventLog.setScrollbarsShown (true);
            eventLog.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (13.0f)));
            eventLog.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2c2c2e));
            eventLog.setColour (juce::TextEditor::textColourId,       juce::Colour (0xffdddddd));
            eventLog.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff48484a));
            addAndMakeVisible (eventLog);

            // ---- Status label ----
            statusLabel.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8e8e93));
            statusLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (statusLabel);

            // ---- Bottom buttons ----
            clearButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            clearButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            clearButton.onClick = [this]
            {
                events_.clear();
                eventLog.clear();
                processor.getSoundDetective().clearPendingEvents();
                updateStatus();
            };
            addAndMakeVisible (clearButton);

            saveCsvButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            saveCsvButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            saveCsvButton.onClick = [this] { doSaveCsv(); };
            addAndMakeVisible (saveCsvButton);

            updateStatus();

#if JUCE_MAC || JUCE_IOS
            statusLabel.setText ("Powered by Apple SoundAnalysis (ML)", juce::dontSendNotification);
#else
            statusLabel.setText ("Heuristic mode (no ML framework available on this platform)",
                                 juce::dontSendNotification);
#endif
        }

        ~Content() override
        {
            processor.setSoundDetectiveEnabled (false);
        }

        //======================================================================
        void addEvents (const std::vector<SoundEvent>& newEvents)
        {
            for (const auto& ev : newEvents)
            {
                events_.push_back (ev);
                juce::String line =
                    juce::Time (ev.timestampMs).toString (false, true, true, true)
                    + "  " + ev.label.paddedRight (' ', 24)
                    + "  " + juce::String (ev.confidence * 100.0f, 0) + " %\n";
                eventLog.moveCaretToEnd();
                eventLog.insertTextAtCaret (line);
            }
            // Keep log from growing unboundedly (max 500 events)
            if (events_.size() > 500)
            {
                events_.erase (events_.begin(), events_.begin() + 100);
                rebuildLogText();
            }
            updateStatus();
        }

        const std::vector<SoundEvent>& getEvents() const noexcept { return events_; }

        //======================================================================
        void resized() override
        {
            auto r = getLocalBounds().reduced (6);

            // Top row
            auto row1 = r.removeFromTop (28).reduced (0, 2);
            enableButton.setBounds    (row1.removeFromLeft (110));
            row1.removeFromLeft (8);
            thresholdLabel.setBounds  (row1.removeFromLeft (120));
            thresholdSlider.setBounds (row1.removeFromLeft (220));
            r.removeFromTop (4);

            // Bottom row
            auto rowB = r.removeFromBottom (28).reduced (0, 2);
            clearButton.setBounds   (rowB.removeFromLeft (100));
            rowB.removeFromLeft (8);
            saveCsvButton.setBounds (rowB.removeFromLeft (180));
            rowB.removeFromLeft (8);
            statusLabel.setBounds   (rowB);
            r.removeFromBottom (4);

            eventLog.setBounds (r);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1c1c1e));
        }

    private:
        //======================================================================
        void updateStatus()
        {
            enableButton.setButtonText (enableButton.getToggleState()
                                        ? "SoundDetective ON"
                                        : "SoundDetective OFF");
        }

        void rebuildLogText()
        {
            juce::String text;
            for (const auto& ev : events_)
                text += juce::Time (ev.timestampMs).toString (false, true, true, true)
                      + "  " + ev.label.paddedRight (' ', 24)
                      + "  " + juce::String (ev.confidence * 100.0f, 0) + " %\n";
            eventLog.setText (text);
            eventLog.moveCaretToEnd();
        }

        void doSaveCsv()
        {
            if (events_.empty()) return;
            fileChooser = std::make_unique<juce::FileChooser> (
                "Save detected events as CSV",
                juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                    .getChildFile ("SoundDetective.csv"),
                "*.csv");
            fileChooser->launchAsync (
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file == juce::File{}) return;
                    juce::FileOutputStream stream (file);
                    if (!stream.openedOk()) return;
                    stream.setPosition (0); stream.truncate();
                    stream.writeText ("Timestamp,Label,Confidence\n", false, false, nullptr);
                    for (const auto& ev : events_)
                    {
                        stream.writeText (
                            juce::Time (ev.timestampMs).toString (true, true, true, true)
                            + "," + ev.label
                            + "," + juce::String (ev.confidence, 3) + "\n",
                            false, false, nullptr);
                    }
                });
        }

        //======================================================================
        SPLMeterAudioProcessor& processor;

        juce::TextButton  enableButton    { "SoundDetective OFF" };
        juce::Label       thresholdLabel;
        juce::Slider      thresholdSlider;
        juce::TextEditor  eventLog;
        juce::Label       statusLabel;
        juce::TextButton  clearButton     { "Clear" };
        juce::TextButton  saveCsvButton   { "Save Events CSV..." };

        std::vector<SoundEvent>          events_;
        std::unique_ptr<juce::FileChooser> fileChooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

public:
    //==========================================================================
    explicit SoundDetectiveWindow (SPLMeterAudioProcessor& p)
        : juce::DocumentWindow ("SoundDetective",
                                juce::Colour (0xff1c1c1e),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        auto* c = new Content (p);
        c->setSize (560, 400);
        setContentOwned (c, true);
        setResizable (true, false);
    }

    void closeButtonPressed() override { setVisible (false); }

    void addEvents (const std::vector<SoundEvent>& ev)
    {
        if (auto* c = dynamic_cast<Content*> (getContentComponent()))
            c->addEvents (ev);
    }

    const std::vector<SoundEvent>* getEvents() const
    {
        if (auto* c = dynamic_cast<const Content*> (getContentComponent()))
            return &c->getEvents();
        return nullptr;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundDetectiveWindow)
};
