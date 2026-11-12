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
#include "SensorConfig.h"
#include "Vibration/VibrationEngine.h"
#include "Vibration/CalibratorMeasurement.h"
#include "Vibration/Units.h"
#include "Vibration/Profiles.h"
#include "RtHandover.h"
#include "WavRecording.h"
#include <array>
#include <atomic>
#include <deque>

//==============================================================================
// Accelerometer values of one log entry (vibration spec §7.3), SI units of the
// quantity (m/s², m/s, m). Slots X, Y, Z, Σ as vib::Axis.
struct VibrationLog
{
    bool  valid    = false;   // live accelerometer readings in this entry
    int   quantity = 0;       // vib::Quantity
    int   axisMask = 0;       // present X/Y/Z
    bool  uniaxial = false;
    float rms[4]  {};
    float peak[4] {};         // max |x| in the last snapshot interval
    int   mainKind = 0;       // vib::MainValue of the profile
    float main     = 0.0f;    // the profile's main value
};

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
    VibrationLog vib;
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
        // The standalone app takes every input the device opens (up to the 32
        // mute/role slots); plugins keep 1–8 (vibration spec §10). Rejecting a
        // device with more inputs would make JUCE fall back to a single input.
        const int maxIns = wrapperType == wrapperType_Standalone ? SensorConfig::kMaxInputs : 8;
        int n = layouts.getMainInputChannels();
        return n >= 1 && n <= maxIns;
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

    // Full persistent state: APVTS parameters + UIState + MIDI CC + channel names.
    std::unique_ptr<juce::XmlElement> createStateXml();
    void restoreStateXml (const juce::XmlElement& xml);

    // Non-parameter user settings (view options, loaded file paths). Kept as a
    // child of apvts.state so it is saved and restored together with the parameters.
    juce::ValueTree getUiState() { return apvts.state.getOrCreateChildWithName ("UIState", nullptr); }

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
        sessionPeakDBCSPL_.store (-999.0f);
        noiseDosePct_.store (0.0f);
        clearLog();
    }

    void clearLog()
    {
        juce::SpinLock::ScopedLockType lock (logLock);
        logEntries.clear();
    }

    // FFT circular buffer (audio thread → GUI, lock-free)
    // Sized to the largest selectable L_FFT window (524288 = 512k samples).
    static constexpr int kFftCircBufSize = 524288;
    // channel -1 = mono mix of all inputs; 0 … kMaxFftChannels-1 = that input alone
    // (post mute / bandpass / correction). Inputs without a buffer read as silence.
    static constexpr int kMaxFftChannels = 8;
    void copyFftWindow (float* dest, int size, int channel = -1) const noexcept;

    // FFT source (parameter "fftSource"): what the timeline FFT analyses.
    // Values are persisted — only append new entries.
    enum FftSource { FftSourceMix = 0, FftSourceDual = 1, FftSourceInput1 = 2 };
    static constexpr int kNumFftSources = FftSourceInput1 + kMaxFftChannels;
    int  getFftSource() const noexcept;
    // Sets "fftSource" and keeps the legacy "fftDual" flag in step, so sessions
    // stay readable by v3.5.2 (message thread).
    void setFftSource (int source);
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
    void setDawSync    (bool d) { dawSync_.store (d); getUiState().setProperty ("dawSync", d, nullptr); }
    bool isDawSync     ()       const noexcept { return dawSync_.load(); }
    bool isDawPlaying  ()       const noexcept { return dawIsPlaying_.load(); }
    juce::int64 getPauseOffsetMs    () const noexcept { return pauseOffsetMs_.load(); }
    juce::int64 getLastEntryWallMs  () const noexcept { return lastEntryWallMs_.load(); }

    //==========================================================================
    // Monitor (pass-through)
    void setMonitorEnabled (bool e) noexcept { monitorEnabled.store (e); }
    bool isMonitorEnabled  ()       noexcept { return monitorEnabled.load(); }

    // Output VU level (linear peak per channel, post monitor gain & impulse-fidelity inject)
    float getOutputPeakL () const noexcept { return outputPeakL_.load (std::memory_order_relaxed); }
    float getOutputPeakR () const noexcept { return outputPeakR_.load (std::memory_order_relaxed); }

    // DIN 15905-5: session-long C-weighted peak (does not decay with peakHoldTime)
    float getSessionPeakDBCSPL () const noexcept { return sessionPeakDBCSPL_.load (std::memory_order_relaxed); }
    void  resetSessionPeak     ()       noexcept
    {
        sessionPeakDBCSPL_.store (-999.0f, std::memory_order_relaxed);
        resetVibrationStats();   // session statistics reset together (spec §5.3)
    }

    // NIOSH REL: cumulative 8-hour noise-dose percentage (100% = 85 dB(A) for 8 h,
    // 3 dB exchange rate, 80 dB(A) threshold).
    float getNoiseDosePct () const noexcept { return noiseDosePct_.load (std::memory_order_relaxed); }
    void  resetNoiseDose  ()       noexcept { noiseDosePct_.store (0.0f, std::memory_order_relaxed); }

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
    // WAV recorder: inputs 1 + 2, or every non-Off input (≤ 8) with an
    // accelerometer, with track roles + calibration in the iXML chunk (spec §7.5)
    bool saveWavToFile (const juce::File& destFile);
    std::vector<int> getRecordedInputs() const;

    // File playback of an SPLMeter recording: the recorded track roles and
    // calibration drive the mix and the vibration analysis
    std::vector<WavRecording::Track> getFileTracks() const { return fileTracks_; }
    bool fileHasAccelerometer() const noexcept { return fileHasAccel_.load(); }
    // Vibration readings exist: live accelerometer, or playback of a recording with one
    bool isVibrationAvailable() const;

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
    // Sensor roles per input (vibration spec §3.1). Default: all Mic.
    // Persisted in UIState/Inputs; mirrored into atomics for the audio thread.
    SensorRole getChannelRole (int ch) const noexcept
    {
        jassert (ch >= 0 && ch < SensorConfig::kMaxInputs);
        return SensorConfig::fromInt (channelRoles_[(size_t) ch].load (std::memory_order_relaxed));
    }
    void setChannelRole (int ch, SensorRole role);
    SensorConfig::Roles getChannelRoles() const noexcept;
    SensorConfig::Validity getSensorValidity() const noexcept
    {
        return SensorConfig::validate (getChannelRoles(), getTotalNumInputChannels());
    }

    //==========================================================================
    // Accelerometer (vibration spec §3–§5). Settings persist in UIState/Accel,
    // per-input calibration in UIState/Inputs ("cal<ch>"). Message thread only.
    vib::VibrationSettings getVibrationSettings() const;
    void   setVibrationSettings (const vib::VibrationSettings& settings);
    double getAccelCalDb (int ch) const;
    void   setAccelCalDb (int ch, double calDb);

    // Latest readout (published every 125 ms); valid == false without accelerometer
    vib::VibrationSnapshot getVibrationSnapshot() const noexcept
    {
        vib::VibrationSnapshot s;
        if (! vibSnapshot_.read (s)) s.valid = false;
        return s;
    }
    void resetVibrationStats() noexcept { vibResetRequest_.store (true, std::memory_order_release); }

    // Sensor information for the report (UIState/Accel; no engine rebuild)
    struct AccelSensorInfo
    {
        juce::String serial;
        double lowerLimitHz = 0.5;      // usable range from the datasheet (spec §3.2)
        double upperLimitHz = 10000.0;
        double exposureHours = 8.0;     // daily exposure time T for A(8)
    };
    AccelSensorInfo getAccelSensorInfo() const;
    void setAccelSensorInfo (const AccelSensorInfo& info);

    // Measurement profile (spec §6, UIState/Accel "profile"): applying one sets the
    // accelerometer settings and display unit; later edits make it "Custom (based on …)"
    vib::Profile getVibrationProfile() const;
    void applyVibrationProfile (vib::Profile profile);
    juce::String getVibrationProfileLabel() const;    // e.g. "Custom (based on Hand-arm (ISO 5349-1))"
    juce::String getVibrationProfileKey() const;      // CSV/JSON: "hand-arm" or "custom(hand-arm)"
    juce::String getVibrationMainLabel() const;       // e.g. "a_w"

    // Display unit of the vibration readout (UIState/Accel "displayUnit")
    vib::DisplayUnit getVibrationDisplayUnit() const;
    void setVibrationDisplayUnit (vib::DisplayUnit unit);

    // JSON settings export/import (spec §8): "inputs" (role + calibration per input)
    // and "accel" (sensor settings). Import ignores absent blocks (older files).
    juce::var getInputsJson() const;
    juce::var getAccelJson() const;
    void setInputsAndAccelFromJson (const juce::var& inputs, const juce::var& accel);

    // Calibration date/method per input (UIState/Inputs "calDate<ch>", "calMethod<ch>")
    juce::String getAccelCalInfo (int ch) const;     // e.g. "2026-11-12 · calibrator", empty if never
    void setAccelCalInfo (int ch, const juce::String& method);   // stamps today's date

    // Live input level per channel (raw, after mute), for the Inputs table.
    // RMS is smoothed with τ = 125 ms; the peak is the maximum since the last read.
    float getInputRms (int ch) const noexcept
    {
        return ch >= 0 && ch < SensorConfig::kMaxInputs ? inputRms_[(size_t) ch].load (std::memory_order_relaxed) : 0.0f;
    }
    float takeInputPeak (int ch) noexcept
    {
        return ch >= 0 && ch < SensorConfig::kMaxInputs ? inputPeak_[(size_t) ch].exchange (0.0f, std::memory_order_relaxed) : 0.0f;
    }

    // Handheld-calibrator measurement on one input (spec §4). Message thread:
    // start → poll getAccelCalibrationProgress() → takeAccelCalibrationResult().
    bool   startAccelCalibration (int ch, double referenceHz);
    void   cancelAccelCalibration() noexcept;
    double getAccelCalibrationProgress() const noexcept { return calProgress_.load (std::memory_order_relaxed); }
    // Returns true (and fills result) once the measurement has finished
    bool   takeAccelCalibrationResult (vib::CalibratorMeasurement::Result& result) noexcept;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    AWeightingFilter  aWeightL;
    CWeightingFilter  cWeightL;

    // Sensor roles (audio-thread mirror of UIState/Inputs)
    std::array<std::atomic<int>, SensorConfig::kMaxInputs> channelRoles_ {};
    void loadChannelRolesFromState();

    // Accelerometer engine hand-over (real-time safe): the message thread builds a
    // complete engine and puts it in vibPending_; the audio thread swaps it in at the
    // start of a block and parks the previous one in vibRetired_, which the message
    // thread deletes on its next rebuild (or in the destructor). The audio thread only
    // swaps when vibRetired_ is empty, so it never allocates or frees.
    std::unique_ptr<vib::VibrationEngine> buildVibrationEngine() const;
    void rebuildVibrationEngine();
    std::atomic<vib::VibrationEngine*>    vibPending_ { nullptr };
    std::atomic<vib::VibrationEngine*>    vibRetired_ { nullptr };
    vib::VibrationEngine*                 vibActive_  = nullptr;   // audio thread
    std::atomic<bool>                     vibResetRequest_ { false };
    vib::SeqLock<vib::VibrationSnapshot>  vibSnapshot_;
    vib::VibrationSnapshot                vibWork_;                 // audio thread
    bool                                  vibLogLive_ = false;      // audio thread: vibWork_ is current

    // Input level meters (audio thread writes)
    std::array<std::atomic<float>, SensorConfig::kMaxInputs> inputRms_ {}, inputPeak_ {};
    std::array<double, SensorConfig::kMaxInputs>             inputMs_ {};   // audio thread

    // Calibrator measurement hand-over: the message thread configures calMeas_
    // only while calState_ is Idle, the audio thread uses it only while Running,
    // and the message thread reads the result once it is Done.
    enum CalState : int { CalIdle = 0, CalRunning, CalCancel, CalDone };
    std::atomic<int>            calState_ { CalIdle };
    std::atomic<double>         calProgress_ { 0.0 };
    int                         calChannel_ = -1;
    vib::CalibratorMeasurement  calMeas_;

    // Accelerometer channels are restored from here after the correction
    // convolution, which processes all channels at once (allocated in prepareToPlay)
    juce::AudioBuffer<float> nonMicScratch_;

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
    // Per-input FFT ring buffers (same write position as fftCircBuf_). Allocated in
    // prepareToPlay for the inputs present and never freed or moved afterwards, so
    // the GUI can read through the published pointer at any time.
    std::array<std::unique_ptr<float[]>, kMaxFftChannels> fftChStorage_;
    std::array<std::atomic<float*>, kMaxFftChannels>     fftChBuf_ {};
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

    // Output VU peak values (linear |x|, sampled per processBlock at the output stage)
    std::atomic<float> outputPeakL_ { 0.0f };
    std::atomic<float> outputPeakR_ { 0.0f };

    // DIN 15905-5: session-long max C-weighted peak (in dB SPL, with calOffset applied)
    std::atomic<float> sessionPeakDBCSPL_ { -999.0f };

    // NIOSH REL cumulative noise-dose (% of allowable 8-h exposure)
    std::atomic<float> noiseDosePct_ { 0.0f };

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
    RtHandover<WavRecording::Recorder> wavRec_;
    std::vector<int>                   wavLayout_;   // message thread: inputs of the published recorder
    void rebuildWavRecorder (bool audioStopped);

    // File playback (roles per file channel; Mic for files without SPLMeter metadata)
    std::array<std::atomic<int>, WavRecording::kMaxTracks> fileRoles_ {};
    std::atomic<int>                   fileNumChannels_ { 2 };
    std::atomic<bool>                  fileHasAccel_ { false };
    std::vector<WavRecording::Track>   fileTracks_;   // message thread
    RtHandover<vib::VibrationEngine>   vibFile_;      // engine for file playback
    std::unique_ptr<vib::VibrationEngine> buildFileVibrationEngine() const;
    int                                vibSource_ = 0;   // audio thread: 0 live, 1 file

    // Impulse Fidelity test signal generator + capture
    juce::AudioBuffer<float>  impFidTestSignal_;
    std::vector<float>        impFidCapture_;
    std::atomic<bool>         impFidActive_    { false };
    std::atomic<int>          impFidPos_       { 0 };
    int                       impFidSignalLen_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPLMeterAudioProcessor)
};
