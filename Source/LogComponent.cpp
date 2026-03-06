#include "LogComponent.h"

// Resolution index → subdivisions per octave
static const int kBandNForRes[5] = { 1, 3, 6, 12, 24 };

// SPL series colours
const juce::Colour LogComponent::colPeakSPL  { 0xff5ac8fa };  // blue
const juce::Colour LogComponent::colRmsSPL   { 0xff34c759 };  // green
const juce::Colour LogComponent::colPeakDBA  { 0xffff9500 };  // orange
const juce::Colour LogComponent::colRmsDBA   { 0xffbf5af2 };  // purple
const juce::Colour LogComponent::colPeakDBC  { 0xffff2d55 };  // red
const juce::Colour LogComponent::colRmsDBC   { 0xffffe620 };  // yellow

// Psychoacoustic series colours
const juce::Colour LogComponent::colRoughness   { 0xff00c7be };  // teal
const juce::Colour LogComponent::colFluctuation { 0xffeb34b1 };  // pink
const juce::Colour LogComponent::colSharpness   { 0xffc77dff };  // violet
const juce::Colour LogComponent::colLoudness    { 0xffffe57f };  // gold

//==============================================================================
LogComponent::LogComponent (SPLMeterAudioProcessor& p)
    : processor (p)
{
    durationLabel.setText ("Keep last (s):", juce::dontSendNotification);
    durationLabel.setFont (juce::Font (juce::FontOptions().withHeight (22.0f)));
    durationLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaeaeb2));
    addAndMakeVisible (durationLabel);

    durationSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    durationSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 100, 36);
    durationSlider.setColour (juce::Slider::textBoxTextColourId,    juce::Colours::white);
    durationSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (durationSlider);

    durationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "logDuration", durationSlider);

    // SPL series visibility checkboxes
    auto setupVisBtn = [this] (juce::ToggleButton& btn, juce::Colour colour)
    {
        btn.setToggleState (true, juce::dontSendNotification);
        btn.setColour (juce::ToggleButton::textColourId,         juce::Colours::white);
        btn.setColour (juce::ToggleButton::tickColourId,         colour);
        btn.setColour (juce::ToggleButton::tickDisabledColourId, colour.darker (0.5f));
        btn.onClick = [this] { repaint(); };
        addAndMakeVisible (btn);
    };
    setupVisBtn (splVisButton, colPeakSPL);
    setupVisBtn (dbaVisButton, colPeakDBA);
    setupVisBtn (dbcVisButton, colPeakDBC);

    // Psychoacoustic visibility checkboxes (radio-button behaviour)
    auto setupPsychoBtn = [this] (juce::ToggleButton& btn, juce::Colour colour,
                                   PsychoMetric metric)
    {
        btn.setColour (juce::ToggleButton::textColourId,         juce::Colours::white);
        btn.setColour (juce::ToggleButton::tickColourId,         colour);
        btn.setColour (juce::ToggleButton::tickDisabledColourId, colour.darker (0.5f));
        btn.onClick = [this, &btn, metric]
        {
            if (btn.getToggleState())
            {
                // Deselect the other three
                for (auto* b : { &roughnessVisButton, &fluctuationVisButton,
                                 &sharpnessVisButton, &loudnessVisButton })
                    if (b != &btn)
                        b->setToggleState (false, juce::dontSendNotification);
                selectedMetric = metric;
            }
            else
            {
                selectedMetric = PsychoMetric::Off;
            }
            repaint();
        };
        addAndMakeVisible (btn);
    };
    roughnessVisButton.setToggleState   (true, juce::dontSendNotification);  // default on
    setupPsychoBtn (roughnessVisButton,   colRoughness,   PsychoMetric::Roughness);
    setupPsychoBtn (fluctuationVisButton, colFluctuation, PsychoMetric::Fluctuation);
    setupPsychoBtn (sharpnessVisButton,   colSharpness,   PsychoMetric::Sharpness);
    setupPsychoBtn (loudnessVisButton,    colLoudness,    PsychoMetric::Loudness);

    // Default: Hann window (index 0)
    for (int i = 0; i < kFftSize; ++i)
        windowCoeffs_[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                       * i / (kFftSize - 1)));
    currentWindowType_ = 0;

    startTimerHz (8);
}

