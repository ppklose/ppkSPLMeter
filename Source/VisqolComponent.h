#pragma once
#include <JuceHeader.h>
#include "BandChart.h"
#include "visqol_ffi.h"
#include <vector>

//==============================================================================
/**
 * ViSQOL quality-analysis panel integrated into SPLMeter.
 * Colours mirror SPLMeter's dark / light theme exactly.
 * Input WAVs are automatically converted (sample rate + mono + 16-bit)
 * using JUCE's built-in audio I/O — no extra dependencies required.
 */
class VisqolComponent : public juce::Component
{
public:
    // Info captured during conversion, displayed after analysis
    struct ConversionInfo
    {
        double sourceSampleRate = 0.0;
        int    targetSampleRate = 0;
        int    sourceBitDepth   = 0;
        int    sourceChannels   = 0;
    };

    VisqolComponent()
        : refFile_ ("refFile", juce::File{}, true, false, false,
                    "*.wav", {}, "Select reference WAV"),
          degFile_ ("degFile", juce::File{}, true, false, false,
                    "*.wav", {}, "Select degraded WAV")
    {
        addAndMakeVisible (refLabel_);    addAndMakeVisible (refFile_);
        addAndMakeVisible (degLabel_);    addAndMakeVisible (degFile_);
        addAndMakeVisible (modeLabel_);

        modeBox_.addItem ("Wideband (speech)", 1);
        modeBox_.addItem ("Fullband (audio)",  2);
        modeBox_.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (modeBox_);

        runButton_.onClick = [this] { runAnalysis(); };
        addAndMakeVisible (runButton_);

        convInfoLabel_.setFont (juce::FontOptions (10.5f));
        convInfoLabel_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (convInfoLabel_);

        moslqoValue_.setFont (juce::FontOptions (18.0f).withStyle ("Bold"));
        vnsimValue_ .setFont (juce::FontOptions (18.0f).withStyle ("Bold"));
        addAndMakeVisible (moslqoLabel_);  addAndMakeVisible (moslqoValue_);
        addAndMakeVisible (vnsimLabel_);   addAndMakeVisible (vnsimValue_);

        addAndMakeVisible (chart_);

        applyTheme (false);
    }

    ~VisqolComponent() override { analysisThread_.stopThread (4000); }

    void setLightMode (bool light) { applyTheme (light); }

    // -----------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        g.fillAll (lightMode_ ? juce::Colour (0xfff2f2f7) : juce::Colour (0xff1c1c1e));

        g.setColour (lightMode_ ? juce::Colour (0xffc7c7cc) : juce::Colour (0xff48484a));
        g.fillRect (16, scoreRowY_ - 6, getWidth() - 32, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);

        const int rowH   = 30;
        const int labelW = 120;
        const int gap    = 8;

        auto makeRow = [&] (juce::Label& lbl, juce::Component& widget)
        {
            auto row = area.removeFromTop (rowH);
            area.removeFromTop (gap);
            lbl.setBounds    (row.removeFromLeft (labelW));
            widget.setBounds (row);
        };

        makeRow (refLabel_,  refFile_);
        makeRow (degLabel_,  degFile_);
        makeRow (modeLabel_, modeBox_);

        area.removeFromTop (gap);
        runButton_.setBounds (area.removeFromTop (rowH).withSizeKeepingCentre (160, rowH));
        area.removeFromTop (4);
        convInfoLabel_.setBounds (area.removeFromTop (34));
        area.removeFromTop (gap);

        scoreRowY_ = area.getY();
        auto scoreRow = area.removeFromTop (rowH);
        {
            auto left = scoreRow.removeFromLeft (scoreRow.getWidth() / 2);
            moslqoLabel_.setBounds (left.removeFromLeft (labelW));
            moslqoValue_.setBounds (left);
        }
        vnsimLabel_.setBounds (scoreRow.removeFromLeft (labelW));
        vnsimValue_.setBounds (scoreRow);

