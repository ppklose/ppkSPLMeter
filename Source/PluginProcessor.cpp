#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

const char* const SPLMeterAudioProcessor::kMidiParamIds[SPLMeterAudioProcessor::kNumMidiParams]
    = { "calOffset", "peakHoldTime", "fftGain" };

static float linearToDBFS (float linear)
{
    return 20.0f * std::log10 (std::max (linear, 1e-10f));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SPLMeterAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "logDuration", "Log Duration (s)",
        juce::NormalisableRange<float> (1.0f, 600.0f, 1.0f), 60.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "peakHoldTime", "Peak Hold Time (s)",
        juce::NormalisableRange<float> (0.1f, 5.0f, 0.1f), 2.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "splTimeWeight", "SPL Time Weight",
        juce::StringArray { "FAST", "SLOW" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "calOffset", "Calibration Offset (dB)",
        juce::NormalisableRange<float> (80.0f, 180.0f, 0.1f), 127.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "fftGain", "FFT Gain (dB)",
        juce::NormalisableRange<float> (-40.0f, 40.0f, 0.5f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "fftSmoothing", "FFT Smoothing",
        juce::NormalisableRange<float> (0.0f, 0.95f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "monitorGain", "Monitor Gain (dB)",
        juce::NormalisableRange<float> (-60.0f, 32.0f, 0.5f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "spectroGain", "Spectrogram Gain (dB)",
        juce::NormalisableRange<float> (-40.0f, 40.0f, 0.5f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "spectroFftSize", "Spectrogram FFT Size",
        juce::StringArray { "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192",
                            "32768", "65536", "131072", "262144", "524288" }, 6));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "fftPeakHold", "FFT Peak Hold", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "fftDisplayMode", "FFT Display Mode",
        juce::StringArray { "Bars", "Area", "Bars+Peak" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "fftBandRes", "FFT Band Resolution",
        juce::StringArray { "1/1 Oct", "1/3 Oct", "1/6 Oct", "1/12 Oct", "1/24 Oct" }, 1));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "fftWindowType", "FFT Window",
        juce::StringArray { "Hann", "Hamming", "Blackman", "Flat-top", "Rect" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "fftOverlap", "FFT Overlap",
        juce::StringArray { "0%", "25%", "50%", "75%" }, 2));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "fftAvgCycles", "FFT Avg Cycles",
        1, 999, 1));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "lfftSize", "L_FFT Size",
        juce::StringArray { "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192",
                            "32768", "65536", "131072", "262144", "524288" }, 8));

    // TA Lärm land-use category (default: Allgemeines Wohngebiet)
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "taLaermCategory", "TA Lärm Category",
        juce::StringArray { "Industriegebiet",
                            "Gewerbegebiet",
                            "Misch-/Dorf-/Kerngebiet",
                            "Allg. Wohngebiet (WA)",
                            "Reines Wohngebiet (WR)",
                            "Kurgebiet / Krankenhaus" }, 3));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "fftRTAMode", "FFT RTA +3dB/oct", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "fftDual", "Dual FFT (Input 1 + 2)", false));   // legacy (≤ 3.5.2), mirrors fftSource == Dual

    {
        juce::StringArray sources { "Mix", "Dual (1+2)" };
        for (int ch = 1; ch <= SPLMeterAudioProcessor::kMaxFftChannels; ++ch)
            sources.add ("Input " + juce::String (ch));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "fftSource", "FFT Source", sources, 0));
    }

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "bandpassEnabled", "20-20k Bandpass", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "line94Enabled", "94 dB Reference Line", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "correctionEnabled", "Correction Filter", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "graphOverlayEnabled", "Graph Overlay 1", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "graphOverlay2Enabled", "Graph Overlay 2", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "fftLowerFreq", "FFT Lower Freq (Hz)",
        juce::NormalisableRange<float> (10.0f, 20000.0f, 1.0f, 0.4f), 20.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "fftUpperFreq", "FFT Upper Freq (Hz)",
        juce::NormalisableRange<float> (100.0f, 100000.0f, 1.0f, 0.3f), 20000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lfftYMin", "L_FFT Y-axis Min (dB)",
        juce::NormalisableRange<float> (0.0f, 120.0f, 1.0f), 20.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lfftYMax", "L_FFT Y-axis Max (dB)",
        juce::NormalisableRange<float> (30.0f, 150.0f, 1.0f), 130.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "splYMin", "SPL Y-axis Min (dB)",
        juce::NormalisableRange<float> (20.0f, 120.0f, 1.0f), 20.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "splYMax", "SPL Y-axis Max (dB)",
        juce::NormalisableRange<float> (30.0f, 130.0f, 1.0f), 130.0f));

    // Per-metric right-axis zoom ranges
    // { paramPrefix, label, absMin, absMax, defaultMin, defaultMax, step }
    struct MetricRange { const char* id; const char* name; float lo; float hi; float defMin; float defMax; float step; };
    const MetricRange metricRanges[] = {
        { "roughness",      "Roughness",      0, 100, 0, 100, 1  },
        { "fluctuation",    "Fluctuation",    0, 100, 0, 100, 1  },
        { "sharpness",      "Sharpness",      0,  10, 0,   5, 0.1f },
        { "loudness",       "Loudness",       0, 200, 0, 100, 1  },
        { "annoyance",      "Annoyance",      0, 200, 0, 100, 1  },
        { "impulsiveness",  "Impulsiveness",  0,  60, 0,  40, 1  },
        { "tonality",       "Tonality",       0, 100, 0, 100, 1  },
        { "bassComfort",    "Bass LCeq-LAeq", 0,  40, 0,  30, 1  },
    };
    for (const auto& m : metricRanges)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::String (m.id) + "YMin", juce::String (m.name) + " Y Min",
            juce::NormalisableRange<float> (m.lo, m.hi, m.step), m.defMin));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::String (m.id) + "YMax", juce::String (m.name) + " Y Max",
            juce::NormalisableRange<float> (m.lo, m.hi, m.step), m.defMax));
    }

    for (int i = 0; i < 32; ++i)
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            "channelMute" + juce::String (i),
            "Mute IN" + juce::String (i + 1).paddedLeft ('0', 2), false));

    return { params.begin(), params.end() };
}

//==============================================================================
SPLMeterAudioProcessor::SPLMeterAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::discreteChannels (32), true)
                      .withOutput ("Output", juce::AudioChannelSet::discreteChannels (32), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    formatManager.registerBasicFormats();
}

SPLMeterAudioProcessor::~SPLMeterAudioProcessor()
{
    delete vibPending_.exchange (nullptr);
    delete vibRetired_.exchange (nullptr);
    delete vibActive_;
}

