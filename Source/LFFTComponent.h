#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cmath>
#include <vector>
#include <algorithm>

//==============================================================================
// CallOutBox panel for L_FFT Y-axis dB range
class LFFTYRangePanel : public juce::Component
{
public:
    explicit LFFTYRangePanel (SPLMeterAudioProcessor& p)
    {
        auto setupSlider = [this] (juce::Slider& s, juce::Label& l, const juce::String& title)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
            s.setTextValueSuffix (" dB");
            s.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
            s.setColour (juce::Slider::thumbColourId,          juce::Colour (0xff5ac8fa));
            s.setColour (juce::Slider::textBoxTextColourId,    juce::Colours::white);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            l.setText (title, juce::dontSendNotification);
            l.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
            l.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
            l.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (s);
            addAndMakeVisible (l);
        };
        setupSlider (minSlider_, minLabel_, "Min");
        setupSlider (maxSlider_, maxLabel_, "Max");
        minAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "lfftYMin", minSlider_);
        maxAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "lfftYMax", maxSlider_);
        setSize (300, 76);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);
        auto row1 = b.removeFromTop (28); b.removeFromTop (4);
        auto row2 = b.removeFromTop (28);
        minLabel_.setBounds (row1.removeFromLeft (52)); minSlider_.setBounds (row1);
        maxLabel_.setBounds (row2.removeFromLeft (52)); maxSlider_.setBounds (row2);
    }

private:
    juce::Slider minSlider_, maxSlider_;
    juce::Label  minLabel_,  maxLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> minAttach_, maxAttach_;
};

//==============================================================================
// CallOutBox panel for L_FFT X-axis freq range
class LFFTXRangePanel : public juce::Component
{
public:
    explicit LFFTXRangePanel (SPLMeterAudioProcessor& p)
    {
        auto setupSlider = [this] (juce::Slider& s, juce::Label& l, const juce::String& title)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
            s.setTextValueSuffix (" Hz");
            s.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
            s.setColour (juce::Slider::thumbColourId,          juce::Colour (0xff5ac8fa));
            s.setColour (juce::Slider::textBoxTextColourId,    juce::Colours::white);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            l.setText (title, juce::dontSendNotification);
            l.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
            l.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
            l.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (s);
            addAndMakeVisible (l);
        };
        setupSlider (lowerSlider_, lowerLabel_, "f Lower");
        setupSlider (upperSlider_, upperLabel_, "f Upper");
        lowerAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "fftLowerFreq", lowerSlider_);
        upperAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, "fftUpperFreq", upperSlider_);
        setSize (300, 76);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);
        auto row1 = b.removeFromTop (28); b.removeFromTop (4);
        auto row2 = b.removeFromTop (28);
        lowerLabel_.setBounds (row1.removeFromLeft (52)); lowerSlider_.setBounds (row1);
        upperLabel_.setBounds (row2.removeFromLeft (52)); upperSlider_.setBounds (row2);
    }

private:
    juce::Slider lowerSlider_, upperSlider_;
    juce::Label  lowerLabel_,  upperLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowerAttach_, upperAttach_;
};

//==============================================================================
// A stored FFT snapshot
struct LFFTSnapshot
{
    juce::String        name;
    std::vector<float>  bands;
    std::vector<float>  peak;
    int                 numBands = 0;
    int                 bandN    = 0;
    float               freqMin  = 20.0f;
    bool                visible  = true;
    juce::Colour        colour;
    int                 lineStyle = 1;  // 0=Continuous, 1=Dashed, 2=Dotted, 3=Dash-dot
};