LogComponent::~LogComponent() { stopTimer(); }

void LogComponent::setLightMode (bool light) noexcept
{
    if (lightMode_ == light) return;
    lightMode_ = light;

    juce::Colour textPrimary = light ? juce::Colour (0xff1c1c1e) : juce::Colours::white;
    juce::Colour textSecond  = light ? juce::Colour (0xff6c6c70) : juce::Colour (0xffaeaeb2);

    durationLabel.setColour (juce::Label::textColourId, textSecond);
    durationSlider.setColour (juce::Slider::textBoxTextColourId, textPrimary);
    for (auto* b : { &splVisButton, &dbaVisButton, &dbcVisButton,
                     &roughnessVisButton, &fluctuationVisButton,
                     &sharpnessVisButton, &loudnessVisButton })
        b->setColour (juce::ToggleButton::textColourId, textPrimary);

    repaint();
}

//==============================================================================
void LogComponent::resized()
{
    auto area = getLocalBounds().reduced (4);
    auto controlRow = area.removeFromBottom (48);

    durationLabel.setBounds  (controlRow.removeFromLeft (200));
    durationSlider.setBounds (controlRow);

    // Position SPL visibility checkboxes in the legend strip (row 1 of the plot header)
    const float marginL  = 80.0f;
    const float marginR  = 75.0f;
    const float controlH = 52.0f;
    auto graphBounds = getLocalBounds().reduced (4).withTrimmedBottom (static_cast<int> (controlH));
    const float plotX = graphBounds.getX() + marginL;
    const float plotW = graphBounds.getWidth() - marginL - marginR;
    const float btnW  = plotW / 3.0f;
    const float btnY  = graphBounds.getY();
    const float btnH  = 36.0f;
    splVisButton.setBounds (juce::Rectangle<float> (plotX,           btnY, btnW, btnH).toNearestInt());
    dbaVisButton.setBounds (juce::Rectangle<float> (plotX + btnW,    btnY, btnW, btnH).toNearestInt());
    dbcVisButton.setBounds (juce::Rectangle<float> (plotX + btnW*2,  btnY, btnW, btnH).toNearestInt());

    // Psychoacoustic checkbox row (row 2)
    const float psychoBtnY = btnY + btnH + 4.0f;
    const float psychoBtnW = plotW / 4.0f;
    roughnessVisButton.setBounds   (juce::Rectangle<float> (plotX,                  psychoBtnY, psychoBtnW, btnH).toNearestInt());
    fluctuationVisButton.setBounds (juce::Rectangle<float> (plotX + psychoBtnW,     psychoBtnY, psychoBtnW, btnH).toNearestInt());
    sharpnessVisButton.setBounds   (juce::Rectangle<float> (plotX + psychoBtnW*2,   psychoBtnY, psychoBtnW, btnH).toNearestInt());
    loudnessVisButton.setBounds    (juce::Rectangle<float> (plotX + psychoBtnW*3,   psychoBtnY, psychoBtnW, btnH).toNearestInt());
}

void LogComponent::timerCallback()
{
    if (fftEnabled_)
        computeFftBands();
    rows = processor.copyLog();
    repaint();
}