//==============================================================================
void SPLMeterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    aWeightL.prepare (sampleRate);
    cWeightL.prepare (sampleRate);
    roughnessEst.prepare (sampleRate);
    sharpnessEst.prepare (sampleRate);
    fluctuationEst.prepare (sampleRate);
    impulsivenessEst.prepare (sampleRate);
    tonalityEst.prepare (sampleRate);
    soundDetective_.prepare (sampleRate);

    rawPeak = aPeak = cPeak = rawPeakHeld = aPeakHeld = cPeakHeld = 0.0f;
    peakHoldCounterRaw = peakHoldCounterA = peakHoldCounterC = 0;
    rmsSmoothedRaw = rmsSmoothedA = rmsSmoothedC = 0.0;

    const int timeWeightIndex = static_cast<int> (apvts.getRawParameterValue ("splTimeWeight")->load());
    const double tau = (timeWeightIndex == 0) ? 0.125 : 1.0;
    rmsAlpha = 1.0 - std::exp (-1.0 / (tau * sampleRate));
    prevTimeWeightIndex = timeWeightIndex;

    logIntervalSamples = static_cast<int> (0.125 * sampleRate);
    logSampleCounter = 0;

    // WAV circular buffer: sized to current logDuration
    {
        const float logDurS = apvts.getRawParameterValue ("logDuration")->load();
        wavBufSize_ = static_cast<int> (logDurS * sampleRate);
        wavCircBuf_[0].assign (wavBufSize_, 0.0f);
        wavCircBuf_[1].assign (wavBufSize_, 0.0f);
        wavWritePos_.store (0, std::memory_order_relaxed);
    }

    // 20-20k bandpass: 8th-order Butterworth Q values for each biquad stage
    static const double bpQ[kBpStages] = { 0.5098, 0.6013, 0.8999, 2.5629 };
    for (int st = 0; st < kBpStages; ++st)
    {
        auto hpCoeff = juce::IIRCoefficients::makeHighPass (sampleRate, 20.0,    bpQ[st]);
        auto lpCoeff = juce::IIRCoefficients::makeLowPass  (sampleRate, 20000.0, bpQ[st]);
        for (int ch = 0; ch < 32; ++ch)
        {
            bpHP[ch][st].setCoefficients (hpCoeff);
            bpHP[ch][st].reset();
            bpLP[ch][st].setCoefficients (lpCoeff);
            bpLP[ch][st].reset();
        }
    }

    // Correction filter convolution engine
    correctionPreparedCh_ = juce::jmax (getTotalNumInputChannels(), 2);
    correctionConv_.prepare ({ sampleRate, (juce::uint32) samplesPerBlock,
                               (juce::uint32) correctionPreparedCh_ });
    correctionConv_.reset();
    if (!correctionPoints_.empty())
        rebuildCorrectionFIR();

    transportSource.prepareToPlay (samplesPerBlock, sampleRate);
    fileReadBuffer.setSize (2, samplesPerBlock, false, true, false);

    // Per-input FFT buffers: grow only (never freed while the GUI may read them).
    // Inputs 1 + 2 always exist so Dual FFT works on mono devices too.
    for (int ch = 0; ch < juce::jlimit (2, kMaxFftChannels, getTotalNumInputChannels()); ++ch)
    {
        if (fftChStorage_[(size_t) ch] == nullptr)
        {
            fftChStorage_[(size_t) ch] = std::make_unique<float[]> ((size_t) kFftCircBufSize);   // zeroed
            fftChBuf_[(size_t) ch].store (fftChStorage_[(size_t) ch].get(), std::memory_order_release);
        }
    }

    // Accelerometer engine for the new sample rate. The audio thread is stopped here,
    // so the active engine can be replaced directly.
    delete vibPending_.exchange (nullptr);
    delete vibRetired_.exchange (nullptr);
    delete vibActive_;
    vibActive_ = buildVibrationEngine().release();
    vibWork_ = {};
    vibSnapshot_.write (vibWork_);

    // A running calibrator measurement was set up for the old rate: abandon it
    calState_.store (CalIdle, std::memory_order_release);
    calProgress_.store (0.0, std::memory_order_relaxed);
    inputMs_.fill (0.0);

    nonMicScratch_.setSize (juce::jmax (1, juce::jmin (getTotalNumInputChannels(), SensorConfig::kMaxInputs)),
                            samplesPerBlock, false, true, false);
}

void SPLMeterAudioProcessor::releaseResources()
{
    transportSource.releaseResources();
}

