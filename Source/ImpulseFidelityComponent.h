#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>
#include <cmath>

//==============================================================================
class ImpulseFidelityWindow : public juce::DocumentWindow
{
    //--------------------------------------------------------------------------
    static constexpr int kNumFreqs = 21;
    static constexpr float kFrequencies[kNumFreqs] = {
        100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
        1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000
    };
    static constexpr int kBurstOnMs  = 200;
    static constexpr int kBurstOffMs = 300;
    static constexpr int kLeaderMs   = 500;
    static constexpr int kFadeMs     = 1;

    struct BurstResult
    {
        float freq          = 0.0f;
        float attackTimeMs  = 0.0f;
        float releaseTimeMs = 0.0f;
        float overshootPct  = 0.0f;
        std::vector<float> refEnvelope;
        std::vector<float> capEnvelope;
    };

    //--------------------------------------------------------------------------
    class Content : public juce::Component,
                    private juce::Timer
    {
    public:
        explicit Content (SPLMeterAudioProcessor& p)
            : processor (p)
        {
            // Run button
            runButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3c));
            runButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff34c759));
            runButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            runButton.onClick = [this] { doRunTest(); };
            addAndMakeVisible (runButton);

            // Stop button
            stopButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3c));
            stopButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            stopButton.onClick = [this]
            {
                processor.stopImpulseFidelityTest();
                stopTimer();
                statusLabel.setText ("Stopped.", juce::dontSendNotification);
            };
            addAndMakeVisible (stopButton);

            // Level slider
            levelLabel.setText ("Level:", juce::dontSendNotification);
            levelLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            levelLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
            levelLabel.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (levelLabel);

            levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
            levelSlider.setTextValueSuffix (" dBFS");
            levelSlider.setRange (-20.0, 0.0, 0.5);
            levelSlider.setValue (-6.0, juce::dontSendNotification);
            levelSlider.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
            levelSlider.setColour (juce::Slider::thumbColourId,          juce::Colour (0xff5ac8fa));
            levelSlider.setColour (juce::Slider::textBoxTextColourId,    juce::Colours::white);
            levelSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible (levelSlider);

            // Status label
            statusLabel.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8e8e93));
            statusLabel.setJustificationType (juce::Justification::centredLeft);
            statusLabel.setText ("Ready. Connect DUT and click Run Test.", juce::dontSendNotification);
            addAndMakeVisible (statusLabel);

            // Frequency selector
            freqLabel.setText ("Frequency:", juce::dontSendNotification);
            freqLabel.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
            freqLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
            freqLabel.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (freqLabel);

            for (int i = 0; i < kNumFreqs; ++i)
            {
                juce::String label = (kFrequencies[i] >= 1000)
                    ? juce::String (kFrequencies[i] / 1000.0f, 1) + " kHz"
                    : juce::String (static_cast<int> (kFrequencies[i])) + " Hz";
                freqSelector.addItem (label, i + 1);
            }
            freqSelector.setSelectedId (11, juce::dontSendNotification); // default 1 kHz
            freqSelector.onChange = [this] { repaint(); };
            freqSelector.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff3a3a3c));
            freqSelector.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
            freqSelector.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff48484a));
            addAndMakeVisible (freqSelector);
        }

        ~Content() override { stopTimer(); }

        //----------------------------------------------------------------------
        void resized() override
        {
            auto r = getLocalBounds().reduced (6);

            // Row 1: transport + level
            auto row1 = r.removeFromTop (28).reduced (0, 2);
            runButton.setBounds  (row1.removeFromLeft (80));
            row1.removeFromLeft (4);
            stopButton.setBounds (row1.removeFromLeft (50));
            row1.removeFromLeft (8);
            levelLabel.setBounds (row1.removeFromLeft (42));
            levelSlider.setBounds (row1.removeFromLeft (160));
            row1.removeFromLeft (8);
            statusLabel.setBounds (row1);
            r.removeFromTop (4);

            // Row 2: frequency selector
            auto row2 = r.removeFromTop (24).reduced (0, 2);
            freqLabel.setBounds (row2.removeFromLeft (70));
            row2.removeFromLeft (4);
            freqSelector.setBounds (row2.removeFromLeft (120));
            r.removeFromTop (4);

            // Bottom: metrics table (painted)
            metricsArea = r.removeFromBottom (160);
            r.removeFromBottom (4);

            plotArea = r;
        }

        //----------------------------------------------------------------------
        void timerCallback() override
        {
            int pos = processor.getImpulseFidelityPosition();
            int len = processor.getImpulseFidelitySignalLength();

            if (len > 0)
            {
                int pct = static_cast<int> (100.0f * pos / len);
                statusLabel.setText ("Running... " + juce::String (pct) + "%",
                                     juce::dontSendNotification);
            }

            if (!processor.isImpulseFidelityActive() && len > 0)
            {
                stopTimer();
                capturedData = processor.getImpulseFidelityCapture();
                analyzeResults();
                statusLabel.setText ("Complete.", juce::dontSendNotification);
                repaint();
            }
        }

        //----------------------------------------------------------------------
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1c1c1e));

            paintPlot (g);
            paintMetrics (g);
        }

    private:
        //----------------------------------------------------------------------
        void doRunTest()
        {
            if (processor.isFileModeActive())
            {
                statusLabel.setText ("Disable File mode before running test.",
                                     juce::dontSendNotification);
                return;
            }

            sampleRate_ = processor.getSampleRate();
            auto signal = generateTestSignal();
            results.clear();
            capturedData.clear();
            processor.startImpulseFidelityTest (signal);
            startTimer (50);
            statusLabel.setText ("Running... 0%", juce::dontSendNotification);
        }

        //----------------------------------------------------------------------
        juce::AudioBuffer<float> generateTestSignal()
        {
            const float levelLin = juce::Decibels::decibelsToGain (
                static_cast<float> (levelSlider.getValue()));

            const int leaderSamples = static_cast<int> (kLeaderMs * 0.001 * sampleRate_);
            const int burstOnSamples  = static_cast<int> (kBurstOnMs  * 0.001 * sampleRate_);
            const int burstOffSamples = static_cast<int> (kBurstOffMs * 0.001 * sampleRate_);
            const int fadeSamples = static_cast<int> (kFadeMs * 0.001 * sampleRate_);
            const int burstTotal = burstOnSamples + burstOffSamples;
            const int totalSamples = leaderSamples + kNumFreqs * burstTotal;

            juce::AudioBuffer<float> signal (1, totalSamples);
            signal.clear();

            for (int fi = 0; fi < kNumFreqs; ++fi)
            {
                const float freq = kFrequencies[fi];
                const int offset = leaderSamples + fi * burstTotal;
                float* data = signal.getWritePointer (0);

                for (int s = 0; s < burstOnSamples; ++s)
                {
                    float sample = std::sin (2.0f * juce::MathConstants<float>::pi
                                             * freq * s / static_cast<float> (sampleRate_));
                    // Cosine fade in/out
                    float env = 1.0f;
                    if (s < fadeSamples)
                        env = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi
                                                        * s / fadeSamples));
                    else if (s >= burstOnSamples - fadeSamples)
                        env = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi
                                      * (burstOnSamples - 1 - s) / fadeSamples));
                    data[offset + s] = sample * levelLin * env;
                }
            }

            return signal;
        }

        //----------------------------------------------------------------------
        void analyzeResults()
        {
            results.clear();

            const int leaderSamples = static_cast<int> (kLeaderMs * 0.001 * sampleRate_);
            const int burstOnSamples  = static_cast<int> (kBurstOnMs  * 0.001 * sampleRate_);
            const int burstOffSamples = static_cast<int> (kBurstOffMs * 0.001 * sampleRate_);
            const int burstTotal = burstOnSamples + burstOffSamples;

            // Envelope smoothing: ~1ms time constant
            const double envAlpha = 1.0 - std::exp (-1.0 / (0.001 * sampleRate_));

            for (int fi = 0; fi < kNumFreqs; ++fi)
            {
                BurstResult br;
                br.freq = kFrequencies[fi];

                const int offset = leaderSamples + fi * burstTotal;
                const int segLen = burstTotal;

                if (offset + segLen > static_cast<int> (capturedData.size()))
                    break;

                // Extract envelopes for captured signal
                br.capEnvelope.resize (static_cast<size_t> (segLen));
                double envState = 0.0;
                for (int s = 0; s < segLen; ++s)
                {
                    double x = std::fabs (capturedData[static_cast<size_t> (offset + s)]);
                    envState += envAlpha * (x - envState);
                    br.capEnvelope[static_cast<size_t> (s)] = static_cast<float> (envState);
                }

                // Reference envelope (ideal burst shape through same smoothing)
                br.refEnvelope.resize (static_cast<size_t> (segLen));
                envState = 0.0;
                const float levelLin = juce::Decibels::decibelsToGain (
                    static_cast<float> (levelSlider.getValue()));
                const int fadeSamples = static_cast<int> (kFadeMs * 0.001 * sampleRate_);
                for (int s = 0; s < segLen; ++s)
                {
                    float ref = 0.0f;
                    if (s < burstOnSamples)
                    {
                        float env = 1.0f;
                        if (s < fadeSamples)
                            env = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi
                                                            * s / fadeSamples));
                        else if (s >= burstOnSamples - fadeSamples)
                            env = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi
                                          * (burstOnSamples - 1 - s) / fadeSamples));
                        ref = levelLin * env;
                    }
                    envState += envAlpha * (static_cast<double> (ref) - envState);
                    br.refEnvelope[static_cast<size_t> (s)] = static_cast<float> (envState);
                }

                // Steady-state level: average of middle 50% of "on" period
                float steadyState = 0.0f;
                {
                    int ss0 = burstOnSamples / 4;
                    int ss1 = burstOnSamples * 3 / 4;
                    float sum = 0.0f;
                    int count = 0;
                    for (int s = ss0; s < ss1; ++s)
                    {
                        sum += br.capEnvelope[static_cast<size_t> (s)];
                        ++count;
                    }
                    steadyState = (count > 0) ? sum / count : 1.0f;
                }
                if (steadyState < 1e-10f) steadyState = 1e-10f;

                // Attack time: 10% to 90% of steady-state
                float t10 = -1.0f, t90 = -1.0f;
                for (int s = 0; s < burstOnSamples; ++s)
                {
                    float val = br.capEnvelope[static_cast<size_t> (s)];
                    if (t10 < 0.0f && val >= 0.1f * steadyState)
                        t10 = static_cast<float> (s) / static_cast<float> (sampleRate_) * 1000.0f;
                    if (t90 < 0.0f && val >= 0.9f * steadyState)
                        t90 = static_cast<float> (s) / static_cast<float> (sampleRate_) * 1000.0f;
                }
                br.attackTimeMs = (t10 >= 0.0f && t90 >= 0.0f) ? (t90 - t10) : -1.0f;

                // Release time: 90% to 10% (scanning from burst end)
                float r90 = -1.0f, r10 = -1.0f;
                for (int s = burstOnSamples; s < segLen; ++s)
                {
                    float val = br.capEnvelope[static_cast<size_t> (s)];
                    if (r90 < 0.0f && val <= 0.9f * steadyState)
                        r90 = static_cast<float> (s - burstOnSamples) / static_cast<float> (sampleRate_) * 1000.0f;
                    if (r10 < 0.0f && val <= 0.1f * steadyState)
                        r10 = static_cast<float> (s - burstOnSamples) / static_cast<float> (sampleRate_) * 1000.0f;
                }
                br.releaseTimeMs = (r90 >= 0.0f && r10 >= 0.0f) ? (r10 - r90) : -1.0f;

                // Overshoot
                float peakEnv = 0.0f;
                for (int s = 0; s < burstOnSamples; ++s)
                    peakEnv = std::max (peakEnv, br.capEnvelope[static_cast<size_t> (s)]);
                br.overshootPct = (peakEnv / steadyState - 1.0f) * 100.0f;
                if (br.overshootPct < 0.0f) br.overshootPct = 0.0f;

                // Normalize envelopes for display
                float normFactor = 1.0f / std::max (steadyState, 1e-10f);
                for (auto& v : br.capEnvelope) v *= normFactor;
                float refMax = *std::max_element (br.refEnvelope.begin(), br.refEnvelope.end());
                float refNorm = (refMax > 1e-10f) ? 1.0f / refMax : 1.0f;
                for (auto& v : br.refEnvelope) v *= refNorm;

                results.push_back (br);
            }
        }

        //----------------------------------------------------------------------
        void paintPlot (juce::Graphics& g)
        {
            if (plotArea.isEmpty()) return;

            int selIdx = freqSelector.getSelectedId() - 1;
            if (selIdx < 0 || selIdx >= static_cast<int> (results.size()))
            {
                // Draw empty plot with label
                g.setColour (juce::Colour (0xff48484a));
                g.drawRect (plotArea);
                g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
                g.setColour (juce::Colour (0xff8e8e93));
                g.drawText ("Run a test to see envelope comparison",
                            plotArea, juce::Justification::centred);
                return;
            }

            const auto& br = results[static_cast<size_t> (selIdx)];
            if (br.refEnvelope.empty()) return;

            const float margin = 40.0f;
            const float botM   = 24.0f;
            auto pa = plotArea.toFloat();
            float pL = pa.getX() + margin;
            float pR = pa.getRight() - 12.0f;
            float pT = pa.getY() + 8.0f;
            float pB = pa.getBottom() - botM;
            float pW = pR - pL;
            float pH = pB - pT;
            if (pW < 10.0f || pH < 10.0f) return;

            int segLen = static_cast<int> (br.refEnvelope.size());
            float durationMs = static_cast<float> (segLen) / static_cast<float> (sampleRate_) * 1000.0f;
            float yMax = 1.3f; // headroom for overshoot

            auto xToScreen = [&] (float ms) { return pL + (ms / durationMs) * pW; };
            auto yToScreen = [&] (float v)  { return pB - (v / yMax) * pH; };

            // Grid
            g.setColour (juce::Colour (0xff3a3a3c));
            for (float v = 0.0f; v <= yMax; v += 0.25f)
                g.drawHorizontalLine (static_cast<int> (yToScreen (v)), pL, pR);
            for (float ms = 0.0f; ms <= durationMs; ms += 50.0f)
                g.drawVerticalLine (static_cast<int> (xToScreen (ms)), pT, pB);

            // Y-axis labels
            g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
            g.setColour (juce::Colour (0xffaeaeb2));
            for (float v = 0.0f; v <= yMax; v += 0.25f)
                g.drawText (juce::String (v, 2),
                            static_cast<int> (pa.getX()), static_cast<int> (yToScreen (v) - 6),
                            static_cast<int> (margin - 4), 12,
                            juce::Justification::centredRight);

            // X-axis labels
            for (float ms = 0.0f; ms <= durationMs; ms += 100.0f)
                g.drawText (juce::String (static_cast<int> (ms)) + "ms",
                            static_cast<int> (xToScreen (ms) - 20), static_cast<int> (pB + 2),
                            40, 16, juce::Justification::centred);

            // Border
            g.setColour (juce::Colour (0xff48484a));
            g.drawRect (pL, pT, pW, pH, 1.0f);

            // Clip
            g.saveState();
            g.reduceClipRegion (static_cast<int> (pL), static_cast<int> (pT),
                                static_cast<int> (pW), static_cast<int> (pH));

            // Draw reference envelope (blue)
            {
                juce::Path path;
                for (int s = 0; s < segLen; ++s)
                {
                    float ms = static_cast<float> (s) / static_cast<float> (sampleRate_) * 1000.0f;
                    float x = xToScreen (ms);
                    float y = yToScreen (br.refEnvelope[static_cast<size_t> (s)]);
                    if (s == 0) path.startNewSubPath (x, y);
                    else        path.lineTo (x, y);
                }
                g.setColour (juce::Colour (0xff5ac8fa));
                g.strokePath (path, juce::PathStrokeType (1.5f));
            }

            // Draw captured envelope (orange)
            {
                juce::Path path;
                for (int s = 0; s < segLen; ++s)
                {
                    float ms = static_cast<float> (s) / static_cast<float> (sampleRate_) * 1000.0f;
                    float x = xToScreen (ms);
                    float y = yToScreen (br.capEnvelope[static_cast<size_t> (s)]);
                    if (s == 0) path.startNewSubPath (x, y);
                    else        path.lineTo (x, y);
                }
                g.setColour (juce::Colour (0xffff9f0a));
                g.strokePath (path, juce::PathStrokeType (1.5f));
            }

            g.restoreState();

            // Legend
            g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
            float lgX = pR - 120.0f;
            float lgY = pT + 4.0f;
            g.setColour (juce::Colour (0xff5ac8fa));
            g.fillRect (lgX, lgY + 3.0f, 12.0f, 2.0f);
            g.drawText ("Reference", static_cast<int> (lgX + 16), static_cast<int> (lgY),
                        60, 10, juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xffff9f0a));
            g.fillRect (lgX, lgY + 15.0f, 12.0f, 2.0f);
            g.drawText ("Captured", static_cast<int> (lgX + 16), static_cast<int> (lgY + 12),
                        60, 10, juce::Justification::centredLeft);
        }

        //----------------------------------------------------------------------
        void paintMetrics (juce::Graphics& g)
        {
            if (metricsArea.isEmpty()) return;

            auto ma = metricsArea.toFloat();
            g.setColour (juce::Colour (0xff2c2c2e));
            g.fillRoundedRectangle (ma, 4.0f);

            if (results.empty())
            {
                g.setColour (juce::Colour (0xff8e8e93));
                g.setFont (juce::Font (juce::FontOptions().withHeight (12.0f)));
                g.drawText ("No results yet.", metricsArea, juce::Justification::centred);
                return;
            }

            // Header
            const float rowH = 18.0f;
            const float cols[] = { 0.0f, 80.0f, 170.0f, 270.0f, 370.0f };
            const char* headers[] = { "Frequency", "Attack (ms)", "Release (ms)", "Overshoot (%)", "Rating" };
            const float x0 = ma.getX() + 8.0f;
            float y = ma.getY() + 4.0f;

            g.setFont (juce::Font (juce::FontOptions().withName ("Courier New").withHeight (11.0f)));
            g.setColour (juce::Colour (0xffaeaeb2));
            for (int c = 0; c < 5; ++c)
                g.drawText (headers[c], static_cast<int> (x0 + cols[c]), static_cast<int> (y),
                            80, static_cast<int> (rowH), juce::Justification::centredLeft);

            y += rowH;
            g.setColour (juce::Colour (0xff48484a));
            g.drawHorizontalLine (static_cast<int> (y), x0, ma.getRight() - 8.0f);
            y += 2.0f;

            // Rows
            for (const auto& br : results)
            {
                if (y + rowH > ma.getBottom()) break;

                // Frequency
                juce::String freqStr = (br.freq >= 1000)
                    ? juce::String (br.freq / 1000.0f, 1) + "k"
                    : juce::String (static_cast<int> (br.freq));
                g.setColour (juce::Colours::white);
                g.drawText (freqStr + " Hz", static_cast<int> (x0 + cols[0]), static_cast<int> (y),
                            80, static_cast<int> (rowH), juce::Justification::centredLeft);

                // Attack
                g.setColour (ratingColour (br.attackTimeMs, 2.0f, 5.0f));
                g.drawText (br.attackTimeMs >= 0.0f ? juce::String (br.attackTimeMs, 1) : "N/A",
                            static_cast<int> (x0 + cols[1]), static_cast<int> (y),
                            80, static_cast<int> (rowH), juce::Justification::centredLeft);

                // Release
                g.setColour (ratingColour (br.releaseTimeMs, 2.0f, 5.0f));
                g.drawText (br.releaseTimeMs >= 0.0f ? juce::String (br.releaseTimeMs, 1) : "N/A",
                            static_cast<int> (x0 + cols[2]), static_cast<int> (y),
                            80, static_cast<int> (rowH), juce::Justification::centredLeft);

                // Overshoot
                g.setColour (ratingColour (br.overshootPct, 5.0f, 15.0f));
                g.drawText (juce::String (br.overshootPct, 1),
                            static_cast<int> (x0 + cols[3]), static_cast<int> (y),
                            80, static_cast<int> (rowH), juce::Justification::centredLeft);

                // Overall rating
                float worstMetric = std::max ({ br.attackTimeMs, br.releaseTimeMs });
                juce::String ratingStr;
                juce::Colour rCol;
                if (worstMetric < 0.0f)                          { ratingStr = "N/A"; rCol = juce::Colour (0xff8e8e93); }
                else if (worstMetric <= 2.0f && br.overshootPct < 5.0f)  { ratingStr = "Excellent"; rCol = juce::Colour (0xff34c759); }
                else if (worstMetric <= 5.0f && br.overshootPct < 15.0f) { ratingStr = "Good";      rCol = juce::Colour (0xffffe620); }
                else                                                      { ratingStr = "Poor";      rCol = juce::Colour (0xffff453a); }
                g.setColour (rCol);
                g.drawText (ratingStr, static_cast<int> (x0 + cols[4]), static_cast<int> (y),
                            80, static_cast<int> (rowH), juce::Justification::centredLeft);

                y += rowH;
            }
        }

        //----------------------------------------------------------------------
        static juce::Colour ratingColour (float value, float goodThresh, float warnThresh)
        {
            if (value < 0.0f) return juce::Colour (0xff8e8e93);
            if (value <= goodThresh) return juce::Colour (0xff34c759);
            if (value <= warnThresh) return juce::Colour (0xffffe620);
            return juce::Colour (0xffff453a);
        }

        //----------------------------------------------------------------------
        SPLMeterAudioProcessor& processor;
        double sampleRate_ = 44100.0;

        juce::TextButton runButton  { "Run Test" };
        juce::TextButton stopButton { "Stop" };
        juce::Label      levelLabel;
        juce::Slider     levelSlider;
        juce::Label      statusLabel;
        juce::Label      freqLabel;
        juce::ComboBox   freqSelector;

        std::vector<BurstResult> results;
        std::vector<float>       capturedData;

        juce::Rectangle<int> plotArea;
        juce::Rectangle<int> metricsArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

public:
    //--------------------------------------------------------------------------
    explicit ImpulseFidelityWindow (SPLMeterAudioProcessor& p)
        : juce::DocumentWindow ("Impulse Fidelity",
                                juce::Colour (0xff1c1c1e),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        auto* c = new Content (p);
        c->setSize (750, 550);
        setContentOwned (c, true);
        setResizable (true, false);
    }

    void closeButtonPressed() override { setVisible (false); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImpulseFidelityWindow)
};
