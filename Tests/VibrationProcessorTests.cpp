#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ProcessorTestHelpers.h"

#include <chrono>

// Vibration spec step 4: accelerometer engine inside the processor — routing,
// calibration, detectors, vector sum, engine hand-over, pause/reset, persistence.

using Catch::Approx;
using namespace ProcTest;

namespace
{
using SignalPerChannel = std::function<float (int ch, int n)>;

struct Rig
{
    std::unique_ptr<SPLMeterAudioProcessor> proc;
    int numInputs = 0;
    int n = 0;   // samples processed so far

    explicit Rig (int inputs) : numInputs (inputs)
    {
        proc = std::make_unique<SPLMeterAudioProcessor>();
        auto layout = proc->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (inputs);
        REQUIRE (proc->setBusesLayout (layout));
    }

    void prepare() { proc->prepareToPlay (kSampleRate, kBlockSize); }

    void run (double seconds, const SignalPerChannel& signal)
    {
        juce::AudioBuffer<float> buffer (juce::jmax (proc->getTotalNumInputChannels(),
                                                     proc->getTotalNumOutputChannels()), kBlockSize);
        juce::MidiBuffer midi;
        const int blocks = (int) (seconds * kSampleRate) / kBlockSize;
        for (int b = 0; b < blocks; ++b, n += kBlockSize)
        {
            buffer.clear();
            for (int ch = 0; ch < numInputs; ++ch)
                for (int s = 0; s < kBlockSize; ++s)
                    buffer.setSample (ch, s, signal (ch, n + s));
            proc->processBlock (buffer, midi);
        }
    }

    vib::VibrationSnapshot snap() const { return proc->getVibrationSnapshot(); }
};

// Unit calibration: 1 FS = 1 m/s² (calDb 120). τ = 0.125 s settles fast but ripples
// at low frequencies (±4 % RMS at 8 Hz) — low-frequency checks use τ = 1 s.
void configureTriax (SPLMeterAudioProcessor& p, int firstInput, vib::VibrationSettings s = {},
                     double tauSec = 0.125)
{
    p.setChannelRole (firstInput,     SensorRole::AccelX);
    p.setChannelRole (firstInput + 1, SensorRole::AccelY);
    p.setChannelRole (firstInput + 2, SensorRole::AccelZ);
    for (int i = 0; i < 3; ++i) p.setAccelCalDb (firstInput + i, 120.0);
    s.tauSec = tauSec;
    p.setVibrationSettings (s);
}

const double kRms = 1.0 / std::sqrt (2.0);
} // namespace

TEST_CASE ("Vibration in processor: triaxial RMS, peak, peak-to-peak and vector sum", "[vibration][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Rig rig (4);   // mic on input 1, triax on inputs 2–4
    configureTriax (*rig.proc, 1);
    rig.prepare();
    rig.run (3.0, [] (int ch, int n)
    {
        switch (ch)
        {
            case 0:  return sine (1000.0, 0.1, n);   // microphone
            case 1:  return sine (80.0,  1.0, n);    // X: 1 m/s² peak
            case 2:  return sine (160.0, 2.0, n);    // Y: 2 m/s² peak
            default: return sine (40.0,  0.5, n);    // Z: 0.5 m/s² peak
        }
    });

    const auto s = rig.snap();
    REQUIRE (s.valid);
    CHECK (s.axisMask == 0b111);
    CHECK_FALSE (s.uniaxial);
    CHECK (s.axis[vib::AxisX].rms == Approx (1.0 * kRms).epsilon (0.01));
    CHECK (s.axis[vib::AxisY].rms == Approx (2.0 * kRms).epsilon (0.01));
    CHECK (s.axis[vib::AxisZ].rms == Approx (0.5 * kRms).epsilon (0.01));
    CHECK (s.axis[vib::AxisX].peak == Approx (1.0).epsilon (0.01));
    CHECK (s.axis[vib::AxisX].p2p  == Approx (2.0).epsilon (0.01));
    CHECK (s.axis[vib::AxisY].peakMax == Approx (2.0).epsilon (0.01));
    CHECK (s.axis[vib::AxisSum].rms == Approx (std::sqrt (0.5 + 2.0 + 0.125)).epsilon (0.01));
    CHECK (s.seconds == Approx (3.0).margin (0.15));

    // The microphone path is unaffected: 0.1 peak sine = −23 dBFS → 104 dB at calOffset 127
    CHECK (rig.proc->getRMSSPL() == Approx (103.99).margin (0.05));
}

