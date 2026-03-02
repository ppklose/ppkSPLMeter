#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cmath>
#include <array>
#include <algorithm>

//==============================================================================
class SpectrogramComponent : public juce::Component,
                             private juce::Timer
{
public:
    explicit SpectrogramComponent (SPLMeterAudioProcessor& p)
        : processor (p)
    {
        startTimerHz (30);
    }

    ~SpectrogramComponent() override { stopTimer(); }

    //==========================================================================
    void resized() override
    {
        const int w = getWidth();
        const int h = getHeight();
        if (w > 1 && h > 1)
            spectrogramImage = juce::Image (juce::Image::RGB, w, h, true);
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1c1c1e));

        if (spectrogramImage.getWidth() > 1)
            g.drawImage (spectrogramImage, getLocalBounds().toFloat());

        // (frequency labels rendered by LogComponent overlay)
    }

private:
    //==========================================================================
    void timerCallback() override
    {
        const double sr = processor.getSampleRate();
        sampleRate = (sr > 0.0) ? sr : 44100.0;

        if (spectrogramImage.getWidth() < 2)
            return;

        // Pull all available samples from processor FIFO
        std::array<float, 2048> tmp;
        int pulled = processor.pullSpectroSamples (tmp.data(), (int) tmp.size());

        bool drew = false;
        for (int i = 0; i < pulled; ++i)
        {
            // Circular input buffer for overlap
            inputBuf[inputHead % kFFTSize] = tmp[i];
            ++inputHead;
            ++hopCounter;

            if (hopCounter >= kHopSize)
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
    void drawColumn()
    {
        const int w = spectrogramImage.getWidth();
        const int h = spectrogramImage.getHeight();

        // Copy circular buffer → fftData in chronological order
        for (int i = 0; i < kFFTSize; ++i)
            fftData[i] = inputBuf[(inputHead - kFFTSize + i + kFFTSize * 4) % kFFTSize];

        // Apply Hann window
        for (int i = 0; i < kFFTSize; ++i)
        {
            float w_ = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * i / (kFFTSize - 1)));
            fftData[i] *= w_;
        }

        // Zero upper half and run FFT
        std::fill (fftData.begin() + kFFTSize, fftData.end(), 0.0f);
        forwardFFT.performFrequencyOnlyForwardTransform (fftData.data());

        // Scroll image left by 1 pixel
        spectrogramImage.moveImageSection (0, 0, 1, 0, w - 1, h);

        // Draw new right column
        const float logMin  = std::log10 (kFreqMin);
        const float logMax  = std::log10 (kFreqMax);
        const float binHz   = static_cast<float> (sampleRate) / kFFTSize;
        const int   numBins = kFFTSize / 2;

        for (int y = 0; y < h; ++y)
        {
            // y=0 → top → high freq
            float frac = 1.0f - static_cast<float>(y) / (h - 1);
            float freq = std::pow (10.0f, logMin + frac * (logMax - logMin));
            int   bin  = juce::roundToInt (freq / binHz);
            bin = juce::jlimit (0, numBins - 1, bin);

            float magnitude = fftData[bin];
            float gainDB = processor.apvts.getRawParameterValue ("spectroGain")->load();
            float dB = 20.0f * std::log10 (std::max (magnitude / kFFTSize, 1e-10f)) + gainDB;
            float level = juce::jlimit (0.0f, 1.0f, (dB - kDbFloor) / (kDbCeil - kDbFloor));

            spectrogramImage.setPixelAt (w - 1, y, levelToColour (level));
        }
    }

    //==========================================================================
    static juce::Colour levelToColour (float level) noexcept
    {
        struct Stop { float pos; juce::Colour col; };
        static const Stop stops[] = {
            { 0.00f, juce::Colour (0xff000000) },   // black
            { 0.20f, juce::Colour (0xff1a0050) },   // deep purple
            { 0.40f, juce::Colour (0xff0040ff) },   // blue
            { 0.58f, juce::Colour (0xff00e5ff) },   // cyan
            { 0.72f, juce::Colour (0xff00ff44) },   // green
            { 0.86f, juce::Colour (0xffffff00) },   // yellow
            { 1.00f, juce::Colour (0xffff2200) },   // red
        };
        constexpr int N = 6;

        if (level <= 0.0f) return stops[0].col;
        if (level >= 1.0f) return stops[N].col;

        for (int i = 0; i < N; ++i)
        {
            if (level <= stops[i + 1].pos)
            {
                float t = (level - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
                return stops[i].col.interpolatedWith (stops[i + 1].col, t);
            }
        }
        return stops[N].col;
    }

    //==========================================================================
    SPLMeterAudioProcessor& processor;

    static constexpr int   kFFTOrder = 11;
    static constexpr int   kFFTSize  = 1 << kFFTOrder;   // 2048
    static constexpr int   kHopSize  = 256;               // ~172 columns/sec @44.1kHz

    static constexpr float kFreqMin  = 20.0f;
    static constexpr float kFreqMax  = 20000.0f;
    static constexpr float kDbFloor  = -80.0f;
    static constexpr float kDbCeil   = 0.0f;

    juce::dsp::FFT forwardFFT { kFFTOrder };

    std::array<float, kFFTSize>     inputBuf  {};
    std::array<float, kFFTSize * 2> fftData   {};
    int   inputHead  = 0;
    int   hopCounter = 0;
    double sampleRate = 44100.0;

    juce::Image spectrogramImage { juce::Image::RGB, 1, 1, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramComponent)
};
