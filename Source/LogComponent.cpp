#include "LogComponent.h"

const float LogComponent::kFftBandCenters[LogComponent::kNumFftBands] = {
    20.f, 25.f, 31.5f, 40.f, 50.f, 63.f, 80.f, 100.f, 125.f, 160.f,
    200.f, 250.f, 315.f, 400.f, 500.f, 630.f, 800.f, 1000.f, 1250.f, 1600.f,
    2000.f, 2500.f, 3150.f, 4000.f, 5000.f, 6300.f, 8000.f, 10000.f, 12500.f, 16000.f, 20000.f
};

// SPL series colours
const juce::Colour LogComponent::colPeakSPL  { 0xff5ac8fa };  // blue
const juce::Colour LogComponent::colRmsSPL   { 0xff34c759 };  // green
const juce::Colour LogComponent::colPeakDBA  { 0xffff9500 };  // orange
const juce::Colour LogComponent::colRmsDBA   { 0xffbf5af2 };  // purple
const juce::Colour LogComponent::colPeakDBC  { 0xffff2d55 };  // red
const juce::Colour LogComponent::colRmsDBC   { 0xffffe620 };  // yellow

// Psychoacoustic series colours
const juce::Colour LogComponent::colRoughness   { 0xff34eba1 };  // mint
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

    for (int i = 0; i < kFftSize; ++i)
        hannWindow_[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                    * i / (kFftSize - 1)));

    startTimerHz (8);
}

LogComponent::~LogComponent() { stopTimer(); }

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
}

void LogComponent::timerCallback()
{
    if (fftEnabled_)
        computeFftBands();
    rows = processor.copyLog();
    repaint();
}