TEST_CASE ("Vibration in processor: k-factors weight the vector sum", "[vibration][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    vib::VibrationSettings settings;
    settings.k[0] = settings.k[1] = 1.4;   // ISO 2631-1 health, seated
    Rig rig (3);
    configureTriax (*rig.proc, 0, settings);
    rig.prepare();
    rig.run (3.0, [] (int ch, int n) { return sine (20.0 + 30.0 * ch, 1.0, n); });

    const auto s = rig.snap();
    REQUIRE (s.valid);
    CHECK (s.axis[vib::AxisSum].rms == Approx (std::sqrt (1.96 * 0.5 + 1.96 * 0.5 + 0.5)).epsilon (0.01));
}

TEST_CASE ("Vibration in processor: calibration, integration and weighting", "[vibration][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("calibrator-derived calDb reads 10 m/s²")
    {
        Rig rig (1);
        rig.proc->setChannelRole (0, SensorRole::AccelUniaxial);
        rig.proc->setAccelCalDb (0, vib::calibration::calDbFromCalibrator (std::pow (10.0, -20.0 / 20.0)));
        vib::VibrationSettings s; s.tauSec = 0.125;
        rig.proc->setVibrationSettings (s);
        rig.prepare();
        const double amp = std::pow (10.0, -20.0 / 20.0) * std::sqrt (2.0);   // −20 dBFS RMS
        rig.run (2.0, [amp] (int, int n) { return sine (159.2, amp, n); });

        const auto snap = rig.snap();
        REQUIRE (snap.valid);
        CHECK (snap.uniaxial);
        CHECK (snap.axisMask == 0b001);
        CHECK (snap.axis[vib::AxisX].rms == Approx (10.0).epsilon (0.01));
        CHECK (snap.axis[vib::AxisSum].rms == Approx (snap.axis[vib::AxisX].rms).epsilon (1e-6));
    }

    SECTION ("velocity")
    {
        vib::VibrationSettings s;
        s.quantity = vib::Quantity::Velocity;
        Rig rig (3);
        configureTriax (*rig.proc, 0, s);
        rig.prepare();
        rig.run (3.0, [] (int, int n) { return sine (50.0, 1.0, n); });
        const auto snap = rig.snap();
        REQUIRE (snap.valid);
        CHECK (snap.quantity == (int) vib::Quantity::Velocity);
        CHECK (snap.axis[vib::AxisZ].rms == Approx (1.0 / (2.0 * vib::kPi * 50.0) * kRms).epsilon (0.01));
    }

    SECTION ("Wd on X/Y, Wk on Z")
    {
        vib::VibrationSettings s;
        s.highPassHz   = 0.1;
        s.weighting[0] = s.weighting[1] = vib::Weighting::Wd;
        s.weighting[2] = vib::Weighting::Wk;
        Rig rig (3);
        configureTriax (*rig.proc, 0, s, 1.0);
        rig.prepare();
        rig.run (10.0, [] (int, int n) { return sine (8.0, 1.0, n); });
        const auto snap = rig.snap();
        REQUIRE (snap.valid);
        CHECK (snap.weighting[2] == (int) vib::Weighting::Wk);
        const double dbX = 20.0 * std::log10 (snap.axis[vib::AxisX].rms / kRms);
        const double dbZ = 20.0 * std::log10 (snap.axis[vib::AxisZ].rms / kRms);
        CHECK (dbX == Approx (20.0 * std::log10 (vib::analogMagnitude (vib::Weighting::Wd, 8.0))).margin (0.15));
        CHECK (dbZ == Approx (20.0 * std::log10 (vib::analogMagnitude (vib::Weighting::Wk, 8.0))).margin (0.15));
    }
}

TEST_CASE ("Vibration in processor: engine follows role changes at runtime", "[vibration][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Rig rig (3);
    rig.prepare();
    auto signal = [] (int ch, int n) { return sine (100.0, 0.5 + 0.25 * ch, n); };

    rig.run (0.5, signal);
    CHECK_FALSE (rig.snap().valid);   // no accelerometer → no readout

    configureTriax (*rig.proc, 0);    // while "running"
    rig.run (1.5, signal);
    auto s = rig.snap();
    REQUIRE (s.valid);
    CHECK (s.axis[vib::AxisY].rms == Approx (0.75 * kRms).epsilon (0.02));

    rig.proc->setChannelRole (1, SensorRole::AccelX);   // duplicate X → invalid config
    rig.run (0.5, signal);
    CHECK_FALSE (rig.snap().valid);

    rig.proc->setChannelRole (1, SensorRole::AccelY);
    rig.proc->setChannelRole (2, SensorRole::Mic);      // biaxial X/Y is fine
    rig.run (1.0, signal);
    s = rig.snap();
    REQUIRE (s.valid);
    CHECK (s.axisMask == 0b011);
}

