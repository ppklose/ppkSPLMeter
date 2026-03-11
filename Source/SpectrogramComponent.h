#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cmath>
#include <array>
#include <algorithm>
#include <complex>
#include <vector>

//==============================================================================
class SpectrogramComponent : public juce::Component,
                             private juce::Timer
{
public:
    explicit SpectrogramComponent (SPLMeterAudioProcessor& p)
        : processor (p)
    {
        rebuildFFT (fftOrder_);
        startTimerHz (30);
    }

    ~SpectrogramComponent() override { stopTimer(); }

    void setFormantsEnabled (bool e) { formantsEnabled_ = e; }

    void setFftOrder (int order)
    {
        fftOrder_ = juce::jlimit (9, 13, order);
        rebuildFFT (fftOrder_);
        clearSpectrogramImage();
    }

    void setHopSize    (int hop)  { hopSize_ = hop; hopCounter = 0; }
    void setDbFloor    (float db) { dbFloor_ = db; }
    void setDbCeil     (float db) { dbCeil_  = db; }
    void setColourMap  (int map)  { colourMap_  = map; }
    void setWindowType (int wt)   { windowType_ = wt; }

    void setFreqMin (float f) { freqMin_ = f; clearSpectrogramImage(); repaint(); }
    void setFreqMax (float f) { freqMax_ = f; clearSpectrogramImage(); repaint(); }

    //==========================================================================
    void resized() override
    {
        const int w = getWidth() - kLabelWidth;
        const int h = getHeight();
        if (w > 1 && h > 1)
        {
            spectrogramImage = juce::Image (juce::Image::RGB, w, h, true);
            clearSpectrogramImage();
        }
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1c1c1e));

        if (spectrogramImage.getWidth() > 1)
            g.drawImage (spectrogramImage,
                         juce::Rectangle<float> ((float) kLabelWidth, 0.0f,
                                                 (float) spectrogramImage.getWidth(),
                                                 (float) getHeight()));

        // Frequency axis labels + gridlines
        const int h = getHeight();
        static const float kLabelFreqs[] = { 20000.f, 10000.f, 5000.f, 2000.f,
                                             1000.f, 500.f, 200.f, 100.f, 50.f, 20.f };
        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));

        for (float freq : kLabelFreqs)
        {
            if (freq < freqMin_ * 0.5f || freq > freqMax_ * 2.0f) continue;

            float frac = (std::log10 (freq)    - std::log10 (freqMin_))
                       / (std::log10 (freqMax_) - std::log10 (freqMin_));
            int y = juce::roundToInt ((1.0f - frac) * (h - 1));
            if (y < 0 || y >= h) continue;

            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine (y, (float) kLabelWidth, (float) getWidth());

            juce::String label = (freq >= 1000.f)
                ? juce::String ((int)(freq / 1000.f)) + "k"
                : juce::String ((int) freq);

            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.drawText (label, 0, y - 6, kLabelWidth - 4, 12,
                        juce::Justification::centredRight, false);
        }
    }

