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
    void setFftEnabled (bool e) noexcept { fftEnabled_ = e; repaint(); }
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

    // FFT
    static constexpr int   kFftOrder    = 13;    // 2^13 = 8192
    static constexpr int   kFftSize     = 1 << kFftOrder;
    static constexpr int   kMaxFftBands = 256;   // enough for 1/24-Oct (~240 bands)

    juce::dsp::FFT                  fft_             { kFftOrder };
    std::array<float, kFftSize>     windowCoeffs_    {};   // current window function
    std::array<float, kFftSize>     fftInputHistory_ {};   // overlap history
    std::array<float, kFftSize * 2> fftBuffer_       {};
    float                           fftBands_[kMaxFftBands] {};
    float                           fftBandsSmoothed_[kMaxFftBands] {};
    float                           fftPeakBands_[kMaxFftBands] {};
    double                          fftPeakTimestamps_[kMaxFftBands] {};
    std::vector<float>              fftAvgAccum_;
    int                             fftAvgCount_        = 0;
    int                             currentNumBands_    = 0;
    int                             currentBandN_       = -1;
    int                             currentWindowType_  = -1;
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
    enum class PsychoMetric { Roughness = 0, Fluctuation, Sharpness, Loudness, Annoyance, Impulsiveness, Tonality, Off };
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogComponent)
};
