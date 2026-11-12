#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <optional>

//==============================================================================
// "Inputs & Sensors" window (vibration spec §7.1): a role per input, live level,
// accelerometer calibration, and the accelerometer sensor settings.
namespace InputsUi
{
// Source literals are UTF-8; juce::String (const char*) would read them as ASCII
inline juce::String U (const char* utf8) { return juce::String::fromUTF8 (utf8); }

struct Palette
{
    juce::Colour bg, card, border, text, subtext, field, accent, ok, warn, bad;

    static Palette get (bool light)
    {
        if (light)
            return { juce::Colour (0xffe5e5ea), juce::Colour (0xfff5f5f7), juce::Colour (0xffc7c7cc),
                     juce::Colour (0xff1c1c1e), juce::Colour (0xff6c6c70), juce::Colour (0xffd1d1d6),
                     juce::Colour (0xff007aff), juce::Colour (0xff248a3d), juce::Colour (0xffc93400),
                     juce::Colour (0xffd70015) };
        return { juce::Colour (0xff2c2c2e), juce::Colour (0xff333335), juce::Colour (0xff48484a),
                 juce::Colours::white,      juce::Colour (0xff8e8e93), juce::Colour (0xff3a3a3c),
                 juce::Colour (0xff5ac8fa), juce::Colour (0xff34c759), juce::Colour (0xffff9f0a),
                 juce::Colour (0xffff453a) };
    }

    void style (juce::ComboBox& c) const
    {
        c.setColour (juce::ComboBox::backgroundColourId, field);
        c.setColour (juce::ComboBox::textColourId,       text);
        c.setColour (juce::ComboBox::outlineColourId,    border);
        c.setColour (juce::ComboBox::arrowColourId,      subtext);
    }
};

inline juce::String roleName (SensorRole r)
{
    switch (r)
    {
        case SensorRole::Mic:           return "Mic";
        case SensorRole::AccelX:        return "Accel X";
        case SensorRole::AccelY:        return "Accel Y";
        case SensorRole::AccelZ:        return "Accel Z";
        case SensorRole::AccelUniaxial: return "Accel (uniaxial)";
        case SensorRole::Off:           return "Off";
    }
    return {};
}

// Short tag shown on the channel buttons in Settings
inline juce::String roleTag (SensorRole r)
{
    switch (r)
    {
        case SensorRole::AccelX:        return "X";
        case SensorRole::AccelY:        return "Y";
        case SensorRole::AccelZ:        return "Z";
        case SensorRole::AccelUniaxial: return "ACC";
        case SensorRole::Off:           return "OFF";
        case SensorRole::Mic:           break;
    }
    return {};
}

inline juce::String weightingName (vib::Weighting w)
{
    switch (w)
    {
        case vib::Weighting::None:      return "Unweighted";
        case vib::Weighting::BandLimit: return "Band limit";
        case vib::Weighting::Wb:        return "Wb  vertical, rail (ISO 2631-4)";
        case vib::Weighting::Wc:        return "Wc  seat back (ISO 2631-1)";
        case vib::Weighting::Wd:        return "Wd  whole body x/y (ISO 2631-1)";
        case vib::Weighting::We:        return "We  rotational (ISO 2631-1)";
        case vib::Weighting::Wf:        return "Wf  motion sickness (ISO 2631-1)";
        case vib::Weighting::Wh:        return "Wh  hand-arm (ISO 5349-1)";
        case vib::Weighting::Wj:        return "Wj  head, recumbent (ISO 2631-1)";
        case vib::Weighting::Wk:        return "Wk  whole body z (ISO 2631-1)";
        case vib::Weighting::Wm:        return "Wm  buildings (ISO 2631-2)";
        case vib::Weighting::KB:        return "KB  DIN 4150-2 (velocity)";
    }
    return {};
}

inline juce::String formatAccel (double ms2)
{
    if (ms2 <= 0.0)   return U ("0 m/s²");
    if (ms2 >= 100.0) return juce::String (ms2, 0) + U (" m/s²");
    if (ms2 >= 1.0)   return juce::String (ms2, 2) + U (" m/s²");
    if (ms2 >= 0.01)  return juce::String (ms2 * 1000.0, 1) + U (" mm/s²");
    return juce::String (ms2 * 1.0e6, 0) + U (" µm/s²");
}

inline juce::String formatDbfs (double rms)
{
    return rms > 1.0e-6 ? juce::String (20.0 * std::log10 (rms), 1) + " dBFS" : juce::String (U ("−∞ dBFS"));
}

//==============================================================================
// Click-to-edit number with range and unit
class NumberField  : public juce::Label
{
public:
    std::function<void (double)> onValueChange;

    NumberField (double minV, double maxV, int decimals, juce::String unit = {})
        : min_ (minV), max_ (maxV), decimals_ (decimals), unit_ (std::move (unit))
    {
        setEditable (true, true, false);
        setJustificationType (juce::Justification::centred);
        onTextChange = [this]
        {
            const auto t = getText().retainCharacters ("0123456789.,-eE").replaceCharacter (',', '.');
            const double v = t.isNotEmpty() ? t.getDoubleValue() : value_;
            setValue (juce::jlimit (min_, max_, v), juce::sendNotification);
        };
    }

    void setValue (double v, juce::NotificationType n = juce::dontSendNotification)
    {
        const bool changed = std::abs (v - value_) > 1.0e-12;
        value_ = v;
        setText (juce::String (v, decimals_) + (unit_.isNotEmpty() ? " " + unit_ : juce::String()),
                 juce::dontSendNotification);
        if (changed && n != juce::dontSendNotification && onValueChange)
            onValueChange (v);
    }
    double getValue() const noexcept { return value_; }

    void style (const Palette& p)
    {
        setColour (juce::Label::backgroundColourId, p.field);
        setColour (juce::Label::textColourId, p.text);
        setColour (juce::Label::textWhenEditingColourId, p.text);
        setColour (juce::TextEditor::backgroundColourId, p.field);
        setColour (juce::TextEditor::textColourId, p.text);
        setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.4f));
    }