//==============================================================================
void SPLMeterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // ---- MIDI CC learn / apply ----
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (msg.isController())
        {
            const int cc  = msg.getControllerNumber();
            const float v = msg.getControllerValue() / 127.0f;

            int learnIdx = midiLearnParamIndex.load();
            if (learnIdx >= 0)
            {
                midiCC[learnIdx].store (cc);
                midiLearnParamIndex.store (-1);
            }

            for (int i = 0; i < kNumMidiParams; ++i)
            {
                if (midiCC[i].load() == cc)
                    if (auto* p = apvts.getParameter (kMidiParamIds[i]))
                        p->setValueNotifyingHost (v);
            }
        }
    }

    const float calOffset    = apvts.getRawParameterValue ("calOffset")->load();
    const float peakHoldSecs = apvts.getRawParameterValue ("peakHoldTime")->load();
    const float logDurationS = apvts.getRawParameterValue ("logDuration")->load();

    const int peakHoldSamples = static_cast<int> (peakHoldSecs * currentSampleRate);

    // Update time-weight alpha; reset everything if the mode changed
    const int timeWeightIndex = static_cast<int> (apvts.getRawParameterValue ("splTimeWeight")->load());
    if (timeWeightIndex != prevTimeWeightIndex)
    {
        const double tau = (timeWeightIndex == 0) ? 0.125 : 1.0;
        rmsAlpha = 1.0 - std::exp (-1.0 / (tau * currentSampleRate));
        prevTimeWeightIndex = timeWeightIndex;
        rmsSmoothedRaw = rmsSmoothedA = rmsSmoothedC = 0.0;
        rawPeak = aPeak = cPeak = rawPeakHeld = aPeakHeld = cPeakHeld = 0.0f;
        peakHoldCounterRaw = peakHoldCounterA = peakHoldCounterC = 0;
    }

    // DAW transport sync
    const bool isDawSyncEnabled = dawSync_.load();
    bool dawPlaying = true;
    if (isDawSyncEnabled)
    {
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
                dawPlaying = pos->getIsPlaying();
            else
                dawPlaying = false;
        }
        else
        {
            dawPlaying = false;
        }
        dawIsPlaying_.store (dawPlaying);
    }
    const bool skipMetering = paused_.load() || (isDawSyncEnabled && !dawPlaying);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // --- Per-channel muting (applied to live buffer before any processing) ---
    for (int ch = 0; ch < juce::jmin (numChannels, 32); ++ch)
    {
        if (apvts.getRawParameterValue ("channelMute" + juce::String (ch))->load() > 0.5f)
            buffer.clear (ch, 0, numSamples);
    }

    // --- Sensor roles: only Mic inputs feed the SPL mix, bandpass and correction ---
    // (File mode keeps mixing all file channels; role-aware playback follows with
    //  multichannel recording, vibration spec §7.5.)
    bool isMic[SensorConfig::kMaxInputs];
    bool allMic = true;
    for (int ch = 0; ch < SensorConfig::kMaxInputs; ++ch)
    {
        isMic[ch] = channelRoles_[(size_t) ch].load (std::memory_order_relaxed) == (int) SensorRole::Mic;
        allMic = allMic && (isMic[ch] || ch >= numChannels);
    }

    // --- File mode: pull audio from transport instead of live input ---
    const bool fileMode = fileModeActive.load();

    // --- Accelerometer: take over a newly built engine, then feed the live inputs ---
    // (before bandpass/correction, which never touch accelerometer inputs anyway;
    //  file playback gets role-aware analysis with multichannel recording, spec §7.5)
    if (vibPending_.load (std::memory_order_acquire) != nullptr
        && vibRetired_.load (std::memory_order_acquire) == nullptr)
    {
        if (auto* next = vibPending_.exchange (nullptr, std::memory_order_acq_rel))
        {
            vibRetired_.store (vibActive_, std::memory_order_release);
            vibActive_ = next;
            vibWork_ = {};
            vibSnapshot_.write (vibWork_);   // invalid until the new engine publishes
        }
    }
    if (vibActive_ != nullptr && vibActive_->hasAxes() && ! fileMode)
    {
        if (vibResetRequest_.exchange (false, std::memory_order_acq_rel))
            vibActive_->requestReset();
        const float* axisIn[3];
        for (int a = 0; a < 3; ++a)
        {
            const int ch = vibActive_->inputChannel (a);
            axisIn[a] = (ch >= 0 && ch < numChannels) ? buffer.getReadPointer (ch) : nullptr;
        }
        if (vibActive_->process (axisIn, numSamples, ! skipMetering, vibWork_))
            vibSnapshot_.write (vibWork_);
    }
    vibLogLive_ = vibActive_ != nullptr && vibActive_->hasAxes() && ! fileMode && vibWork_.valid;

    // --- Live input level per channel (Inputs table) ---
    const int numLiveInputs = juce::jmin (getTotalNumInputChannels(), numChannels, SensorConfig::kMaxInputs);
    if (numSamples > 0)
    {
        const double a = 1.0 - std::exp (-numSamples / (0.125 * currentSampleRate));
        for (int ch = 0; ch < numLiveInputs; ++ch)
        {
            const float* x = buffer.getReadPointer (ch);
            double sumSq = 0.0;
            float  peak  = 0.0f;
            for (int s = 0; s < numSamples; ++s)
            {
                sumSq += (double) x[s] * x[s];
                peak = juce::jmax (peak, std::abs (x[s]));
            }
            auto& ms = inputMs_[(size_t) ch];
            ms += a * (sumSq / numSamples - ms);
            inputRms_[(size_t) ch].store ((float) std::sqrt (ms), std::memory_order_relaxed);
            if (peak > inputPeak_[(size_t) ch].load (std::memory_order_relaxed))
                inputPeak_[(size_t) ch].store (peak, std::memory_order_relaxed);
        }
    }

    // --- Calibrator measurement on one live input ---
    {
        const int cs = calState_.load (std::memory_order_acquire);
        if (cs == CalCancel)
            calState_.store (CalIdle, std::memory_order_release);
        else if (cs == CalRunning && calChannel_ < numLiveInputs)
        {
            calMeas_.process (buffer.getReadPointer (calChannel_), numSamples);
            calProgress_.store (calMeas_.progress(), std::memory_order_relaxed);
            if (calMeas_.finished())
                calState_.store (CalDone, std::memory_order_release);
        }
    }
    if (fileMode)
    {
        fileReadBuffer.setSize (2, numSamples, false, true, true);
        juce::AudioSourceChannelInfo info (&fileReadBuffer, 0, numSamples);
        transportSource.getNextAudioBlock (info);

        // Auto-stop when the file has been fully played back
        const double len = transportSource.getLengthInSeconds();
        if (len > 0.0 && transportSource.getCurrentPosition() >= len - 0.05)
        {
            transportSource.stop();
            fileModeActive.store (false);
        }
    }

    // Push Ch0 + Ch1 to WAV circular buffer (before bandpass/correction)
    if (wavBufSize_ > 0)
    {
        const juce::AudioBuffer<float>& wavSrc = fileMode ? fileReadBuffer : buffer;
        const int nSrc = wavSrc.getNumChannels();
        const float* ch0 = nSrc > 0 ? wavSrc.getReadPointer (0) : nullptr;
        const float* ch1 = nSrc > 1 ? wavSrc.getReadPointer (1) : ch0;
        int pos = wavWritePos_.load (std::memory_order_relaxed);
        for (int s = 0; s < numSamples; ++s)
        {
            wavCircBuf_[0][pos] = ch0 ? ch0[s] : 0.0f;
            wavCircBuf_[1][pos] = ch1 ? ch1[s] : 0.0f;
            if (++pos >= wavBufSize_) pos = 0;
        }
        wavWritePos_.store (pos, std::memory_order_release);
    }

    // 20Hz-20kHz bandpass (48dB/oct) applied per channel before mono mix
    if (apvts.getRawParameterValue ("bandpassEnabled")->load() > 0.5f)
    {
        auto applyBP = [this, &isMic] (juce::AudioBuffer<float>& buf, bool respectRoles)
        {
            const int nCh = std::min (buf.getNumChannels(), 8);
            const int nSa = buf.getNumSamples();
            for (int ch = 0; ch < nCh; ++ch)
            {
                if (respectRoles && ! isMic[ch])
                    continue;
                float* data = buf.getWritePointer (ch);
                for (int st = 0; st < kBpStages; ++st)
                {
                    bpHP[ch][st].processSamples (data, nSa);
                    bpLP[ch][st].processSamples (data, nSa);
                }
            }
        };
        if (fileMode) applyBP (fileReadBuffer, false);
        else          applyBP (buffer, true);
    }

    // Correction filter (applied after bandpass, before metering)
    if (apvts.getRawParameterValue ("correctionEnabled")->load() > 0.5f)
    {
        // loadImpulseResponse() is async — give the background thread a few
        // blocks to finish before we route real audio through the convolution.
        if (correctionPending_.load() && !correctionLoaded_.load())
        {
            if (--correctionWarmupLeft_ <= 0)
            {
                correctionLoaded_.store (true);
                correctionPending_.store (false);
            }
        }

        if (correctionLoaded_.load())
        {
            auto& src = fileMode ? fileReadBuffer : buffer;
            juce::dsp::AudioBlock<float> block (src);
            // Clamp to the channel count the convolution was prepared for
            const auto chClamped = juce::jmin (block.getNumChannels(),
                                               (size_t) correctionPreparedCh_);
            auto sub = block.getSubsetChannelBlock (0, chClamped);

            // The convolution runs on all channels at once: keep non-Mic inputs untouched
            const bool keepNonMic = ! fileMode && ! allMic
                                 && numSamples <= nonMicScratch_.getNumSamples();
            if (keepNonMic)
                for (int ch = 0; ch < juce::jmin ((int) chClamped, nonMicScratch_.getNumChannels()); ++ch)
                    if (! isMic[ch])
                        nonMicScratch_.copyFrom (ch, 0, src, ch, 0, numSamples);

            juce::dsp::ProcessContextReplacing<float> ctx (sub);
            correctionConv_.process (ctx);

            if (keepNonMic)
                for (int ch = 0; ch < juce::jmin ((int) chClamped, nonMicScratch_.getNumChannels()); ++ch)
                    if (! isMic[ch])
                        src.copyFrom (ch, 0, nonMicScratch_, ch, 0, numSamples);
        }
    }

    // Count channels that actually carry signal.
    // ASIO interfaces present all hardware channels (e.g. 32) even when only
    // one input is connected, so we must not divide by the total channel count —
    // that would give ~20-30 dB too low on ASIO vs. Windows Audio / Core Audio.
    // Muted channels have already been zeroed above, so they contribute 0 here.
    int activeChannels = 0;
    for (int ch = 0; ch < numChannels; ++ch)
        if ((ch >= SensorConfig::kMaxInputs || isMic[ch])
            && buffer.getMagnitude (ch, 0, numSamples) > 1e-9f)
            ++activeChannels;
    if (activeChannels == 0) activeChannels = 1;   // silence guard
    const float channelScale = 1.0f / static_cast<float> (activeChannels);

    // Each input individually, for per-input / Dual FFT (same processing stage as the mono mix)
    const juce::AudioBuffer<float>& chSrc = fileMode ? fileReadBuffer : buffer;
    const float* chIn[kMaxFftChannels];
    float*       chOut[kMaxFftChannels];
    for (int ch = 0; ch < kMaxFftChannels; ++ch)
    {
        chIn[ch]  = ch < chSrc.getNumChannels() ? chSrc.getReadPointer (ch) : nullptr;
        chOut[ch] = fftChBuf_[(size_t) ch].load (std::memory_order_acquire);
    }

    for (int s = 0; s < numSamples; ++s)
    {
        // Mix source channels to mono
        float raw = 0.0f;
        if (fileMode)
        {
            const int fileCh    = fileReadBuffer.getNumChannels();
            const float fileScl = fileCh > 0 ? 1.0f / static_cast<float> (fileCh) : 1.0f;
            for (int ch = 0; ch < fileCh; ++ch)
                raw += fileReadBuffer.getReadPointer (ch)[s];
            raw *= fileScl;
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
                if (ch >= SensorConfig::kMaxInputs || isMic[ch])
                    raw += buffer.getReadPointer (ch)[s];
            raw *= channelScale;
        }

        float aSample = aWeightL.processSample (raw);
        float cSample = cWeightL.processSample (raw);

        // Feed FFT circular buffer
        {
            int pos = fftWritePos_.load (std::memory_order_relaxed);
            fftCircBuf_[pos] = raw;
            for (int ch = 0; ch < kMaxFftChannels; ++ch)
                if (chOut[ch] != nullptr)
                    chOut[ch][pos] = chIn[ch] != nullptr ? chIn[ch][s] : 0.0f;
            fftWritePos_.store ((pos + 1) % kFftCircBufSize, std::memory_order_release);
        }

        // Feed SoundDetective (single sample at a time — batched internally)
        soundDetective_.pushSamples (&raw, 1);

        if (!skipMetering)
        {
            roughnessEst.processSample (raw);
            atomicRoughness.store (roughnessEst.getRoughness());

            sharpnessEst.processSample (raw);
            atomicSharpness.store (sharpnessEst.getSharpness());

            fluctuationEst.processSample (raw);
            atomicFluctuation.store (fluctuationEst.getFluctuation());

            impulsivenessEst.processSample (raw);
            atomicImpulsiveness.store (impulsivenessEst.getImpulsiveness());

            tonalityEst.processSample (raw);
            atomicTonality.store (tonalityEst.getTonality());

            // Peak tracking
            auto trackPeak = [&] (float absVal, float& peak, float& peakHeld, int& counter)
            {
                if (absVal >= peak) { peak = absVal; counter = peakHoldSamples; }
                else if (counter > 0) { --counter; }
                else { peak *= 0.9999f; }
                peakHeld = peak;
            };
            trackPeak (std::fabs (raw),     rawPeak, rawPeakHeld, peakHoldCounterRaw);
            trackPeak (std::fabs (aSample), aPeak,   aPeakHeld,   peakHoldCounterA);
            trackPeak (std::fabs (cSample), cPeak,   cPeakHeld,   peakHoldCounterC);

            // Exponential RMS smoothing (IEC 61672 FAST/SLOW time weighting)
            rmsSmoothedRaw += rmsAlpha * (static_cast<double> (raw)     * raw     - rmsSmoothedRaw);
            rmsSmoothedA   += rmsAlpha * (static_cast<double> (aSample) * aSample - rmsSmoothedA);
            rmsSmoothedC   += rmsAlpha * (static_cast<double> (cSample) * cSample - rmsSmoothedC);

            if (++logSampleCounter >= logIntervalSamples)
            {
                logSampleCounter = 0;
                float rawRms = static_cast<float> (std::sqrt (rmsSmoothedRaw));
                float aRms   = static_cast<float> (std::sqrt (rmsSmoothedA));
                float cRms   = static_cast<float> (std::sqrt (rmsSmoothedC));

                atomicPeakSPL.store    (linearToDBFS (rawPeakHeld) + calOffset);
                atomicPeakDBASPL.store (linearToDBFS (aPeakHeld)   + calOffset);
                atomicPeakDBCSPL.store (linearToDBFS (cPeakHeld)   + calOffset);
                atomicRMSSPL.store     (linearToDBFS (rawRms)      + calOffset);
                atomicRMSDBASPL.store  (linearToDBFS (aRms)        + calOffset);
                atomicRMSDBCSPL.store  (linearToDBFS (cRms)        + calOffset);

                float dbaDB    = linearToDBFS (aRms) + calOffset;
                float soneSnap = (dbaDB >= 40.0f) ? std::pow (2.0f, (dbaDB - 40.0f) / 10.0f)
                               : (dbaDB < 2.0f ? 0.0f : std::pow (dbaDB / 40.0f, 2.642f));
                pushLogEntry (rawPeakHeld, aPeakHeld, cPeakHeld, rawRms, aRms, cRms, calOffset,
                              roughnessEst.getRoughness(), fluctuationEst.getFluctuation(),
                              sharpnessEst.getSharpness(), soneSnap,
                              impulsivenessEst.getImpulsiveness(), tonalityEst.getTonality());
                pruneLog (logDurationS);
            }
        }
    }

    // Impulse Fidelity: capture input BEFORE monitor mute
    const bool impFidRunning = impFidActive_.load (std::memory_order_acquire);
    if (impFidRunning)
    {
        int pos = impFidPos_.load (std::memory_order_relaxed);
        for (int s = 0; s < numSamples && pos < impFidSignalLen_; ++s, ++pos)
        {
            float monoIn = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                monoIn += buffer.getReadPointer (ch)[s];
            monoIn /= static_cast<float> (juce::jmax (1, numChannels));
            impFidCapture_[static_cast<size_t> (pos)] = monoIn;
        }
    }

    // File mode: route the file audio to the device output. fileReadBuffer was
    // filled at the top of processBlock and is the source for metering; without
    // this copy the live input would still reach the output during file playback.
    if (fileMode)
    {
        const int srcCh = fileReadBuffer.getNumChannels();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (srcCh > 0)
                buffer.copyFrom (ch, 0, fileReadBuffer,
                                 juce::jmin (ch, srcCh - 1), 0, numSamples);
            else
                buffer.clear (ch, 0, numSamples);
        }
    }

    // Apply monitor gain or mute output
    if (monitorEnabled.load())
    {
        const float gainDB  = apvts.getRawParameterValue ("monitorGain")->load();
        const float gainLin = juce::Decibels::decibelsToGain (gainDB, -60.0f);
        buffer.applyGain (gainLin);
    }
    else
    {
        buffer.clear();
    }

    // Impulse Fidelity: inject test signal AFTER monitor (always reaches output)
    if (impFidRunning)
    {
        int pos = impFidPos_.load (std::memory_order_relaxed);
        const float* testData = impFidTestSignal_.getReadPointer (0);
        for (int s = 0; s < numSamples; ++s)
        {
            const float sample = (pos < impFidSignalLen_) ? testData[pos] : 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[s] = sample;
            ++pos;
        }
        impFidPos_.store (pos, std::memory_order_release);
        if (pos >= impFidSignalLen_)
            impFidActive_.store (false, std::memory_order_release);
    }

    // Output VU sampling: per-channel block peak for L (ch 0) and R (ch 1, falls back to L)
    if (numSamples > 0 && numChannels > 0)
    {
        const auto magL = buffer.getMagnitude (0, 0, numSamples);
        const auto magR = (numChannels > 1) ? buffer.getMagnitude (1, 0, numSamples) : magL;
        outputPeakL_.store (magL, std::memory_order_relaxed);
        outputPeakR_.store (magR, std::memory_order_relaxed);
    }
    else
    {
        outputPeakL_.store (0.0f, std::memory_order_relaxed);
        outputPeakR_.store (0.0f, std::memory_order_relaxed);
    }
}