//==============================================================================
// Real-time FFT spectrum analyzer with 1/N octave band smoothing
class LFFTComponent : public juce::Component,
                      private juce::Timer
{
public:
    explicit LFFTComponent (SPLMeterAudioProcessor& p)
        : processor (p)
    {
        rebuildFFT (fftOrder_);
        std::fill (bandsSmoothed_, bandsSmoothed_ + kMaxBands, 20.0f);
        std::fill (bandsPeak_,     bandsPeak_     + kMaxBands, 20.0f);
        std::fill (peakTimestamps_, peakTimestamps_ + kMaxBands, 0.0);

        // Y-axis zoom button
        auto styleZoom = [] (juce::TextButton& b)
        {
            b.setButtonText ("");
            b.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        };

        styleZoom (yZoomButton_);
        yZoomButton_.onClick = [this]
        {
            auto panel = std::make_unique<LFFTYRangePanel> (processor);
            juce::CallOutBox::launchAsynchronously (
                std::move (panel), yZoomButton_.getScreenBounds(), nullptr);
        };
        addAndMakeVisible (yZoomButton_);

        // X-axis zoom button
        styleZoom (xZoomButton_);
        xZoomButton_.onClick = [this]
        {
            auto panel = std::make_unique<LFFTXRangePanel> (processor);
            juce::CallOutBox::launchAsynchronously (
                std::move (panel), xZoomButton_.getScreenBounds(), nullptr);
        };
        addAndMakeVisible (xZoomButton_);

        // Snapshot button
        snapshotButton_.setButtonText (juce::String::fromUTF8 ("\xe2\x9c\x8e"));
        snapshotButton_.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
        snapshotButton_.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a3a3c));
        snapshotButton_.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        snapshotButton_.setTooltip ("Store current FFT as overlay");
        snapshotButton_.onClick = [this] { storeSnapshot(); };
        addAndMakeVisible (snapshotButton_);

        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    ~LFFTComponent() override { stopTimer(); }

    // Public access for the snapshot list
    std::vector<LFFTSnapshot>& getSnapshots() { return snapshots_; }

    void storeSnapshot()
    {
        if (numBands_ < 1) return;

        static const juce::Colour kColours[] = {
            juce::Colour (0xff34c759),   // green
            juce::Colour (0xffff9f0a),   // orange
            juce::Colour (0xffff375f),   // pink
            juce::Colour (0xffbf5af2),   // purple
            juce::Colour (0xff64d2ff),   // light blue
            juce::Colour (0xffffcc00),   // yellow
            juce::Colour (0xffac8e68),   // tan
            juce::Colour (0xff30d158),   // mint
        };

        LFFTSnapshot snap;
        snap.name      = juce::Time::getCurrentTime().toString (true, true, false, false);
        snap.bands.assign (bandsSmoothed_, bandsSmoothed_ + numBands_);
        snap.peak.assign (bandsPeak_, bandsPeak_ + numBands_);
        snap.numBands  = numBands_;
        snap.bandN     = bandN_;
        snap.freqMin   = freqMin_;
        snap.colour    = kColours[snapshots_.size() % 8];
        snap.visible   = true;

        snapshots_.push_back (std::move (snap));

        if (onSnapshotChanged) onSnapshotChanged();
    }

    void removeSnapshot (int index)
    {
        if (index >= 0 && index < static_cast<int> (snapshots_.size()))
        {
            undoStack_.push_back ({ index, snapshots_[index] });
            snapshots_.erase (snapshots_.begin() + index);
            if (onSnapshotChanged) onSnapshotChanged();
        }
    }

    void undoLastDelete()
    {
        if (undoStack_.empty()) return;
        auto& item = undoStack_.back();
        int pos = juce::jlimit (0, static_cast<int> (snapshots_.size()), item.index);
        snapshots_.insert (snapshots_.begin() + pos, std::move (item.snapshot));
        undoStack_.pop_back();
        if (onSnapshotChanged) onSnapshotChanged();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        {
            undoLastDelete();
            return true;
        }
        return false;
    }

    std::function<void()> onSnapshotChanged;

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.fillAll (juce::Colour (0xff1c1c1e));

        const float leftMargin  = 42.0f;
        const float bottomMargin = 24.0f;
        const float topMargin    = 8.0f;
        const float rightMargin  = 8.0f;

        const auto plotArea = juce::Rectangle<float> (
            leftMargin, topMargin,
            bounds.getWidth() - leftMargin - rightMargin,
            bounds.getHeight() - topMargin - bottomMargin);

        if (plotArea.getWidth() < 2.0f || plotArea.getHeight() < 2.0f)
            return;

        drawGrid (g, plotArea);

        // Draw magnifying glass icons
        drawMagnifyingGlass (g, yZoomButton_.getBounds().toFloat());
        drawMagnifyingGlass (g, xZoomButton_.getBounds().toFloat());

        if (numBands_ < 1) return;

        const float dbRange = dbCeil_ - dbFloor_;
        if (dbRange < 1.0f) return;

        auto freqToX = [&] (float freq) -> float
        {
            float t = std::log2 (freq / freqMin_) / std::log2 (freqMax_ / freqMin_);
            return plotArea.getX() + juce::jlimit (0.0f, 1.0f, t) * plotArea.getWidth();
        };

        auto dbToY = [&] (float db) -> float
        {
            float yNorm = juce::jlimit (0.0f, 1.0f, (db - dbFloor_) / dbRange);
            return plotArea.getBottom() - yNorm * plotArea.getHeight();
        };

        g.reduceClipRegion (plotArea.toNearestInt());

        const float stepFactor     = std::pow (2.0f, 1.0f / static_cast<float> (bandN_));
        const float halfBandFactor = std::sqrt (stepFactor);

        const juce::Colour colFill  { 0xaa5ac8fa };
        const juce::Colour colSolid { 0xff5ac8fa };
        const juce::Colour colPeak  { 0xffffcc00 };

        if (displayMode_ == 1)  // Area
        {
            juce::Path area;
            float fc = freqMin_;
            for (int b = 0; b < numBands_; ++b, fc *= stepFactor)
            {
                float x = freqToX (fc);
                float y = dbToY (bandsSmoothed_[b]);
                if (b == 0) area.startNewSubPath (x, y);
                else        area.lineTo (x, y);
            }
            float fcLast = freqMin_ * std::pow (stepFactor, static_cast<float> (numBands_ - 1));
            area.lineTo (freqToX (fcLast * halfBandFactor), plotArea.getBottom());
            area.lineTo (freqToX (freqMin_ / halfBandFactor), plotArea.getBottom());
            area.closeSubPath();

            g.setColour (colFill);
            g.fillPath (area);
            g.setColour (colSolid);
            g.strokePath (area, juce::PathStrokeType (2.0f));

            if (peakHold_)
            {
                juce::Path peakPath;
                fc = freqMin_;
                for (int b = 0; b < numBands_; ++b, fc *= stepFactor)
                {
                    float x = freqToX (fc);
                    float y = dbToY (bandsPeak_[b]);
                    if (b == 0) peakPath.startNewSubPath (x, y);
                    else        peakPath.lineTo (x, y);
                }
                g.setColour (colPeak.withAlpha (0.7f));
                g.strokePath (peakPath, juce::PathStrokeType (1.0f));
            }
        }
        else  // Bars (0) or Bars+Peak (2)
        {
            float fc = freqMin_;
            for (int b = 0; b < numBands_; ++b, fc *= stepFactor)
            {
                float x1   = freqToX (fc / halfBandFactor);
                float x2   = freqToX (fc * halfBandFactor);
                float yBot = plotArea.getBottom();
                float yTop = std::min (dbToY (bandsSmoothed_[b]), yBot - 3.0f);

                if (x2 - x1 < 1.0f) continue;

                g.setColour (colFill);
                g.fillRect (x1, yTop, x2 - x1 - 1.0f, yBot - yTop);
                g.setColour (colSolid);
                g.fillRect (x1, yTop, x2 - x1 - 1.0f, 2.0f);

                if (displayMode_ == 2 && peakHold_)
                {
                    float yPeak = dbToY (bandsPeak_[b]);
                    g.setColour (colPeak);
                    g.fillRect (x1, yPeak, x2 - x1 - 1.0f, 2.0f);
                }
            }
        }

        // --- Snapshot overlays ---
        for (const auto& snap : snapshots_)
        {
            if (! snap.visible || snap.numBands < 1) continue;

            const float snStep = std::pow (2.0f, 1.0f / static_cast<float> (snap.bandN));

            // Band curve
            {
                juce::Path src;
                float fc = snap.freqMin;
                for (int b = 0; b < snap.numBands; ++b, fc *= snStep)
                {
                    float x = freqToX (fc);
                    float y = dbToY (snap.bands[b]);
                    if (b == 0) src.startNewSubPath (x, y);
                    else        src.lineTo (x, y);
                }
                g.setColour (snap.colour);
                strokeWithStyle (g, src, 1.5f, snap.lineStyle);
            }

            // Peak curve (thinner, slightly transparent)
            if (! snap.peak.empty())
            {
                juce::Path src;
                float fc = snap.freqMin;
                for (int b = 0; b < snap.numBands; ++b, fc *= snStep)
                {
                    float x = freqToX (fc);
                    float y = dbToY (snap.peak[b]);
                    if (b == 0) src.startNewSubPath (x, y);
                    else        src.lineTo (x, y);
                }
                g.setColour (snap.colour.withAlpha (0.5f));
                strokeWithStyle (g, src, 1.0f, snap.lineStyle);
            }
        }
    }

    void resized() override
    {
        const float leftMargin   = 42.0f;
        const float bottomMargin = 24.0f;

        yZoomButton_.setBounds (static_cast<int> (leftMargin / 2.0f - 14.0f),
                                getHeight() / 2 + 20, 28, 28);

        xZoomButton_.setBounds (getWidth() / 2 + 20,
                                getHeight() - static_cast<int> (bottomMargin) + 1,
                                28, 22);

        snapshotButton_.setBounds (static_cast<int> (leftMargin) + 4, 10, 40, 20);
    }

