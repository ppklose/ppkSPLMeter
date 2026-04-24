#pragma once
#include <JuceHeader.h>
#include "AWeightingFilter.h"
#include "CWeightingFilter.h"
#include "RoughnessEstimator.h"
#include "SharpnessEstimator.h"
#include "FluctuationStrengthEstimator.h"
#include "ImpulsivenessEstimator.h"
#include "TonalityEstimator.h"
#include "SoundDetective.h"
#include <atomic>
#include <deque>

//==============================================================================
struct LogEntry
{
    juce::int64  timestampMs;
    float peakSPL     = 0.0f;
    float peakDBASPL  = 0.0f;
    float peakDBCSPL  = 0.0f;
    float rmsSPL      = 0.0f;
    float rmsDBASPL   = 0.0f;
    float rmsDBCSPL   = 0.0f;
    // Psychoacoustic metrics
    float roughness        = 0.0f;
    float fluctuation      = 0.0f;
    float sharpness        = 0.0f;
    float loudnessSone     = 0.0f;
    float psychoAnnoyance  = 0.0f;
    float impulsiveness    = 0.0f;
    float tonality         = 0.0f;
};

//==============================================================================
class SPLMeterAudioProcessor  : public juce::AudioProcessor
{
public:
    SPLMeterAudioProcessor();
    ~SPLMeterAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        int n = layouts.getMainInputChannels();
        return n >= 1 && n <= 8;
    }

    //==========================================================================
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    float getPeakSPL()    const noexcept { return atomicPeakSPL.load(); }
    float getPeakDBASPL() const noexcept { return atomicPeakDBASPL.load(); }
    float getPeakDBCSPL() const noexcept { return atomicPeakDBCSPL.load(); }
    float getRMSSPL()     const noexcept { return atomicRMSSPL.load(); }
    float getRMSDBASPL()  const noexcept { return atomicRMSDBASPL.load(); }
    float getRMSDBCSPL()  const noexcept { return atomicRMSDBCSPL.load(); }
    float getRoughness()      const noexcept { return atomicRoughness.load(); }
    float getSharpness()      const noexcept { return atomicSharpness.load(); }
    float getFluctuation()    const noexcept { return atomicFluctuation.load(); }
    float getImpulsiveness()  const noexcept { return atomicImpulsiveness.load(); }
    float getTonality()       const noexcept { return atomicTonality.load(); }
    float getLoudnessSone() const noexcept
    {
        float phons = atomicRMSDBASPL.load();
        if (phons < 2.0f) return 0.0f;
        return (phons >= 40.0f)
               ? std::pow (2.0f, (phons - 40.0f) / 10.0f)
               : std::pow (phons / 40.0f, 2.642f);
    }

    float getPsychoAnnoyance() const noexcept
    {
        const float N = getLoudnessSone();
        if (N < 0.01f) return 0.0f;
        const float S   = atomicSharpness.load();
        const float R   = atomicRoughness.load()   / 100.0f;   // % → [0,1]
        const float F   = atomicFluctuation.load() / 100.0f;
        const float wS  = (S > 1.75f) ? (S - 1.75f) / 4.0f * N / (N + 10.0f) : 0.0f;
        const float wFR = std::pow (N, 0.4f) * (0.07f * std::sqrt (F) + 0.2f * std::sqrt (R));
        return N * (1.0f + std::sqrt (wS * wS + wFR * wFR));
    }

    std::vector<LogEntry> copyLog();

    void resetPeak() noexcept
    {
        rawPeak = aPeak = cPeak = rawPeakHeld = aPeakHeld = cPeakHeld = 0.0f;
        peakHoldCounterRaw = peakHoldCounterA = peakHoldCounterC = 0;
    }

    // Called when the FAST/SLOW mode is switched - resets meter + log
    void resetForModeSwitch() noexcept
    {
        resetPeak();
        rmsSmoothedRaw = rmsSmoothedA = rmsSmoothedC = 0.0;
        atomicPeakSPL.store (-999.0f);
        atomicPeakDBASPL.store (-999.0f);
        atomicPeakDBCSPL.store (-999.0f);
        atomicRMSSPL.store (-999.0f);
        atomicRMSDBASPL.store (-999.0f);
        atomicRMSDBCSPL.store (-999.0f);
        clearLog();
    }

    void clearLog()
    {
        juce::SpinLock::ScopedLockType lock (logLock);
        logEntries.clear();
    }

    // FFT circular buffer (audio thread → GUI, lock-free)
    static constexpr int kFftCircBufSize = 8192;
    void copyFftWindow (float* dest, int size) const noexcept;
    int  pullSpectroSamples (float* dest, int maxSamples) noexcept;
    double getSampleRate() const noexcept { return currentSampleRate; }

    //==========================================================================
    // Pause / DAW sync
    void setPaused (bool p) noexcept
    {
        if (p && !paused_.load())
            pauseStartMs_ = juce::Time::currentTimeMillis();
        else if (!p && paused_.load())
            pauseOffsetMs_.fetch_add (juce::Time::currentTimeMillis() - pauseStartMs_);
        paused_.store (p);
    }
    bool isPaused      ()       const noexcept { return paused_.load(); }
    void setDawSync    (bool d) noexcept { dawSync_.store (d); }
    bool isDawSync     ()       const noexcept { return dawSync_.load(); }
    bool isDawPlaying  ()       const noexcept { return dawIsPlaying_.load(); }
    juce::int64 getPauseOffsetMs    () const noexcept { return pauseOffsetMs_.load(); }
    juce::int64 getLastEntryWallMs  () const noexcept { return lastEntryWallMs_.load(); }

    //==========================================================================
    // Monitor (pass-through)
    void setMonitorEnabled (bool e) noexcept { monitorEnabled.store (e); }
    bool isMonitorEnabled  ()       noexcept { return monitorEnabled.load(); }

    //==========================================================================
    // File mode
    void loadFile   (const juce::File& file);
    void setFileMode (bool active);
    bool isFileModeActive() const noexcept { return fileModeActive.load(); }
    double getFilePosition() const { return transportSource.getCurrentPosition(); }
    double getFileLength()   const { return transportSource.getLengthInSeconds(); }
    bool isFilePlaying()     const { return transportSource.isPlaying(); }
    void seekTo (double positionSeconds)    { transportSource.setPosition (positionSeconds); }
    void startPlayback()                    { transportSource.start(); fileModeActive.store (true); }
    void stopPlayback()                     { transportSource.stop(); }

    //==========================================================================
    // Correction filter
    void loadCorrectionFilter (const juce::File& file);
    void clearCorrectionFilter();
    bool isCorrectionLoaded()       const noexcept { return correctionLoaded_.load(); }
    juce::String getCorrectionFileName() const     { return correctionFileName_; }

    // Metadata parsed from correction file header (Sens Factor / SERNO)
    std::atomic<bool>  correctionMetaReady_ { false };
    juce::String       correctionSerno_;
    float              correctionCalibration_ { 0.0f };

    //==========================================================================
    // Graph overlay 1 (visual only - no audio processing)
    void loadGraphOverlay  (const juce::File& file);
    void clearGraphOverlay ();
    bool isGraphOverlayLoaded()         const noexcept { return graphOverlayLoaded_.load(); }
    juce::String getGraphOverlayFileName() const       { return graphOverlayFileName_; }
    const std::vector<std::pair<float,float>>& getGraphOverlayPoints() const { return graphOverlayPoints_; }

    // Graph overlay 2 (visual only - no audio processing)
    void loadGraphOverlay2  (const juce::File& file);
    void clearGraphOverlay2 ();
    bool isGraphOverlay2Loaded()          const noexcept { return graphOverlay2Loaded_.load(); }
    juce::String getGraphOverlay2FileName() const        { return graphOverlay2FileName_; }
    const std::vector<std::pair<float,float>>& getGraphOverlay2Points() const { return graphOverlay2Points_; }

    //==========================================================================
    // WAV recorder
    bool saveWavToFile (const juce::File& destFile);

    //==========================================================================
    // Impulse Fidelity
    void startImpulseFidelityTest (const juce::AudioBuffer<float>& signal);
    void stopImpulseFidelityTest();
    bool isImpulseFidelityActive()     const noexcept { return impFidActive_.load(); }
    int  getImpulseFidelityPosition()  const noexcept { return impFidPos_.load(); }
    int  getImpulseFidelitySignalLength() const noexcept { return impFidSignalLen_; }
    std::vector<float> getImpulseFidelityCapture();

    //==========================================================================
    // SoundDetective
    SoundDetective& getSoundDetective() noexcept { return soundDetective_; }
    void setSoundDetectiveEnabled (bool e) noexcept { soundDetective_.setEnabled (e); }
    std::vector<SoundEvent> popSoundEvents() { return soundDetective_.popNewEvents(); }

    //==========================================================================
    // MIDI Learn - param indices: 0=calOffset, 1=peakHoldTime, 2=fftGain
    static constexpr int kNumMidiParams = 3;
    static const char* const kMidiParamIds[kNumMidiParams];

    void startMidiLearn  (int paramIndex) noexcept { midiLearnParamIndex.store (paramIndex); }
    void cancelMidiLearn ()               noexcept { midiLearnParamIndex.store (-1); }
    void clearMidiCC     (int paramIndex) noexcept { midiCC[paramIndex].store (-1); }
    void setMidiCC       (int paramIndex, int cc) noexcept { midiCC[paramIndex].store (cc); }
    int  getMidiCC       (int paramIndex) const noexcept { return midiCC[paramIndex].load(); }
    bool isMidiLearning  (int paramIndex) const noexcept { return midiLearnParamIndex.load() == paramIndex; }

    //==========================================================================
    // Channel names (UI only, not used in audio thread)
    juce::String getChannelName (int ch) const
    {
        jassert (ch >= 0 && ch < 32);
        if (channelNames_[ch].isNotEmpty())
            return channelNames_[ch];
        return "IN" + juce::String (ch + 1).paddedLeft ('0', 2);
    }
    void setChannelName (int ch, const juce::String& name)
    {
        jassert (ch >= 0 && ch < 32);
        channelNames_[ch] = name;
    }

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    AWeightingFilter  aWeightL;
    CWeightingFilter  cWeightL;

    // 20Hz-20kHz bandpass: 8th-order Butterworth = 4 cascaded biquad stages
    static constexpr int kBpStages = 4;
    juce::IIRFilter bpHP[32][kBpStages];
    juce::IIRFilter bpLP[32][kBpStages];
    RoughnessEstimator           roughnessEst;
    SharpnessEstimator           sharpnessEst;
    FluctuationStrengthEstimator fluctuationEst;
    ImpulsivenessEstimator       impulsivenessEst;
    TonalityEstimator            tonalityEst;
    SoundDetective               soundDetective_;
    double currentSampleRate = 44100.0;

    // Peak tracking
    float rawPeak = 0.0f,  aPeak = 0.0f,  cPeak = 0.0f;
    float rawPeakHeld = 0.0f, aPeakHeld = 0.0f, cPeakHeld = 0.0f;
    int   peakHoldCounterRaw = 0, peakHoldCounterA = 0, peakHoldCounterC = 0;

    // Exponential RMS smoothing (IEC 61672 time weighting)
    double rmsSmoothedRaw = 0.0, rmsSmoothedA = 0.0, rmsSmoothedC = 0.0;
    double rmsAlpha       = 0.0;
    int    prevTimeWeightIndex = -1;

    // Log interval (125 ms)
    int  logIntervalSamples = 0;
    int  logSampleCounter   = 0;

    // Atomic meter readouts
    std::atomic<float> atomicPeakSPL    { -999.0f };
    std::atomic<float> atomicPeakDBASPL { -999.0f };
    std::atomic<float> atomicPeakDBCSPL { -999.0f };
    std::atomic<float> atomicRMSSPL     { -999.0f };
    std::atomic<float> atomicRMSDBASPL  { -999.0f };
    std::atomic<float> atomicRMSDBCSPL  { -999.0f };
    std::atomic<float> atomicRoughness      { 0.0f };
    std::atomic<float> atomicSharpness      { 0.0f };
    std::atomic<float> atomicFluctuation    { 0.0f };
    std::atomic<float> atomicImpulsiveness  { 0.0f };
    std::atomic<float> atomicTonality       { 0.0f };

    juce::SpinLock       logLock;
    std::deque<LogEntry> logEntries;

    std::atomic<int> midiCC[kNumMidiParams]  { {-1}, {-1}, {-1} };
    std::atomic<int> midiLearnParamIndex     { -1 };

    std::array<float, kFftCircBufSize> fftCircBuf_ {};
    std::atomic<int>                   fftWritePos_ { 0 };
    std::atomic<int>                   spectroReadPos_ { 0 };


    std::atomic<juce::int64>  lastEntryWallMs_ { 0 };
    std::atomic<bool>         paused_         { false };
    std::atomic<bool>         dawSync_        { false };
    std::atomic<bool>         dawIsPlaying_   { true };
    juce::int64               pauseStartMs_   { 0 };      // set on GUI thread only
    std::atomic<juce::int64>  pauseOffsetMs_  { 0 };      // cumulative pause duration

    // File playback
    juce::AudioFormatManager                         formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource>   readerSource;
    juce::AudioTransportSource                       transportSource;
    std::atomic<bool>                                fileModeActive  { false };
    std::atomic<bool>                                monitorEnabled  { false };
    juce::AudioBuffer<float>                         fileReadBuffer;

    void pushLogEntry (float rawPeak, float aPeak, float cPeak,
                       float rawRms,  float aRms,  float cRms,
                       float calOffset,
                       float roughness, float fluctuation,
                       float sharpness, float loudnessSone,
                       float impulsiveness, float tonality);
    void pruneLog (float logDurationSeconds);

    // Correction filter
    juce::dsp::Convolution                  correctionConv_;
    std::atomic<bool>                       correctionLoaded_ { false };
    std::atomic<bool>                       correctionPending_ { false };
    int                                     correctionWarmupLeft_ { 0 };
    int                                     correctionPreparedCh_ { 0 };
    juce::String                            correctionFileName_;
    std::vector<std::pair<float,float>>     correctionPoints_; // (freq, spl)
    void rebuildCorrectionFIR();


    // Graph overlay 1
    std::atomic<bool>                       graphOverlayLoaded_ { false };
    juce::String                            graphOverlayFileName_;
    std::vector<std::pair<float,float>>     graphOverlayPoints_; // (freq, spl)

    // Graph overlay 2
    std::atomic<bool>                       graphOverlay2Loaded_ { false };
    juce::String                            graphOverlay2FileName_;
    std::vector<std::pair<float,float>>     graphOverlay2Points_; // (freq, spl)

    juce::String channelNames_[32];

    // WAV circular buffer: stereo (Ch0 + Ch1), sized to logDuration at prepareToPlay
    std::vector<float>  wavCircBuf_[2];
    std::atomic<int>    wavWritePos_ { 0 };
    int                 wavBufSize_  { 0 };

    // Impulse Fidelity test signal generator + capture
    juce::AudioBuffer<float>  impFidTestSignal_;
    std::vector<float>        impFidCapture_;
    std::atomic<bool>         impFidActive_    { false };
    std::atomic<int>          impFidPos_       { 0 };
    int                       impFidSignalLen_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPLMeterAudioProcessor)
};