//==============================================================================
void SPLMeterAudioProcessor::pushLogEntry (float rawPk, float aPk, float cPk,
                                            float rawRms, float aRms, float cRms,
                                            float calOffset,
                                            float roughness, float fluctuation,
                                            float sharpness, float loudnessSone,
                                            float impulsiveness, float tonality)
{
    LogEntry e;
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    e.timestampMs  = nowMs - pauseOffsetMs_.load (std::memory_order_relaxed);
    lastEntryWallMs_.store (nowMs, std::memory_order_relaxed);
    e.peakSPL      = linearToDBFS (rawPk)  + calOffset;
    e.peakDBASPL   = linearToDBFS (aPk)    + calOffset;
    e.peakDBCSPL   = linearToDBFS (cPk)    + calOffset;
    {
        // DIN 15905-5 LCpeak: session-long maximum (audio thread only writer)
        const float prev = sessionPeakDBCSPL_.load (std::memory_order_relaxed);
        if (e.peakDBCSPL > prev)
            sessionPeakDBCSPL_.store (e.peakDBCSPL, std::memory_order_relaxed);
    }
    e.rmsSPL       = linearToDBFS (rawRms) + calOffset;
    e.rmsDBASPL    = linearToDBFS (aRms)   + calOffset;
    e.rmsDBCSPL    = linearToDBFS (cRms)   + calOffset;

    // NIOSH REL: accumulate noise-dose contribution for this 125 ms tick.
    // Criterion 85 dB(A) for 8 h, 3 dB exchange rate, 80 dB(A) threshold.
    if (e.rmsDBASPL >= 80.0f)
    {
        const double tAllowedSec = 28800.0
                                 / std::pow (2.0, (static_cast<double> (e.rmsDBASPL) - 85.0) / 3.0);
        const double contribPct  = (0.125 / tAllowedSec) * 100.0;
        const float prev = noiseDosePct_.load (std::memory_order_relaxed);
        noiseDosePct_.store (prev + static_cast<float> (contribPct), std::memory_order_relaxed);
    }
    e.roughness      = roughness;
    e.fluctuation    = fluctuation;
    e.sharpness      = sharpness;
    e.loudnessSone   = loudnessSone;
    e.impulsiveness  = impulsiveness;
    e.tonality       = tonality;

    if (vibLogLive_)
    {
        e.vib.valid    = true;
        e.vib.quantity = vibWork_.quantity;
        e.vib.axisMask = vibWork_.axisMask;
        e.vib.uniaxial = vibWork_.uniaxial;
        for (int a = 0; a < vib::kNumAxisSlots; ++a)
        {
            e.vib.rms[a]  = vibWork_.axis[a].rms;
            e.vib.peak[a] = vibWork_.axis[a].peak;
        }
    }

    {
        const float N   = loudnessSone;
        const float R   = roughness   / 100.0f;
        const float F   = fluctuation / 100.0f;
        const float wS  = (sharpness > 1.75f)
                          ? (sharpness - 1.75f) / 4.0f * N / (N + 10.0f) : 0.0f;
        const float wFR = (N > 0.01f)
                          ? std::pow (N, 0.4f) * (0.07f * std::sqrt (F) + 0.2f * std::sqrt (R))
                          : 0.0f;
        e.psychoAnnoyance = (N > 0.01f) ? N * (1.0f + std::sqrt (wS * wS + wFR * wFR)) : 0.0f;
    }

    juce::SpinLock::ScopedLockType lock (logLock);
    logEntries.push_back (e);
}