private:
    static constexpr int kMaxBands = 512;
    static constexpr int kBandNForRes[5] = { 1, 3, 6, 12, 24 };

    void rebuildFFT (int order)
    {
        fftSize_   = 1 << order;
        forwardFFT = std::make_unique<juce::dsp::FFT> (order);
        fftData_.assign (fftSize_ * 2, 0.0f);
        windowBuf_.assign (fftSize_, 0.0f);
    }

    void timerCallback() override
    {
        const double sr = processor.getSampleRate();
        sampleRate_ = (sr > 0.0) ? sr : 44100.0;

        const float gain      = processor.apvts.getRawParameterValue ("fftGain")->load();
        const float smoothing = processor.apvts.getRawParameterValue ("fftSmoothing")->load();
        const int   winType   = static_cast<int> (processor.apvts.getRawParameterValue ("fftWindowType")->load());
        const bool  rtaMode   = processor.apvts.getRawParameterValue ("fftRTAMode")->load() > 0.5f;
        const int   resIdx    = static_cast<int> (processor.apvts.getRawParameterValue ("fftBandRes")->load());
        displayMode_          = static_cast<int> (processor.apvts.getRawParameterValue ("fftDisplayMode")->load());
        peakHold_             = processor.apvts.getRawParameterValue ("fftPeakHold")->load() > 0.5f;
        freqMin_              = processor.apvts.getRawParameterValue ("fftLowerFreq")->load();
        freqMax_              = processor.apvts.getRawParameterValue ("fftUpperFreq")->load();
        dbFloor_              = processor.apvts.getRawParameterValue ("lfftYMin")->load();
        dbCeil_               = processor.apvts.getRawParameterValue ("lfftYMax")->load();
        const float calOffset = processor.apvts.getRawParameterValue ("calOffset")->load();
        const float holdSecs  = processor.apvts.getRawParameterValue ("peakHoldTime")->load();

        const int avgCycles = juce::jlimit (1, 999,
            static_cast<int> (processor.apvts.getRawParameterValue ("fftAvgCycles")->load()));
        const float gainLinear = std::pow (10.0f, gain / 20.0f);

        processor.copyFftWindow (windowBuf_.data(), fftSize_);

        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int i = 0; i < fftSize_; ++i)
        {
            float w;
            const float phase = twoPi * i / (fftSize_ - 1);
            switch (winType)
            {
                case 1:  w = 0.54f - 0.46f * std::cos (phase); break;
                case 2:  w = 0.42f - 0.5f * std::cos (phase) + 0.08f * std::cos (2.0f * phase); break;
                case 3:  w = 1.0f - 1.9320f * std::cos (phase) + 1.2862f * std::cos (2.0f * phase)
                              - 0.3227f * std::cos (3.0f * phase) + 0.0279f * std::cos (4.0f * phase); break;
                case 4:  w = 1.0f; break;
                default: w = 0.5f * (1.0f - std::cos (phase)); break;
            }
            fftData_[i] = windowBuf_[i] * w * gainLinear;
        }
        std::fill (fftData_.begin() + fftSize_, fftData_.end(), 0.0f);

        forwardFFT->performFrequencyOnlyForwardTransform (fftData_.data());

        const int halfSize = fftSize_ / 2;

        if (static_cast<int> (avgAccum_.size()) != halfSize)
        {
            avgAccum_.assign (halfSize, 0.0f);
            avgCount_ = 0;
        }

        for (int k = 0; k < halfSize; ++k)
            avgAccum_[k] += fftData_[k];

        ++avgCount_;

        if (avgCount_ < avgCycles)
            return;

        const float invCount = 1.0f / static_cast<float> (avgCount_);
        for (int k = 0; k < halfSize; ++k)
            fftData_[k] = avgAccum_[k] * invCount;

        avgAccum_.assign (halfSize, 0.0f);
        avgCount_ = 0;

        const int N = kBandNForRes[juce::jlimit (0, 4, resIdx)];

        if (N != bandN_)
        {
            std::fill (bandsSmoothed_, bandsSmoothed_ + kMaxBands, dbFloor_);
            std::fill (bandsPeak_,     bandsPeak_     + kMaxBands, dbFloor_);
            std::fill (peakTimestamps_, peakTimestamps_ + kMaxBands, 0.0);
            bandN_ = N;
        }

        const float stepFactor     = std::pow (2.0f, 1.0f / static_cast<float> (N));
        const float halfBandFactor = std::sqrt (stepFactor);
        const float normFactor     = static_cast<float> (fftSize_);

        numBands_ = 0;
        for (float fc = freqMin_; fc <= freqMax_ && numBands_ < kMaxBands; fc *= stepFactor)
        {
            const int b     = numBands_++;
            const int binLo = std::max (1,        static_cast<int> (fc / halfBandFactor * fftSize_ / sampleRate_));
            const int binHi = std::min (halfSize,  static_cast<int> (fc * halfBandFactor * fftSize_ / sampleRate_) + 1);

            float peak = 0.0f;
            for (int k = binLo; k < binHi; ++k)
                peak = std::max (peak, fftData_[k]);

            float dbSPL = 20.0f * std::log10 (peak / normFactor + 1e-10f) + calOffset;
            if (rtaMode)
                dbSPL += 3.0f * std::log2 (fc / freqMin_);

            bands_[b] = juce::jlimit (dbFloor_, dbCeil_, dbSPL);
        }

        const double nowMs = juce::Time::getMillisecondCounterHiRes();

        for (int b = 0; b < numBands_; ++b)
        {
            bandsSmoothed_[b] = smoothing * bandsSmoothed_[b] + (1.0f - smoothing) * bands_[b];

            if (peakHold_)
            {
                if (bandsSmoothed_[b] >= bandsPeak_[b]
                    || (nowMs - peakTimestamps_[b]) > holdSecs * 1000.0)
                {
                    bandsPeak_[b]      = bandsSmoothed_[b];
                    peakTimestamps_[b] = nowMs;
                }
            }
            else
            {
                bandsPeak_[b] = dbFloor_;
            }
        }

        repaint();
    }

    void drawGrid (juce::Graphics& g, const juce::Rectangle<float>& plotArea) const
    {
        const float logMin   = std::log10 (freqMin_);
        const float logMax   = std::log10 (freqMax_);
        const float logRange = logMax - logMin;
        const float dbRange  = dbCeil_ - dbFloor_;

        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));

        g.setColour (juce::Colour (0xff3a3a3c));
        for (float db = std::ceil (dbFloor_ / 10.0f) * 10.0f; db <= dbCeil_; db += 10.0f)
        {
            float yNorm = (db - dbFloor_) / dbRange;
            float y     = plotArea.getBottom() - yNorm * plotArea.getHeight();
            g.drawHorizontalLine ((int) y, plotArea.getX(), plotArea.getRight());

            g.setColour (juce::Colour (0xffaaaaaa));
            g.drawText (juce::String ((int) db) + " dB",
                        0.0f, y - 6.0f, plotArea.getX() - 4.0f, 12.0f,
                        juce::Justification::centredRight, false);
            g.setColour (juce::Colour (0xff3a3a3c));
        }

        static const float gridFreqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000 };
        for (float freq : gridFreqs)
        {
            if (freq < freqMin_ || freq > freqMax_) continue;
            float xNorm = (std::log10 (freq) - logMin) / logRange;
            float x     = plotArea.getX() + xNorm * plotArea.getWidth();
            g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());

            g.setColour (juce::Colour (0xffaaaaaa));
            juce::String label = (freq >= 1000.0f)
                ? juce::String ((int) (freq / 1000.0f)) + "k"
                : juce::String ((int) freq);
            g.drawText (label,
                        x - 20.0f, plotArea.getBottom() + 2.0f, 40.0f, 16.0f,
                        juce::Justification::centred, false);
            g.setColour (juce::Colour (0xff3a3a3c));
        }
    }

    void drawMagnifyingGlass (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        area = area.reduced (3.0f);
        const float r     = area.getWidth() * 0.32f;
        const float cx    = area.getCentreX() - r * 0.15f;
        const float cy    = area.getCentreY() - r * 0.15f;
        const float angle = juce::MathConstants<float>::pi * 0.785f;
        g.setColour (juce::Colour (0xffaeaeb2));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
        g.drawLine (cx + std::cos (angle) * r,       cy + std::sin (angle) * r,
                    cx + std::cos (angle) * r * 1.9f, cy + std::sin (angle) * r * 1.9f, 1.5f);
    }

    // 0=Continuous, 1=Dashed, 2=Dotted, 3=Dash-dot
    static void strokeWithStyle (juce::Graphics& g, const juce::Path& src, float thickness, int style)
    {
        if (style == 0)
        {
            g.strokePath (src, juce::PathStrokeType (thickness));
        }
        else
        {
            const float dash[]    = { 6.0f, 3.0f };
            const float dot[]     = { 2.0f, 3.0f };
            const float dashDot[] = { 6.0f, 3.0f, 2.0f, 3.0f };
            const float* pattern;
            int count;
            switch (style)
            {
                case 2:  pattern = dot;     count = 2; break;
                case 3:  pattern = dashDot; count = 4; break;
                default: pattern = dash;    count = 2; break;
            }
            juce::Path dashed;
            juce::PathStrokeType (thickness).createDashedStroke (dashed, src, pattern, count);
            g.fillPath (dashed);
        }
    }

    //==========================================================================
    SPLMeterAudioProcessor& processor;

    int   fftOrder_ = 11;
    int   fftSize_  = 1 << 11;
    float freqMin_  = 20.0f;
    float freqMax_  = 20000.0f;
    float dbFloor_  = 20.0f;
    float dbCeil_   = 130.0f;
    int   displayMode_ = 0;
    bool  peakHold_ = false;
    int   bandN_    = 0;
    int   numBands_ = 0;
    double sampleRate_ = 44100.0;

    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::vector<float> windowBuf_;
    std::vector<float> fftData_;
    std::vector<float> avgAccum_;
    int avgCount_ = 0;

    float bands_[kMaxBands]          {};
    float bandsSmoothed_[kMaxBands]  {};
    float bandsPeak_[kMaxBands]      {};
    double peakTimestamps_[kMaxBands] {};

    juce::TextButton yZoomButton_, xZoomButton_;
    juce::TextButton snapshotButton_;

    std::vector<LFFTSnapshot> snapshots_;

    struct UndoItem { int index; LFFTSnapshot snapshot; };
    std::vector<UndoItem> undoStack_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFFTComponent)
};