//==============================================================================
void LogComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);

    const float controlH = 52.0f;
    const juce::Rectangle<float> graphArea (
        bounds.getX(), bounds.getY(),
        bounds.getWidth(), bounds.getHeight() - controlH);

    const float marginL = 80.0f;
    const float marginR = 75.0f;   // right axis labels
    const float marginT = 120.0f;  // two checkbox rows
    const float marginB = 50.0f;

    const juce::Rectangle<float> plot (
        graphArea.getX() + marginL,
        graphArea.getY() + marginT,
        graphArea.getWidth()  - marginL - marginR,
        graphArea.getHeight() - marginT - marginB);

    const juce::Colour bgMain     = lightMode_ ? juce::Colour (0xfff2f2f7) : juce::Colour (0xff1c1c1e);
    const juce::Colour textSecond = lightMode_ ? juce::Colour (0xff48484a) : juce::Colour (0xff8e8e93);
    const juce::Colour gridColour = lightMode_ ? juce::Colour (0x25000000) : juce::Colour (0x403a3a3c);

    // ---- Background ----
    g.setColour (bgMain);
    g.fillRect (bounds);

    if (plot.getWidth() < 2.0f || plot.getHeight() < 2.0f)
        return;

    // ---- 1/3-octave FFT overlay ----
    if (fftEnabled_)
        drawFftOverlay (g, plot);

    // ---- Left Y-axis mapping (dB SPL) ----
    auto splToY = [&] (float spl) -> float {
        float t = (spl - kYMin) / (kYMax - kYMin);
        t = juce::jlimit (0.0f, 1.0f, t);
        return plot.getBottom() - t * plot.getHeight();
    };

    // ---- Right Y-axis range per metric ----
    float rightMin = 0.0f, rightMax = 100.0f;
    juce::String rightUnit;
    juce::Colour psychoColour;

    switch (selectedMetric)
    {
        case PsychoMetric::Roughness:
            rightMin = 0; rightMax = 100; rightUnit = "%";
            psychoColour = colRoughness; break;
        case PsychoMetric::Fluctuation:
            rightMin = 0; rightMax = 100; rightUnit = "%";
            psychoColour = colFluctuation; break;
        case PsychoMetric::Sharpness:
            rightMin = 0; rightMax = 5; rightUnit = "acum";
            psychoColour = colSharpness; break;
        case PsychoMetric::Loudness:
            rightMin = 0; rightMax = 100; rightUnit = "sone";
            psychoColour = colLoudness; break;
    }

    auto rightToY = [&] (float val) -> float {
        float t = (val - rightMin) / (rightMax - rightMin);
        t = juce::jlimit (0.0f, 1.0f, t);
        return plot.getBottom() - t * plot.getHeight();
    };

    // ---- Left Y-axis grid + labels ----
    g.setFont (juce::Font (juce::FontOptions().withHeight (20.0f)));
    for (float db = kYMin; db <= kYMax; db += 10.0f)
    {
        float y = splToY (db);
        g.setColour (gridColour);
        g.drawHorizontalLine (static_cast<int>(y), plot.getX(), plot.getRight());
        g.setColour (textSecond);
        g.drawText (juce::String (static_cast<int>(db)),
                    static_cast<int>(graphArea.getX()), static_cast<int>(y) - 14,
                    static_cast<int>(marginL) - 6, 28,
                    juce::Justification::centredRight, false);
    }

    // Left Y-axis label
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.setFont (juce::Font (juce::FontOptions().withHeight (20.0f).withStyle ("Bold")));
        g.setColour (textSecond);
        g.addTransform (juce::AffineTransform::rotation (
            -juce::MathConstants<float>::halfPi,
            graphArea.getX() + 18.0f, plot.getCentreY()));
        g.drawText ("dB SPL",
                    static_cast<int>(graphArea.getX() + 18.0f - 60.0f),
                    static_cast<int>(plot.getCentreY()) - 14,
                    120, 28, juce::Justification::centred, false);
    }

    // ---- Right Y-axis labels ----
    {
        g.setFont (juce::Font (juce::FontOptions().withHeight (18.0f)));
        const int numTicks = 4;
        for (int i = 0; i <= numTicks; ++i)
        {
            float val = rightMin + (rightMax - rightMin) * i / numTicks;
            float y   = rightToY (val);
            g.setColour (gridColour);
            g.drawHorizontalLine (static_cast<int>(y), plot.getX(), plot.getRight());
            g.setColour (psychoColour.withAlpha (0.8f));
            juce::String label = (rightUnit == "acum")
                                 ? juce::String (val, 1)
                                 : juce::String (static_cast<int>(val));
            g.drawText (label,
                        static_cast<int>(plot.getRight()) + 4, static_cast<int>(y) - 14,
                        static_cast<int>(marginR) - 8, 28,
                        juce::Justification::centredLeft, false);
        }

        // Right Y-axis unit label
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.setFont (juce::Font (juce::FontOptions().withHeight (18.0f).withStyle ("Bold")));
            g.setColour (psychoColour);
            float rx = plot.getRight() + marginR - 18.0f;
            g.addTransform (juce::AffineTransform::rotation (
                juce::MathConstants<float>::halfPi, rx, plot.getCentreY()));
            g.drawText (rightUnit,
                        static_cast<int>(rx - 40.0f),
                        static_cast<int>(plot.getCentreY()) - 10,
                        80, 20, juce::Justification::centred, false);
        }
    }

    // Legend strip axis labels (drawn in left margin, aligned with checkbox rows)
    {
        const float row1Y = graphArea.getY();
        const float row2Y = graphArea.getY() + 36.0f + 4.0f;
        const float labelX = graphArea.getX();
        const float labelW = marginL - 6.0f;
        const float rowH   = 36.0f;

        g.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
        g.setColour (textSecond);
        g.drawText ("Left y axis:",  static_cast<int>(labelX), static_cast<int>(row1Y),
                    static_cast<int>(labelW), static_cast<int>(rowH),
                    juce::Justification::centredRight, false);
        g.drawText ("Right y axis:", static_cast<int>(labelX), static_cast<int>(row2Y),
                    static_cast<int>(labelW), static_cast<int>(rowH),
                    juce::Justification::centredRight, false);
    }

    // Checkbox rows are rendered by the ToggleButton children

    if (rows.empty())
    {
        g.setColour (textSecond);
        g.setFont (juce::Font (juce::FontOptions().withHeight (24.0f)));
        g.drawText ("No data yet", plot.toNearestInt(), juce::Justification::centred, false);
        return;
    }

    // ---- Time range ----
    const juce::int64 tMax = rows.back().timestampMs;
    const float durationMs = processor.apvts.getRawParameterValue ("logDuration")->load() * 1000.0f;
    const juce::int64 tMin = tMax - static_cast<juce::int64> (durationMs);

    auto timeToX = [&] (juce::int64 ts) -> float {
        float t = static_cast<float>(ts - tMin) / static_cast<float>(tMax - tMin);
        t = juce::jlimit (0.0f, 1.0f, t);
        return plot.getX() + t * plot.getWidth();
    };

    // ---- X-axis labels ----
    {
        g.setFont (juce::Font (juce::FontOptions().withHeight (20.0f)));
        g.setColour (textSecond);
        const int numXTicks = 5;
        for (int i = 0; i <= numXTicks; ++i)
        {
            float frac = static_cast<float>(i) / numXTicks;
            float x    = plot.getX() + frac * plot.getWidth();
            juce::int64 ts = tMin + static_cast<juce::int64>(frac * durationMs);
            juce::String label = juce::Time (ts).toString (false, true, true, false);
            g.drawHorizontalLine (static_cast<int>(plot.getBottom()), x - 1.0f, x + 1.0f);
            g.drawText (label, static_cast<int>(x) - 70,
                        static_cast<int>(plot.getBottom()) + 26,
                        140, 22, juce::Justification::centred, false);
        }
    }

    // ---- SPL series (left axis) ----
    auto drawSeries = [&] (juce::Colour colour,
                            std::function<float(const LogEntry&)> getValue)
    {
        juce::Path path;
        bool started = false;
        for (const auto& e : rows)
        {
            float x = timeToX (e.timestampMs);
            float y = splToY (getValue (e));
            if (!started) { path.startNewSubPath (x, y); started = true; }
            else          { path.lineTo (x, y); }
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (3.0f));
    };

    if (splVisButton.getToggleState()) drawSeries (colPeakSPL, [] (const LogEntry& e) { return e.peakSPL;    });
    if (dbaVisButton.getToggleState()) drawSeries (colPeakDBA, [] (const LogEntry& e) { return e.peakDBASPL; });
    if (dbcVisButton.getToggleState()) drawSeries (colPeakDBC, [] (const LogEntry& e) { return e.peakDBCSPL; });

    // ---- Selected psychoacoustic metric (right axis) ----
    if (selectedMetric != PsychoMetric::Off)
    {
        juce::Path path;
        bool started = false;
        for (const auto& e : rows)
        {
            float val = 0.0f;
            switch (selectedMetric)
            {
                case PsychoMetric::Roughness:   val = e.roughness;    break;
                case PsychoMetric::Fluctuation: val = e.fluctuation;  break;
                case PsychoMetric::Sharpness:   val = e.sharpness;    break;
                case PsychoMetric::Loudness:    val = e.loudnessSone; break;
                default: break;
            }
            float x = timeToX (e.timestampMs);
            float y = rightToY (val);
            if (!started) { path.startNewSubPath (x, y); started = true; }
            else          { path.lineTo (x, y); }
        }
        g.setColour (psychoColour);
        g.strokePath (path, juce::PathStrokeType (2.5f));
    }

    // ---- 94 dB reference line ----
    if (processor.apvts.getRawParameterValue ("line94Enabled")->load() > 0.5f)
    {
        const float y94 = splToY (94.0f);
        const juce::Colour refRed (0xffff453a);
        g.setColour (refRed);
        // Dashed line
        float dashLen = 10.0f, gapLen = 6.0f, x = plot.getX();
        while (x < plot.getRight())
        {
            float segEnd = std::min (x + dashLen, plot.getRight());
            g.drawLine (x, y94, segEnd, y94, 1.5f);
            x += dashLen + gapLen;
        }
        // Label on the left
        g.setFont (juce::Font (juce::FontOptions().withHeight (17.0f).withStyle ("Bold")));
        g.setColour (refRed);
        g.drawText ("94 dB",
                    static_cast<int> (graphArea.getX()),
                    static_cast<int> (y94) - 12,
                    static_cast<int> (marginL) - 4, 22,
                    juce::Justification::centredRight, false);
    }
}

