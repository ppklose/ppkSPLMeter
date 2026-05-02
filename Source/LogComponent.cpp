#include "LogComponent.h"
#include "SettingsComponent.h"

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
const juce::Colour LogComponent::colRoughness      { 0xff00c7be };  // teal
const juce::Colour LogComponent::colFluctuation    { 0xffeb34b1 };  // pink
const juce::Colour LogComponent::colSharpness      { 0xffc77dff };  // violet
const juce::Colour LogComponent::colLoudness       { 0xffffe57f };  // gold
const juce::Colour LogComponent::colAnnoyance      { 0xffff6b6b };  // coral red
const juce::Colour LogComponent::colImpulsiveness  { 0xffff9f0a };  // amber
const juce::Colour LogComponent::colTonality       { 0xff30d158 };  // lime green

//==============================================================================
LogComponent::LogComponent (SPLMeterAudioProcessor& p)
    : processor (p)
{
    durationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "logDuration", durationSlider);

    // SPL series visibility checkboxes
    auto setupVisBtn = [this] (juce::ToggleButton& btn, juce::Colour colour, bool defaultOn)
    {
        btn.setToggleState (defaultOn, juce::dontSendNotification);
        btn.setColour (juce::ToggleButton::textColourId,         juce::Colours::white);
        btn.setColour (juce::ToggleButton::tickColourId,         colour);
        btn.setColour (juce::ToggleButton::tickDisabledColourId, colour.darker (0.5f));
        btn.onClick = [this] { repaint(); };
        addAndMakeVisible (btn);
    };
    // First-launch defaults: only dBA visible; psychoacoustic overlay off.
    setupVisBtn (splVisButton, colPeakSPL, false);
    setupVisBtn (dbaVisButton, colPeakDBA, true);
    setupVisBtn (dbcVisButton, colPeakDBC, false);

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
                // Deselect the others
                for (auto* b : { &roughnessVisButton, &fluctuationVisButton,
                                 &sharpnessVisButton, &loudnessVisButton,
                                 &annoyanceVisButton, &impulsivenessVisButton,
                                 &tonalityVisButton })
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
    // First-launch default: psychoacoustic overlay off.
    setupPsychoBtn (roughnessVisButton,      colRoughness,      PsychoMetric::Roughness);
    setupPsychoBtn (fluctuationVisButton,    colFluctuation,    PsychoMetric::Fluctuation);
    setupPsychoBtn (sharpnessVisButton,      colSharpness,      PsychoMetric::Sharpness);
    setupPsychoBtn (loudnessVisButton,       colLoudness,       PsychoMetric::Loudness);
    setupPsychoBtn (annoyanceVisButton,      colAnnoyance,      PsychoMetric::Annoyance);
    setupPsychoBtn (impulsivenessVisButton,  colImpulsiveness,  PsychoMetric::Impulsiveness);
    setupPsychoBtn (tonalityVisButton,       colTonality,       PsychoMetric::Tonality);

    // Y-axis zoom button (magnifying glass — opens SPL range panel)
    yZoomButton_.setButtonText ("");
    yZoomButton_.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    yZoomButton_.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    yZoomButton_.onClick = [this]
    {
        auto panel = std::make_unique<SplRangePanel> (processor);
        juce::CallOutBox::launchAsynchronously (
            std::move (panel),
            yZoomButton_.getScreenBounds(),
            nullptr);
    };
    addAndMakeVisible (yZoomButton_);

    // X-axis zoom button (magnifying glass — opens log duration panel)
    xZoomButton_.setButtonText ("");
    xZoomButton_.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    xZoomButton_.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    xZoomButton_.onClick = [this]
    {
        auto panel = std::make_unique<DurationPanel> (processor);
        juce::CallOutBox::launchAsynchronously (
            std::move (panel),
            xZoomButton_.getScreenBounds(),
            nullptr);
    };
    addAndMakeVisible (xZoomButton_);

    // Right Y-axis zoom button (magnifying glass — opens psychoacoustic range panel)
    rightZoomButton_.setButtonText ("");
    rightZoomButton_.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    rightZoomButton_.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    rightZoomButton_.onClick = [this]
    {
        // Map selected metric → param prefix, label, unit
        struct MetricInfo { const char* prefix; const char* label; const char* unit; };
        const MetricInfo info[] = {
            { "roughness",     "Roughness",      "%"    },
            { "fluctuation",   "Fluctuation",    "%"    },
            { "sharpness",     "Sharpness",      "acum" },
            { "loudness",      "Loudness",       "sone" },
            { "annoyance",     "Annoyance",      "PA"   },
            { "impulsiveness", "Impulsiveness",  "dB"   },
            { "tonality",      "Tonality",       "%"    },
        };
        int idx = static_cast<int> (selectedMetric);
        if (idx < 0 || idx >= 7) return;
        const auto& m = info[idx];
        auto panel = std::make_unique<PsychoRangePanel> (
            processor, m.prefix, m.label, m.unit);
        juce::CallOutBox::launchAsynchronously (
            std::move (panel),
            rightZoomButton_.getScreenBounds(),
            nullptr);
    };
    addAndMakeVisible (rightZoomButton_);

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
                     &sharpnessVisButton, &loudnessVisButton,
                     &annoyanceVisButton, &impulsivenessVisButton,
                     &tonalityVisButton })
        b->setColour (juce::ToggleButton::textColourId, textPrimary);

    repaint();
}