//==============================================================================
// CallOutBox panel for snapshot colour + line style
class SnapshotSettingsPanel : public juce::Component
{
public:
    SnapshotSettingsPanel (LFFTSnapshot& snap, std::function<void()> onChange)
        : snap_ (snap), onChange_ (std::move (onChange))
    {
        // Colour buttons
        static const juce::Colour kColours[] = {
            juce::Colour (0xff34c759), juce::Colour (0xffff9f0a),
            juce::Colour (0xffff375f), juce::Colour (0xffbf5af2),
            juce::Colour (0xff64d2ff), juce::Colour (0xffffcc00),
            juce::Colour (0xffac8e68), juce::Colour (0xff30d158),
            juce::Colour (0xff5ac8fa), juce::Colour (0xffff453a),
        };

        for (int i = 0; i < 10; ++i)
        {
            auto* btn = colourButtons_.add (new juce::TextButton());
            btn->setColour (juce::TextButton::buttonColourId,   kColours[i]);
            btn->setColour (juce::TextButton::buttonOnColourId, kColours[i]);
            btn->setButtonText ("");
            btn->onClick = [this, i]
            {
                snap_.colour = kColours[i];
                if (onChange_) onChange_();
            };
            addAndMakeVisible (btn);
        }

        // Style buttons
        static const char* styleNames[] = { "Solid", "Dash", "Dot", "DaDot" };
        for (int i = 0; i < 4; ++i)
        {
            auto* btn = styleButtons_.add (new juce::TextButton (styleNames[i]));
            btn->setRadioGroupId (9001);
            btn->setClickingTogglesState (true);
            btn->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
            btn->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ac8fa));
            btn->setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            btn->setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
            btn->setToggleState (snap_.lineStyle == i, juce::dontSendNotification);
            btn->onClick = [this, i]
            {
                snap_.lineStyle = i;
                if (onChange_) onChange_();
            };
            addAndMakeVisible (btn);
        }

        colourLabel_.setText ("Colour", juce::dontSendNotification);
        colourLabel_.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
        colourLabel_.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
        addAndMakeVisible (colourLabel_);

        styleLabel_.setText ("Style", juce::dontSendNotification);
        styleLabel_.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
        styleLabel_.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
        addAndMakeVisible (styleLabel_);

        setSize (220, 80);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);

        // Colour row
        auto colourRow = b.removeFromTop (14);
        colourLabel_.setBounds (colourRow);
        b.removeFromTop (2);
        auto swatchRow = b.removeFromTop (20);
        const int sw = swatchRow.getWidth() / 10;
        for (int i = 0; i < 10; ++i)
            colourButtons_[i]->setBounds (swatchRow.removeFromLeft (sw).reduced (1, 0));

        b.removeFromTop (4);

        // Style row
        auto styleRow = b.removeFromTop (14);
        styleLabel_.setBounds (styleRow);
        b.removeFromTop (2);
        auto btnRow = b.removeFromTop (22);
        const int bw = btnRow.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            styleButtons_[i]->setBounds (btnRow.removeFromLeft (bw).reduced (1, 0));
    }