protected:
    juce::TextEditor* createEditorComponent() override
    {
        auto* ed = juce::Label::createEditorComponent();
        ed->setText (juce::String (value_, decimals_), false);
        ed->setJustification (juce::Justification::centred);
        return ed;
    }

private:
    double min_, max_;
    int decimals_;
    juce::String unit_;
    double value_ = std::numeric_limits<double>::quiet_NaN();
};

//==============================================================================
// Horizontal level bar: RMS bar + decaying peak tick, −80 … 0 dBFS
class LevelBar  : public juce::Component
{
public:
    void setLevels (float rms, float peak)
    {
        rms_ = rms;
        peak_ = std::max (peak, peak_ * 0.85f);
        repaint();
    }
    void setPalette (const Palette& p) { pal_ = p; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.0f, 6.0f);
        g.setColour (pal_.field);
        g.fillRoundedRectangle (b, 3.0f);

        auto toX = [&] (float lin)
        {
            const float db = lin > 1.0e-6f ? 20.0f * std::log10 (lin) : -120.0f;
            return b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
        };
        const float xr = toX (rms_);
        const auto col = peak_ >= 0.99f ? pal_.bad : (peak_ >= 0.5f ? pal_.warn : pal_.ok);
        g.setColour (col.withAlpha (0.85f));
        g.fillRoundedRectangle (b.withRight (xr), 3.0f);
        if (peak_ > 1.0e-6f)
        {
            g.setColour (col);
            const float xp = toX (peak_);
            g.fillRect (juce::Rectangle<float> (xp - 1.0f, b.getY(), 2.0f, b.getHeight()));
        }
    }

private:
    float rms_ = 0.0f, peak_ = 0.0f;
    Palette pal_ = Palette::get (false);
};

//==============================================================================
// Calibration dialog for one accelerometer input (spec §4)
class CalibrationComponent  : public juce::Component,
                              private juce::Timer
{
public:
    CalibrationComponent (SPLMeterAudioProcessor& p, int channel, bool light)
        : processor_ (p), ch_ (channel), pal_ (Palette::get (light))
    {
        auto setupLabel = [this] (juce::Label& l, const juce::String& text, bool sub = false)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (juce::Font (juce::FontOptions().withHeight (sub ? 12.5f : 14.0f)));
            l.setColour (juce::Label::textColourId, sub ? pal_.subtext : pal_.text);
            addAndMakeVisible (l);
        };
        auto setupButton = [this] (juce::TextButton& b)
        {
            b.setColour (juce::TextButton::buttonColourId,   pal_.field);
            b.setColour (juce::TextButton::buttonOnColourId, pal_.accent);
            b.setColour (juce::TextButton::textColourOffId,  pal_.text);
            b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
            addAndMakeVisible (b);
        };

        setupLabel (titleLabel_, processor_.getChannelName (ch_) + U ("  ·  ")
                                 + roleName (processor_.getChannelRole (ch_)));
        titleLabel_.setFont (juce::Font (juce::FontOptions().withHeight (16.0f).withStyle ("Bold")));
        setupLabel (currentLabel_, {}, true);
        setupLabel (levelLabel_, {}, true);

        for (auto* b : { &calibratorTab_, &datasheetTab_ })
        {
            setupButton (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4711);
            b->onClick = [this] { updateMode(); };
        }
        calibratorTab_.setToggleState (true, juce::dontSendNotification);

        // --- Calibrator ---
        setupLabel (refLabel_, "Reference");
        refCombo_.addItem (U ("10 m/s² RMS at 159.2 Hz (140 dB)"), 1);
        refCombo_.addItem ("1 g RMS at 159.2 Hz (139.8 dB)", 2);
        refCombo_.addItem ("Custom", 3);
        refCombo_.setSelectedId (1, juce::dontSendNotification);
        refCombo_.onChange = [this] { updateMode(); };
        addAndMakeVisible (refCombo_);
        setupLabel (customLevelLabel_, "Level");
        setupLabel (customFreqLabel_, "Frequency");
        customLevel_.setValue (10.0);
        customFreq_.setValue (159.2);
        for (auto* f : { &customLevel_, &customFreq_ }) { f->style (pal_); addAndMakeVisible (*f); }
        setupButton (measureButton_);
        measureButton_.onClick = [this] { toggleMeasurement(); };
        addAndMakeVisible (progress_);
        progress_.setColour (juce::ProgressBar::foregroundColourId, pal_.accent);
        progress_.setColour (juce::ProgressBar::backgroundColourId, pal_.field);
        setupLabel (resultLabel_, "Mount the sensor on the calibrator, switch it on, then press Measure.", true);
        resultLabel_.setJustificationType (juce::Justification::topLeft);

        // --- Datasheet ---
        setupLabel (sensLabel_, "Sensitivity");
        sensField_.setValue (10.2);
        sensUnit_.addItem (U ("mV/(m/s²)"), 1);
        sensUnit_.addItem ("mV/g", 2);
        sensUnit_.setSelectedId (1, juce::dontSendNotification);
        setupLabel (fsLabel_, "Interface full scale");
        fsField_.setValue (1.0);
        fsUnit_.addItem ("Vrms at 0 dBFS", 1);
        fsUnit_.addItem ("dBu at 0 dBFS", 2);
        fsUnit_.setSelectedId (1, juce::dontSendNotification);
        for (auto* f : { &sensField_, &fsField_ })
        {
            f->style (pal_);
            f->onValueChange = [this] (double) { updateDatasheet(); };
            addAndMakeVisible (*f);
        }
        for (auto* c : { &refCombo_, &sensUnit_, &fsUnit_ })
            pal_.style (*c);
        for (auto* c : { &sensUnit_, &fsUnit_ })
        {
            c->onChange = [this] { updateDatasheet(); };
            addAndMakeVisible (*c);
        }
        setupLabel (datasheetResult_, {}, true);
        datasheetResult_.setJustificationType (juce::Justification::topLeft);

        setupButton (applyButton_);
        setupButton (closeButton_);
        applyButton_.onClick = [this] { apply(); };
        closeButton_.onClick = [this] { closeDialog(); };

        updateMode();
        updateDatasheet();
        timerCallback();
        startTimerHz (15);
        setSize (560, 400);
    }

    ~CalibrationComponent() override
    {
        stopTimer();
        if (measuring_)
            processor_.cancelAccelCalibration();
    }

    void paint (juce::Graphics& g) override { g.fillAll (pal_.bg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        titleLabel_.setBounds (r.removeFromTop (24));
        currentLabel_.setBounds (r.removeFromTop (20));
        levelLabel_.setBounds (r.removeFromTop (20));
        r.removeFromTop (8);
        auto tabs = r.removeFromTop (28);
        calibratorTab_.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).reduced (2, 0));
        datasheetTab_.setBounds (tabs.reduced (2, 0));
        r.removeFromTop (12);

        auto buttons = r.removeFromBottom (30);
        closeButton_.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        applyButton_.setBounds (buttons.removeFromRight (160));
        r.removeFromBottom (8);

        // Calibrator
        {
            auto c = r;
            auto row = c.removeFromTop (28);
            refLabel_.setBounds (row.removeFromLeft (150));
            refCombo_.setBounds (row);
            c.removeFromTop (6);
            row = c.removeFromTop (28);
            customLevelLabel_.setBounds (row.removeFromLeft (150));
            customLevel_.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (16);
            customFreqLabel_.setBounds (row.removeFromLeft (80));
            customFreq_.setBounds (row.removeFromLeft (110));
            c.removeFromTop (10);
            row = c.removeFromTop (28);
            measureButton_.setBounds (row.removeFromLeft (150));
            row.removeFromLeft (10);
            progress_.setBounds (row.reduced (0, 4));
            c.removeFromTop (8);
            resultLabel_.setBounds (c);
        }
        // Datasheet
        {
            auto c = r;
            auto row = c.removeFromTop (28);
            sensLabel_.setBounds (row.removeFromLeft (150));
            sensField_.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            sensUnit_.setBounds (row.removeFromLeft (130));
            c.removeFromTop (6);
            row = c.removeFromTop (28);
            fsLabel_.setBounds (row.removeFromLeft (150));
            fsField_.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            fsUnit_.setBounds (row.removeFromLeft (160));
            c.removeFromTop (10);
            datasheetResult_.setBounds (c);
        }
    }

