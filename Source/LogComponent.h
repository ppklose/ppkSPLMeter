#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SoundDetective.h"

//==============================================================================
class LogComponent  : public juce::Component,
                      private juce::Timer
{
public:
    explicit LogComponent (SPLMeterAudioProcessor&);
    ~LogComponent() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    bool isOff()        const noexcept { return selectedMetric == PsychoMetric::Off; }
    void setFftEnabled (bool e)
    {
        fftEnabled_ = e;
        processor.getUiState().setProperty ("logFftEnabled", e, nullptr);
        repaint();
    }
    void setLightMode  (bool light) noexcept;

    // Sound event markers on the timeline
    void setSoundEvents (const std::vector<SoundEvent>& events) { soundEvents_ = events; repaint(); }

    // Pause event markers on the timeline
    struct PauseEvent { juce::int64 startMs; juce::int64 durationMs; };
    void setPauseEvents (const std::vector<PauseEvent>& events) { pauseEvents_ = events; }

    // User markers on the timeline (red diamond + optional text)
    struct MarkerEvent { juce::int64 timestampMs; juce::String text; };
    void setMarkerEvents (const std::vector<MarkerEvent>& events) { markerEvents_ = events; }

private:
    void timerCallback() override;
    void computeFftBands();
    void drawFftOverlay (juce::Graphics&, const juce::Rectangle<float>& plot);

    SPLMeterAudioProcessor& processor;

    juce::Label  durationLabel;
    juce::Slider durationSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> durationAttachment;

    juce::ToggleButton splVisButton          { "dB SPL"     };
    juce::ToggleButton dbaVisButton          { "dBA SPL"    };
    juce::ToggleButton dbcVisButton          { "dBC SPL"    };
    juce::ToggleButton roughnessVisButton      { "Roughness"  };
    juce::ToggleButton fluctuationVisButton    { "Fluctuation" };
    juce::ToggleButton sharpnessVisButton      { "Sharpness"  };
    juce::ToggleButton loudnessVisButton       { "Loudness" };
    juce::ToggleButton annoyanceVisButton      { "Annoyance" };
    juce::ToggleButton impulsivenessVisButton  { "Impulsiveness" };
    juce::ToggleButton tonalityVisButton       { "Tonality" };
    juce::ToggleButton bassComfortVisButton    { "Bass LCeq-LAeq" };

    // FFT
    static constexpr int   kMaxFftOrder = 19;    // 2^19 = 524288, matches PluginProcessor::kFftCircBufSize
    static constexpr int   kMaxFftBands = 256;   // enough for 1/24-Oct (~240 bands)

    // Per-signal analysis state. Mix / single-input mode uses fftCh_[0]; Dual FFT
    // feeds fftCh_[0] / fftCh_[1] from inputs 1 / 2 with identical settings.
    // fftSource_ is the processor's FftSource value.
    struct FftChannel
    {
        std::vector<float> inputHistory;   // overlap history
        std::vector<float> buffer;
        std::vector<float> avgAccum;
        int                avgCount = 0;
        float              bands[kMaxFftBands] {};
        float              bandsSmoothed[kMaxFftBands] {};
        float              peakBands[kMaxFftBands] {};
        double             peakTimestamps[kMaxFftBands] {};
    };
    void processFftChannel (FftChannel&, int sourceChannel);
    bool fftDual() const noexcept        { return fftSource_ == SPLMeterAudioProcessor::FftSourceDual; }
    int  numFftChannels() const noexcept { return fftDual() ? 2 : 1; }
    // Processor channel feeding fftCh_[c]: -1 = mix, otherwise the input index
    int  fftSourceChannel (int c) const noexcept
    {
        if (fftDual())                                         return c;
        if (fftSource_ >= SPLMeterAudioProcessor::FftSourceInput1)
            return fftSource_ - SPLMeterAudioProcessor::FftSourceInput1;
        return -1;
    }

    static const juce::Colour colFftIn1;   // Dual FFT: input 1 (green)
    static const juce::Colour colFftIn2;   // Dual FFT: input 2 (red)

    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<float>              windowCoeffs_;      // current window function
    FftChannel                      fftCh_[2];
    int                             currentFftOrder_    = -1;
    int                             currentNumBands_    = 0;
    int                             currentBandN_       = -1;
    int                             currentWindowType_  = -1;
    int                             fftSource_  = SPLMeterAudioProcessor::FftSourceMix;
    bool                            fftEnabled_ = false;
    bool                            lightMode_  = false;

    // FFT crosshair
    bool                            fftCrosshairActive_ = false;
    juce::Point<float>              fftCrosshairPos_;

    std::vector<LogEntry>   rows;
    std::vector<SoundEvent> soundEvents_;
    std::vector<PauseEvent> pauseEvents_;
    std::vector<MarkerEvent> markerEvents_;

    // Selected psychoacoustic metric
    enum class PsychoMetric { Roughness = 0, Fluctuation, Sharpness, Loudness, Annoyance, Impulsiveness, Tonality, BassComfort, Off };
    PsychoMetric selectedMetric = PsychoMetric::Off;


    juce::TextButton yZoomButton_;
    juce::TextButton xZoomButton_;
    juce::TextButton rightZoomButton_;

    // Graph constants (absolute physical limits, used for FFT data clamping)
    static constexpr float kYMin = 20.0f;
    static constexpr float kYMax = 130.0f;

    // Colours - SPL series
    static const juce::Colour colPeakSPL;
    static const juce::Colour colRmsSPL;
    static const juce::Colour colPeakDBA;
    static const juce::Colour colRmsDBA;
    static const juce::Colour colPeakDBC;
    static const juce::Colour colRmsDBC;

    // Colours - psychoacoustic series
    static const juce::Colour colRoughness;
    static const juce::Colour colFluctuation;
    static const juce::Colour colSharpness;
    static const juce::Colour colLoudness;
    static const juce::Colour colAnnoyance;
    static const juce::Colour colImpulsiveness;
    static const juce::Colour colTonality;
    static const juce::Colour colBassComfort;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogComponent)
};