void SPLMeterAudioProcessor::pruneLog (float logDurationSeconds)
{
    juce::int64 cutoffMs = juce::Time::currentTimeMillis()
                           - static_cast<juce::int64> (logDurationSeconds * 1000.0f);
    juce::SpinLock::ScopedLockType lock (logLock);
    while (!logEntries.empty() && logEntries.front().timestampMs < cutoffMs)
        logEntries.pop_front();
}

void SPLMeterAudioProcessor::copyFftWindow (float* dest, int size, int channel) const noexcept
{
    const float* src = fftCircBuf_.data();
    if (channel >= 0)
    {
        src = channel < kMaxFftChannels ? fftChBuf_[(size_t) channel].load (std::memory_order_acquire)
                                        : nullptr;
        if (src == nullptr)
        {
            std::fill (dest, dest + size, 0.0f);
            return;
        }
    }
    int writePos = fftWritePos_.load (std::memory_order_acquire);
    for (int i = 0; i < size; ++i)
    {
        int readPos = (writePos - size + i + kFftCircBufSize) % kFftCircBufSize;
        dest[i] = src[readPos];
    }
}

int SPLMeterAudioProcessor::pullSpectroSamples (float* dest, int maxSamples) noexcept
{
    const int writePos = fftWritePos_.load (std::memory_order_acquire);
    int readPos = spectroReadPos_.load (std::memory_order_relaxed);
    int available = (writePos - readPos + kFftCircBufSize) % kFftCircBufSize;
    int count = std::min (available, maxSamples);
    for (int i = 0; i < count; ++i)
        dest[i] = fftCircBuf_[(readPos + i) % kFftCircBufSize];
    spectroReadPos_.store ((readPos + count) % kFftCircBufSize, std::memory_order_release);
    return count;
}

std::vector<LogEntry> SPLMeterAudioProcessor::copyLog()
{
    juce::SpinLock::ScopedLockType lock (logLock);
    return { logEntries.begin(), logEntries.end() };
}

//==============================================================================
juce::AudioProcessorEditor* SPLMeterAudioProcessor::createEditor()
{
    return new SPLMeterAudioProcessorEditor (*this);
}

std::unique_ptr<juce::XmlElement> SPLMeterAudioProcessor::createStateXml()
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    for (int i = 0; i < kNumMidiParams; ++i)
        xml->setAttribute (juce::String ("midiCC_") + kMidiParamIds[i], midiCC[i].load());
    for (int i = 0; i < 32; ++i)
        if (channelNames_[i].isNotEmpty())
            xml->setAttribute ("channelName_" + juce::String (i), channelNames_[i]);
    return xml;
}

void SPLMeterAudioProcessor::restoreStateXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (apvts.state.getType()))
        return;

    for (int i = 0; i < kNumMidiParams; ++i)
        midiCC[i].store (xml.getIntAttribute (juce::String ("midiCC_") + kMidiParamIds[i], -1));
    for (int i = 0; i < 32; ++i)
        channelNames_[i] = xml.getStringAttribute ("channelName_" + juce::String (i), "");
    apvts.replaceState (juce::ValueTree::fromXml (xml));
    loadChannelRolesFromState();
    rebuildVibrationEngine();

    // ≤ 3.5.2 sessions have no fftSource: derive it from the legacy Dual FFT flag
    {
        bool hasSource = false;
        for (auto* child : xml.getChildWithTagNameIterator ("PARAM"))
            hasSource = hasSource || child->getStringAttribute ("id") == "fftSource";
        if (! hasSource)
            setFftSource (apvts.getRawParameterValue ("fftDual")->load() > 0.5f ? FftSourceDual
                                                                                : FftSourceMix);
    }

    // Apply the non-parameter settings held in UIState
    auto ui = getUiState();
    dawSync_.store ((bool) ui.getProperty ("dawSync", false));
    soundDetective_.setThreshold ((float) (double) ui.getProperty ("sdThreshold", 0.35));

    // Reload user-chosen files. The loaders force their enable flag on, so keep
    // the persisted flag instead.
    auto reload = [this, &ui] (const char* pathKey, const char* enableId,
                               auto&& load, auto&& clear)
    {
        const juce::File f (ui.getProperty (pathKey).toString());
        if (! f.existsAsFile())
        {
            clear();
            return;
        }
        auto* enable = apvts.getParameter (enableId);
        const float wasEnabled = enable->getValue();
        load (f);
        enable->setValueNotifyingHost (wasEnabled);
    };
    const auto keepUiState = ui.createCopy();  // clear*() would drop the stored paths
    reload ("correctionFile",    "correctionEnabled",
            [this] (const juce::File& f) { loadCorrectionFilter (f); }, [this] { clearCorrectionFilter(); });
    reload ("graphOverlayFile",  "graphOverlayEnabled",
            [this] (const juce::File& f) { loadGraphOverlay (f); },     [this] { clearGraphOverlay(); });
    reload ("graphOverlay2File", "graphOverlay2Enabled",
            [this] (const juce::File& f) { loadGraphOverlay2 (f); },    [this] { clearGraphOverlay2(); });
    ui.copyPropertiesFrom (keepUiState, nullptr);

#if SPLMETER_HAS_TFLITE
    const juce::File model (ui.getProperty ("tfliteModel").toString());
    if (model.existsAsFile())
    {
        TFLiteDetector::Config cfg;
        cfg.modelPath       = model.getFullPathName();
        cfg.modelSampleRate = ui.getProperty ("tfliteSampleRate", 16000);
        juce::String err;
        soundDetective_.loadTFLiteModel (cfg, err);
    }
#endif
}

void SPLMeterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    copyXmlToBinary (*createStateXml(), destData);
}

void SPLMeterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreStateXml (*xml);
}

//==============================================================================
int SPLMeterAudioProcessor::getFftSource() const noexcept
{
    return juce::jlimit (0, kNumFftSources - 1,
                         (int) apvts.getRawParameterValue ("fftSource")->load());
}

void SPLMeterAudioProcessor::setFftSource (int source)
{
    source = juce::jlimit (0, kNumFftSources - 1, source);
    if (auto* p = apvts.getParameter ("fftSource"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) source));
    if (auto* d = apvts.getParameter ("fftDual"))
        d->setValueNotifyingHost (source == FftSourceDual ? 1.0f : 0.0f);
}

//==============================================================================
// Sensor roles — persisted in UIState/Inputs as "role<ch>" (non-Mic entries only)
void SPLMeterAudioProcessor::setChannelRole (int ch, SensorRole role)
{
    if (ch < 0 || ch >= SensorConfig::kMaxInputs)
        return;
    channelRoles_[(size_t) ch].store ((int) role, std::memory_order_relaxed);

    auto inputs = getUiState().getOrCreateChildWithName ("Inputs", nullptr);
    const juce::Identifier key ("role" + juce::String (ch));
    if (role == SensorRole::Mic) inputs.removeProperty (key, nullptr);
    else                         inputs.setProperty (key, (int) role, nullptr);

    rebuildVibrationEngine();
}

//==============================================================================
// Accelerometer settings and engine
namespace
{
    const juce::Identifier kAccelNode ("Accel");
    const char* const kWeightingKeys[3] = { "weightingX", "weightingY", "weightingZ" };
    const char* const kKKeys[3]         = { "kx", "ky", "kz" };
}

