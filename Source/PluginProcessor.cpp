#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

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
        "spectroGain", "Spectrogram Gain (dB)",
        juce::NormalisableRange<float> (-40.0f, 40.0f, 0.5f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
SPLMeterAudioProcessor::SPLMeterAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::discreteChannels (8), true)
                      .withOutput ("Output", juce::AudioChannelSet::discreteChannels (8), true)),
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

    transportSource.prepareToPlay (samplesPerBlock, sampleRate);
    fileReadBuffer.setSize (2, samplesPerBlock, false, true, false);
}

void SPLMeterAudioProcessor::releaseResources()
{
    transportSource.releaseResources();
}

//==============================================================================
void SPLMeterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

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

        // Feed spectrogram FIFO (drop if full)
        {
            int s1, n1, s2, n2;
            spectroFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) spectroBuffer[(size_t)s1] = raw;
            if (n2 > 0) spectroBuffer[(size_t)s2] = raw;
            spectroFifo.finishedWrite (n1 + n2);
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
    copyXmlToBinary (*xml, destData);
}

void SPLMeterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SPLMeterAudioProcessor();
}