private:
    bool calibratorMode() const { return calibratorTab_.getToggleState(); }

    double referenceRms() const
    {
        switch (refCombo_.getSelectedId())
        {
            case 2:  return vib::calibration::kGravity;
            case 3:  return customLevel_.getValue();
            default: return vib::calibration::kCalibratorRms;
        }
    }
    double referenceHz() const { return refCombo_.getSelectedId() == 3 ? customFreq_.getValue() : 159.2; }

    void updateMode()
    {
        const bool cal = calibratorMode();
        const bool custom = refCombo_.getSelectedId() == 3;
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &refLabel_, &refCombo_, &measureButton_, &progress_, &resultLabel_ })
            c->setVisible (cal);
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &customLevelLabel_, &customLevel_, &customFreqLabel_, &customFreq_ })
            c->setVisible (cal && custom);
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &sensLabel_, &sensField_, &sensUnit_, &fsLabel_, &fsField_, &fsUnit_, &datasheetResult_ })
            c->setVisible (! cal);
        customLevel_.setEnabled (! measuring_);
        customFreq_.setEnabled (! measuring_);
        refCombo_.setEnabled (! measuring_);
        updateApply();
    }

    void updateApply()
    {
        const bool ready = calibratorMode() ? measuredCalDb_.has_value() : datasheetCalDb_.has_value();
        applyButton_.setEnabled (ready && ! measuring_);
        const auto v = calibratorMode() ? measuredCalDb_ : datasheetCalDb_;
        applyButton_.setButtonText (v ? "Apply " + juce::String (*v, 2) + " dB" : juce::String ("Apply"));
    }

    void updateDatasheet()
    {
        double S = sensField_.getValue();
        if (sensUnit_.getSelectedId() == 2)
            S = vib::calibration::mVperMs2FromMVperG (S);
        const double fs = fsField_.getValue();
        const double vrms = fsUnit_.getSelectedId() == 2 ? vib::calibration::dBuToVrms (fs) : fs;

        datasheetCalDb_.reset();
        if (S > 0.0 && vrms > 0.0)
        {
            const double cal = vib::calibration::calDbFromDatasheet (vrms, S);
            if (cal >= vib::calibration::kMinCalDb && cal <= vib::calibration::kMaxCalDb)
            {
                datasheetCalDb_ = cal;
                datasheetResult_.setText (U ("Cal = 120 + 20·log10(√2 · ") + juce::String (vrms * 1000.0, 1)
                                          + " mV / " + juce::String (S, 3) + U (" mV/(m/s²)) = ")
                                          + juce::String (cal, 2) + U (" dB re 1 µm/s²\n")
                                          + "Full-scale sine = " + formatAccel (vib::calibration::scale (cal) / std::sqrt (2.0))
                                          + " RMS",
                                          juce::dontSendNotification);
            }
            else
            {
                datasheetResult_.setText (U ("Result outside 60–220 dB — check the values."), juce::dontSendNotification);
            }
        }
        else
        {
            datasheetResult_.setText ("Enter sensitivity and interface full scale.", juce::dontSendNotification);
        }
        updateApply();
    }

    void toggleMeasurement()
    {
        if (measuring_)
        {
            processor_.cancelAccelCalibration();
            measuring_ = false;
            resultLabel_.setText ("Measurement cancelled.", juce::dontSendNotification);
        }
        else
        {
            measuredCalDb_.reset();
            if (! processor_.startAccelCalibration (ch_, referenceHz()))
            {
                resultLabel_.setText ("Cannot start: the input is not available or the frequency is out of range.",
                                      juce::dontSendNotification);
                return;
            }
            measuring_ = true;
            progressValue_ = 0.0;
            resultLabel_.setText (U ("Measuring…"), juce::dontSendNotification);
        }
        measureButton_.setButtonText (measuring_ ? "Cancel" : "Measure");
        updateMode();
    }

    void timerCallback() override
    {
        const double calDb = processor_.getAccelCalDb (ch_);
        const auto info = processor_.getAccelCalInfo (ch_);
        currentLabel_.setText ("Current calibration: " + juce::String (calDb, 2) + U (" dB re 1 µm/s²")
                               + (info.isNotEmpty() ? "  (" + info + ")" : juce::String ("  (not calibrated yet)")),
                               juce::dontSendNotification);
        const double rms = processor_.getInputRms (ch_);
        levelLabel_.setText ("Input: " + formatDbfs (rms) + U ("  ·  ") + formatAccel (rms * vib::calibration::scale (calDb))
                             + " RMS with the current calibration",
                             juce::dontSendNotification);

        if (! measuring_)
            return;
        progressValue_ = processor_.getAccelCalibrationProgress();
        vib::CalibratorMeasurement::Result r;
        if (! processor_.takeAccelCalibrationResult (r))
            return;

        measuring_ = false;
        measureButton_.setButtonText ("Measure");
        using S = vib::CalibratorMeasurement::Status;
        const juce::String meas = "Measured " + formatDbfs (r.rmsFS) + " at " + juce::String (r.freqHz, 1)
                                + U (" Hz, stability ±") + juce::String (r.fluctuationDb, 2) + " dB.\n";
        switch (r.status)
        {
            case S::Ok:
            {
                const double cal = vib::calibration::calDbFromCalibrator (r.rmsFS, referenceRms());
                if (cal >= vib::calibration::kMinCalDb && cal <= vib::calibration::kMaxCalDb)
                {
                    measuredCalDb_ = cal;
                    resultLabel_.setText (meas + "New calibration: " + juce::String (cal, 2) + U (" dB re 1 µm/s² (")
                                          + juce::String (cal - calDb, 2) + " dB change).",
                                          juce::dontSendNotification);
                }
                else
                {
                    resultLabel_.setText (meas + U ("The result is outside 60–220 dB."), juce::dontSendNotification);
                }
                break;
            }
            case S::Clipped:      resultLabel_.setText (meas + U ("The input clips — reduce the interface gain."), juce::dontSendNotification); break;
            case S::TooLow:       resultLabel_.setText (meas + U ("No calibrator signal — check the sensor and the input."), juce::dontSendNotification); break;
            case S::FrequencyOff: resultLabel_.setText (meas + U ("The frequency deviates more than ±5 % from ")
                                                        + juce::String (referenceHz(), 1) + " Hz.", juce::dontSendNotification); break;
            case S::Unstable:     resultLabel_.setText (meas + U ("The level fluctuates more than ±0.2 dB — hold the calibrator steady."), juce::dontSendNotification); break;
            case S::Running:      break;
        }
        updateMode();
    }

    void apply()
    {
        const auto v = calibratorMode() ? measuredCalDb_ : datasheetCalDb_;
        if (! v) return;
        processor_.setAccelCalDb (ch_, *v);
        processor_.setAccelCalInfo (ch_, calibratorMode() ? "calibrator" : "datasheet");
        closeDialog();
    }

    void closeDialog()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    SPLMeterAudioProcessor& processor_;
    const int ch_;
    Palette pal_;
    bool measuring_ = false;
    double progressValue_ = 0.0;
    std::optional<double> measuredCalDb_, datasheetCalDb_;

    juce::Label titleLabel_, currentLabel_, levelLabel_;
    juce::TextButton calibratorTab_ { "Handheld calibrator" }, datasheetTab_ { "Datasheet" };
    juce::Label refLabel_, customLevelLabel_, customFreqLabel_, resultLabel_;
    juce::ComboBox refCombo_;
    NumberField customLevel_ { 0.001, 1000.0, 3, U ("m/s²") }, customFreq_ { 1.0, 10000.0, 1, "Hz" };
    juce::TextButton measureButton_ { "Measure" };
    juce::ProgressBar progress_ { progressValue_ };
    juce::Label sensLabel_, fsLabel_, datasheetResult_;
    NumberField sensField_ { 0.001, 100000.0, 3 }, fsField_ { -60.0, 100.0, 3 };
    juce::ComboBox sensUnit_, fsUnit_;
    juce::TextButton applyButton_ { "Apply" }, closeButton_ { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalibrationComponent)
};