        area.removeFromTop (gap * 2);
        chart_.setBounds (area);
    }

private:
    // -----------------------------------------------------------------------
    // Audio conversion helper (JUCE-only, GPL-compatible)
    // -----------------------------------------------------------------------
    static bool convertWav (const juce::String& inputPath,
                            const juce::File&   outputFile,
                            int                 targetSampleRate,
                            juce::String&       errorMsg,
                            ConversionInfo&     infoOut)
    {
        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (
            fmt.createReaderFor (juce::File (inputPath)));

        if (reader == nullptr)
        {
            errorMsg = "Cannot read file: " + inputPath;
            return false;
        }

        const auto   totalSamples = (int) reader->lengthInSamples;
        const double sourceSR     = reader->sampleRate;
        const int    numCh        = (int) reader->numChannels;
        const int    srcBits      = (int) reader->bitsPerSample;

        infoOut.sourceSampleRate = sourceSR;
        infoOut.targetSampleRate = targetSampleRate;
        infoOut.sourceBitDepth   = srcBits;
        infoOut.sourceChannels   = numCh;

        // Read all source samples
        juce::AudioBuffer<float> srcBuf (numCh, totalSamples);
        reader->read (&srcBuf, 0, totalSamples, 0, true, true);
        reader.reset();

        // Mix to mono
        juce::AudioBuffer<float> monoBuf (1, totalSamples);
        monoBuf.clear();
        const float chGain = 1.0f / (float) numCh;
        for (int ch = 0; ch < numCh; ++ch)
            monoBuf.addFrom (0, 0, srcBuf, ch, 0, totalSamples, chGain);

        // Resample
        const double ratio      = sourceSR / (double) targetSampleRate;
        const int    outSamples = juce::roundToInt (totalSamples / ratio);
        juce::AudioBuffer<float> outBuf (1, outSamples);

        juce::LagrangeInterpolator interp;
        interp.process (ratio,
                        monoBuf.getReadPointer (0),
                        outBuf.getWritePointer (0),
                        outSamples);

        // Write 16-bit PCM WAV
        outputFile.deleteFile();
        auto outStream = outputFile.createOutputStream();
        if (outStream == nullptr) { errorMsg = "Cannot create temp file"; return false; }

        juce::WavAudioFormat wavFmt;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFmt.createWriterFor (outStream.release(), targetSampleRate, 1, 16, {}, 0));
        if (writer == nullptr) { errorMsg = "Cannot create WAV writer"; return false; }

        writer->writeFromAudioSampleBuffer (outBuf, 0, outSamples);
        return true;
    }

    // Builds a human-readable conversion note for one file
    static juce::String buildConvNote (const juce::String& tag, const ConversionInfo& i)
    {
        auto fmtSR = [] (double sr) -> juce::String
        {
            return sr >= 1000.0 ? juce::String (sr / 1000.0, 1) + " kHz"
                                : juce::String ((int) sr) + " Hz";
        };

        juce::String s = tag + ":  ";
        s += fmtSR (i.sourceSampleRate) + "  ->  " + fmtSR (i.targetSampleRate);
        s += "   |   " + juce::String (i.sourceBitDepth) + "-bit  ->  16-bit";

        if (i.sourceChannels == 2)
            s += "   |   Stereo  ->  Mono";
        else if (i.sourceChannels > 2)
            s += "   |   " + juce::String (i.sourceChannels) + " ch  ->  Mono";

        return s;
    }

    // -----------------------------------------------------------------------
    void applyTheme (bool light)
    {
        lightMode_ = light;

        const juce::Colour bgBtn   = light ? juce::Colour (0xffd1d1d6) : juce::Colour (0xff3a3a3c);
        const juce::Colour textPri = light ? juce::Colour (0xff1c1c1e) : juce::Colours::white;
        const juce::Colour textSec = light ? juce::Colour (0xff6c6c70) : juce::Colour (0xffaeaeb2);
        const juce::Colour accent  = juce::Colour (0xff5ac8fa);
        const juce::Colour outline = light ? juce::Colour (0xffc7c7cc) : juce::Colour (0xff48484a);

        for (auto* l : { &refLabel_, &degLabel_, &modeLabel_, &moslqoLabel_, &vnsimLabel_ })
            l->setColour (juce::Label::textColourId, textSec);
        for (auto* l : { &moslqoValue_, &vnsimValue_ })
            l->setColour (juce::Label::textColourId, textPri);

        convInfoLabel_.setColour (juce::Label::textColourId, textSec);

        runButton_.setColour (juce::TextButton::buttonColourId,   bgBtn);
        runButton_.setColour (juce::TextButton::buttonOnColourId, accent);
        runButton_.setColour (juce::TextButton::textColourOffId,  textPri);
        runButton_.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));

        modeBox_.setColour (juce::ComboBox::backgroundColourId, bgBtn);
        modeBox_.setColour (juce::ComboBox::textColourId,       textPri);
        modeBox_.setColour (juce::ComboBox::outlineColourId,    outline);
        modeBox_.setColour (juce::ComboBox::arrowColourId,      textSec);

        for (auto* fc : { &refFile_, &degFile_ })
        {
            fc->setColour (juce::ComboBox::backgroundColourId, bgBtn);
            fc->setColour (juce::ComboBox::textColourId,       textPri);
            fc->setColour (juce::ComboBox::outlineColourId,    outline);
        }

        chart_.setLightMode (light);
        repaint();
    }

    // -----------------------------------------------------------------------
    void runAnalysis()
    {
        if (analysisThread_.isThreadRunning())
            return;

        auto refPath = refFile_.getCurrentFile().getFullPathName();
        auto degPath = degFile_.getCurrentFile().getFullPathName();

        if (refPath.isEmpty() || degPath.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Missing files",
                "Please select both a reference and a degraded WAV file.");
            return;
        }

        runButton_.setEnabled (false);
        runButton_.setButtonText ("Converting...");
        convInfoLabel_.setText ("", juce::dontSendNotification);
        moslqoValue_.setText ("...", juce::dontSendNotification);
        vnsimValue_ .setText ("...", juce::dontSendNotification);
        chart_.clear();

        analysisThread_.refPath = refPath;
        analysisThread_.degPath = degPath;
        analysisThread_.mode    = modeBox_.getSelectedId() - 1;
        analysisThread_.startThread();
    }

    void onAnalysisComplete (bool success, const VisqolResult& result,
                             const ConversionInfo& refInfo, const ConversionInfo& degInfo)
    {
        runButton_.setEnabled (true);
        runButton_.setButtonText ("Run Analysis");

        // Always show conversion notes
        if (refInfo.sourceSampleRate > 0.0)
        {
            convInfoLabel_.setText (
                buildConvNote ("Ref", refInfo) + "\n" + buildConvNote ("Deg", degInfo),
                juce::dontSendNotification);
        }

        if (!success)
        {
            juce::String errMsg (result.error_msg);
            errMsg = errMsg.replace ("\xe2\x80\xa6", "...");
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Analysis failed",
                errMsg.isEmpty() ? "Unknown error" : errMsg);
            moslqoValue_.setText ("-", juce::dontSendNotification);
            vnsimValue_ .setText ("-", juce::dontSendNotification);
            return;
        }

        moslqoValue_.setText (juce::String (result.moslqo, 3), juce::dontSendNotification);
        vnsimValue_ .setText (juce::String (result.vnsim,  3), juce::dontSendNotification);

        int count = juce::jlimit (0, VISQOL_MAX_BANDS, result.band_count);
        std::vector<double> fvnsim (result.fvnsim,            result.fvnsim            + count);
        std::vector<double> freqs  (result.center_freq_bands, result.center_freq_bands + count);
        chart_.setBands (fvnsim, freqs);
    }

    // -----------------------------------------------------------------------
    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread (VisqolComponent& owner)
            : juce::Thread ("VisqolAnalysis"), owner_ (owner) {}

        void run() override
        {
            const int targetSR = (mode == 0) ? 16000 : 48000;

            auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory);
            auto tmpRef = tmpDir.getChildFile ("splmeter_visqol_ref.wav");
            auto tmpDeg = tmpDir.getChildFile ("splmeter_visqol_deg.wav");

            ConversionInfo refInfo, degInfo;
            juce::String convError;

            if (!VisqolComponent::convertWav (refPath, tmpRef, targetSR, convError, refInfo) ||
                !VisqolComponent::convertWav (degPath, tmpDeg, targetSR, convError, degInfo))
            {
                success = false;
                juce::String msg = "Audio conversion failed: " + convError;
                msg.copyToUTF8 (result.error_msg, sizeof (result.error_msg) - 1);
                juce::MessageManager::callAsync ([this, refInfo, degInfo]
                {
                    owner_.onAnalysisComplete (success, result, refInfo, degInfo);
                });
                return;
            }

            // Update button to show we moved on to analysis
            juce::MessageManager::callAsync ([this]
            {
                owner_.runButton_.setButtonText ("Running...");
            });