TEST_CASE ("Vibration in processor: inputs the device lacks are ignored", "[vibration][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Rig rig (2);   // e.g. a stereo DAW track
    configureTriax (*rig.proc, 0);   // Z would be input 3, which does not exist
    rig.prepare();
    rig.run (1.0, [] (int, int n) { return sine (100.0, 1.0, n); });
    const auto s = rig.snap();
    REQUIRE (s.valid);
    CHECK (s.axisMask == 0b011);
}

TEST_CASE ("Vibration in processor: pause holds the detectors, session reset clears maxima", "[vibration][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Rig rig (3);
    configureTriax (*rig.proc, 0);
    rig.prepare();
    // Reset only after settling: peakMax legitimately includes the high-pass
    // start-up transient, and the filters still hold ~2 ms of earlier signal
    rig.run (0.5, [] (int, int n) { return sine (100.0, 2.0, n); });
    rig.proc->resetSessionPeak();
    rig.run (1.0, [] (int, int n) { return sine (100.0, 2.0, n); });
    const auto before = rig.snap();
    REQUIRE (before.valid);
    CHECK (before.axis[vib::AxisX].peakMax == Approx (2.0).epsilon (0.01));

    rig.proc->setPaused (true);
    rig.run (1.0, [] (int, int n) { return sine (100.0, 5.0, n); });   // ignored while paused
    const auto paused = rig.snap();
    CHECK (paused.seconds == Approx (before.seconds));
    CHECK (paused.axis[vib::AxisX].peakMax == Approx (2.0).epsilon (0.01));
    rig.proc->setPaused (false);

    rig.run (1.0, [] (int, int n) { return sine (100.0, 0.5, n); });   // let the 1 Hz HP transient of the level jump decay
    rig.proc->resetSessionPeak();
    rig.run (1.0, [] (int, int n) { return sine (100.0, 0.5, n); });
    const auto after = rig.snap();
    CHECK (after.axis[vib::AxisX].peakMax == Approx (0.5).epsilon (0.02));
    CHECK (after.seconds == Approx (1.0).margin (0.15));
}

TEST_CASE ("Vibration in processor: settings and calibration persist in the plugin state", "[vibration][processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto proc = std::make_unique<SPLMeterAudioProcessor>();
    vib::VibrationSettings s;
    s.highPassHz   = 0.4;
    s.quantity     = vib::Quantity::Displacement;
    s.weighting[0] = vib::Weighting::Wd;
    s.weighting[2] = vib::Weighting::Wk;
    s.bandLowHz    = 2.0;
    s.bandHighHz   = 500.0;
    s.tauSec       = 0.125;
    s.k[0] = 1.4; s.k[1] = 1.4; s.k[2] = 1.0;
    proc->setVibrationSettings (s);
    proc->setChannelRole (2, SensorRole::AccelZ);
    proc->setAccelCalDb (2, 153.25);

    juce::MemoryBlock blob;
    proc->getStateInformation (blob);
    auto restored = std::make_unique<SPLMeterAudioProcessor>();
    restored->setStateInformation (blob.getData(), (int) blob.getSize());

    const auto r = restored->getVibrationSettings();
    CHECK (r.highPassHz == Approx (0.4));
    CHECK (r.quantity == vib::Quantity::Displacement);
    CHECK (r.weighting[0] == vib::Weighting::Wd);
    CHECK (r.weighting[1] == vib::Weighting::None);
    CHECK (r.weighting[2] == vib::Weighting::Wk);
    CHECK (r.bandLowHz == Approx (2.0));
    CHECK (r.bandHighHz == Approx (500.0));
    CHECK (r.tauSec == Approx (0.125));
    CHECK (r.k[0] == Approx (1.4));
    CHECK (restored->getAccelCalDb (2) == Approx (153.25));
    CHECK (restored->getAccelCalDb (5) == Approx (140.0));   // default
    CHECK (restored->getChannelRole (2) == SensorRole::AccelZ);
}

TEST_CASE ("Vibration in processor: sensor info and calibration date persist", "[vibration][processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto proc = std::make_unique<SPLMeterAudioProcessor>();
    CHECK (proc->getAccelCalInfo (1).isEmpty());
    proc->setAccelSensorInfo ({ "SN 4711", 0.3, 8000.0 });
    proc->setAccelCalInfo (1, "calibrator");
    CHECK (proc->getVibrationDisplayUnit() == vib::DisplayUnit::Linear);
    proc->setVibrationDisplayUnit (vib::DisplayUnit::Db);

    juce::MemoryBlock blob;
    proc->getStateInformation (blob);
    auto restored = std::make_unique<SPLMeterAudioProcessor>();
    restored->setStateInformation (blob.getData(), (int) blob.getSize());

    const auto info = restored->getAccelSensorInfo();
    CHECK (info.serial == "SN 4711");
    CHECK (info.lowerLimitHz == Approx (0.3));
    CHECK (info.upperLimitHz == Approx (8000.0));
    CHECK (restored->getVibrationDisplayUnit() == vib::DisplayUnit::Db);
    CHECK (restored->getAccelCalInfo (1)
           == juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + " · calibrator");
}

