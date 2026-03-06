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
        juce::NormalisableRange<float> (80.0f, 140.0f, 0.1f), 127.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "fftGain", "FFT Gain (dB)",
        juce::NormalisableRange<float> (-40.0f, 40.0f, 0.5f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "fftSmoothing", "FFT Smoothing",
        juce::NormalisableRange<float> (0.0f, 0.95f, 0.01f), 0.0f));

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

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "fftRTAMode", "FFT RTA +3dB/oct", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "bandpassEnabled", "20-20k Bandpass", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "line94Enabled", "94 dB Reference Line", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "correctionEnabled", "Correction Filter", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "graphOverlayEnabled", "Graph Overlay", false));

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

SPLMeterAudioProcessor::~SPLMeterAudioProcessor() {}

//==============================================================================
void SPLMeterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    aWeightL.prepare (sampleRate);
    cWeightL.prepare (sampleRate);
    roughnessEst.prepare (sampleRate);
    sharpnessEst.prepare (sampleRate);
    fluctuationEst.prepare (sampleRate);

    rawPeak = aPeak = cPeak = rawPeakHeld = aPeakHeld = cPeakHeld = 0.0f;
    peakHoldCounterRaw = peakHoldCounterA = peakHoldCounterC = 0;
    rmsSmoothedRaw = rmsSmoothedA = rmsSmoothedC = 0.0;

    const int timeWeightIndex = static_cast<int> (apvts.getRawParameterValue ("splTimeWeight")->load());
    const double tau = (timeWeightIndex == 0) ? 0.125 : 1.0;
    rmsAlpha = 1.0 - std::exp (-1.0 / (tau * sampleRate));
    prevTimeWeightIndex = timeWeightIndex;

    logIntervalSamples = static_cast<int> (0.125 * sampleRate);
    logSampleCounter = 0;

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
    correctionConv_.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 8 });
    correctionConv_.reset();
    if (!correctionPoints_.empty())
        rebuildCorrectionFIR();

    transportSource.prepareToPlay (samplesPerBlock, sampleRate);
    fileReadBuffer.setSize (2, samplesPerBlock, false, true, false);
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

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // --- Per-channel muting (applied to live buffer before any processing) ---
    for (int ch = 0; ch < juce::jmin (numChannels, 32); ++ch)
    {
        if (apvts.getRawParameterValue ("channelMute" + juce::String (ch))->load() > 0.5f)
            buffer.clear (ch, 0, numSamples);
    }

    // --- File mode: pull audio from transport instead of live input ---
    const bool fileMode = fileModeActive.load();
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

    // 20Hz–20kHz bandpass (48dB/oct) applied per channel before mono mix
    if (apvts.getRawParameterValue ("bandpassEnabled")->load() > 0.5f)
    {
        auto applyBP = [this] (juce::AudioBuffer<float>& buf)
        {
            const int nCh = std::min (buf.getNumChannels(), 8);
            const int nSa = buf.getNumSamples();
            for (int ch = 0; ch < nCh; ++ch)
            {
                float* data = buf.getWritePointer (ch);
                for (int st = 0; st < kBpStages; ++st)
                {
                    bpHP[ch][st].processSamples (data, nSa);
                    bpLP[ch][st].processSamples (data, nSa);
                }
            }
        };
        if (fileMode) applyBP (fileReadBuffer);
        else          applyBP (buffer);
    }

    // Correction filter (applied after bandpass, before metering)
    if (correctionLoaded_.load() && apvts.getRawParameterValue ("correctionEnabled")->load() > 0.5f)
    {
        auto& src = fileMode ? fileReadBuffer : buffer;
        juce::dsp::AudioBlock<float> block (src);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        correctionConv_.process (ctx);
    }

    const float channelScale = numChannels > 0 ? 1.0f / static_cast<float> (numChannels) : 1.0f;

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
                raw += buffer.getReadPointer (ch)[s];
            raw *= channelScale;
        }

        float aSample = aWeightL.processSample (raw);
        float cSample = cWeightL.processSample (raw);

        // Feed FFT circular buffer
        {
            int pos = fftWritePos_.load (std::memory_order_relaxed);
            fftCircBuf_[pos] = raw;
            fftWritePos_.store ((pos + 1) % kFftCircBufSize, std::memory_order_release);
        }

        roughnessEst.processSample (raw);
        atomicRoughness.store (roughnessEst.getRoughness());

        sharpnessEst.processSample (raw);
        atomicSharpness.store (sharpnessEst.getSharpness());

        fluctuationEst.processSample (raw);
        atomicFluctuation.store (fluctuationEst.getFluctuation());

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
                          sharpnessEst.getSharpness(), soneSnap);
            pruneLog (logDurationS);
        }
    }

    // Mute output if monitoring is disabled
    if (!monitorEnabled.load())
        buffer.clear();
}

//==============================================================================
void SPLMeterAudioProcessor::pushLogEntry (float rawPk, float aPk, float cPk,
                                            float rawRms, float aRms, float cRms,
                                            float calOffset,
                                            float roughness, float fluctuation,
                                            float sharpness, float loudnessSone)
{
    LogEntry e;
    e.timestampMs  = juce::Time::currentTimeMillis();
    e.peakSPL      = linearToDBFS (rawPk)  + calOffset;
    e.peakDBASPL   = linearToDBFS (aPk)    + calOffset;
    e.peakDBCSPL   = linearToDBFS (cPk)    + calOffset;
    e.rmsSPL       = linearToDBFS (rawRms) + calOffset;
    e.rmsDBASPL    = linearToDBFS (aRms)   + calOffset;
    e.rmsDBCSPL    = linearToDBFS (cRms)   + calOffset;
    e.roughness    = roughness;
    e.fluctuation  = fluctuation;
    e.sharpness    = sharpness;
    e.loudnessSone = loudnessSone;

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

void SPLMeterAudioProcessor::copyFftWindow (float* dest, int size) const noexcept
{
    int writePos = fftWritePos_.load (std::memory_order_acquire);
    for (int i = 0; i < size; ++i)
    {
        int readPos = (writePos - size + i + kFftCircBufSize) % kFftCircBufSize;
        dest[i] = fftCircBuf_[readPos];
    }
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

void SPLMeterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    for (int i = 0; i < kNumMidiParams; ++i)
        xml->setAttribute (juce::String ("midiCC_") + kMidiParamIds[i], midiCC[i].load());
    copyXmlToBinary (*xml, destData);
}

void SPLMeterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        for (int i = 0; i < kNumMidiParams; ++i)
            midiCC[i].store (xml->getIntAttribute (juce::String ("midiCC_") + kMidiParamIds[i], -1));
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
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
    rebuildCorrectionFIR();
}

void SPLMeterAudioProcessor::clearCorrectionFilter()
{
    correctionPoints_.clear();
    correctionFileName_ = {};
    correctionLoaded_.store (false);
    correctionConv_.reset();
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
    correctionLoaded_.store (true);
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
    graphOverlayLoaded_.store (true);
}

void SPLMeterAudioProcessor::clearGraphOverlay()
{
    graphOverlayPoints_.clear();
    graphOverlayFileName_ = {};
    graphOverlayLoaded_.store (false);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SPLMeterAudioProcessor();
}