#if defined(VISQOL_MODEL_PATH_DEFAULT)
            if (mode == 1)
            {
#if defined(_WIN32)
                _putenv_s ("VISQOL_MODEL_PATH", VISQOL_MODEL_PATH_DEFAULT);
#else
                setenv ("VISQOL_MODEL_PATH", VISQOL_MODEL_PATH_DEFAULT, 1);
#endif
            }
#endif
            result  = {};
            int ret = visqol_run (tmpRef.getFullPathName().toRawUTF8(),
                                  tmpDeg.getFullPathName().toRawUTF8(),
                                  mode, &result);
            success = (ret == 0);

            tmpRef.deleteFile();
            tmpDeg.deleteFile();

            juce::MessageManager::callAsync ([this, refInfo, degInfo]
            {
                owner_.onAnalysisComplete (success, result, refInfo, degInfo);
            });
        }

        juce::String refPath;
        juce::String degPath;
        int          mode    = 0;
        VisqolResult result  = {};
        bool         success = false;

    private:
        VisqolComponent& owner_;
    };

    AnalysisThread analysisThread_ { *this };

    // -----------------------------------------------------------------------
    juce::Label             refLabel_      { {}, "Reference file:" };
    juce::FilenameComponent refFile_;
    juce::Label             degLabel_      { {}, "Degraded file:" };
    juce::FilenameComponent degFile_;
    juce::Label             modeLabel_     { {}, "Mode:" };
    juce::ComboBox          modeBox_;
    juce::TextButton        runButton_     { "Run Analysis" };
    juce::Label             convInfoLabel_;
    juce::Label             moslqoLabel_   { {}, "MOS-LQO:" };
    juce::Label             moslqoValue_   { {}, "-" };
    juce::Label             vnsimLabel_    { {}, "VNSIM:" };
    juce::Label             vnsimValue_    { {}, "-" };
    BandChart               chart_;

    bool lightMode_ = false;
    int  scoreRowY_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisqolComponent)
};

//==============================================================================
class VisqolWindow : public juce::DocumentWindow
{
public:
    VisqolWindow()
        : juce::DocumentWindow ("ViSQOL Quality Analysis",
                                juce::Colour (0xff2c2c2e),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        auto* content = new VisqolComponent();
        content->setSize (700, 580);
        setContentOwned (content, true);
        setResizable (true, false);
    }

    void setLightMode (bool light)
    {
        setBackgroundColour (light ? juce::Colour (0xffe5e5ea) : juce::Colour (0xff2c2c2e));
        if (auto* c = dynamic_cast<VisqolComponent*> (getContentComponent()))
            c->setLightMode (light);
    }

    void closeButtonPressed() override { setVisible (false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisqolWindow)
};