private:
    //==========================================================================
    void rebuildFFT (int order)
    {
        fftSize_   = 1 << order;
        forwardFFT = std::make_unique<juce::dsp::FFT> (order);
        inputBuf.assign (fftSize_,     0.0f);
        fftData.assign  (fftSize_ * 2, 0.0f);
        inputHead  = 0;
        hopCounter = 0;
    }

    void clearSpectrogramImage()
    {
        if (spectrogramImage.getWidth() > 1)
            spectrogramImage.clear (spectrogramImage.getBounds(), juce::Colour (0xff1c1c1e));
    }

    //==========================================================================
    void timerCallback() override
    {
        const double sr = processor.getSampleRate();
        sampleRate = (sr > 0.0) ? sr : 44100.0;

        if (spectrogramImage.getWidth() < 2)
            return;

        std::array<float, 2048> tmp;
        int pulled = processor.pullSpectroSamples (tmp.data(), (int) tmp.size());

        bool drew = false;
        for (int i = 0; i < pulled; ++i)
        {
            inputBuf[inputHead % fftSize_] = tmp[i];
            ++inputHead;
            ++hopCounter;

            if (hopCounter >= hopSize_)
            {
                hopCounter = 0;
                drawColumn();
                drew = true;
            }
        }

        if (drew)
            repaint();
    }

    //==========================================================================
    // LPC-based formant detection
    std::vector<float> detectFormants (const float* windowed, int n)
    {
        constexpr int order = 12;

        std::vector<float> x (n);
        x[0] = windowed[0];
        for (int i = 1; i < n; ++i)
            x[i] = windowed[i] - 0.97f * windowed[i - 1];

        std::vector<double> r (order + 1, 0.0);
        for (int k = 0; k <= order; ++k)
            for (int i = k; i < n; ++i)
                r[k] += (double) x[i] * x[i - k];

        if (r[0] < 1e-10) return {};

        std::vector<double> a (order + 1, 0.0);
        a[0] = 1.0;
        double err = r[0];
        for (int i = 1; i <= order; ++i)
        {
            double lam = 0.0;
            for (int j = 1; j < i; ++j)
                lam += a[j] * r[i - j];
            lam = -(r[i] + lam) / err;

            auto aPrev = a;
            for (int j = 1; j < i; ++j)
                a[j] = aPrev[j] + lam * aPrev[i - j];
            a[i] = lam;

            err *= (1.0 - lam * lam);
            if (err < 1e-12) break;
        }

        std::vector<std::complex<double>> roots (order);
        for (int i = 0; i < order; ++i)
        {
            double angle = juce::MathConstants<double>::twoPi * i / order;
            roots[i] = std::polar (0.9, angle);
        }

        auto polyEval = [&] (std::complex<double> z) -> std::complex<double>
        {
            std::complex<double> v = 1.0;
            for (int k = 1; k <= order; ++k)
                v = v * z + a[k];
            return v;
        };

        for (int iter = 0; iter < 80; ++iter)
        {
            for (int i = 0; i < order; ++i)
            {
                std::complex<double> denom = 1.0;
                for (int j = 0; j < order; ++j)
                    if (j != i) denom *= (roots[i] - roots[j]);
                if (std::abs (denom) > 1e-14)
                    roots[i] -= polyEval (roots[i]) / denom;
            }
        }

        std::vector<float> formants;
        for (auto& root : roots)
        {
            if (root.imag() <= 0.0) continue;
            double mag = std::abs (root);
            if (mag < 0.5 || mag > 1.0) continue;

            double freq = std::arg (root) * sampleRate / juce::MathConstants<double>::twoPi;
            double bw   = -std::log (mag) * sampleRate / juce::MathConstants<double>::pi;

            if (freq >= 50.0 && freq <= 5500.0 && bw < 600.0)
                formants.push_back ((float) freq);
        }

        std::sort (formants.begin(), formants.end());
        return formants;
    }

    //==========================================================================
    void drawColumn()
    {
        const int imgW = spectrogramImage.getWidth();
        const int imgH = spectrogramImage.getHeight();

        // Copy circular buffer → fftData in chronological order
        for (int i = 0; i < fftSize_; ++i)
            fftData[i] = inputBuf[(inputHead - fftSize_ + i + fftSize_ * 4) % fftSize_];

        // Apply window function
        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int i = 0; i < fftSize_; ++i)
        {
            float w;
            const float phase = twoPi * i / (fftSize_ - 1);
            switch (windowType_)
            {
                case 1: // Hamming
                    w = 0.54f - 0.46f * std::cos (phase);
                    break;
                case 2: // Blackman
                    w = 0.42f - 0.5f  * std::cos (phase)
                              + 0.08f * std::cos (2.0f * phase);
                    break;
                case 3: // Flat-top
                    w = 1.0f
                      - 1.9320f * std::cos (phase)
                      + 1.2862f * std::cos (2.0f * phase)
                      - 0.3227f * std::cos (3.0f * phase)
                      + 0.0279f * std::cos (4.0f * phase);
                    break;
                default: // Hann
                    w = 0.5f * (1.0f - std::cos (phase));
                    break;
            }
            fftData[i] *= w;
        }

        // Detect formants from windowed frame (before FFT overwrites fftData)
        std::vector<float> formants;
        if (formantsEnabled_)
            formants = detectFormants (fftData.data(), fftSize_);

        // Zero upper half and run FFT
        std::fill (fftData.begin() + fftSize_, fftData.end(), 0.0f);
        forwardFFT->performFrequencyOnlyForwardTransform (fftData.data());

        // Scroll image left
        spectrogramImage.moveImageSection (0, 0, 1, 0, imgW - 1, imgH);

        // Draw spectrogram column
        const float logMin  = std::log10 (freqMin_);
        const float logMax  = std::log10 (freqMax_);
        const float binHz   = static_cast<float> (sampleRate) / fftSize_;
        const int   numBins = fftSize_ / 2;
        const float gainDB  = processor.apvts.getRawParameterValue ("spectroGain")->load();

        for (int y = 0; y < imgH; ++y)
        {
            float frac = 1.0f - static_cast<float>(y) / (imgH - 1);
            float freq = std::pow (10.0f, logMin + frac * (logMax - logMin));
            int   bin  = juce::roundToInt (freq / binHz);
            bin = juce::jlimit (0, numBins - 1, bin);

            float magnitude = fftData[bin];
            float dB = 20.0f * std::log10 (std::max (magnitude / fftSize_, 1e-10f)) + gainDB;
            float level = juce::jlimit (0.0f, 1.0f, (dB - dbFloor_) / (dbCeil_ - dbFloor_));
            spectrogramImage.setPixelAt (imgW - 1, y, levelToColour (level));
        }

        // Overlay formant dots
        if (formantsEnabled_)
        {
            static const juce::Colour formantColours[] = {
                juce::Colour (0xffff4444),
                juce::Colour (0xff44ff44),
                juce::Colour (0xff44ffff),
                juce::Colour (0xffffaa00),
            };

            for (int fi = 0; fi < (int) formants.size() && fi < 4; ++fi)
            {
                float frac = (std::log10 (formants[fi]) - logMin) / (logMax - logMin);
                int   y    = juce::roundToInt ((1.0f - frac) * (imgH - 1));
                auto  col  = formantColours[fi];

                for (int dy = -2; dy <= 2; ++dy)
                {
                    int py = juce::jlimit (0, imgH - 1, y + dy);
                    spectrogramImage.setPixelAt (imgW - 1, py, col);
                }
            }
        }
    }

    //==========================================================================
    // Colour map helpers
    static juce::Colour interpStops (float level,
                                     const std::pair<float, juce::Colour>* stops, int n) noexcept
    {
        if (level <= stops[0].first)   return stops[0].second;
        if (level >= stops[n-1].first) return stops[n-1].second;
        for (int i = 0; i < n - 1; ++i)
        {
            if (level <= stops[i+1].first)
            {
                float t = (level - stops[i].first) / (stops[i+1].first - stops[i].first);
                return stops[i].second.interpolatedWith (stops[i+1].second, t);
            }
        }
        return stops[n-1].second;
    }

    juce::Colour levelToColour (float level) const noexcept
    {
        switch (colourMap_)
        {
            case 1: // Greyscale
            {
                const uint8 v = (uint8) juce::roundToInt (level * 255.0f);
                return juce::Colour (v, v, v);
            }
            case 2: // Inferno
            {
                static const std::pair<float, juce::Colour> stops[] = {
                    { 0.00f, juce::Colour (0xff000004) },
                    { 0.25f, juce::Colour (0xff51127c) },
                    { 0.50f, juce::Colour (0xffb5367a) },
                    { 0.75f, juce::Colour (0xfffb8761) },
                    { 1.00f, juce::Colour (0xfffcfdbf) },
                };
                return interpStops (level, stops, 5);
            }
            case 3: // Viridis
            {
                static const std::pair<float, juce::Colour> stops[] = {
                    { 0.00f, juce::Colour (0xff440154) },
                    { 0.25f, juce::Colour (0xff31688e) },
                    { 0.50f, juce::Colour (0xff35b779) },
                    { 0.75f, juce::Colour (0xff90d743) },
                    { 1.00f, juce::Colour (0xfffde725) },
                };
                return interpStops (level, stops, 5);
            }
            default: // Fire (original)
            {
                static const std::pair<float, juce::Colour> stops[] = {
                    { 0.00f, juce::Colour (0xff000000) },
                    { 0.20f, juce::Colour (0xff1a0050) },
                    { 0.40f, juce::Colour (0xff0040ff) },
                    { 0.58f, juce::Colour (0xff00e5ff) },
                    { 0.72f, juce::Colour (0xff00ff44) },
                    { 0.86f, juce::Colour (0xffffff00) },
                    { 1.00f, juce::Colour (0xffff2200) },
                };
                return interpStops (level, stops, 7);
            }
        }
    }

    //==========================================================================
    SPLMeterAudioProcessor& processor;

    static constexpr int kLabelWidth = 46;

    // Options (all mutable at runtime)
    int   fftOrder_   = 11;
    int   fftSize_    = 1 << 11;
    int   hopSize_    = 256;
    float freqMin_    = 20.0f;
    float freqMax_    = 20000.0f;
    float dbFloor_    = -80.0f;
    float dbCeil_     =  0.0f;
    int   colourMap_  = 0;   // 0=Fire 1=Greyscale 2=Inferno 3=Viridis
    int   windowType_ = 0;   // 0=Hann 1=Hamming 2=Blackman 3=Flat-top
    bool  formantsEnabled_ = false;

    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::vector<float> inputBuf;
    std::vector<float> fftData;
    int    inputHead  = 0;
    int    hopCounter = 0;
    double sampleRate = 44100.0;

    juce::Image spectrogramImage { juce::Image::RGB, 1, 1, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramComponent)
};