//==============================================================================
// One row of the inputs table
class InputRow  : public juce::Component
{
public:
    std::function<void (int)> onCalibrate;

    InputRow (SPLMeterAudioProcessor& p, int channel) : processor_ (p), ch_ (channel)
    {
        addAndMakeVisible (nameLabel_);
        for (int r = 0; r <= (int) SensorRole::Off; ++r)
            roleCombo_.addItem (roleName ((SensorRole) r), r + 1);
        roleCombo_.onChange = [this]
        {
            processor_.setChannelRole (ch_, SensorConfig::fromInt (roleCombo_.getSelectedId() - 1));
        };
        addAndMakeVisible (roleCombo_);
        addAndMakeVisible (level_);
        addAndMakeVisible (levelText_);
        levelText_.setJustificationType (juce::Justification::centredRight);
        calField_.onValueChange = [this] (double v)
        {
            processor_.setAccelCalDb (ch_, v);
            processor_.setAccelCalInfo (ch_, "manual");
        };
        addAndMakeVisible (calField_);
        calButton_.onClick = [this] { if (onCalibrate) onCalibrate (ch_); };
        addAndMakeVisible (calButton_);
        addAndMakeVisible (calInfo_);
        for (auto* l : { &nameLabel_, &levelText_, &calInfo_ })
            l->setFont (juce::Font (juce::FontOptions().withHeight (13.0f)));
    }

    void applyPalette (const Palette& p)
    {
        pal_ = p;
        nameLabel_.setColour (juce::Label::textColourId, p.text);
        levelText_.setColour (juce::Label::textColourId, p.subtext);
        calInfo_.setColour (juce::Label::textColourId, p.subtext);
        calField_.style (p);
        p.style (roleCombo_);
        level_.setPalette (p);
        calButton_.setColour (juce::TextButton::buttonColourId, p.field);
        calButton_.setColour (juce::TextButton::textColourOffId, p.text);
        repaint();
    }