void LogComponent::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 5; ++i)
    {
        if (metricSelectRects[i].contains (e.position))
        {
            PsychoMetric next = static_cast<PsychoMetric> (i);
            if (next == PsychoMetric::Off)
                processor.clearLog();
            selectedMetric = next;
            repaint();
            return;
        }
    }
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
    const float marginT = 80.0f;   // two legend rows
    const float marginB = 50.0f;

    const juce::Rectangle<float> plot (
        graphArea.getX() + marginL,
        graphArea.getY() + marginT,
        graphArea.getWidth()  - marginL - marginR,
        graphArea.getHeight() - marginT - marginB);

    // ---- Background ----
    g.setColour (juce::Colour (0xff1c1c1e));
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
        g.setColour (juce::Colour (0x403a3a3c));
        g.drawHorizontalLine (static_cast<int>(y), plot.getX(), plot.getRight());
        g.setColour (juce::Colour (0xff8e8e93));
        g.drawText (juce::String (static_cast<int>(db)),
                    static_cast<int>(graphArea.getX()), static_cast<int>(y) - 14,
                    static_cast<int>(marginL) - 6, 28,
                    juce::Justification::centredRight, false);
    }

    // Left Y-axis label
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.setFont (juce::Font (juce::FontOptions().withHeight (20.0f).withStyle ("Bold")));
        g.setColour (juce::Colour (0xff8e8e93));
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
            g.setColour (juce::Colour (0x403a3a3c));
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

    // Legend strip row 1 is rendered by the ToggleButton children (splVisButton etc.)

    // ---- Legend strip row 2: psychoacoustic metric selectors ----
    {
        const float rowY = graphArea.getY() + 42.0f;
        const float rowH = 32.0f;

        struct MetricInfo { PsychoMetric metric; juce::Colour colour; const char* name; };
        const MetricInfo metrics[] = {
            { PsychoMetric::Roughness,   colRoughness,           "Roughness"   },
            { PsychoMetric::Fluctuation, colFluctuation,         "Fluctuation" },
            { PsychoMetric::Sharpness,   colSharpness,           "Sharpness"   },
            { PsychoMetric::Loudness,    colLoudness,            "Loudness"    },
            { PsychoMetric::Off,         juce::Colour(0xffff453a), "OFF"       },
        };

        const float slotW = plot.getWidth() / 5.0f;
        g.setFont (juce::Font (juce::FontOptions().withHeight (20.0f).withStyle ("Bold")));

        for (int i = 0; i < 5; ++i)
        {
            float x = plot.getX() + i * slotW;
            metricSelectRects[i] = { x, rowY, slotW, rowH };

            bool selected = (selectedMetric == metrics[i].metric);
            juce::Colour textCol = selected ? juce::Colour (0xffffd60a) : juce::Colour (0xff8e8e93);

            g.setColour (textCol);
            g.drawText (metrics[i].name,
                        static_cast<int>(x), static_cast<int>(rowY),
                        static_cast<int>(slotW), static_cast<int>(rowH),
                        juce::Justification::centred, false);

            if (selected)
            {
                // Underline: draw a line under the text
                float textW = g.getCurrentFont().getStringWidthFloat (metrics[i].name);
                float underX = x + (slotW - textW) * 0.5f;
                float underY = rowY + rowH - 3.0f;
                g.setColour (juce::Colour (0xffffd60a));
                g.fillRect (underX, underY, textW, 2.0f);
            }
        }
    }

    if (rows.empty())
    {
        g.setColour (juce::Colour (0xff8e8e93));
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
        g.setColour (juce::Colour (0xff8e8e93));
        const int numXTicks = 5;
        for (int i = 0; i <= numXTicks; ++i)
        {
            float frac = static_cast<float>(i) / numXTicks;
            float x    = plot.getX() + frac * plot.getWidth();
            juce::int64 ts = tMin + static_cast<juce::int64>(frac * durationMs);
            juce::String label = juce::Time (ts).toString (false, true, true, false);
            g.drawHorizontalLine (static_cast<int>(plot.getBottom()), x - 1.0f, x + 1.0f);
            g.drawText (label, static_cast<int>(x) - 70,
                        static_cast<int>(plot.getBottom()) + 4,
                        140, 28, juce::Justification::centred, false);
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
}

//==============================================================================
void LogComponent::computeFftBands()
{
    processor.copyFftWindow (fftBuffer_.data(), kFftSize);

    const float fftGainDB   = processor.apvts.getRawParameterValue ("fftGain")->load();
    const float gainLinear  = std::pow (10.0f, fftGainDB / 20.0f);
    const float calOffset   = processor.apvts.getRawParameterValue ("calOffset")->load();
    const double sampleRate = processor.getSampleRate();

    // Apply Hann window + gain; zero imaginary half
    for (int i = 0; i < kFftSize; ++i)
        fftBuffer_[i] = fftBuffer_[i] * hannWindow_[i] * gainLinear;
    std::fill (fftBuffer_.begin() + kFftSize, fftBuffer_.end(), 0.0f);

    fft_.performFrequencyOnlyForwardTransform (fftBuffer_.data(), true);
    // fftBuffer_[0..kFftSize/2] now holds magnitudes

    const float bandFactor = std::pow (2.0f, 1.0f / 6.0f);  // 2^(1/6)
    const float normFactor = static_cast<float> (kFftSize); // matches JUCE SimpleFFT convention

    for (int b = 0; b < kNumFftBands; ++b)
    {
        float fc   = kFftBandCenters[b];
        int binLo  = std::max (1,            static_cast<int> (fc / bandFactor * kFftSize / sampleRate));
        int binHi  = std::min (kFftSize / 2, static_cast<int> (fc * bandFactor * kFftSize / sampleRate) + 1);

        float peak = 0.0f;
        for (int k = binLo; k < binHi; ++k)
            peak = std::max (peak, fftBuffer_[k]);

        float dbSPL = 20.0f * std::log10 (peak / normFactor + 1e-10f) + calOffset;
        fftBands_[b] = juce::jlimit (kYMin, kYMax, dbSPL);
    }
}

void LogComponent::drawFftOverlay (juce::Graphics& g, const juce::Rectangle<float>& plot)
{
    const float bandFactor = std::pow (2.0f, 1.0f / 6.0f);
    const float fMin = kFftBandCenters[0]              / bandFactor;
    const float fMax = kFftBandCenters[kNumFftBands-1] * bandFactor;

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

    for (int b = 0; b < kNumFftBands; ++b)
    {
        float fc  = kFftBandCenters[b];
        float x1   = freqToX (fc / bandFactor);
        float x2   = freqToX (fc * bandFactor);
        float yBot = plot.getBottom();
        float yTop = std::min (splToY (fftBands_[b]), yBot - 3.0f);   // min 3 px bar

        if (x2 - x1 < 1.0f) continue;

        // Bar fill
        g.setColour (juce::Colour (0xaa34c759));   // 67% opaque green
        g.fillRect (x1, yTop, x2 - x1 - 1.0f, yBot - yTop);

        // Top highlight line
        g.setColour (juce::Colour (0xff34c759));   // fully opaque highlight
        g.fillRect (x1, yTop, x2 - x1 - 1.0f, 2.0f);
    }
}