//==============================================================================
class SpectrogramWindow : public juce::DocumentWindow
{
    //==========================================================================
    struct Content : public juce::Component
    {
        Content (SPLMeterAudioProcessor& p) : spectro (p)
        {
            addAndMakeVisible (spectro);

            // --- Gain slider (row 1) ---
            gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
            gainSlider.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
            gainSlider.setColour (juce::Slider::thumbColourId,          juce::Colour (0xff5ac8fa));
            gainSlider.setColour (juce::Slider::textBoxTextColourId,    juce::Colours::white);
            gainSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            gainSlider.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff3a3a3c));
            gainSlider.setTooltip ("Spectrogram display gain (dB)");
            addAndMakeVisible (gainSlider);

            gainLabel.setText ("Gain", juce::dontSendNotification);
            gainLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            gainLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
            gainLabel.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (gainLabel);

            // --- Formants toggle (row 1) ---
            formantsButton.setClickingTogglesState (true);
            formantsButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
            formantsButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ac8fa));
            formantsButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            formantsButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff1c1c1e));
            formantsButton.setTooltip ("Overlay LPC formant tracks (F1–F4)");
            formantsButton.onClick = [this] { spectro.setFormantsEnabled (formantsButton.getToggleState()); };
            addAndMakeVisible (formantsButton);

            gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                p.apvts, "spectroGain", gainSlider);

            // --- Row 2 controls ---
            auto styleCombo = [&] (juce::ComboBox& c, const juce::String& tooltip)
            {
                c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff3a3a3c));
                c.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
                c.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff48484a));
                c.setColour (juce::ComboBox::arrowColourId,      juce::Colours::white);
                c.setTooltip (tooltip);
                addAndMakeVisible (c);
            };

            auto makeRowLabel = [&] (juce::Label& lbl, const juce::String& text)
            {
                lbl.setText (text, juce::dontSendNotification);
                lbl.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
                lbl.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
                lbl.setJustificationType (juce::Justification::centredRight);
                addAndMakeVisible (lbl);
            };

            auto styleSlider2 = [&] (juce::Slider& s, const juce::String& tooltip)
            {
                s.setSliderStyle (juce::Slider::LinearHorizontal);
                s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
                s.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
                s.setColour (juce::Slider::thumbColourId,          juce::Colour (0xff5ac8fa));
                s.setColour (juce::Slider::textBoxTextColourId,    juce::Colours::white);
                s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                s.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff3a3a3c));
                s.setTooltip (tooltip);
                addAndMakeVisible (s);
            };

            // FFT Size
            makeRowLabel (fftSizeLabel, "FFT");
            fftSizeCombo.addItem ("512",  1);
            fftSizeCombo.addItem ("1024", 2);
            fftSizeCombo.addItem ("2048", 3);
            fftSizeCombo.addItem ("4096", 4);
            fftSizeCombo.setSelectedId (3, juce::dontSendNotification);
            fftSizeCombo.onChange = [this]
            {
                const int orders[] = { 9, 10, 11, 12 };
                spectro.setFftOrder (orders[fftSizeCombo.getSelectedId() - 1]);
            };
            styleCombo (fftSizeCombo, "FFT window size — larger = better frequency resolution");

            // Hop Size
            makeRowLabel (hopLabel, "Hop");
            hopCombo.addItem ("64",  1);
            hopCombo.addItem ("128", 2);
            hopCombo.addItem ("256", 3);
            hopCombo.addItem ("512", 4);
            hopCombo.setSelectedId (3, juce::dontSendNotification);
            hopCombo.onChange = [this]
            {
                const int hops[] = { 64, 128, 256, 512 };
                spectro.setHopSize (hops[hopCombo.getSelectedId() - 1]);
            };
            styleCombo (hopCombo, "Hop size — smaller = faster scroll");

            // Window function
            makeRowLabel (windowLabel, "Win");
            windowCombo.addItem ("Hann",     1);
            windowCombo.addItem ("Hamming",  2);
            windowCombo.addItem ("Blackman", 3);
            windowCombo.addItem ("Flat-top", 4);
            windowCombo.setSelectedId (1, juce::dontSendNotification);
            windowCombo.onChange = [this] { spectro.setWindowType (windowCombo.getSelectedId() - 1); };
            styleCombo (windowCombo, "Analysis window function");

            // Colour map
            makeRowLabel (colourLabel, "Map");
            colourCombo.addItem ("Fire",      1);
            colourCombo.addItem ("Greyscale", 2);
            colourCombo.addItem ("Inferno",   3);
            colourCombo.addItem ("Viridis",   4);
            colourCombo.setSelectedId (1, juce::dontSendNotification);
            colourCombo.onChange = [this] { spectro.setColourMap (colourCombo.getSelectedId() - 1); };
            styleCombo (colourCombo, "Colour map");

            // dB Floor
            makeRowLabel (floorLabel, "Floor");
            floorSlider.setRange (-120.0, -20.0, 1.0);
            floorSlider.setValue (-80.0, juce::dontSendNotification);
            floorSlider.setTextValueSuffix (" dB");
            floorSlider.onValueChange = [this] { spectro.setDbFloor ((float) floorSlider.getValue()); };
            styleSlider2 (floorSlider, "Noise floor threshold (dB)");

            // dB Ceiling
            makeRowLabel (ceilLabel, "Ceil");
            ceilSlider.setRange (-40.0, 40.0, 1.0);
            ceilSlider.setValue (0.0, juce::dontSendNotification);
            ceilSlider.setTextValueSuffix (" dB");
            ceilSlider.onValueChange = [this] { spectro.setDbCeil ((float) ceilSlider.getValue()); };
            styleSlider2 (ceilSlider, "Display ceiling (dB)");

            // Freq Min
            makeRowLabel (freqMinLabel, "Min");
            freqMinCombo.addItem ("20 Hz",  1);
            freqMinCombo.addItem ("50 Hz",  2);
            freqMinCombo.addItem ("100 Hz", 3);
            freqMinCombo.addItem ("200 Hz", 4);
            freqMinCombo.setSelectedId (1, juce::dontSendNotification);
            freqMinCombo.onChange = [this]
            {
                const float freqs[] = { 20.0f, 50.0f, 100.0f, 200.0f };
                spectro.setFreqMin (freqs[freqMinCombo.getSelectedId() - 1]);
            };
            styleCombo (freqMinCombo, "Lowest displayed frequency");

            // Freq Max
            makeRowLabel (freqMaxLabel, "Max");
            freqMaxCombo.addItem ("2 kHz",  1);
            freqMaxCombo.addItem ("5 kHz",  2);
            freqMaxCombo.addItem ("10 kHz", 3);
            freqMaxCombo.addItem ("20 kHz", 4);
            freqMaxCombo.setSelectedId (4, juce::dontSendNotification);
            freqMaxCombo.onChange = [this]
            {
                const float freqs[] = { 2000.0f, 5000.0f, 10000.0f, 20000.0f };
                spectro.setFreqMax (freqs[freqMaxCombo.getSelectedId() - 1]);
            };
            styleCombo (freqMaxCombo, "Highest displayed frequency");
        }

        void resized() override
        {
            auto r = getLocalBounds();

            // Row 2 (bottom): all new controls
            auto row2 = r.removeFromBottom (28).reduced (4, 4);

            // Row 1 (above row 2): gain + formants
            auto row1 = r.removeFromBottom (28).reduced (4, 4);
            formantsButton.setBounds (row1.removeFromRight (90));
            row1.removeFromRight (4);
            gainLabel.setBounds (row1.removeFromLeft (36));
            gainSlider.setBounds (row1);

            // Row 2 layout (left to right)
            auto tbL2 = [&] (juce::Label& lbl, juce::Component& ctrl, int labelW, int ctrlW)
            {
                lbl.setBounds  (row2.removeFromLeft (labelW));
                ctrl.setBounds (row2.removeFromLeft (ctrlW));
                row2.removeFromLeft (6);
            };

            tbL2 (fftSizeLabel,  fftSizeCombo,  30,  70);
            tbL2 (hopLabel,      hopCombo,       26,  70);
            tbL2 (windowLabel,   windowCombo,    26,  90);
            tbL2 (colourLabel,   colourCombo,    26,  90);
            tbL2 (floorLabel,    floorSlider,    34, 130);
            tbL2 (ceilLabel,     ceilSlider,     26, 120);
            tbL2 (freqMinLabel,  freqMinCombo,   26,  80);
            tbL2 (freqMaxLabel,  freqMaxCombo,   26,  80);

            spectro.setBounds (r);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1c1c1e)); }

        SpectrogramComponent spectro;

        // Row 1
        juce::Slider     gainSlider;
        juce::Label      gainLabel;
        juce::TextButton formantsButton { "Formants" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

        // Row 2
        juce::Label    fftSizeLabel, hopLabel, windowLabel, colourLabel;
        juce::Label    floorLabel, ceilLabel, freqMinLabel, freqMaxLabel;
        juce::ComboBox fftSizeCombo, hopCombo, windowCombo, colourCombo;
        juce::ComboBox freqMinCombo, freqMaxCombo;
        juce::Slider   floorSlider, ceilSlider;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

public:
    explicit SpectrogramWindow (SPLMeterAudioProcessor& p)
        : juce::DocumentWindow ("Spectrogram",
                                juce::Colour (0xff1c1c1e),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        auto* content = new Content (p);
        content->setSize (900, 456);   // 28px taller for extra row
        setContentOwned (content, true);
        setResizable (true, false);
    }

    void closeButtonPressed() override { setVisible (false); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramWindow)
};
