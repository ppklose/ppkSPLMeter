#pragma once
#include <JuceHeader.h>
#include "AWeightingFilter.h"
#include "CWeightingFilter.h"
#include "RoughnessEstimator.h"
#include "SharpnessEstimator.h"
#include "FluctuationStrengthEstimator.h"
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
    float roughness   = 0.0f;
    float fluctuation = 0.0f;
    float sharpness   = 0.0f;
    float loudnessSone = 0.0f;
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
    float getRoughness()  const noexcept { return atomicRoughness.load(); }
    float getSharpness()    const noexcept { return atomicSharpness.load(); }
    float getFluctuation()  const noexcept { return atomicFluctuation.load(); }
    float getLoudnessSone() const noexcept
    {
        float phons = atomicRMSDBASPL.load();
        if (phons < 2.0f) return 0.0f;
        return (phons >= 40.0f)
               ? std::pow (2.0f, (phons - 40.0f) / 10.0f)
               : std::pow (phons / 40.0f, 2.642f);
    }

    std::vector<LogEntry> copyLog();

    // Spectrogram audio feed (audio thread → GUI thread, lock-free)
    static constexpr int kSpectroFifoSize = 1 << 16;   // 65536 samples
    int pullSpectroSamples (float* dest, int maxCount) noexcept
    {
        int start1, size1, start2, size2;
        int toRead = std::min (maxCount, spectroFifo.getNumReady());
        spectroFifo.prepareToRead (toRead, start1, size1, start2, size2);
        if (size1 > 0) std::memcpy (dest,         spectroBuffer.data() + start1, (size_t)size1 * sizeof(float));
        if (size2 > 0) std::memcpy (dest + size1,  spectroBuffer.data() + start2, (size_t)size2 * sizeof(float));
        spectroFifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    void resetPeak() noexcept
    {
        rawPeak = aPeak = cPeak = rawPeakHeld = aPeakHeld = cPeakHeld = 0.0f;
        peakHoldCounterRaw = peakHoldCounterA = peakHoldCounterC = 0;
    }

    // Called when the FAST/SLOW mode is switched — resets meter + log
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

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    AWeightingFilter  aWeightL;
    CWeightingFilter  cWeightL;
    RoughnessEstimator roughnessEst;
    SharpnessEstimator          sharpnessEst;
    FluctuationStrengthEstimator fluctuationEst;
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
    std::atomic<float> atomicRoughness  { 0.0f };
    std::atomic<float> atomicSharpness    { 0.0f };
    std::atomic<float> atomicFluctuation  { 0.0f };

    juce::SpinLock       logLock;
    std::deque<LogEntry> logEntries;

    juce::AbstractFifo                          spectroFifo   { kSpectroFifoSize };
    std::array<float, kSpectroFifoSize>         spectroBuffer {};

    void pushLogEntry (float rawPeak, float aPeak, float cPeak,
                       float rawRms,  float aRms,  float cRms,
                       float calOffset,
                       float roughness, float fluctuation,
                       float sharpness, float loudnessSone);
    void pruneLog (float logDurationSeconds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPLMeterAudioProcessor)
};