private:
    LFFTSnapshot& snap_;
    std::function<void()> onChange_;
    juce::OwnedArray<juce::TextButton> colourButtons_;
    juce::OwnedArray<juce::TextButton> styleButtons_;
    juce::Label colourLabel_, styleLabel_;
};

//==============================================================================
class LFFTWindow : public juce::DocumentWindow
{
    //--------------------------------------------------------------------------
    // Custom row component with cog button
    struct SnapshotRowComponent : public juce::Component
    {
        LFFTComponent& lfft;
        juce::ListBox& listBox;
        int row = -1;

        juce::TextButton cogButton;

        SnapshotRowComponent (LFFTComponent& l, juce::ListBox& lb)
            : lfft (l), listBox (lb)
        {
            cogButton.setButtonText (juce::String::fromUTF8 ("\xe2\x9a\x99"));  // U+2699 gear
            cogButton.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
            cogButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffaeaeb2));
            cogButton.onClick = [this]
            {
                auto& snaps = lfft.getSnapshots();
                if (row < 0 || row >= static_cast<int> (snaps.size())) return;

                auto panel = std::make_unique<SnapshotSettingsPanel> (
                    snaps[row],
                    [this] { listBox.repaint(); });

                juce::CallOutBox::launchAsynchronously (
                    std::move (panel), cogButton.getScreenBounds(), nullptr);
            };
            addAndMakeVisible (cogButton);
        }

        void setRow (int r) { row = r; }

        void resized() override
        {
            cogButton.setBounds (getWidth() - 20, 0, 20, getHeight());
        }

        void paint (juce::Graphics& g) override
        {
            auto& snaps = lfft.getSnapshots();
            if (row < 0 || row >= static_cast<int> (snaps.size())) return;

            const auto& snap = snaps[row];
            const int w = getWidth();
            const int h = getHeight();

            if (listBox.isRowSelected (row))
                g.fillAll (juce::Colour (0xff48484a));
            else
                g.fillAll (juce::Colour (0xff2c2c2e));

            // Colour dot
            g.setColour (snap.colour);
            g.fillEllipse (4.0f, h / 2.0f - 4.0f, 8.0f, 8.0f);

            // Name
            g.setColour (snap.visible ? juce::Colours::white : juce::Colour (0xff6c6c70));
            g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            g.drawText (snap.name, 16, 0, w - 40, h, juce::Justification::centredLeft, true);
        }
    };

    //--------------------------------------------------------------------------
    struct SnapshotListModel : public juce::ListBoxModel
    {
        LFFTComponent& lfft;
        juce::ListBox& listBox;

        SnapshotListModel (LFFTComponent& l, juce::ListBox& lb) : lfft (l), listBox (lb) {}

        int getNumRows() override { return static_cast<int> (lfft.getSnapshots().size()); }

        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}

        juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
        {
            auto* comp = dynamic_cast<SnapshotRowComponent*> (existing);
            if (comp == nullptr)
                comp = new SnapshotRowComponent (lfft, listBox);
            comp->setRow (row);
            return comp;
        }

        void listBoxItemClicked (int row, const juce::MouseEvent& e) override
        {
            auto& snaps = lfft.getSnapshots();
            if (row < 0 || row >= static_cast<int> (snaps.size())) return;

            if (e.mods.isRightButtonDown())
            {
                lfft.removeSnapshot (row);
                listBox.updateContent();
            }
            else
            {
                // Only toggle visibility if click was NOT on the cog area
                if (e.x < listBox.getWidth() - 20)
                {
                    snaps[row].visible = ! snaps[row].visible;
                    listBox.repaint();
                }
            }
        }

        void deleteKeyPressed (int lastRowSelected) override
        {
            lfft.removeSnapshot (lastRowSelected);
            listBox.updateContent();
        }
    };

    //--------------------------------------------------------------------------
    struct Content : public juce::Component
    {
        Content (SPLMeterAudioProcessor& p)
            : lfft (p), listModel (lfft, listBox)
        {
            addAndMakeVisible (lfft);

            listBox.setModel (&listModel);
            listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff2c2c2e));
            listBox.setColour (juce::ListBox::outlineColourId,    juce::Colour (0xff48484a));
            listBox.setRowHeight (20);
            addAndMakeVisible (listBox);

            lfft.onSnapshotChanged = [this] { listBox.updateContent(); };
        }

        void resized() override
        {
            auto r = getLocalBounds();
            listBox.setBounds (r.removeFromLeft (140));
            lfft.setBounds (r);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1c1c1e)); }

        LFFTComponent    lfft;
        juce::ListBox    listBox;
        SnapshotListModel listModel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

public:
    explicit LFFTWindow (SPLMeterAudioProcessor& p)
        : juce::DocumentWindow ("L_FFT",
                                juce::Colour (0xff1c1c1e),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        auto* content = new Content (p);
        content->setSize (840, 400);
        setContentOwned (content, true);
        setResizable (true, false);
    }

    void closeButtonPressed() override { setVisible (false); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFFTWindow)
};