//==============================================================================
void LogComponent::resized()
{
    // Uniform 4-column grid for all checkbox rows
    const float marginL  = 80.0f;
    const float marginR  = 75.0f;
    const float controlH = 0.0f;
    auto graphBounds = getLocalBounds().reduced (4).withTrimmedBottom (static_cast<int> (controlH));
    const float plotX  = graphBounds.getX() + marginL;
    const float plotW  = graphBounds.getWidth() - marginL - marginR;
    const float colW   = plotW / 4.0f;
    const float btnY   = graphBounds.getY();
    const float btnH   = 32.0f;
    const float rowGap = 4.0f;

    // Row 1 — SPL series (3 of 4 columns)
    splVisButton.setBounds (juce::Rectangle<float> (plotX,          btnY, colW, btnH).toNearestInt());
    dbaVisButton.setBounds (juce::Rectangle<float> (plotX + colW,   btnY, colW, btnH).toNearestInt());
    dbcVisButton.setBounds (juce::Rectangle<float> (plotX + colW*2, btnY, colW, btnH).toNearestInt());

    // Row 2 — psychoacoustic (4 columns)
    const float row2Y = btnY + btnH + rowGap;
    roughnessVisButton.setBounds   (juce::Rectangle<float> (plotX,          row2Y, colW, btnH).toNearestInt());
    fluctuationVisButton.setBounds (juce::Rectangle<float> (plotX + colW,   row2Y, colW, btnH).toNearestInt());
    sharpnessVisButton.setBounds   (juce::Rectangle<float> (plotX + colW*2, row2Y, colW, btnH).toNearestInt());
    loudnessVisButton.setBounds    (juce::Rectangle<float> (plotX + colW*3, row2Y, colW, btnH).toNearestInt());

    // Row 3 — psychoacoustic continued (3 of 4 columns)
    const float row3Y = row2Y + btnH + rowGap;
    annoyanceVisButton.setBounds     (juce::Rectangle<float> (plotX,          row3Y, colW, btnH).toNearestInt());
    impulsivenessVisButton.setBounds (juce::Rectangle<float> (plotX + colW,   row3Y, colW, btnH).toNearestInt());
    tonalityVisButton.setBounds      (juce::Rectangle<float> (plotX + colW*2, row3Y, colW, btnH).toNearestInt());

    // Shared geometry for zoom buttons
    {
        const float marginT  = 116.0f;
        const float marginB  = 50.0f;
        auto fullArea  = getLocalBounds().toFloat().reduced (4.0f);
        float graphH   = fullArea.getHeight() - controlH;
        float plotBottom  = fullArea.getY() + graphH - marginB;
        float plotCentreY = fullArea.getY() + marginT + (graphH - marginT - marginB) / 2.0f;
        float plotCentreX = fullArea.getX() + marginL
                            + (fullArea.getWidth() - marginL - marginR) / 2.0f;

        // Y-axis zoom button: just below the rotated "dB SPL" label
        float labelX = fullArea.getX() + 18.0f;
        yZoomButton_.setBounds (static_cast<int> (labelX - 14.0f),
                                static_cast<int> (plotCentreY + 64.0f),
                                28, 28);

        // X-axis zoom button: centred horizontally in the bottom margin
        xZoomButton_.setBounds (static_cast<int> (plotCentreX - 14.0f),
                                static_cast<int> (plotBottom + 8.0f),
                                28, 28);

        // Right Y-axis zoom button: just below the rotated unit label
        float rx = fullArea.getX() + fullArea.getWidth() - marginR + marginR - 18.0f;
        rightZoomButton_.setBounds (static_cast<int> (rx - 14.0f),
                                    static_cast<int> (plotCentreY + 50.0f),
                                    28, 28);
    }
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

    const juce::Rectangle<float> graphArea (
        bounds.getX(), bounds.getY(),
        bounds.getWidth(), bounds.getHeight());

    const float marginL = 80.0f;
    const float marginR = 75.0f;   // right axis labels
    const float marginT = 116.0f;  // three checkbox rows (3×32 + 2×4 = 104, +12 pad)
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
    const float yDispMin = processor.apvts.getRawParameterValue ("splYMin")->load();
    const float yDispMax = processor.apvts.getRawParameterValue ("splYMax")->load();

    auto splToY = [&] (float spl) -> float {
        float t = (spl - yDispMin) / (yDispMax - yDispMin);
        t = juce::jlimit (0.0f, 1.0f, t);
        return plot.getBottom() - t * plot.getHeight();
    };

    // ---- Right Y-axis range per metric (user-adjustable) ----
    juce::String rightParamPrefix;
    juce::String rightUnit;
    juce::Colour psychoColour;

    switch (selectedMetric)
    {
        case PsychoMetric::Roughness:     rightParamPrefix = "roughness";     rightUnit = "%";    psychoColour = colRoughness;      break;
        case PsychoMetric::Fluctuation:   rightParamPrefix = "fluctuation";   rightUnit = "%";    psychoColour = colFluctuation;    break;
        case PsychoMetric::Sharpness:     rightParamPrefix = "sharpness";     rightUnit = "acum"; psychoColour = colSharpness;      break;
        case PsychoMetric::Loudness:      rightParamPrefix = "loudness";      rightUnit = "sone"; psychoColour = colLoudness;       break;
        case PsychoMetric::Annoyance:     rightParamPrefix = "annoyance";     rightUnit = "PA";   psychoColour = colAnnoyance;      break;
        case PsychoMetric::Impulsiveness: rightParamPrefix = "impulsiveness"; rightUnit = "dB";   psychoColour = colImpulsiveness;  break;
        case PsychoMetric::Tonality:      rightParamPrefix = "tonality";      rightUnit = "%";    psychoColour = colTonality;       break;
        default: break;
    }

    float rightMin = 0.0f, rightMax = 100.0f;
    if (rightParamPrefix.isNotEmpty())
    {
        rightMin = processor.apvts.getRawParameterValue (rightParamPrefix + "YMin")->load();
        rightMax = processor.apvts.getRawParameterValue (rightParamPrefix + "YMax")->load();
        if (rightMax <= rightMin) rightMax = rightMin + 1.0f;
    }

    auto rightToY = [&] (float val) -> float {
        float t = (val - rightMin) / (rightMax - rightMin);
        t = juce::jlimit (0.0f, 1.0f, t);
        return plot.getBottom() - t * plot.getHeight();
    };

    // ---- Left Y-axis grid + labels ----
    g.setFont (juce::Font (juce::FontOptions().withHeight (20.0f)));
    {
        // Draw ticks at every 10 dB within the current display range
        float firstTick = std::ceil (yDispMin / 10.0f) * 10.0f;
        for (float db = firstTick; db <= yDispMax + 0.5f; db += 10.0f)
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
    }

    // Draw magnifying glass icon on the zoom button
    {
        auto ib = yZoomButton_.getBounds().toFloat().reduced (3.0f);
        const float r  = ib.getWidth() * 0.32f;
        const float cx = ib.getCentreX() - r * 0.15f;
        const float cy = ib.getCentreY() - r * 0.15f;
        const float angle = juce::MathConstants<float>::pi * 0.785f;
        g.setColour (textSecond);
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
        g.drawLine (cx + std::cos (angle) * r, cy + std::sin (angle) * r,
                    cx + std::cos (angle) * r * 1.9f, cy + std::sin (angle) * r * 1.9f, 1.5f);
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
            const float range = rightMax - rightMin;
            juce::String label = (range < 10.0f)
                                 ? juce::String (val, 1)
                                 : juce::String (static_cast<int>(val + 0.5f));
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
        // Rows: same geometry as resized() — 32px tall, 4px gap
        const float row1Y  = graphArea.getY();
        const float row2Y  = graphArea.getY() + 32.0f + 4.0f;
        const float row3Y  = graphArea.getY() + 72.0f + 4.0f;
        const float labelX = graphArea.getX();
        const float labelW = marginL - 6.0f;
        const float rowH   = 32.0f;

        g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
        g.setColour (textSecond);
        g.drawText ("Left axis:",  static_cast<int>(labelX), static_cast<int>(row1Y),
                    static_cast<int>(labelW), static_cast<int>(rowH),
                    juce::Justification::centredRight, false);
        g.drawText ("Right axis:", static_cast<int>(labelX), static_cast<int>(row2Y),
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

    // Draw magnifying glass icon on the x-axis zoom button
    {
        auto ib = xZoomButton_.getBounds().toFloat().reduced (3.0f);
        const float r  = ib.getWidth() * 0.32f;
        const float cx = ib.getCentreX() - r * 0.15f;
        const float cy = ib.getCentreY() - r * 0.15f;
        const float angle = juce::MathConstants<float>::pi * 0.785f;
        g.setColour (textSecond);
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
        g.drawLine (cx + std::cos (angle) * r, cy + std::sin (angle) * r,
                    cx + std::cos (angle) * r * 1.9f, cy + std::sin (angle) * r * 1.9f, 1.5f);
    }

    // Draw magnifying glass icon on the right Y-axis zoom button
    if (selectedMetric != PsychoMetric::Off)
    {
        auto ib = rightZoomButton_.getBounds().toFloat().reduced (3.0f);
        const float r  = ib.getWidth() * 0.32f;
        const float cx = ib.getCentreX() - r * 0.15f;
        const float cy = ib.getCentreY() - r * 0.15f;
        const float angle = juce::MathConstants<float>::pi * 0.785f;
        g.setColour (psychoColour.withAlpha (0.8f));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
        g.drawLine (cx + std::cos (angle) * r, cy + std::sin (angle) * r,
                    cx + std::cos (angle) * r * 1.9f, cy + std::sin (angle) * r * 1.9f, 1.5f);
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
                case PsychoMetric::Roughness:      val = e.roughness;        break;
                case PsychoMetric::Fluctuation:    val = e.fluctuation;      break;
                case PsychoMetric::Sharpness:      val = e.sharpness;        break;
                case PsychoMetric::Loudness:       val = e.loudnessSone;     break;
                case PsychoMetric::Annoyance:      val = e.psychoAnnoyance;  break;
                case PsychoMetric::Impulsiveness:  val = e.impulsiveness;    break;
                case PsychoMetric::Tonality:       val = e.tonality;         break;
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

    // ---- Sound event markers ----
    {
        const juce::Colour flagBg   (0xccff453a);
        const juce::Colour flagText (juce::Colours::white);
        const float dashLen = 4.0f, gapLen = 3.0f;
        const float flagH   = 14.0f;
        const float flagPad = 4.0f;

        g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));

        // Keep a set of already-drawn x positions to avoid label overlap
        std::vector<float> usedFlagX;

        for (const auto& ev : soundEvents_)
        {
            if (ev.timestampMs < tMin || ev.timestampMs > tMax) continue;

            float x = timeToX (ev.timestampMs);

            // Dashed red vertical line spanning the full plot height
            g.setColour (flagBg);
            float y = plot.getY();
            while (y < plot.getBottom())
            {
                float segEnd = std::min (y + dashLen, plot.getBottom());
                g.drawLine (x, y, x, segEnd, 1.5f);
                y += dashLen + gapLen;
            }

            // Flag: position it just above the plot, avoiding overlap with neighbours
            float approxLabelW = std::min (
                110.0f,
                g.getCurrentFont().getStringWidthFloat (ev.label) + 2.0f * flagPad);
            float flagX = x - approxLabelW * 0.5f;
            // Push right if overlapping a previous flag
            for (float used : usedFlagX)
                if (std::fabs (flagX - used) < approxLabelW + 2.0f)
                    flagX = used + approxLabelW + 3.0f;
            flagX = juce::jlimit (plot.getX(), plot.getRight() - approxLabelW, flagX);
            usedFlagX.push_back (flagX);

            const float flagY = plot.getY() - flagH - 2.0f;

            g.setColour (flagBg);
            g.fillRoundedRectangle (flagX, flagY, approxLabelW, flagH, 2.0f);

            // Small downward stem from flag to plot top
            g.drawLine (x, flagY + flagH, x, plot.getY(), 1.5f);

            g.setColour (flagText);
            g.drawText (ev.label,
                        (int) flagX, (int) flagY, (int) approxLabelW, (int) flagH,
                        juce::Justification::centred, true);
        }
    }

    // ---- Pause event markers (yellow dashed line + flag) ----
    {
        const juce::Colour pauseCol  (0xffffcc00);   // yellow
        const juce::Colour pauseText (0xff1c1c1e);   // near-black text
        const float dashLen = 4.0f, gapLen = 3.0f;
        const float flagH   = 14.0f, flagPad = 4.0f;

        g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));

        std::vector<float> usedFlagX;

        for (const auto& ev : pauseEvents_)
        {
            if (ev.startMs < tMin || ev.startMs > tMax) continue;

            float x = timeToX (ev.startMs);

            // Yellow dashed vertical line spanning full plot height
            g.setColour (pauseCol);
            float y = plot.getY();
            while (y < plot.getBottom())
            {
                float segEnd = std::min (y + dashLen, plot.getBottom());
                g.drawLine (x, y, x, segEnd, 1.5f);
                y += dashLen + gapLen;
            }

            // Build label: "⏸ Xs" or "⏸ M:SS"
            const juce::int64 totalSecs = ev.durationMs / 1000;
            juce::String label;
            if (totalSecs < 60)
                label = juce::String (totalSecs) + "s pause";
            else
                label = juce::String (totalSecs / 60) + ":"
                      + juce::String (totalSecs % 60).paddedLeft ('0', 2) + " pause";

            // Flag: inside the top of the plot to avoid overlapping SoundDetective flags above
            float approxLabelW = std::min (110.0f, g.getCurrentFont().getStringWidthFloat (label) + 2.0f * flagPad);
            float flagX = x - approxLabelW * 0.5f;
            for (float used : usedFlagX)
                if (std::fabs (flagX - used) < approxLabelW + 2.0f)
                    flagX = used + approxLabelW + 3.0f;
            flagX = juce::jlimit (plot.getX(), plot.getRight() - approxLabelW, flagX);
            usedFlagX.push_back (flagX);

            const float flagY = plot.getY() + 2.0f;

            g.setColour (pauseCol);
            g.fillRoundedRectangle (flagX, flagY, approxLabelW, flagH, 2.0f);

            g.setColour (pauseText);
            g.drawText (label,
                        (int) flagX, (int) flagY, (int) approxLabelW, (int) flagH,
                        juce::Justification::centred, true);
        }
    }

    // ---- User marker events (red diamond + dashed red vertical line) ----
    {
        const juce::Colour markerCol (0xffff453a);    // red
        const float dashLen = 4.0f, gapLen = 3.0f;
        const float diamondR = 6.0f;

        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));

        std::vector<float> usedFlagX;

        for (const auto& mk : markerEvents_)
        {
            if (mk.timestampMs < tMin || mk.timestampMs > tMax) continue;

            float x = timeToX (mk.timestampMs);

            // Dashed red vertical line spanning full plot height
            g.setColour (markerCol);
            float y = plot.getY();
            while (y < plot.getBottom())
            {
                float segEnd = std::min (y + dashLen, plot.getBottom());
                g.drawLine (x, y, x, segEnd, 1.5f);
                y += dashLen + gapLen;
            }

            // Red diamond at the top of the dashed line
            float dY = plot.getY() - diamondR - 2.0f;
            juce::Path diamond;
            diamond.startNewSubPath (x, dY - diamondR);
            diamond.lineTo (x + diamondR * 0.7f, dY);
            diamond.lineTo (x, dY + diamondR);
            diamond.lineTo (x - diamondR * 0.7f, dY);
            diamond.closeSubPath();
            g.setColour (markerCol);
            g.fillPath (diamond);

            // Text label (if provided) below the diamond, inside the plot
            if (mk.text.isNotEmpty())
            {
                const float flagH = 13.0f, flagPad = 3.0f;
                float approxLabelW = std::min (
                    120.0f,
                    g.getCurrentFont().getStringWidthFloat (mk.text) + 2.0f * flagPad);
                float flagX = x - approxLabelW * 0.5f;
                for (float used : usedFlagX)
                    if (std::fabs (flagX - used) < approxLabelW + 2.0f)
                        flagX = used + approxLabelW + 3.0f;
                flagX = juce::jlimit (plot.getX(), plot.getRight() - approxLabelW, flagX);
                usedFlagX.push_back (flagX);

                const float flagY = plot.getY() + 2.0f;
                g.setColour (markerCol);
                g.fillRoundedRectangle (flagX, flagY, approxLabelW, flagH, 2.0f);
                g.setColour (juce::Colours::white);
                g.drawText (mk.text,
                            (int) flagX, (int) flagY, (int) approxLabelW, (int) flagH,
                            juce::Justification::centred, true);
            }
        }
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

    // ---- FFT crosshair ----
    if (fftEnabled_ && fftCrosshairActive_ && currentNumBands_ > 0
        && plot.contains (fftCrosshairPos_))
    {
        const float fMin = processor.apvts.getRawParameterValue ("fftLowerFreq")->load();
        const float fMax = processor.apvts.getRawParameterValue ("fftUpperFreq")->load();

        // Pixel → frequency (log scale)
        const float frac = (fftCrosshairPos_.x - plot.getX()) / plot.getWidth();
        const float freq = fMin * std::pow (fMax / fMin, frac);

        // Find nearest FFT band
        const int resIdx = static_cast<int> (processor.apvts.getRawParameterValue ("fftBandRes")->load());
        static constexpr int kBandN[] = { 1, 3, 6, 12, 24 };
        const int N = kBandN[juce::jlimit (0, 4, resIdx)];
        const float stepFactor = std::pow (2.0f, 1.0f / static_cast<float> (N));

        float fc = fMin;
        int bestBand = 0;
        float bestDist = 1e30f;
        for (int b = 0; b < currentNumBands_; ++b, fc *= stepFactor)
        {
            float dist = std::abs (std::log2 (freq) - std::log2 (fc));
            if (dist < bestDist) { bestDist = dist; bestBand = b; }
        }

        const float bandFreq = fMin * std::pow (stepFactor, static_cast<float> (bestBand));
        const float bandDB   = fftBandsSmoothed_[bestBand];

        // Frequency → pixel X (log scale)
        const float crossX = plot.getX() + juce::jlimit (0.0f, 1.0f,
            std::log2 (bandFreq / fMin) / std::log2 (fMax / fMin)) * plot.getWidth();

        // dB → pixel Y
        const float crossY = plot.getBottom()
            - juce::jlimit (0.0f, 1.0f, (bandDB - yDispMin) / (yDispMax - yDispMin))
              * plot.getHeight();

        const juce::Colour crossCol = lightMode_ ? juce::Colour (0xcc000000)
                                                  : juce::Colour (0xccffffff);

        // Vertical line
        g.setColour (crossCol.withAlpha (0.4f));
        g.drawVerticalLine (static_cast<int> (crossX), plot.getY(), plot.getBottom());

        // Horizontal line
        g.drawHorizontalLine (static_cast<int> (crossY), plot.getX(), plot.getRight());

        // Small dot at intersection
        g.setColour (crossCol);
        g.fillEllipse (crossX - 4.0f, crossY - 4.0f, 8.0f, 8.0f);

        // Label: frequency + level
        juce::String freqStr = (bandFreq >= 1000.0f)
            ? juce::String (bandFreq / 1000.0f, 2) + " kHz"
            : juce::String (static_cast<int> (bandFreq + 0.5f)) + " Hz";
        juce::String label = freqStr + "  " + juce::String (bandDB, 1) + " dB";

        g.setFont (juce::Font (juce::FontOptions().withHeight (16.0f).withStyle ("Bold")));
        const float tw = g.getCurrentFont().getStringWidthFloat (label) + 12.0f;
        const float th = 22.0f;

        // Position label: prefer upper-right of crosshair, flip if near edges
        float lx = crossX + 10.0f;
        float ly = crossY - th - 6.0f;
        if (lx + tw > plot.getRight())  lx = crossX - tw - 10.0f;
        if (ly < plot.getY())           ly = crossY + 6.0f;

        g.setColour ((lightMode_ ? juce::Colour (0xe0f2f2f7) : juce::Colour (0xe01c1c1e)));
        g.fillRoundedRectangle (lx, ly, tw, th, 4.0f);
        g.setColour (crossCol);
        g.drawText (label, static_cast<int> (lx), static_cast<int> (ly),
                    static_cast<int> (tw), static_cast<int> (th),
                    juce::Justification::centred, false);
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

    // --- Accumulate for cycle averaging ---
    const int avgCycles = juce::jlimit (1, 999,
        static_cast<int> (processor.apvts.getRawParameterValue ("fftAvgCycles")->load()));

    if (static_cast<int> (fftAvgAccum_.size()) != kFftSize / 2)
    {
        fftAvgAccum_.assign (kFftSize / 2, 0.0f);
        fftAvgCount_ = 0;
    }

    for (int k = 0; k < kFftSize / 2; ++k)
        fftAvgAccum_[k] += fftBuffer_[k];

    ++fftAvgCount_;

    if (fftAvgCount_ < avgCycles)
        return;

    const float invAvg = 1.0f / static_cast<float> (fftAvgCount_);
    for (int k = 0; k < kFftSize / 2; ++k)
        fftBuffer_[k] = fftAvgAccum_[k] * invAvg;

    fftAvgAccum_.assign (kFftSize / 2, 0.0f);
    fftAvgCount_ = 0;

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

    const float fftLo = processor.apvts.getRawParameterValue ("fftLowerFreq")->load();
    const float fftHi = processor.apvts.getRawParameterValue ("fftUpperFreq")->load();

    currentNumBands_ = 0;
    for (float fc = fftLo; fc <= fftHi && currentNumBands_ < kMaxFftBands; fc *= stepFactor)
    {
        const int b    = currentNumBands_++;
        const int binLo = std::max (1,            static_cast<int> (fc / halfBandFactor * kFftSize / sampleRate));
        const int binHi = std::min (kFftSize / 2, static_cast<int> (fc * halfBandFactor * kFftSize / sampleRate) + 1);

        float peak = 0.0f;
        for (int k = binLo; k < binHi; ++k)
            peak = std::max (peak, fftBuffer_[k]);

        float dbSPL = 20.0f * std::log10 (peak / normFactor + 1e-10f) + calOffset;
        if (rtaMode)
            dbSPL += 3.0f * std::log2 (fc / fftLo);   // +3 dB/oct relative to lower limit
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

    // Log-scale X mapping using current FFT frequency range
    const float fMin = processor.apvts.getRawParameterValue ("fftLowerFreq")->load();
    const float fMax = processor.apvts.getRawParameterValue ("fftUpperFreq")->load();

    auto freqToX = [&] (float freq) -> float
    {
        float t = std::log2 (freq / fMin) / std::log2 (fMax / fMin);
        return plot.getX() + juce::jlimit (0.0f, 1.0f, t) * plot.getWidth();
    };

    const float yDispMin = processor.apvts.getRawParameterValue ("splYMin")->load();
    const float yDispMax = processor.apvts.getRawParameterValue ("splYMax")->load();

    auto splToY = [&] (float spl) -> float
    {
        float t = (spl - yDispMin) / (yDispMax - yDispMin);
        return plot.getBottom() - juce::jlimit (0.0f, 1.0f, t) * plot.getHeight();
    };

    const int  displayMode = static_cast<int> (processor.apvts.getRawParameterValue ("fftDisplayMode")->load());
    const bool peakHoldOn  = processor.apvts.getRawParameterValue ("fftPeakHold")->load() > 0.5f;
    const int  numBands    = currentNumBands_;

    const juce::Colour colFill    { 0xaa34c759 };
    const juce::Colour colSolid   { 0xff34c759 };
    const juce::Colour colPeak    { 0xffffcc00 };  // amber peak line

    // Iterate bands starting from lower frequency limit
    float fc0 = fMin;

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

    // ---- Graph Overlay 2: dashed orange frequency-response curve ----
    if (processor.apvts.getRawParameterValue ("graphOverlay2Enabled")->load() > 0.5f
        && processor.isGraphOverlay2Loaded())
    {
        const auto& pts = processor.getGraphOverlay2Points();
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

            g.setColour (juce::Colour (0xffff9f0a));  // orange
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

        const float labelFreqs[] = { 10.f, 20.f, 50.f, 100.f, 200.f, 500.f,
                                     1000.f, 2000.f, 5000.f, 10000.f, 20000.f };

        g.setFont (juce::Font (juce::FontOptions().withHeight (15.0f)));

        for (float freq : labelFreqs)
        {
            if (freq < fMin * 0.99f || freq > fMax * 1.01f) continue;

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

//==============================================================================
void LogComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! fftEnabled_) return;

    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const juce::Rectangle<float> plot (
        bounds.getX() + 80.0f,
        bounds.getY() + 116.0f,
        bounds.getWidth()  - 80.0f - 75.0f,
        bounds.getHeight() - 116.0f - 50.0f);

    if (plot.contains (e.position))
    {
        fftCrosshairActive_ = ! fftCrosshairActive_;
        fftCrosshairPos_ = e.position;
        repaint();
    }
    else
    {
        if (fftCrosshairActive_)
        {
            fftCrosshairActive_ = false;
            repaint();
        }
    }
}

void LogComponent::mouseMove (const juce::MouseEvent& e)
{
    if (! fftCrosshairActive_ || ! fftEnabled_) return;

    fftCrosshairPos_ = e.position;
    repaint();
}

void LogComponent::mouseExit (const juce::MouseEvent&)
{
    if (fftCrosshairActive_)
    {
        fftCrosshairActive_ = false;
        repaint();
    }
}