TEST_CASE ("Vibration in processor: input level meters", "[vibration][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Rig rig (3);
    rig.prepare();
    rig.run (1.0, [] (int ch, int n) { return ch == 2 ? sine (159.2, 0.5, n) : 0.0f; });
    CHECK (rig.proc->getInputRms (2) == Approx (0.5 * kRms).epsilon (0.01));
    CHECK (rig.proc->getInputRms (0) < 1.0e-9f);
    CHECK (rig.proc->takeInputPeak (2) == Approx (0.5).epsilon (0.001));
    CHECK (rig.proc->takeInputPeak (2) < 1.0e-9f);   // reset by the read
}

TEST_CASE ("Vibration in processor: calibrator measurement on a live input", "[vibration][processor][calibration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Rig rig (4);
    rig.proc->setChannelRole (3, SensorRole::AccelUniaxial);
    vib::VibrationSettings vs;
    vs.tauSec = 0.125;
    rig.proc->setVibrationSettings (vs);
    rig.prepare();
    vib::CalibratorMeasurement::Result r;

    // Calibrator (10 m/s² RMS) reads −26 dBFS RMS on input 4
    const double amp = std::sqrt (2.0) * std::pow (10.0, -26.0 / 20.0);
    auto signal = [amp] (int ch, int n) { return ch == 3 ? sine (159.2, (float) amp, n) : 0.0f; };

    REQUIRE_FALSE (rig.proc->startAccelCalibration (7, 159.2));   // no such input
    REQUIRE (rig.proc->startAccelCalibration (3, 159.2));
    CHECK_FALSE (rig.proc->startAccelCalibration (3, 159.2));     // already running
    rig.run (1.0, signal);
    CHECK_FALSE (rig.proc->takeAccelCalibrationResult (r));
    CHECK (rig.proc->getAccelCalibrationProgress() > 0.2);
    rig.run (3.0, signal);
    REQUIRE (rig.proc->takeAccelCalibrationResult (r));
    CHECK (r.status == vib::CalibratorMeasurement::Status::Ok);
    const double calDb = vib::calibration::calDbFromCalibrator (r.rmsFS);
    CHECK (calDb == Approx (166.0).margin (0.01));

    // Applying it makes the engine read the calibrator level
    rig.proc->setAccelCalDb (3, calDb);
    rig.run (3.0, signal);
    CHECK (rig.snap().axis[vib::AxisX].rms == Approx (10.0).epsilon (0.005));

    SECTION ("cancel stops a running measurement")
    {
        REQUIRE (rig.proc->startAccelCalibration (3, 159.2));
        rig.run (0.5, signal);
        rig.proc->cancelAccelCalibration();
        rig.run (0.1, signal);
        rig.run (5.0, signal);
        CHECK_FALSE (rig.proc->takeAccelCalibrationResult (r));
        CHECK (rig.proc->startAccelCalibration (3, 159.2));   // idle again
    }
}

TEST_CASE ("Vibration in processor: CPU budget", "[vibration][processor][performance]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto timeFor = [] (bool withTriax)
    {
        Rig rig (4);
        if (withTriax)
        {
            vib::VibrationSettings s;
            s.quantity     = vib::Quantity::Velocity;
            s.weighting[0] = s.weighting[1] = vib::Weighting::Wd;
            s.weighting[2] = vib::Weighting::Wk;
            configureTriax (*rig.proc, 1, s);
        }
        rig.prepare();
        auto signal = [] (int ch, int n) { return sine (50.0 * (ch + 1), 0.3, n) + noise (ch, n, 0.05f); };
        rig.run (0.5, signal);   // warm-up
        const auto t0 = std::chrono::steady_clock::now();
        rig.run (10.0, signal);
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    };

    const double micOnly = timeFor (false);
    const double withVib = timeFor (true);
    const double overheadPct = (withVib - micOnly) / 10.0 * 100.0;   // % of one core in real time
    WARN ("10 s of audio: mic only " << micOnly * 1000.0 << " ms, mic + triax "
          << withVib * 1000.0 << " ms → vibration overhead " << overheadPct << " % of one core");
    CHECK (overheadPct < 5.0);
}