//==============================================================================
void LogComponent::computeFftBands()
{
    const float  fftGainDB  = processor.apvts.getRawParameterValue ("fftGain")->load();
    const float  gainLinear = std::pow (10.0f, fftGainDB / 20.0f);
    const float  calOffset  = processor.apvts.getRawParameterValue ("calOffset")->load();
    const double sampleRate = processor.getSampleRate();

    // ---- Window function (rebuild if changed) ----
    const int wType = static_cast<int> (processor.apvts.getRawParameterValue ("fftWindowType")->load());
    if (wType != currentWindowType_)
    {
        currentWindowType_ = wType;
        const float tp = juce::MathConstants<float>::twoPi;
        for (int i = 0; i < kFftSize; ++i)
        {
            const float c1 = std::cos (tp * i / (kFftSize - 1));
            const float c2 = std::cos (2.0f * tp * i / (kFftSize - 1));
            const float c3 = std::cos (3.0f * tp * i / (kFftSize - 1));
            const float c4 = std::cos (4.0f * tp * i / (kFftSize - 1));
            switch (wType)
            {
                case 1:  windowCoeffs_[i] = 0.54f - 0.46f * c1; break;                              // Hamming
                case 2:  windowCoeffs_[i] = 0.42f - 0.5f * c1 + 0.08f * c2; break;                 // Blackman
                case 3:  windowCoeffs_[i] = 0.2156f - 0.4160f*c1 + 0.2781f*c2
                                          - 0.0836f*c3 + 0.0069f*c4; break;                         // Flat-top
                case 4:  windowCoeffs_[i] = 1.0f; break;                                            // Rectangular
                default: windowCoeffs_[i] = 0.5f * (1.0f - c1); break;                              // Hann
            }
        }
    }

    // ---- Overlap: maintain fftInputHistory_ ----
    const int overlapIdx = static_cast<int> (processor.apvts.getRawParameterValue ("fftOverlap")->load());
    static constexpr int kOverlapPct[4] = { 0, 25, 50, 75 };
    const int overlap  = kOverlapPct[juce::jlimit (0, 3, overlapIdx)];
    const int hopSize  = kFftSize * (100 - overlap) / 100;  // new samples this frame
    const int keepSize = kFftSize - hopSize;

    // Get a fresh full window from the processor into a temp buffer
    std::array<float, kFftSize> tempBuf {};
    processor.copyFftWindow (tempBuf.data(), kFftSize);

    if (overlap > 0 && keepSize > 0)
    {
        // Shift history left by hopSize, fill right portion with newest samples
        std::memmove (fftInputHistory_.data(),
                      fftInputHistory_.data() + hopSize,
                      keepSize * sizeof (float));
        std::memcpy  (fftInputHistory_.data() + keepSize,
                      tempBuf.data() + keepSize,
                      hopSize * sizeof (float));
    }
    else
    {
        fftInputHistory_ = tempBuf;
    }

    // Apply window function + gain; zero imaginary half
    for (int i = 0; i < kFftSize; ++i)
        fftBuffer_[i] = fftInputHistory_[i] * windowCoeffs_[i] * gainLinear;
    std::fill (fftBuffer_.begin() + kFftSize, fftBuffer_.end(), 0.0f);

    fft_.performFrequencyOnlyForwardTransform (fftBuffer_.data(), true);
    // fftBuffer_[0..kFftSize/2] now holds magnitudes

    const float normFactor = static_cast<float> (kFftSize);

    // Determine resolution (N = subdivisions per octave)
    const int resIdx = static_cast<int> (processor.apvts.getRawParameterValue ("fftBandRes")->load());
    const int N      = kBandNForRes[juce::jlimit (0, 4, resIdx)];

    // Reset smoothed/peak arrays when resolution changes
    if (N != currentBandN_)
    {
        std::fill (fftBandsSmoothed_,   fftBandsSmoothed_   + kMaxFftBands, kYMin);
        std::fill (fftPeakBands_,       fftPeakBands_       + kMaxFftBands, kYMin);
        std::fill (fftPeakTimestamps_,  fftPeakTimestamps_  + kMaxFftBands, 0.0);
        currentBandN_ = N;
    }

    // Compute band energies dynamically
    const float stepFactor     = std::pow (2.0f, 1.0f / static_cast<float> (N));
    const float halfBandFactor = std::sqrt (stepFactor);  // = 2^(1/(2N))

    const bool rtaMode = processor.apvts.getRawParameterValue ("fftRTAMode")->load() > 0.5f;

    currentNumBands_ = 0;
    for (float fc = 20.0f; fc <= 20000.0f && currentNumBands_ < kMaxFftBands; fc *= stepFactor)
    {
        const int b    = currentNumBands_++;
        const int binLo = std::max (1,            static_cast<int> (fc / halfBandFactor * kFftSize / sampleRate));
        const int binHi = std::min (kFftSize / 2, static_cast<int> (fc * halfBandFactor * kFftSize / sampleRate) + 1);

        float peak = 0.0f;
        for (int k = binLo; k < binHi; ++k)
            peak = std::max (peak, fftBuffer_[k]);

        float dbSPL = 20.0f * std::log10 (peak / normFactor + 1e-10f) + calOffset;
        if (rtaMode)
            dbSPL += 3.0f * std::log2 (fc / 20.0f);   // +3 dB/oct relative to 20 Hz
        fftBands_[b] = juce::jlimit (kYMin, kYMax, dbSPL);
    }

    // Smoothing + peak hold
    const float  alpha      = processor.apvts.getRawParameterValue ("fftSmoothing")->load();
    const bool   peakHoldOn = processor.apvts.getRawParameterValue ("fftPeakHold")->load() > 0.5f;
    const float  holdSecs   = processor.apvts.getRawParameterValue ("peakHoldTime")->load();
    const double nowMs      = juce::Time::getMillisecondCounterHiRes();

    for (int b = 0; b < currentNumBands_; ++b)
    {
        fftBandsSmoothed_[b] = alpha * fftBandsSmoothed_[b] + (1.0f - alpha) * fftBands_[b];

        if (peakHoldOn)
        {
            if (fftBandsSmoothed_[b] >= fftPeakBands_[b]
                || (nowMs - fftPeakTimestamps_[b]) > holdSecs * 1000.0)
            {
                fftPeakBands_[b]      = fftBandsSmoothed_[b];
                fftPeakTimestamps_[b] = nowMs;
            }
        }
        else
        {
            fftPeakBands_[b] = kYMin;
        }
    }
}