vib::VibrationSettings SPLMeterAudioProcessor::getVibrationSettings() const
{
    vib::VibrationSettings s;
    const auto node = apvts.state.getChildWithName ("UIState").getChildWithName (kAccelNode);
    if (! node.isValid())
        return s;
    s.highPassHz = juce::jlimit (0.05, 50.0, (double) node.getProperty ("highPassHz", s.highPassHz));
    s.quantity   = static_cast<vib::Quantity> (juce::jlimit (0, 2, (int) node.getProperty ("quantity", 0)));
    for (int a = 0; a < 3; ++a)
    {
        s.weighting[a] = static_cast<vib::Weighting> (juce::jlimit (0, (int) vib::Weighting::KB,
                                                                    (int) node.getProperty (kWeightingKeys[a], 0)));
        s.k[a] = juce::jlimit (0.0, 5.0, (double) node.getProperty (kKKeys[a], 1.0));
    }
    s.bandLowHz  = juce::jlimit (0.1, 1000.0, (double) node.getProperty ("bandLowHz",  s.bandLowHz));
    s.bandHighHz = juce::jlimit (1.0, 20000.0, (double) node.getProperty ("bandHighHz", s.bandHighHz));
    s.tauSec     = juce::jlimit (0.01, 10.0, (double) node.getProperty ("tauSec", s.tauSec));
    return s;
}

void SPLMeterAudioProcessor::setVibrationSettings (const vib::VibrationSettings& s)
{
    auto node = getUiState().getOrCreateChildWithName (kAccelNode, nullptr);
    node.setProperty ("highPassHz", s.highPassHz, nullptr);
    node.setProperty ("quantity",   (int) s.quantity, nullptr);
    for (int a = 0; a < 3; ++a)
    {
        node.setProperty (kWeightingKeys[a], (int) s.weighting[a], nullptr);
        node.setProperty (kKKeys[a],         s.k[a], nullptr);
    }
    node.setProperty ("bandLowHz",  s.bandLowHz,  nullptr);
    node.setProperty ("bandHighHz", s.bandHighHz, nullptr);
    node.setProperty ("tauSec",     s.tauSec,     nullptr);
    rebuildVibrationEngine();
}

double SPLMeterAudioProcessor::getAccelCalDb (int ch) const
{
    const auto inputs = apvts.state.getChildWithName ("UIState").getChildWithName ("Inputs");
    const double v = inputs.isValid() ? (double) inputs.getProperty ("cal" + juce::String (ch), 140.0) : 140.0;
    return juce::jlimit (vib::calibration::kMinCalDb, vib::calibration::kMaxCalDb, v);
}

void SPLMeterAudioProcessor::setAccelCalDb (int ch, double calDb)
{
    if (ch < 0 || ch >= SensorConfig::kMaxInputs)
        return;
    getUiState().getOrCreateChildWithName ("Inputs", nullptr)
        .setProperty ("cal" + juce::String (ch),
                      juce::jlimit (vib::calibration::kMinCalDb, vib::calibration::kMaxCalDb, calDb), nullptr);
    rebuildVibrationEngine();
}

std::unique_ptr<vib::VibrationEngine> SPLMeterAudioProcessor::buildVibrationEngine() const
{
    vib::AxisInput axes[3];
    bool uniaxial = false;
    const auto roles = getChannelRoles();
    const int  numInputs = juce::jmin (getTotalNumInputChannels(), SensorConfig::kMaxInputs);

    if (SensorConfig::isValid (SensorConfig::validate (roles, numInputs)))
    {
        for (int ch = 0; ch < numInputs; ++ch)
        {
            int axis = -1;
            switch (roles[(size_t) ch])
            {
                case SensorRole::AccelX:        axis = vib::AxisX; break;
                case SensorRole::AccelY:        axis = vib::AxisY; break;
                case SensorRole::AccelZ:        axis = vib::AxisZ; break;
                case SensorRole::AccelUniaxial: axis = vib::AxisX; uniaxial = true; break;
                case SensorRole::Mic:
                case SensorRole::Off:           break;
            }
            if (axis >= 0)
                axes[axis] = { ch, getAccelCalDb (ch) };
        }
    }
    // Without a (valid) accelerometer this is an empty engine: hasAxes() == false
    return std::make_unique<vib::VibrationEngine> (currentSampleRate, getVibrationSettings(), axes, uniaxial);
}

void SPLMeterAudioProcessor::rebuildVibrationEngine()
{
    delete vibRetired_.exchange (nullptr, std::memory_order_acq_rel);   // no longer used by the audio thread
    delete vibPending_.exchange (buildVibrationEngine().release(), std::memory_order_acq_rel);
}

SensorConfig::Roles SPLMeterAudioProcessor::getChannelRoles() const noexcept
{
    SensorConfig::Roles r;
    for (int ch = 0; ch < SensorConfig::kMaxInputs; ++ch)
        r[(size_t) ch] = getChannelRole (ch);
    return r;
}

void SPLMeterAudioProcessor::loadChannelRolesFromState()
{
    const auto inputs = getUiState().getChildWithName ("Inputs");
    for (int ch = 0; ch < SensorConfig::kMaxInputs; ++ch)
    {
        const int v = inputs.isValid() ? (int) inputs.getProperty ("role" + juce::String (ch), 0) : 0;
        channelRoles_[(size_t) ch].store ((int) SensorConfig::fromInt (v), std::memory_order_relaxed);
    }
}

SPLMeterAudioProcessor::AccelSensorInfo SPLMeterAudioProcessor::getAccelSensorInfo() const
{
    AccelSensorInfo info;
    const auto node = apvts.state.getChildWithName ("UIState").getChildWithName (kAccelNode);
    if (! node.isValid())
        return info;
    info.serial       = node.getProperty ("sensorSerial", juce::String()).toString();
    info.lowerLimitHz = juce::jlimit (0.01, 1000.0,  (double) node.getProperty ("lowerLimitHz", info.lowerLimitHz));
    info.upperLimitHz = juce::jlimit (1.0, 100000.0, (double) node.getProperty ("upperLimitHz", info.upperLimitHz));
    return info;
}

void SPLMeterAudioProcessor::setAccelSensorInfo (const AccelSensorInfo& info)
{
    auto node = getUiState().getOrCreateChildWithName (kAccelNode, nullptr);
    node.setProperty ("sensorSerial", info.serial,       nullptr);
    node.setProperty ("lowerLimitHz", info.lowerLimitHz, nullptr);
    node.setProperty ("upperLimitHz", info.upperLimitHz, nullptr);
}

vib::DisplayUnit SPLMeterAudioProcessor::getVibrationDisplayUnit() const
{
    const auto node = apvts.state.getChildWithName ("UIState").getChildWithName (kAccelNode);
    return (vib::DisplayUnit) juce::jlimit (0, (int) vib::DisplayUnit::Db,
                                            node.isValid() ? (int) node.getProperty ("displayUnit", 0) : 0);
}

void SPLMeterAudioProcessor::setVibrationDisplayUnit (vib::DisplayUnit unit)
{
    getUiState().getOrCreateChildWithName (kAccelNode, nullptr).setProperty ("displayUnit", (int) unit, nullptr);
}

namespace
{
    // Keys of UIState/Accel exchanged with JSON settings files
    const char* const kAccelJsonKeys[] = { "highPassHz", "quantity", "weightingX", "weightingY", "weightingZ",
                                           "kx", "ky", "kz", "bandLowHz", "bandHighHz", "tauSec",
                                           "sensorSerial", "lowerLimitHz", "upperLimitHz", "displayUnit" };
}

juce::var SPLMeterAudioProcessor::getInputsJson() const
{
    juce::Array<juce::var> list;
    const auto inputs = apvts.state.getChildWithName ("UIState").getChildWithName ("Inputs");
    for (int ch = 0; ch < SensorConfig::kMaxInputs; ++ch)
    {
        const auto chs = juce::String (ch);
        if (getChannelRole (ch) == SensorRole::Mic && ! inputs.hasProperty ("cal" + chs))
            continue;   // defaults are not written
        auto* o = new juce::DynamicObject();
        o->setProperty ("channel", ch);
        o->setProperty ("role", (int) getChannelRole (ch));
        o->setProperty ("calDb", getAccelCalDb (ch));
        o->setProperty ("calDate",   inputs.getProperty ("calDate"   + chs, juce::String()));
        o->setProperty ("calMethod", inputs.getProperty ("calMethod" + chs, juce::String()));
        list.add (juce::var (o));
    }
    return list;
}

