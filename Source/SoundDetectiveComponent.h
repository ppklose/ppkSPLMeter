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
        explicit Content (SPLMeterAudioProcessor& p, std::function<void()> onClear = nullptr)
            : processor (p), onClear_ (std::move (onClear))
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
                if (onClear_) onClear_();
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

#if SPLMETER_HAS_TFLITE
            // ---- TFLite model row ----
            srLabel_.setText ("SR:", juce::dontSendNotification);
            srLabel_.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            srLabel_.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
            srLabel_.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (srLabel_);

            srField_.setText ("16000", juce::dontSendNotification);
            srField_.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (12.0f)));
            srField_.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2c2c2e));
            srField_.setColour (juce::TextEditor::textColourId,       juce::Colour (0xffdddddd));
            srField_.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff48484a));
            srField_.setInputRestrictions (6, "0123456789");
            addAndMakeVisible (srField_);

            loadModelButton_.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            loadModelButton_.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            loadModelButton_.onClick = [this] { doLoadModel(); };
            addAndMakeVisible (loadModelButton_);

            clearModelButton_.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            clearModelButton_.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            clearModelButton_.onClick = [this]
            {
                processor.getSoundDetective().clearTFLiteModel();
                updateModelInfo();
            };
            addAndMakeVisible (clearModelButton_);

            modelInfoLabel_.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            modelInfoLabel_.setColour (juce::Label::textColourId, juce::Colour (0xff8e8e93));
            modelInfoLabel_.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (modelInfoLabel_);
            updateModelInfo();
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

#if SPLMETER_HAS_TFLITE
            // TFLite model row
            auto row2 = r.removeFromTop (24).reduced (0, 2);
            loadModelButton_.setBounds  (row2.removeFromLeft (120));
            row2.removeFromLeft (6);
            srLabel_.setBounds          (row2.removeFromLeft (24));
            srField_.setBounds          (row2.removeFromLeft (52));
            row2.removeFromLeft (4);
            clearModelButton_.setBounds (row2.removeFromLeft (24));
            row2.removeFromLeft (6);
            modelInfoLabel_.setBounds   (row2);
            r.removeFromTop (4);
#endif

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

#if SPLMETER_HAS_TFLITE
        void doLoadModel()
        {
            fileChooser = std::make_unique<juce::FileChooser> (
                "Load TFLite model",
                juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
                "*.tflite");
            fileChooser->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f == juce::File{}) return;

                    TFLiteDetector::Config cfg;
                    cfg.modelPath      = f.getFullPathName();
                    cfg.modelSampleRate = srField_.getText().getIntValue();
                    if (cfg.modelSampleRate < 8000 || cfg.modelSampleRate > 96000)
                        cfg.modelSampleRate = 16000;
                    cfg.modelSampleRate = juce::jlimit (8000, 96000, cfg.modelSampleRate);

                    juce::String err;
                    processor.getSoundDetective().loadTFLiteModel (cfg, err);

                    if (err.isNotEmpty())
                        modelInfoLabel_.setText ("Error: " + err, juce::dontSendNotification);
                    else
                        updateModelInfo();
                });
        }

        void updateModelInfo()
        {
            const auto info = processor.getSoundDetective().getTFLiteModelInfo();
            if (info.isEmpty())
            {
               #if JUCE_MAC || JUCE_IOS
                modelInfoLabel_.setText ("No model  —  Apple SoundAnalysis active",
                                         juce::dontSendNotification);
               #else
                modelInfoLabel_.setText ("No model  —  Heuristic mode active",
                                         juce::dontSendNotification);
               #endif
            }
            else
            {
                modelInfoLabel_.setText (info, juce::dontSendNotification);
            }
        }
#endif

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

        std::vector<SoundEvent>            events_;
        std::unique_ptr<juce::FileChooser> fileChooser;
        std::function<void()>              onClear_;

#if SPLMETER_HAS_TFLITE
        juce::TextButton  loadModelButton_  { "Load TFLite Model..." };
        juce::TextButton  clearModelButton_ { "x" };
        juce::Label       srLabel_;
        juce::TextEditor  srField_;
        juce::Label       modelInfoLabel_;
#endif

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

public:
    //==========================================================================
    std::function<void()> onEventsCleared;

    explicit SoundDetectiveWindow (SPLMeterAudioProcessor& p)
        : juce::DocumentWindow ("SoundDetective",
                                juce::Colour (0xff1c1c1e),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        auto* c = new Content (p, [this] { if (onEventsCleared) onEventsCleared(); });
#if SPLMETER_HAS_TFLITE
        c->setSize (560, 432);
#else
        c->setSize (560, 400);
#endif
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