    // Columns are shared with the table header
    static void columns (juce::Rectangle<int> r, juce::Rectangle<int> (&out)[6])
    {
        out[0] = r.removeFromLeft (110);   // channel
        out[1] = r.removeFromLeft (150);   // role
        out[2] = r.removeFromLeft (190);   // level bar
        out[3] = r.removeFromLeft (110);   // level text
        out[4] = r.removeFromLeft (100);   // cal (dB)
        out[5] = r;                        // calibrate + info
    }

    void resized() override
    {
        juce::Rectangle<int> c[6];
        columns (getLocalBounds(), c);
        nameLabel_.setBounds (c[0]);
        roleCombo_.setBounds (c[1].reduced (2, 3));
        level_.setBounds (c[2].reduced (6, 0));
        levelText_.setBounds (c[3]);
        calField_.setBounds (c[4].reduced (6, 4));
        auto last = c[5];
        calButton_.setBounds (last.removeFromLeft (100).reduced (2, 3));
        calInfo_.setBounds (last.withTrimmedLeft (6));
    }

    void paint (juce::Graphics& g) override
    {
        if (invalid_)
        {
            g.setColour (pal_.bad.withAlpha (0.18f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        }
    }

    // Called by the window's timer
    void refresh (bool invalid)
    {
        const auto role = processor_.getChannelRole (ch_);
        const bool accel = SensorConfig::isAccel (role);
        nameLabel_.setText (processor_.getChannelName (ch_), juce::dontSendNotification);
        if (! roleCombo_.isPopupActive())
            roleCombo_.setSelectedId ((int) role + 1, juce::dontSendNotification);

        const float rms  = processor_.getInputRms (ch_);
        const float peak = processor_.takeInputPeak (ch_);
        level_.setLevels (rms, peak);

        calField_.setVisible (accel);
        calButton_.setVisible (accel);
        calInfo_.setVisible (accel);
        if (accel)
        {
            const double calDb = processor_.getAccelCalDb (ch_);
            if (! calField_.isBeingEdited())
                calField_.setValue (calDb);
            levelText_.setText (formatAccel (rms * vib::calibration::scale (calDb)), juce::dontSendNotification);
            const auto info = processor_.getAccelCalInfo (ch_);
            calInfo_.setText (info.isNotEmpty() ? info : juce::String ("not calibrated"), juce::dontSendNotification);
        }
        else
        {
            levelText_.setText (role == SensorRole::Off ? juce::String() : formatDbfs (rms), juce::dontSendNotification);
        }
        level_.setAlpha (role == SensorRole::Off ? 0.4f : 1.0f);

        if (invalid != invalid_) { invalid_ = invalid; repaint(); }
    }

private:
    SPLMeterAudioProcessor& processor_;
    const int ch_;
    Palette pal_ = Palette::get (false);
    bool invalid_ = false;

    juce::Label nameLabel_, levelText_, calInfo_;
    juce::ComboBox roleCombo_;
    LevelBar level_;
    NumberField calField_ { vib::calibration::kMinCalDb, vib::calibration::kMaxCalDb, 2, "dB" };
    juce::TextButton calButton_ { U ("Calibrate…") };
};

//==============================================================================
class InputsComponent  : public juce::Component,
                         private juce::Timer
{
public:
    InputsComponent (SPLMeterAudioProcessor& p, bool light) : processor_ (p)
    {
        auto setupLabel = [this] (juce::Label& l, const juce::String& text, float h = 13.0f, bool bold = false)
        {
            l.setText (text, juce::dontSendNotification);
            auto f = juce::FontOptions().withHeight (h);
            l.setFont (juce::Font (bold ? f.withStyle ("Bold") : f));
            l.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
        };

        setupLabel (inputsTitle_, "INPUTS", 11.0f, true);
        setupLabel (inputsDesc_, "Assign a role to each input. Only Mic inputs feed the SPL meter; "
                                 "accelerometer inputs bypass the mix, bandpass and correction filter.", 11.5f);
        setupLabel (statusLabel_, {}, 13.0f, true);
        const juce::String headers[6] = { "Channel", "Role", "Level", "", U ("Cal (dB re 1 µm/s²)"), "Calibration" };
        for (int i = 0; i < 6; ++i)
            setupLabel (headerLabels_[i], headers[i], 11.0f, true);
        addAndMakeVisible (viewport_);
        viewport_.setViewedComponent (&rowsHolder_, false);
        viewport_.setScrollBarsShown (true, false);

        // --- Accelerometer group ---
        setupLabel (accelTitle_, "ACCELEROMETER", 11.0f, true);
        setupLabel (accelDesc_, "Applies to all axes of the sensor. Changes restart the vibration readings.", 11.5f);
        setupLabel (quantityLabel_, "Quantity");
        quantityCombo_.addItem ("Acceleration", 1);
        quantityCombo_.addItem ("Velocity", 2);
        quantityCombo_.addItem ("Displacement", 3);
        quantityCombo_.onChange = [this] { auto s = processor_.getVibrationSettings();
                                           s.quantity = (vib::Quantity) (quantityCombo_.getSelectedId() - 1);
                                           processor_.setVibrationSettings (s); };
        addAndMakeVisible (quantityCombo_);

        setupLabel (highPassLabel_, "High-pass");
        highPass_.onValueChange = [this] (double v) { auto s = processor_.getVibrationSettings();
                                                      s.highPassHz = v; processor_.setVibrationSettings (s); };
        addAndMakeVisible (highPass_);

        setupLabel (tauLabel_, U ("RMS τ"));
        tauCombo_.setTooltip ("Time constant of the running RMS (ISO 2631-1 uses 1 s)");
        for (int i = 0; i < (int) kTaus.size(); ++i)
            tauCombo_.addItem (juce::String (kTaus[(size_t) i], kTaus[(size_t) i] < 1.0 ? 3 : 0) + " s", i + 1);
        tauCombo_.onChange = [this] { auto s = processor_.getVibrationSettings();
                                      s.tauSec = kTaus[(size_t) juce::jlimit (0, (int) kTaus.size() - 1, tauCombo_.getSelectedId() - 1)];
                                      processor_.setVibrationSettings (s); };
        addAndMakeVisible (tauCombo_);

        const char* axisNames[3] = { "Weighting X", "Weighting Y", "Weighting Z" };
        const char* kNames[3]    = { "k x", "k y", "k z" };
        for (int a = 0; a < 3; ++a)
        {
            setupLabel (weightingLabels_[a], axisNames[a]);
            for (int w = 0; w <= (int) vib::Weighting::KB; ++w)
                weightingCombos_[a].addItem (weightingName ((vib::Weighting) w), w + 1);
            weightingCombos_[a].onChange = [this, a] { auto s = processor_.getVibrationSettings();
                                                       s.weighting[a] = (vib::Weighting) (weightingCombos_[a].getSelectedId() - 1);
                                                       processor_.setVibrationSettings (s); };
            addAndMakeVisible (weightingCombos_[a]);

            setupLabel (kLabels_[a], kNames[a]);
            kFields_[a].onValueChange = [this, a] (double v) { auto s = processor_.getVibrationSettings();
                                                               s.k[a] = v; processor_.setVibrationSettings (s); };
            addAndMakeVisible (kFields_[a]);
        }

        setupLabel (bandLabel_, "Band limit");
        bandLow_.onValueChange  = [this] (double v) { auto s = processor_.getVibrationSettings();
                                                      s.bandLowHz = v; processor_.setVibrationSettings (s); };
        bandHigh_.onValueChange = [this] (double v) { auto s = processor_.getVibrationSettings();
                                                      s.bandHighHz = v; processor_.setVibrationSettings (s); };
        addAndMakeVisible (bandLow_);
        addAndMakeVisible (bandHigh_);

        setupLabel (serialLabel_, "Sensor serial");
        serialEditor_.onTextChange = [this] { auto i = processor_.getAccelSensorInfo();
                                              i.serial = serialEditor_.getText().trim();
                                              processor_.setAccelSensorInfo (i); };
        addAndMakeVisible (serialEditor_);
        setupLabel (rangeLabel_, "Usable range");
        lowerLimit_.onValueChange = [this] (double v) { auto i = processor_.getAccelSensorInfo();
                                                        i.lowerLimitHz = v; processor_.setAccelSensorInfo (i); };
        upperLimit_.onValueChange = [this] (double v) { auto i = processor_.getAccelSensorInfo();
                                                        i.upperLimitHz = v; processor_.setAccelSensorInfo (i); };
        addAndMakeVisible (lowerLimit_);
        addAndMakeVisible (upperLimit_);

        setLightMode (light);
        loadAccelSettings();
        rebuildRows();
        timerCallback();
        startTimerHz (20);
    }

    ~InputsComponent() override
    {
        stopTimer();
        closeCalibration();   // the dialog refers to the processor
    }

    void setLightMode (bool light)
    {
        light_ = light;
        pal_ = Palette::get (light);
        for (auto* l : { &inputsTitle_, &accelTitle_ })
            l->setColour (juce::Label::textColourId, pal_.subtext);
        for (auto* l : { &inputsDesc_, &accelDesc_ })
            l->setColour (juce::Label::textColourId, pal_.subtext.withAlpha (0.85f));
        for (auto& l : headerLabels_)
            l.setColour (juce::Label::textColourId, pal_.subtext);
        for (auto* l : { &quantityLabel_, &highPassLabel_, &tauLabel_, &bandLabel_, &serialLabel_, &rangeLabel_,
                         &weightingLabels_[0], &weightingLabels_[1], &weightingLabels_[2],
                         &kLabels_[0], &kLabels_[1], &kLabels_[2] })
            l->setColour (juce::Label::textColourId, pal_.text);
        for (auto* f : { &highPass_, &bandLow_, &bandHigh_, &lowerLimit_, &upperLimit_,
                         &kFields_[0], &kFields_[1], &kFields_[2] })
            f->style (pal_);
        for (auto* c : { &quantityCombo_, &tauCombo_, &weightingCombos_[0], &weightingCombos_[1], &weightingCombos_[2] })
            pal_.style (*c);
        serialEditor_.setColour (juce::TextEditor::backgroundColourId, pal_.field);
        serialEditor_.setColour (juce::TextEditor::textColourId, pal_.text);
        serialEditor_.setColour (juce::TextEditor::outlineColourId, pal_.border);
        for (auto& row : rows_)
            row->applyPalette (pal_);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (pal_.bg);
        for (auto* b : { &inputsCard_, &accelCard_ })
        {
            if (b->isEmpty()) continue;
            g.setColour (pal_.card);
            g.fillRoundedRectangle (b->toFloat(), 10.0f);
            g.setColour (pal_.border);
            g.drawRoundedRectangle (b->toFloat(), 10.0f, 1.0f);
        }
    }

    void resized() override
    {
        constexpr int margin = 16, pad = 14, rowH = 30;
        auto page = getLocalBounds().reduced (margin);

        // Accelerometer card at the bottom (fixed height), inputs card fills the rest
        accelCard_ = {};
        if (accelVisible_)
        {
            accelCard_ = page.removeFromBottom (pad * 2 + 16 + 3 + 16 + 10 + 4 * 28 + 3 * 6);
            page.removeFromBottom (16);
            auto c = accelCard_.reduced (pad);
            accelTitle_.setBounds (c.removeFromTop (16));
            c.removeFromTop (3);
            accelDesc_.setBounds (c.removeFromTop (16));
            c.removeFromTop (10);

            const int colW = c.getWidth() / 3;
            auto cell = [&] (juce::Rectangle<int> row, int col, juce::Label& label, juce::Component& comp, int labelW = 96)
            {
                auto r = row.withX (row.getX() + col * colW).withWidth (colW - 12);
                label.setBounds (r.removeFromLeft (labelW));
                comp.setBounds (r.reduced (0, 2));
            };
            auto row = c.removeFromTop (28);
            cell (row, 0, quantityLabel_, quantityCombo_);
            cell (row, 1, highPassLabel_, highPass_);
            cell (row, 2, tauLabel_, tauCombo_);
            c.removeFromTop (6);
            row = c.removeFromTop (28);
            for (int a = 0; a < 3; ++a)
                cell (row, a, weightingLabels_[a], weightingCombos_[a]);
            c.removeFromTop (6);
            row = c.removeFromTop (28);
            for (int a = 0; a < 3; ++a)
                cell (row, a, kLabels_[a], kFields_[a]);
            c.removeFromTop (6);
            row = c.removeFromTop (28);
            cell (row, 0, serialLabel_, serialEditor_);
            {
                auto r = row.withX (row.getX() + colW).withWidth (colW - 12);
                rangeLabel_.setBounds (r.removeFromLeft (96));
                upperLimit_.setBounds (r.removeFromRight ((r.getWidth() - 8) / 2).reduced (0, 2));
                r.removeFromRight (8);
                lowerLimit_.setBounds (r.reduced (0, 2));
            }
            {
                auto r = row.withX (row.getX() + 2 * colW).withWidth (colW - 12);
                bandLabel_.setBounds (r.removeFromLeft (96));
                bandHigh_.setBounds (r.removeFromRight ((r.getWidth() - 8) / 2).reduced (0, 2));
                r.removeFromRight (8);
                bandLow_.setBounds (r.reduced (0, 2));
            }
        }

        inputsCard_ = page;
        auto c = page.reduced (pad);
        inputsTitle_.setBounds (c.removeFromTop (16));
        c.removeFromTop (3);
        inputsDesc_.setBounds (c.removeFromTop (16));
        c.removeFromTop (8);
        statusLabel_.setBounds (c.removeFromTop (20));
        c.removeFromTop (8);

        juce::Rectangle<int> cols[6];
        InputRow::columns (c.removeFromTop (18).withTrimmedRight (viewport_.getScrollBarThickness()), cols);
        for (int i = 0; i < 6; ++i)
            headerLabels_[i].setBounds (cols[i]);
        c.removeFromTop (4);
        viewport_.setBounds (c);

        const int w = c.getWidth() - viewport_.getScrollBarThickness();
        rowsHolder_.setSize (w, (int) rows_.size() * rowH);
        for (size_t i = 0; i < rows_.size(); ++i)
            rows_[i]->setBounds (0, (int) i * rowH, w, rowH - 2);
    }

private:
    static constexpr std::array<double, 5> kTaus { 0.125, 0.5, 1.0, 2.0, 8.0 };

    int numInputs() const { return juce::jmin (processor_.getTotalNumInputChannels(), SensorConfig::kMaxInputs); }

    void rebuildRows()
    {
        rows_.clear();
        for (int ch = 0; ch < numInputs(); ++ch)
        {
            auto row = std::make_unique<InputRow> (processor_, ch);
            row->applyPalette (pal_);
            row->onCalibrate = [this] (int c) { openCalibration (c); };
            rowsHolder_.addAndMakeVisible (*row);
            rows_.push_back (std::move (row));
        }
        resized();
    }

    void closeCalibration()
    {
        delete calDialog_.getComponent();
    }

    void openCalibration (int ch)
    {
        closeCalibration();
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new CalibrationComponent (processor_, ch, light_));
        o.dialogTitle = "Calibrate " + processor_.getChannelName (ch);
        o.dialogBackgroundColour = pal_.bg;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = false;
        o.resizable = false;
        o.componentToCentreAround = this;
        calDialog_ = o.launchAsync();
    }

    // Reflects the stored settings in the controls (not while one is being edited)
    void loadAccelSettings()
    {
        const auto s = processor_.getVibrationSettings();
        quantityCombo_.setSelectedId ((int) s.quantity + 1, juce::dontSendNotification);
        if (! highPass_.isBeingEdited()) highPass_.setValue (s.highPassHz);
        int tauIdx = 0;
        for (int i = 0; i < (int) kTaus.size(); ++i)
            if (std::abs (kTaus[(size_t) i] - s.tauSec) < std::abs (kTaus[(size_t) tauIdx] - s.tauSec)) tauIdx = i;
        tauCombo_.setSelectedId (tauIdx + 1, juce::dontSendNotification);
        bool anyBand = false;
        for (int a = 0; a < 3; ++a)
        {
            if (! weightingCombos_[a].isPopupActive())
                weightingCombos_[a].setSelectedId ((int) s.weighting[a] + 1, juce::dontSendNotification);
            if (! kFields_[a].isBeingEdited()) kFields_[a].setValue (s.k[a]);
            anyBand = anyBand || s.weighting[a] == vib::Weighting::BandLimit;
        }
        if (! bandLow_.isBeingEdited())  bandLow_.setValue (s.bandLowHz);
        if (! bandHigh_.isBeingEdited()) bandHigh_.setValue (s.bandHighHz);
        bandLabel_.setAlpha (anyBand ? 1.0f : 0.4f);
        bandLow_.setEnabled (anyBand);
        bandHigh_.setEnabled (anyBand);

        const auto info = processor_.getAccelSensorInfo();
        if (! serialEditor_.hasKeyboardFocus (true))
            serialEditor_.setText (info.serial, false);
        if (! lowerLimit_.isBeingEdited()) lowerLimit_.setValue (info.lowerLimitHz);
        if (! upperLimit_.isBeingEdited()) upperLimit_.setValue (info.upperLimitHz);
    }

    void timerCallback() override
    {
        if ((int) rows_.size() != numInputs())
            rebuildRows();

        const auto roles = processor_.getChannelRoles();
        const auto validity = processor_.getSensorValidity();
        const bool valid = SensorConfig::isValid (validity);

        // Status line
        juce::String text;
        juce::Colour col = pal_.subtext;
        auto axisList = [&]
        {
            juce::StringArray parts;
            const char* names[3] = { "X", "Y", "Z" };
            for (int a = 0; a < 3; ++a)
                for (int ch = 0; ch < numInputs(); ++ch)
                    if (roles[(size_t) ch] == (SensorRole) ((int) SensorRole::AccelX + a))
                        parts.add (juce::String (names[a]) + " = " + processor_.getChannelName (ch));
            return parts.joinIntoString (",  ");
        };
        switch (validity)
        {
            case SensorConfig::Validity::NoAccelerometer:
                text = "No accelerometer assigned."; break;
            case SensorConfig::Validity::Triaxial:
                text = "Triaxial accelerometer:  " + axisList(); col = pal_.ok; break;
            case SensorConfig::Validity::Biaxial:
            case SensorConfig::Validity::SingleAxis:
                text = "Accelerometer axes:  " + axisList(); col = pal_.ok; break;
            case SensorConfig::Validity::Uniaxial:
                for (int ch = 0; ch < numInputs(); ++ch)
                    if (roles[(size_t) ch] == SensorRole::AccelUniaxial)
                        text = "Uniaxial accelerometer on " + processor_.getChannelName (ch);
                col = pal_.ok; break;
            case SensorConfig::Validity::DuplicateAxis:
                text = U ("An axis is assigned to more than one input — accelerometer inactive."); col = pal_.bad; break;
            case SensorConfig::Validity::MixedKinds:
                text = U ("Use either X/Y/Z or one uniaxial input (one sensor per session) — accelerometer inactive.");
                col = pal_.bad; break;
        }
        bool anyMic = false;
        for (int ch = 0; ch < numInputs(); ++ch)
            anyMic = anyMic || roles[(size_t) ch] == SensorRole::Mic;
        if (! anyMic && numInputs() > 0)
        {
            text << U ("   No Mic input — the SPL meter has no signal.");
            if (valid) col = pal_.warn;
        }
        statusLabel_.setText (text, juce::dontSendNotification);
        statusLabel_.setColour (juce::Label::textColourId, col);

        // Rows: with an invalid configuration, every accelerometer row is marked
        for (int ch = 0; ch < (int) rows_.size(); ++ch)
            rows_[(size_t) ch]->refresh (! valid && SensorConfig::isAccel (roles[(size_t) ch]));

        // Accelerometer group: shown when any input has an accelerometer role
        bool anyAccel = false;
        bool uniaxial = false;
        for (int ch = 0; ch < numInputs(); ++ch)
        {
            anyAccel = anyAccel || SensorConfig::isAccel (roles[(size_t) ch]);
            uniaxial = uniaxial || roles[(size_t) ch] == SensorRole::AccelUniaxial;
        }
        if (anyAccel != accelVisible_ || uniaxial != uniaxial_)
        {
            accelVisible_ = anyAccel;
            uniaxial_ = uniaxial;
            for (juce::Component* comp : accelComponents())
                comp->setVisible (anyAccel);
            // Uniaxial: one weighting, no vector sum
            weightingLabels_[0].setText (uniaxial ? "Weighting" : "Weighting X", juce::dontSendNotification);
            for (int a = 1; a < 3; ++a)
            {
                weightingLabels_[a].setVisible (anyAccel && ! uniaxial);
                weightingCombos_[a].setVisible (anyAccel && ! uniaxial);
            }
            for (int a = 0; a < 3; ++a)
            {
                kLabels_[a].setVisible (anyAccel && ! uniaxial);
                kFields_[a].setVisible (anyAccel && ! uniaxial);
            }
            resized();
            repaint();
        }
        if (anyAccel)
            loadAccelSettings();
    }

    std::vector<juce::Component*> accelComponents()
    {
        std::vector<juce::Component*> v { &accelTitle_, &accelDesc_, &quantityLabel_, &quantityCombo_,
                                          &highPassLabel_, &highPass_, &tauLabel_, &tauCombo_,
                                          &bandLabel_, &bandLow_, &bandHigh_, &serialLabel_, &serialEditor_,
                                          &rangeLabel_, &lowerLimit_, &upperLimit_ };
        for (int a = 0; a < 3; ++a)
        {
            v.push_back (&weightingLabels_[a]); v.push_back (&weightingCombos_[a]);
            v.push_back (&kLabels_[a]);         v.push_back (&kFields_[a]);
        }
        return v;
    }

    SPLMeterAudioProcessor& processor_;
    bool light_ = false;
    Palette pal_ = Palette::get (false);
    bool accelVisible_ = true, uniaxial_ = true;   // forces the first update
    juce::Rectangle<int> inputsCard_, accelCard_;
    juce::Component::SafePointer<juce::DialogWindow> calDialog_;

    juce::Label inputsTitle_, inputsDesc_, statusLabel_;
    juce::Label headerLabels_[6];
    juce::Viewport viewport_;
    juce::Component rowsHolder_;
    std::vector<std::unique_ptr<InputRow>> rows_;

    juce::Label accelTitle_, accelDesc_;
    juce::Label quantityLabel_, highPassLabel_, tauLabel_, bandLabel_, serialLabel_, rangeLabel_;
    juce::ComboBox quantityCombo_, tauCombo_;
    NumberField highPass_ { 0.1, 20.0, 2, "Hz" };
    juce::Label weightingLabels_[3], kLabels_[3];
    juce::ComboBox weightingCombos_[3];
    NumberField kFields_[3] { { 0.0, 2.0, 2 }, { 0.0, 2.0, 2 }, { 0.0, 2.0, 2 } };
    NumberField bandLow_ { 0.1, 1000.0, 1, "Hz" }, bandHigh_ { 1.0, 20000.0, 0, "Hz" };
    juce::TextEditor serialEditor_;
    NumberField lowerLimit_ { 0.01, 1000.0, 2, "Hz" }, upperLimit_ { 1.0, 100000.0, 0, "Hz" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputsComponent)
};

//==============================================================================
class InputsWindow  : public juce::DocumentWindow
{
public:
    InputsWindow (SPLMeterAudioProcessor& p, bool light)
        : juce::DocumentWindow ("Inputs & Sensors", juce::Colour (0xff1c1c1e), juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (false);
        content_ = new InputsComponent (p, light);
        content_->setSize (960, 720);
        setContentOwned (content_, true);
        setResizable (true, false);
        setResizeLimits (860, 420, 1600, 1400);
    }

    void setLightMode (bool light) { content_->setLightMode (light); }
    void closeButtonPressed() override { setVisible (false); }

private:
    InputsComponent* content_ = nullptr;   // owned by the window
    juce::TooltipWindow tooltipWindow_ { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputsWindow)
};
} // namespace InputsUi