juce::var SPLMeterAudioProcessor::getAccelJson() const
{
    auto* o = new juce::DynamicObject();
    const auto node = apvts.state.getChildWithName ("UIState").getChildWithName (kAccelNode);
    if (node.isValid())
        for (auto* key : kAccelJsonKeys)
            if (node.hasProperty (key))
                o->setProperty (key, node.getProperty (key));
    return juce::var (o);
}

void SPLMeterAudioProcessor::setInputsAndAccelFromJson (const juce::var& inputs, const juce::var& accel)
{
    if (auto* list = inputs.getArray())
    {
        auto node = getUiState().getOrCreateChildWithName ("Inputs", nullptr);
        node.removeAllProperties (nullptr);   // the file describes the complete input setup
        for (const auto& v : *list)
        {
            const int ch = v.getProperty ("channel", -1);
            if (ch < 0 || ch >= SensorConfig::kMaxInputs)
                continue;
            const auto chs = juce::String (ch);
            const auto role = SensorConfig::fromInt (v.getProperty ("role", 0));
            if (role != SensorRole::Mic)
                node.setProperty ("role" + chs, (int) role, nullptr);
            if (v.hasProperty ("calDb"))
                node.setProperty ("cal" + chs, juce::jlimit (vib::calibration::kMinCalDb, vib::calibration::kMaxCalDb,
                                                             (double) v.getProperty ("calDb", 140.0)), nullptr);
            for (auto* key : { "calDate", "calMethod" })
                if (v.getProperty (key, juce::String()).toString().isNotEmpty())
                    node.setProperty (key + chs, v.getProperty (key, {}), nullptr);
        }
        loadChannelRolesFromState();
    }
    if (auto* obj = accel.getDynamicObject())
    {
        auto node = getUiState().getOrCreateChildWithName (kAccelNode, nullptr);
        for (auto* key : kAccelJsonKeys)
            if (obj->hasProperty (key))
                node.setProperty (key, obj->getProperty (key), nullptr);
    }
    rebuildVibrationEngine();   // getters clamp every value
}

juce::String SPLMeterAudioProcessor::getAccelCalInfo (int ch) const
{
    const auto inputs = apvts.state.getChildWithName ("UIState").getChildWithName ("Inputs");
    if (! inputs.isValid())
        return {};
    const auto date   = inputs.getProperty ("calDate"   + juce::String (ch), juce::String()).toString();
    const auto method = inputs.getProperty ("calMethod" + juce::String (ch), juce::String()).toString();
    if (date.isEmpty())
        return {};
    return method.isNotEmpty() ? date + " · " + method : date;
}

void SPLMeterAudioProcessor::setAccelCalInfo (int ch, const juce::String& method)
{
    if (ch < 0 || ch >= SensorConfig::kMaxInputs)
        return;
    auto inputs = getUiState().getOrCreateChildWithName ("Inputs", nullptr);
    inputs.setProperty ("calDate"   + juce::String (ch), juce::Time::getCurrentTime().formatted ("%Y-%m-%d"), nullptr);
    inputs.setProperty ("calMethod" + juce::String (ch), method, nullptr);
}

//==============================================================================
// Calibrator measurement (spec §4)
bool SPLMeterAudioProcessor::startAccelCalibration (int ch, double referenceHz)
{
    if (ch < 0 || ch >= juce::jmin (getTotalNumInputChannels(), SensorConfig::kMaxInputs)
        || referenceHz <= 0.0 || referenceHz > 0.4 * currentSampleRate)
        return false;

    // Only from Idle or Done: the audio thread does not touch calMeas_ then
    int s = calState_.load (std::memory_order_acquire);
    if (s != CalIdle && s != CalDone)
        return false;
    calChannel_ = ch;
    calMeas_.prepare (currentSampleRate, referenceHz);
    calProgress_.store (0.0, std::memory_order_relaxed);
    return calState_.compare_exchange_strong (s, CalRunning, std::memory_order_acq_rel);
}

void SPLMeterAudioProcessor::cancelAccelCalibration() noexcept
{
    int s = CalRunning;
    if (! calState_.compare_exchange_strong (s, CalCancel, std::memory_order_acq_rel) && s == CalDone)
        calState_.store (CalIdle, std::memory_order_release);
}

bool SPLMeterAudioProcessor::takeAccelCalibrationResult (vib::CalibratorMeasurement::Result& result) noexcept
{
    if (calState_.load (std::memory_order_acquire) != CalDone)
        return false;
    result = calMeas_.result();
    calState_.store (CalIdle, std::memory_order_release);
    return true;
}

//==============================================================================
void SPLMeterAudioProcessor::loadFile (const juce::File& file)
{
    auto* reader = formatManager.createReaderFor (file);
    if (reader == nullptr) return;

    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transportSource.setSource (newSource.get(), 0, nullptr, reader->sampleRate);
    readerSource = std::move (newSource);

    transportSource.setPosition (0.0);
    transportSource.start();
    fileModeActive.store (true);

    resetForModeSwitch();
}

void SPLMeterAudioProcessor::setFileMode (bool active)
{
    if (!active)
    {
        transportSource.stop();
        fileModeActive.store (false);
    }
}

//==============================================================================
void SPLMeterAudioProcessor::loadCorrectionFilter (const juce::File& file)
{
    correctionPoints_.clear();

    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());

    // Check first non-empty line for instrument metadata header:
    // "Sens Factor =-11.2dB, AGain =18dB, SERNO: 8102010"
    for (const auto& line : lines)
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty()) continue;

        // Look for both key markers
        const int sensIdx  = trimmed.indexOfIgnoreCase ("Sens Factor");
        const int sernoIdx = trimmed.indexOfIgnoreCase ("SERNO:");
        if (sensIdx >= 0 && sernoIdx >= 0)
        {
            // Parse Sens Factor value: find '=' then read float (may include '-')
            const int eqIdx = trimmed.indexOfChar (sensIdx, '=');
            if (eqIdx >= 0)
            {
                const float sensVal = trimmed.substring (eqIdx + 1).getFloatValue();
                correctionCalibration_ = 94.0f + std::abs (sensVal);

                // Clamp to parameter range [80, 180]
                correctionCalibration_ = juce::jlimit (80.0f, 180.0f, correctionCalibration_);

                auto* param = apvts.getParameter ("calOffset");
                const float norm = param->convertTo0to1 (correctionCalibration_);
                param->setValueNotifyingHost (norm);
            }

            // Parse SERNO value: text after "SERNO:" up to end or next comma
            const int afterSerno = sernoIdx + 6; // length of "SERNO:"
            juce::String sernoStr = trimmed.substring (afterSerno).trim();
            const int commaIdx = sernoStr.indexOfChar (',');
            if (commaIdx >= 0) sernoStr = sernoStr.substring (0, commaIdx).trim();
            correctionSerno_ = sernoStr;

            correctionMetaReady_.store (true);
        }
        break; // only inspect first non-empty line
    }

    for (const auto& line : lines)
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty()
            || trimmed.startsWith ("*")
            || trimmed.startsWith ("#")
            || trimmed.startsWith (";")
            || trimmed.startsWithChar ('/'))
            continue;

        juce::StringArray tokens;
        tokens.addTokens (trimmed, " \t,", "\"");
        tokens.removeEmptyStrings();

        if (tokens.size() >= 2)
        {
            const float freq = tokens[0].getFloatValue();
            const float spl  = tokens[1].getFloatValue();
            if (freq > 0.0f)
                correctionPoints_.push_back ({ freq, spl });
        }
    }

    if (correctionPoints_.empty())
        return;

    std::sort (correctionPoints_.begin(), correctionPoints_.end(),
               [] (auto& a, auto& b) { return a.first < b.first; });

    correctionFileName_ = file.getFileNameWithoutExtension();
    getUiState().setProperty ("correctionFile", file.getFullPathName(), nullptr);
    rebuildCorrectionFIR();

    auto* param = apvts.getParameter ("correctionEnabled");
    param->setValueNotifyingHost (1.0f);
}

void SPLMeterAudioProcessor::clearCorrectionFilter()
{
    correctionPoints_.clear();
    correctionFileName_ = {};
    getUiState().removeProperty ("correctionFile", nullptr);
    correctionPending_.store (false);
    correctionLoaded_.store (false);
    correctionConv_.reset();
    correctionSerno_ = {};
    correctionCalibration_ = 0.0f;
    correctionMetaReady_.store (false);
}