void LogComponent::drawFftOverlay (juce::Graphics& g, const juce::Rectangle<float>& plot)
{
    if (currentNumBands_ < 1) return;

    const int resIdx = static_cast<int> (processor.apvts.getRawParameterValue ("fftBandRes")->load());
    const int N      = kBandNForRes[juce::jlimit (0, 4, resIdx)];

    const float stepFactor     = std::pow (2.0f, 1.0f / static_cast<float> (N));
    const float halfBandFactor = std::sqrt (stepFactor);

    // Fixed 20–20 kHz log-scale X mapping
    const float fMin = 20.0f;
    const float fMax = 20000.0f;

    auto freqToX = [&] (float freq) -> float
    {
        float t = std::log2 (freq / fMin) / std::log2 (fMax / fMin);
        return plot.getX() + juce::jlimit (0.0f, 1.0f, t) * plot.getWidth();
    };

    auto splToY = [&] (float spl) -> float
    {
        float t = (spl - kYMin) / (kYMax - kYMin);
        return plot.getBottom() - juce::jlimit (0.0f, 1.0f, t) * plot.getHeight();
    };

    const int  displayMode = static_cast<int> (processor.apvts.getRawParameterValue ("fftDisplayMode")->load());
    const bool peakHoldOn  = processor.apvts.getRawParameterValue ("fftPeakHold")->load() > 0.5f;
    const int  numBands    = currentNumBands_;

    const juce::Colour colFill    { 0xaa34c759 };
    const juce::Colour colSolid   { 0xff34c759 };
    const juce::Colour colPeak    { 0xffffcc00 };  // amber peak line

    // Iterate bands: fc = 20 * stepFactor^b
    float fc0 = 20.0f;

    if (displayMode == 1)  // ---- Area ----
    {
        juce::Path area;
        float fc = fc0;
        for (int b = 0; b < numBands; ++b, fc *= stepFactor)
        {
            float x = freqToX (fc);
            float y = splToY (fftBandsSmoothed_[b]);
            if (b == 0) area.startNewSubPath (x, y);
            else        area.lineTo (x, y);
        }
        float fcLast = fc0 * std::pow (stepFactor, static_cast<float> (numBands - 1));
        area.lineTo (freqToX (fcLast * halfBandFactor), plot.getBottom());
        area.lineTo (freqToX (fc0    / halfBandFactor), plot.getBottom());
        area.closeSubPath();

        g.setColour (colFill);
        g.fillPath (area);
        g.setColour (colSolid);
        g.strokePath (area, juce::PathStrokeType (2.0f));
    }
    else  // ---- Bars  /  Bars+Peak ----
    {
        float fc = fc0;
        for (int b = 0; b < numBands; ++b, fc *= stepFactor)
        {
            float x1   = freqToX (fc / halfBandFactor);
            float x2   = freqToX (fc * halfBandFactor);
            float yBot = plot.getBottom();
            float yTop = std::min (splToY (fftBandsSmoothed_[b]), yBot - 3.0f);

            if (x2 - x1 < 1.0f) continue;

            g.setColour (colFill);
            g.fillRect (x1, yTop, x2 - x1 - 1.0f, yBot - yTop);
            g.setColour (colSolid);
            g.fillRect (x1, yTop, x2 - x1 - 1.0f, 2.0f);

            if (displayMode == 2 && peakHoldOn)
            {
                float yPeak = splToY (fftPeakBands_[b]);
                g.setColour (colPeak);
                g.fillRect (x1, yPeak, x2 - x1 - 1.0f, 2.0f);
            }
        }
    }

    // ---- Graph Overlay: dashed blue frequency-response curve ----
    if (processor.apvts.getRawParameterValue ("graphOverlayEnabled")->load() > 0.5f
        && processor.isGraphOverlayLoaded())
    {
        const auto& pts = processor.getGraphOverlayPoints();
        if (pts.size() >= 2)
        {
            auto interpSPL = [&] (float freq) -> float
            {
                if (freq <= pts.front().first) return pts.front().second;
                if (freq >= pts.back().first)  return pts.back().second;
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                {
                    const float f0 = pts[i].first, f1 = pts[i + 1].first;
                    if (freq >= f0 && freq <= f1)
                    {
                        const float t = (f1 > f0)
                            ? std::log (freq / f0) / std::log (f1 / f0) : 0.0f;
                        return pts[i].second + t * (pts[i + 1].second - pts[i].second);
                    }
                }
                return 0.0f;
            };

            auto xToFreq = [&] (float px) -> float
            {
                float frac = (px - plot.getX()) / plot.getWidth();
                return fMin * std::pow (fMax / fMin, frac);
            };

            g.setColour (juce::Colour (0xff5ac8fa));  // blue
            const float dashLen = 10.0f, gapLen = 6.0f;
            float x = plot.getX();
            while (x < plot.getRight())
            {
                float dashEnd = std::min (x + dashLen, plot.getRight());
                float y0 = splToY (interpSPL (xToFreq (x)));
                float y1 = splToY (interpSPL (xToFreq (dashEnd)));
                g.drawLine (x, y0, dashEnd, y1, 2.0f);
                x += dashLen + gapLen;
            }
        }
    }

    // ---- Frequency axis: grid lines + labels ----
    {
        const juce::Colour gridCol  = lightMode_ ? juce::Colour (0x25000000) : juce::Colour (0x403a3a3c);
        const juce::Colour labelCol = lightMode_ ? juce::Colour (0xff48484a) : juce::Colour (0xff8e8e93);

        const float labelFreqs[] = { 20.f, 50.f, 100.f, 200.f, 500.f,
                                     1000.f, 2000.f, 5000.f, 10000.f, 20000.f };

        g.setFont (juce::Font (juce::FontOptions().withHeight (15.0f)));

        for (float freq : labelFreqs)
        {
            float x = freqToX (freq);

            // Vertical grid line across the plot
            g.setColour (gridCol);
            g.drawVerticalLine (static_cast<int> (x), plot.getY(), plot.getBottom());

            // Tick mark + label below plot
            g.setColour (labelCol);
            g.drawVerticalLine (static_cast<int> (x), plot.getBottom(), plot.getBottom() + 5.0f);

            juce::String label = freq < 1000.f
                                 ? juce::String (static_cast<int> (freq))
                                 : juce::String (static_cast<int> (freq / 1000.f)) + "k";
            g.drawText (label,
                        static_cast<int> (x) - 20, static_cast<int> (plot.getBottom()) + 6,
                        40, 20, juce::Justification::centred, false);
        }

        // "Hz" axis unit at the far right
        g.setColour (labelCol);
        g.drawText ("Hz",
                    static_cast<int> (plot.getRight()) + 4,
                    static_cast<int> (plot.getBottom()) + 6,
                    36, 20, juce::Justification::centredLeft, false);
    }
}