void SPLMeterAudioProcessor::rebuildCorrectionFIR()
{
    if (correctionPoints_.empty() || currentSampleRate < 1.0) return;

    // Log-linear interpolation of the measured SPL at a given frequency
    auto interpolateSPL = [this] (float freq) -> float
    {
        const auto& pts = correctionPoints_;
        if (freq <= pts.front().first) return pts.front().second;
        if (freq >= pts.back().first)  return pts.back().second;

        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const float f0 = pts[i].first, f1 = pts[i + 1].first;
            if (freq >= f0 && freq <= f1)
            {
                const float t = (f1 > f0) ? std::log (freq / f0) / std::log (f1 / f0) : 0.0f;
                return pts[i].second + t * (pts[i + 1].second - pts[i].second);
            }
        }
        return 0.0f;
    };

    // Build the desired (correction) half-spectrum: invert the measured SPL
    // so that the filter cancels the measurement's frequency-response deviation.
    static constexpr int kFirOrder = 12;          // 4096 taps
    static constexpr int kFirSize  = 1 << kFirOrder;

    std::vector<float> spec (kFirSize * 2, 0.0f); // interleaved Re/Im

    for (int k = 0; k <= kFirSize / 2; ++k)
    {
        const float freq     = (float) k * (float) currentSampleRate / kFirSize;
        const float corrDB   = (freq >= 20.0f && freq <= 20000.0f)
                               ? -interpolateSPL (freq) : 0.0f;
        const float gain     = std::pow (10.0f, corrDB / 20.0f);
        spec[k * 2]     = gain;
        spec[k * 2 + 1] = 0.0f;
    }

    // IFFT: fills conjugate mirror, performs inverse, scales by 1/N
    juce::dsp::FFT fft (kFirOrder);
    fft.performRealOnlyInverseTransform (spec.data());

    // Circular shift by kFirSize/2 → linear-phase FIR; apply Hann window
    juce::AudioBuffer<float> ir (1, kFirSize);
    float* irData = ir.getWritePointer (0);

    for (int n = 0; n < kFirSize; ++n)
    {
        const int   src = (n + kFirSize / 2) % kFirSize;
        const float win = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                    * n / (kFirSize - 1)));
        irData[n] = spec[src * 2] * win;   // real part of IFFT output
    }

    correctionConv_.loadImpulseResponse (std::move (ir),
                                         currentSampleRate,
                                         juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::no);
    // Don't set correctionLoaded_ here — loadImpulseResponse() is async.
    // Let the audio thread warm up the convolution for a few blocks first
    // so the CrossoverMixer has valid internal state before we use its output.
    correctionWarmupLeft_ = 8;
    correctionPending_.store (true);
}

//==============================================================================
void SPLMeterAudioProcessor::loadGraphOverlay (const juce::File& file)
{
    graphOverlayPoints_.clear();

    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());

    for (const auto& line : lines)
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty()
            || trimmed.startsWith ("*")
            || trimmed.startsWith ("#")
            || trimmed.startsWith (";")
            || trimmed.startsWithChar ('/'))
            continue;

        juce::StringArray tokens;
        tokens.addTokens (trimmed, " \t,", "\"");
        tokens.removeEmptyStrings();

        if (tokens.size() >= 2)
        {
            const float freq = tokens[0].getFloatValue();
            const float spl  = tokens[1].getFloatValue();
            if (freq > 0.0f)
                graphOverlayPoints_.push_back ({ freq, spl });
        }
    }

    if (graphOverlayPoints_.empty())
        return;

    std::sort (graphOverlayPoints_.begin(), graphOverlayPoints_.end(),
               [] (auto& a, auto& b) { return a.first < b.first; });

    graphOverlayFileName_ = file.getFileNameWithoutExtension();
    getUiState().setProperty ("graphOverlayFile", file.getFullPathName(), nullptr);
    graphOverlayLoaded_.store (true);
}

void SPLMeterAudioProcessor::clearGraphOverlay()
{
    graphOverlayPoints_.clear();
    graphOverlayFileName_ = {};
    getUiState().removeProperty ("graphOverlayFile", nullptr);
    graphOverlayLoaded_.store (false);
}

//==============================================================================
void SPLMeterAudioProcessor::loadGraphOverlay2 (const juce::File& file)
{
    graphOverlay2Points_.clear();

    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());

    for (const auto& line : lines)
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty()
            || trimmed.startsWith ("*")
            || trimmed.startsWith ("#")
            || trimmed.startsWith (";")
            || trimmed.startsWithChar ('/'))
            continue;

        juce::StringArray tokens;
        tokens.addTokens (trimmed, " \t,", "\"");
        tokens.removeEmptyStrings();

        if (tokens.size() >= 2)
        {
            const float freq = tokens[0].getFloatValue();
            const float spl  = tokens[1].getFloatValue();
            if (freq > 0.0f)
                graphOverlay2Points_.push_back ({ freq, spl });
        }
    }

    if (graphOverlay2Points_.empty())
        return;

    std::sort (graphOverlay2Points_.begin(), graphOverlay2Points_.end(),
               [] (auto& a, auto& b) { return a.first < b.first; });

    graphOverlay2FileName_ = file.getFileNameWithoutExtension();
    getUiState().setProperty ("graphOverlay2File", file.getFullPathName(), nullptr);
    graphOverlay2Loaded_.store (true);
}

void SPLMeterAudioProcessor::clearGraphOverlay2()
{
    graphOverlay2Points_.clear();
    graphOverlay2FileName_ = {};
    getUiState().removeProperty ("graphOverlay2File", nullptr);
    graphOverlay2Loaded_.store (false);
}

//==============================================================================
bool SPLMeterAudioProcessor::saveWavToFile (const juce::File& destFile)
{
    if (wavBufSize_ <= 0 || currentSampleRate < 1.0) return false;

    const float logDurS        = apvts.getRawParameterValue ("logDuration")->load();
    const int   numSamplesToSave = juce::jmin (wavBufSize_,
                                               static_cast<int> (logDurS * currentSampleRate));
    if (numSamplesToSave <= 0) return false;

    // Snapshot: copy the most recent numSamplesToSave samples from the circular buffer.
    // Reading across the audio-thread write is a benign race for audio data.
    const int writePos = wavWritePos_.load (std::memory_order_acquire);

    juce::AudioBuffer<float> snapBuf (2, numSamplesToSave);
    float* out0 = snapBuf.getWritePointer (0);
    float* out1 = snapBuf.getWritePointer (1);

    for (int s = 0; s < numSamplesToSave; ++s)
    {
        const int readPos = (writePos - numSamplesToSave + s + wavBufSize_) % wavBufSize_;
        out0[s] = wavCircBuf_[0][readPos];
        out1[s] = wavCircBuf_[1][readPos];
    }

    // Write stereo 24-bit WAV
    juce::WavAudioFormat wavFmt;
    auto stream = std::make_unique<juce::FileOutputStream> (destFile);
    if (!stream->openedOk()) return false;
    stream->setPosition (0);
    stream->truncate();

    auto* writer = wavFmt.createWriterFor (stream.release(), currentSampleRate, 2, 24, {}, 0);
    if (writer == nullptr) return false;

    std::unique_ptr<juce::AudioFormatWriter> writerOwner (writer);
    return writerOwner->writeFromAudioSampleBuffer (snapBuf, 0, numSamplesToSave);
}

//==============================================================================
// Impulse Fidelity
void SPLMeterAudioProcessor::startImpulseFidelityTest (const juce::AudioBuffer<float>& signal)
{
    impFidActive_.store (false, std::memory_order_seq_cst);

    impFidSignalLen_ = signal.getNumSamples();
    impFidTestSignal_.makeCopyOf (signal);
    impFidCapture_.assign (static_cast<size_t> (impFidSignalLen_), 0.0f);
    impFidPos_.store (0, std::memory_order_relaxed);

    impFidActive_.store (true, std::memory_order_release);
}

void SPLMeterAudioProcessor::stopImpulseFidelityTest()
{
    impFidActive_.store (false, std::memory_order_release);
}

std::vector<float> SPLMeterAudioProcessor::getImpulseFidelityCapture()
{
    return impFidCapture_;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SPLMeterAudioProcessor();
}
