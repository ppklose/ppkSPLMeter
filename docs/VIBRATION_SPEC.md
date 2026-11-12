# SPLMeter — Vibration Measurement (Accelerometer) Specification

Status: **Draft v0.2** · 2026-11-12 · target: **v3.6.0**
(v0.2: open points resolved, weighting parameters and limit values cross-checked against public sources — see §14)

## 1. Goals

Add vibration measurement with IEPE accelerometers to SPLMeter, integrated seamlessly with the existing acoustic measurement — not a separate app mode.

| Decision | Value |
|---|---|
| Use cases | Machinery / condition monitoring, human exposure (whole-body + hand-arm), building vibration, structure-borne sound |
| Hardware | IEPE accelerometer → DC-coupled audio interface with IEPE supply |
| Axes | Triaxial from the start (uniaxial also supported) |
| Mixed use | Microphone and accelerometer simultaneously (e.g. mic on input 1, triax on inputs 2–4) |
| Layout | Vibration panel below the SPL meter, appears when an accelerometer is configured; shared timeline |
| Targets | Standalone, VST3, AU (iOS later) |
| Export | Vibration columns in CSV/JSON **and** a separate vibration report (HTML) |

### Non-goals

- SPLMeter does not claim conformance to ISO 8041 or DIN 45669 as a type-tested instrument. The report states this explicitly (§9.4).
- More than one accelerometer sensor at the same time (e.g. two triaxial sensors). The channel model allows it, but the UI and report in this version handle **one mic + one accelerometer sensor** (decided).
- Report languages other than English (decided: English first; strings are kept in one table so German can be added later).
- Hardware-specific integrations (TEDS readout, DAQ SDKs).

## 2. Current architecture (relevant parts)

- `PluginProcessor` receives up to 32 channels (standalone `discreteChannels(32)`); plugins accept 1–8 (`isBusesLayoutSupported`).
- Per-channel processing: optional mute → optional 20 Hz–20 kHz bandpass (48 dB/oct) → optional correction convolution → **mono mix** of active channels → A/C weighting → SPL detectors with `calOffset`.
- FFT: `fftCircBuf_` (mono mix) + `fftCircBufDual_[2]` (inputs 1 and 2) sharing `fftWritePos_`; `copyFftWindow(dest, size, channel)`.
- Timeline: `LogEntry` pushed every 125 ms.
- WAV recording: **stereo only** (`wavCircBuf_[2]`, writer with 2 channels).
- Non-parameter settings persist in the `UIState` child of `apvts.state` (v3.5.2).

## 3. Channel model

### 3.1 Input channel configuration

Each physical input gets a role. Stored in `UIState/Inputs` (non-automatable, persisted, included in JSON export/import).

```cpp
enum class SensorRole { Mic, AccelX, AccelY, AccelZ, AccelUniaxial, Off };

struct InputChannelConfig
{
    SensorRole role       = SensorRole::Mic;  // default keeps today's behaviour
    // Accelerometer only:
    double     calDb      = 140.0;   // dB re 1 µm/s² at 0 dBFS RMS sine (see §4)
    juce::String sensorSerial;       // free text, goes into the report
};
```

Rules:
- **Default: all channels `Mic`** → behaviour identical to v3.5.2. No migration needed.
- The **mono mix** and the **20 Hz bandpass** only include `Mic` channels. Accelerometer channels bypass both.
- Mute applies to all roles.
- Correction convolution (`correctionFile`) applies to `Mic` channels only.
- Validation: at most one of each `AccelX/Y/Z`, or exactly one `AccelUniaxial`, and not both kinds. Invalid configs are shown in red in Settings and the accelerometer path stays inactive.
- If a configured channel does not exist on the current device or bus (e.g. a stereo DAW track), the Vibration panel shows "Input 3 (Z) not available".

### 3.2 Accelerometer sensor settings

Stored in `UIState/Accel`:

| Setting | Values | Notes |
|---|---|---|
| `profile` | see §6 | Selects weighting, quantity, detectors, report contents |
| `highPassHz` | 0.1 … 20 Hz | DC removal ahead of integration; defaults come from the profile |
| `lowerLimitHz` | 0.1 … 20 Hz | Sensor's lower usable frequency (datasheet); readings and bands below are marked unreliable |
| `upperLimitHz` | 100 … fs/2 | Sensor's upper usable frequency (mounting resonance) |
| `displayUnit` | m/s², mm/s, µm, g, dB | dB = dB re 1 µm/s² / 1 nm/s / 1 pm (ISO 1683) |
| `kx`, `ky`, `kz` | 0 … 2 | Axis multiplying factors for the vector sum; defaults from the profile |
| `calDate` | date | Last calibration, goes into the report |

## 4. Calibration

Calibration works like `calOffset`: a per-channel dB offset that maps dBFS to **dB re 1 µm/s²**.

`level_dB = 20·log10(rms_FS) + calDb`; `a [m/s²] = 10^((level_dB − 120) / 20)`

Two methods are available, per axis, in a calibration dialog:

1. **Handheld calibrator** (preferred): the user mounts the sensor on a calibrator and selects the reference (default **10 m/s² RMS at 159.2 Hz = 140 dB**; also 1 g = 9.80665 m/s² and a custom value). SPLMeter measures for 3 s with a narrow bandpass around the reference frequency and sets `calDb`. It rejects the measurement if the frequency deviates by more than ±5 % or the level fluctuates by more than ±0.2 dB.
2. **Datasheet**: sensitivity *S* in mV/(m/s²) or mV/g, plus the interface's full-scale input voltage *V_FS* (Vrms of a full-scale sine, or dBu at 0 dBFS).
   `calDb = 120 + 20·log10(√2 · V_FS / S)` with *V_FS* in mV RMS and *S* in mV/(m/s²). The √2 is there because sample value 1.0 is the *peak* of a full-scale sine (implemented in `Source/Vibration/Calibration.h`).

The calibration range is wider than `calOffset` (80–180): **60–220 dB**.

## 5. Signal processing

### 5.1 Chain per accelerometer axis

```
raw (FS) ─► ×cal (m/s²) ─► HP (Butterworth, 2nd order, highPassHz)
        ├─► full-rate path: FFT buffer, structure-borne detectors, WAV recording
        └─► quantity (a / v = ∫a / d = ∬a) ─► decimate ÷D ─► weighting ─► detectors
```

- **Precision**: all IIR filters in the vibration path use `double` biquads (TDF-II). Poles at 0.4 Hz are very close to z = 1 at 48 kHz.
- **Implementation**: header-only classes in `Source/Vibration/` (`Biquad.h`, `Weighting.h`, `Decimator.h`, `Calibration.h`, `AccelChannel.h`), no JUCE dependency, unit-tested in `Tests/VibrationDspTests.cpp`.
- **Decimation**: the low-frequency path runs at `fs_d = fs / D`, where D is the largest power of two that keeps `fs_d ≥ 8 kHz` (44.1 kHz → D = 4, 11.025 kHz; 48 kHz → D = 4, 12 kHz; 96 kHz → D = 8, 12 kHz). 8 kHz is the lowest rate NI supports for Wh (upper band limit 1250 Hz); the whole-body weightings are supported from 1 kHz upward, so one shared decimated rate covers all weightings. A cascade of half-band FIR decimators (75 taps, Kaiser β = 9: passband ripple 0.0002 dB up to 0.2·fs_in, stopband −92 dB from 0.3·fs_in) is used. Structure-borne profiles use the full-rate path.
- **Integration**: each integrator is the section `s / (s² + ω_c/Q·s + ω_c²)` at the HP corner (Q = 1/√2), mapped bilinearly: it equals `1/s` above the corner but has no pole at DC, so offsets cannot make it drift. Displacement uses two of them. Integration runs at the **full** rate (0.14 % error at 1 kHz; at 12 kHz it would be 2.3 %), before the decimator.

### 5.2 Frequency weightings (ISO 8041-1)

Each weighting is the product of four sections, implemented as biquads via the bilinear transform with prewarping:

- **Band-limiting**: 2nd-order Butterworth HP at f1 × 2nd-order Butterworth LP at f2
- **a–v transition**: `(1 + s/(2πf3)) / (1 + s/(2πf4·Q4) + s²/(2πf4)²)`
- **Upward step**: `((1 + s/(2πf5·Q5) + s²/(2πf5)²) / (1 + s/(2πf6·Q6) + s²/(2πf6)²)) · (f5/f6)²`
- **Gain**: K

Parameters (Q1 = Q2 = 1/√2 for all; "∞" means the section is bypassed). Cross-checked against public sources ([V], [I]; see §14):

| Weighting | Use | f1 | f2 | f3 | f4 | Q4 | f5 | Q5 | f6 | Q6 | K | Status |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Wb | Whole-body Z (ISO 2631-4, rail) | 0.4 | 100 | 16 | 16 | 0.55 | 2.5 | 0.90 | 4.0 | 0.95 | **1.024** | [V] |
| Wc | Seat-back X | 0.4 | 100 | 8 | 8 | 0.63 | ∞ | — | ∞ | — | 1 | [V], [I] band |
| Wd | Whole-body X/Y | 0.4 | 100 | 2 | 2 | 0.63 | ∞ | — | ∞ | — | 1 | [V], [I] band |
| We | Rotational | 0.4 | 100 | 1 | 1 | 0.63 | ∞ | — | ∞ | — | 1 | [V], [I] band |
| Wj | Head, recumbent | 0.4 | 100 | ∞ | ∞ | — | 3.75 | 0.91 | 5.32 | 0.91 | 1 | [V], [I] band |
| Wk | Whole-body Z | 0.4 | 100 | 12.5 | 12.5 | 0.63 | 2.37 | 0.91 | 3.35 | 0.91 | 1 | [V], [I] band |
| Wm | Buildings (ISO 2631-2) | 10^−0.1 (0.794) | 100 | 1/(0.028·2π) (5.684) | 5.684 | 0.50 | ∞ | — | ∞ | — | 1 | [V] |
| Wh | Hand-arm (ISO 5349-1) | 10^0.8 (6.31) | 10^3.1 (1258.9) | 100/(2π) (15.915) | 15.915 | 0.64 | ∞ | — | ∞ | — | 1 | [V], [I] band |
| Wf | Motion sickness | 0.08 | 0.63 | ∞ | 0.25 | 0.86 | 0.0625 | 0.80 | 0.10 | 0.80 | 1 | [V], [I] band |

"[I] band" means that the band limits (two-pole Butterworth, 12 dB/oct) are confirmed verbatim by the ISO 8041 Amd.1:1999 preview text. The ISO 8041-1:2017 edition was not accessible; the tables in the unit tests (§11) are the final check.

Additionally:

- **KB** (DIN 4150-2 / DIN 45669-1): applied to **velocity**, `|H_KB(f)| = 1 / √(1 + (f0/f)²)` with f0 = 5.6 Hz, plus band limits 1–80 Hz.
- **Band-limit only**: Butterworth HP/LP with user-defined corners (e.g. 10–1000 Hz for ISO 20816).
- **None**: unweighted, full band.

Unit tests (`Tests/VibrationDspTests.cpp`):
- **Parameters vs. reference table [V]** (~300 values): ≤ 0.02 dB for all weightings except Wb (≤ 0.06 dB, systematic ~0.05 dB deviation, single source). Note: [V] evaluates Wm and Wh at exact 1/3-octave frequencies (10^(n/10)) and the others at nominal frequencies.
- **Digital vs. analog** at fs_d = 11.025 and 12 kHz, within each weighting's nominal range (Wk/Wd/Wc/We/Wj/Wb 0.5–80 Hz, Wm/KB 1–80 Hz, Wf 0.1–0.5 Hz, Wh 8–1000 Hz): ±0.1 dB (±0.2 dB in the top band). Two octaves outside: never more than 1 dB less attenuation than analog.

### 5.3 Detectors (per axis and for the vector sum)

| Detector | Definition | Used by |
|---|---|---|
| Running RMS | exponential, τ = 0.125 s (FAST) / 1 s (SLOW) / user-defined; or linear over the measurement | all |
| a_w,rms | linear RMS over the whole measurement | ISO 2631, ISO 5349 |
| MTVV | max of the running RMS with τ = 1 s | ISO 2631 |
| VDV | `(∫ a_w⁴ dt)^(1/4)` in m/s^1.75 | ISO 2631 |
| Crest factor | peak / a_w,rms | ISO 2631 (decides whether VDV is required, CF > 9) |
| Peak, peak-to-peak | with hold (existing `peakHoldTime`) | all |
| PPV | max \|v(t)\| per axis, plus the **dominant frequency** at the peak (zero-crossing half-period around the peak, cross-checked with the FFT) | DIN 4150-3 |
| KB_F(t) | running RMS of KB-weighted velocity, τ = 0.125 s | DIN 4150-2 |
| KB_Fmax | max KB_F(t) | DIN 4150-2 |
| KB_FTm / KB_FTr | max of KB_F per 30 s cycle; `KB_FTr = √(1/N · Σ KB_FTi²)` | DIN 4150-2 |

**Vector sum**: `a_v = √(kx²·ax² + ky²·ay² + kz²·az²)`, computed from the per-axis detectors (RMS-type values) or sample-wise (VDV, peak). The vector sum is labelled Σ.

Accumulating detectors (a_w,rms, MTVV, VDV, PPV, KB_FTr) reset together with the existing session reset (the same button that resets `sessionPeakDBCSPL_`). They pause during DAW-sync pauses, like the log.

### 5.4 Publishing to the GUI

As for SPL: the audio thread writes `std::atomic<float>` values, and the GUI polls them at 30 Hz. A `VibrationSnapshot` struct (per axis + Σ: running RMS, peak, plus the profile values) is published through a seqlock, or via a set of atomics with a version counter, so all values in one GUI frame are consistent.

## 6. Measurement profiles

A profile sets the defaults for weighting, quantity, filters, detectors, k-factors, the big readout value and the report section. Every value stays user-editable; once one is changed, the profile is shown as "Custom (based on …)".

| Profile | Quantity | Weighting per axis X / Y / Z | HP | k (x,y,z) | Main value | Assessment |
|---|---|---|---|---|---|---|
| Structure-borne | a | None | 1 Hz | 1,1,1 | L_a dB re 1 µm/s² | — |
| Machinery ISO 20816 | v | band 10–1000 Hz | 2 Hz | 1,1,1 | v_rms mm/s (max axis) | Zones A–D (§9.3) |
| Machinery low speed | v | band 2–1000 Hz | 1 Hz | 1,1,1 | v_rms mm/s | Zones A–D |
| Whole-body health (ISO 2631-1) | a | Wd / Wd / Wk | 0.1 Hz | 1.4,1.4,1 | a_w max axis, VDV, A(8) | EU 2002/44/EC values |
| Whole-body comfort (ISO 2631-1) | a | Wd / Wd / Wk | 0.1 Hz | 1,1,1 | a_v (Σ) | Comfort scale |
| Hand-arm (ISO 5349-1) | a | Wh / Wh / Wh | 2 Hz | 1,1,1 | a_hv (Σ), A(8) | EU 2002/44/EC values |
| Building – perception (DIN 4150-2) | v | KB on all axes | 0.5 Hz | — | KB_Fmax, KB_FTr | A_u / A_o / A_r |
| Building – structural (DIN 4150-3) | v | band 1–315 Hz | 0.5 Hz | — | PPV per axis + f_dom | Guideline lines |
| Custom | any | any | any | any | — | — |

## 7. UI

### 7.1 Settings → new "Inputs" section

- A table of the device's input channels (up to 8 shown; scrolls for 32): **Channel · Role · Level meter · Cal (dB) · Calibrate…**
- Role combobox: Mic / Accel X / Accel Y / Accel Z / Accel (uniaxial) / Off.
- An "Accelerometer" group appears below when any channel has an accelerometer role: profile, high-pass, lower/upper limit, unit, k-factors, sensor serial, calibration date.
- Psychoacoustic metrics, dBA/dBC and the Sound Detective continue to use the mic mix only. If no channel is `Mic`, the SPL meter shows "No microphone input" instead of meaningless values.

### 7.2 Vibration panel (below the SPL meter)

```
├─ Vibration · Whole-body health ──── [m/s² ▾] [Wd/Wk]┤
│ X ████░░░░░░░░░░  0.12 m/s²   pk 0.41               │
│ Y ███░░░░░░░░░░░  0.09 m/s²   pk 0.30               │
│ Z ██████░░░░░░░░  0.31 m/s²   pk 0.95               │
│ Σ ███████░░░░░░░  0.35 m/s²   VDV 1.8  A(8) 0.29 ●  │
```

- Shown only when an accelerometer is configured; the editor grows by the panel's height (respecting the persisted `extendedHeight` logic).
- Bars use a log scale (dB) internally; the readout is in `displayUnit`.
- Bars are tinted when frequency content below `lowerLimitHz` dominates (unreliable reading).
- Status dot: green / amber / red against the profile's assessment (action value, zone, guideline line).
- Light/dark mode and the iOS title-bar scaling follow existing conventions.

### 7.3 Timeline (LogComponent)

- `LogEntry` gets an optional `VibrationLog` block: per axis running RMS and peak, plus Σ and the profile's main value.
- New toggles next to dB SPL / dBA / dBC: **Vib X · Y · Z · Σ**.
- Vibration traces use the **right y-axis** (already used for psychoacoustic metrics) with its own range in `displayUnit`. If a psychoacoustic metric is shown at the same time, the vibration trace takes precedence on the right axis and the metric falls back to its normalized scale.
- Colours: X orange, Y cyan, Z violet, Σ white/black (colourblind-safe against the mic's green/red Dual FFT colours).

### 7.4 FFT and spectrogram

- Generalize `fftCircBufDual_[2]` to `fftCircBufCh_[kMaxFftChannels]` (8 channels × 524288 floats = 16 MB; allocate only for configured channels).
- `copyFftWindow(dest, size, channel)` keeps its signature; `channel` now indexes physical inputs.
- FFT channel selector replaces the Dual FFT toggle: **Mix · In 1 · In 2 · Dual (1+2) · Vib X/Y/Z · Vib Σ · Mic + Vib**. The existing `fftDual` parameter maps to "Dual (1+2)" so v3.5.2 settings load unchanged.
- Accelerometer FFT data is shown in dB re 1 µm/s² (or in the displayed quantity, converted per bin with 1/(2πf)). When mic and vibration are overlaid, the legend names each unit; both share the dB grid.
- 1/n-octave bands extend down to 0.5 Hz for accelerometer channels (needs FFT ≥ 131072 at 48 kHz; the UI suggests a size).
- Spectrogram: source selector for Mix or one accelerometer axis.

### 7.5 WAV recording

Extend recording to **all non-Off channels** (up to 8), and write channel roles and calibration as a `iXML` chunk (or a sidecar `.json`). This makes file-mode playback able to re-analyse a vibration recording with correct roles and calibration.

## 8. Persistence and export

- All settings in §3 live in `UIState`, so they persist across launches and are part of the plugin state (DAW sessions).
- JSON settings export/import: new `inputs` array and `accel` object.
- CSV log export: added columns `vibX_rms, vibY_rms, vibZ_rms, vibSum_rms, vibX_pk, …, vib_main, vib_unit, vib_profile`. These are empty when no accelerometer is configured, so existing column positions stay the same.

## 9. Vibration report

### 9.1 Format and trigger

- **Self-contained HTML** (inline CSS, inline SVG charts, no external resources), so it opens anywhere and prints to PDF from the browser. JUCE has no PDF writer.
- Save menu: "Save vibration report…". Enabled when an accelerometer is configured and the session has data.
- A dialog before saving asks for: project, location / measurement point, operator, sensor mounting, notes. The values persist (`UIState/Report`).
- Language: English in this version; strings centralized so German can be added.

### 9.2 Contents

1. **Header**: title, project, location, operator, date/time, duration, SPLMeter version.
2. **Setup**: interface (device name, sample rate), sensor serial, roles per channel, calibration (method, `calDb`, date), profile, filters (HP, weighting per axis, band limits), k-factors, `lowerLimitHz`/`upperLimitHz`.
3. **Results table**: per axis + Σ — RMS, peak, crest factor, plus the profile values (MTVV, VDV, PPV + f_dom, KB_Fmax, KB_FTr, v_rms).
4. **Assessment** (§9.3), with the reference values used and a clear pass / attention / exceed verdict.
5. **Charts**: time history per axis + Σ; 1/3-octave spectrum per axis (linear average over the measurement); for DIN 4150-3, PPV vs. dominant frequency plotted over the guideline lines.
6. **Events**: user markers and pauses from the timeline.
7. **Mic summary** (if a mic is configured): LAeq, LCeq, LAFmax for the same period.
8. **Disclaimer** (§9.4).

### 9.3 Assessment reference values

All limit values ship as **editable defaults** in Settings; the report lists the values actually used. Status markers refer to the sources in §14.

- **EU 2002/44/EC** ✔ [E][O]: hand-arm A(8) action 2.5 m/s², limit 5.0 m/s²; whole-body A(8) action 0.5 m/s², limit 1.15 m/s²; alternative whole-body VDV action 9.1, limit 21 m/s^1.75.
  **Germany** (LärmVibrationsArbSchV) ⚠ [D] only: whole-body limit 0.8 m/s² in Z, 1.15 m/s² in X/Y. Offered as a "Germany" preset; check the legal text before release.
  A(8) = a · √(T / 8 h), where *a* is the highest axis value with k-factors applied (whole-body) or a_hv (hand-arm). T is entered by the user (default: measurement duration).
- **ISO 2631-1 comfort scale** (unverified, from memory): < 0.315 not uncomfortable · 0.315–0.63 a little · 0.5–1 fairly · 0.8–1.6 uncomfortable · 1.25–2.5 very · > 2 m/s² extremely uncomfortable. Informational only, no pass/fail.
- **ISO 10816-3 / 20816-3** ✔ [M] (velocity RMS, mm/s), zone boundaries A/B, B/C, C/D:
  | | Rigid | Flexible |
  |---|---|---|
  | Group 1 (> 300 kW, or shaft height ≥ 315 mm) | 2.3 / 4.5 / 7.1 | 3.5 / 7.1 / 11.0 |
  | Group 2 (15–300 kW, or shaft height 160–315 mm) | 1.4 / 2.8 / 4.5 | 2.3 / 4.5 / 7.1 |
  The user selects the group and foundation. ISO 20816-3 keeps the same values.
- **DIN 4150-3**, short-term vibration ✔ [H][G][L] (PPV in mm/s at the foundation, for 1–10 Hz / 10–50 Hz / 50–100 Hz, linearly interpolated within each band; last column is the top floor, horizontal, all frequencies):
  | Line | Building type | 1–10 | 10–50 | 50–100 | Top floor |
  |---|---|---|---|---|---|
  | 1 | Commercial / industrial | 20 | 20–40 | 40–50 | 40 |
  | 2 | Residential | 5 | 5–15 | 15–20 | 15 |
  | 3 | Particularly sensitive / heritage-protected | 3 | 3–8 | 8–10 | 8 |
  Above 100 Hz, the 100 Hz values apply.
  **Long-term vibration** (top floor, horizontal): line 3 = 2.5 mm/s ✔ [T]; lines 1 = 10 and 2 = 5 mm/s ⚠ from memory, verify.
- **DIN 4150-2** ✔ [L] plus consistent values in several expert reports (KB values, dimensionless):
  | Zone | Day A_u / A_o / A_r | Night A_u / A_o / A_r |
  |---|---|---|
  | Industrial (GI) | 0.4 / 6 / 0.2 | 0.3 / 0.6 / 0.15 |
  | Commercial (GE) | 0.3 / 6 / 0.15 | 0.2 / 0.4 / 0.1 |
  | Mixed / village / core (MI, MD, MK) | 0.2 / 5 / 0.1 | 0.15 / 0.3 / 0.07 |
  | Residential (WR, WA) | 0.15 / 3 / 0.07 | 0.1 / 0.2 / 0.05 |
  | Hospitals, spa areas (SO) | 0.1 / 3 / 0.05 | 0.1 / 0.15 / 0.05 |
  Values are from the 1999 edition. Special rules (e.g. a different A_o at night for rail traffic, construction-site tables) are not modelled; the user edits the values in such cases.
  Assessment logic: KB_Fmax ≤ A_u → requirement met; KB_Fmax > A_o → not met; otherwise KB_FTr ≤ A_r → met.

### 9.4 Disclaimer (fixed text)

> Measured with SPLMeter vX.Y.Z using a general-purpose audio interface. The measurement chain has not been type-tested to ISO 8041-1 / DIN 45669-1. Results are indicative and do not replace a measurement with a certified instrument for legal or contractual assessments.

## 10. Plugin specifics

- Input bus: keep 1–8 channels for VST3/AU. Accelerometer roles only make sense when the host provides more than 2 channels. With a stereo track, the Inputs table shows only channels 1–2.
- DAW sync and pause behaviour apply to vibration detectors as they do to SPL.
- Vibration values are not exposed as automatable output parameters in this version.

## 11. Testing

Catch2 unit tests (`Tests/`):

- Weighting responses at 1/3-octave frequencies vs. the ISO 8041-1 tables (±0.1 dB), at 44.1 kHz and 48 kHz (decimated path).
- Calibration: synthetic 159.2 Hz sine at −20 dBFS with calibrator method → `calDb` such that the sine reads exactly 140 dB; datasheet formula round trip.
- Integration: a sine of acceleration *a* at *f* reads `v = a / (2πf)` and `d = a / (2πf)²` within 0.5 % for f ≥ 5·HP.
- VDV of a sine of RMS *a* over *T*: `a · (1.5·T)^(1/4)`.
- MTVV of a burst; crest factor of a known waveform.
- KB_FTr on a synthetic signal with known per-cycle maxima.
- PPV + dominant frequency of a decaying sine.
- Vector sum with k-factors.
- Channel model: default config gives a bit-identical SPL output to v3.5.2 on a reference WAV (regression).

Manual verification: handheld calibrator per axis; compare a short machinery and a structure-borne measurement against a reference instrument if one is available.

## 12. Implementation plan

### Phase 1a — Foundation

1. Channel model in `UIState/Inputs`; mono mix and bandpass take `Mic` channels only; regression test (bit-identical default).
2. Per-channel FFT buffers (`fftCircBufCh_`), FFT channel selector replacing the Dual FFT toggle (with `fftDual` mapping).
3. Accelerometer DSP: calibration, HP, decimation, integration, weightings (§5.1–5.2), with unit tests.
4. Detectors: running RMS, peak, peak-to-peak, vector sum; `VibrationSnapshot` publishing.
5. Settings "Inputs" section, calibration dialog (calibrator + datasheet).
6. Vibration panel.
7. Timeline vibration traces + CSV columns + JSON export/import.
8. Multichannel WAV recording with role and calibration metadata; file-mode playback respects it.

### Phase 1b — Assessment and report

9. Profiles (§6) incl. "Custom (based on …)".
10. Accumulating detectors: a_w,rms, MTVV, VDV, crest factor, PPV + f_dom, KB_F/KB_Fmax/KB_FTr; session reset integration.
11. Assessment engine with editable limits (§9.3) and the panel's status dot.
12. HTML report (§9) with inline SVG charts.
13. README + user documentation; version bump to **v3.6.0** (CMakeLists.txt, SPLMeter.jucer, JucePluginDefines.h, PluginEditor version label).

### Later

- iPad layout for the Vibration panel.
- Multiple accelerometer sensors; per-sensor reports.
- German report strings.
- TEDS and sensor database (sensitivity per serial number).

## 13. Decisions and remaining checks

Decided (2026-11-12):
1. One accelerometer sensor (triaxial or uniaxial) plus the mic in this version.
2. Report in English first.
3. Parameters and limits were cross-checked against public sources (§14).
4. DIN 4150-2 zone values ship as editable defaults.

Remaining ⚠ items to check before release:
- DIN 4150-3 long-term values for lines 1 and 2 (10 / 5 mm/s).
- The German whole-body Z limit of 0.8 m/s² (LärmVibrationsArbSchV legal text).
- The ISO 2631-1 comfort scale bands.
- The Wb gain K = 1.024 and the Wm/Wb parameters (single source [V]). The unit-test tables will catch any inconsistency between parameters and response.
- The KB weighting definition (f0 = 5.6 Hz) and the DIN 4150-2 assessment flow; neither has been cross-checked against a public source yet.

## 14. Sources

- [V] Virtins Technology, *Frequency Weightings for Sound and Vibration Perceived by Humans* (2024), parameter table and 1/3-octave reference values: https://www.virtins.com/doc/Frequency-Weightings-for-Sound-and-Vibration-Perceived-by-Humans-using-Multi-Instrument.pdf
- [I] ISO 8041:1990/Amd.1:1999 preview (band-limiting definition): https://cdn.standards.iteh.ai/samples/27055/fe924c0e30794197b3c57e15abffdb30/ISO-8041-1990-Amd-1-1999.pdf
- [N] NI, Human vibration weighting filters (supported sample rates): https://www.ni.com/en/support/documentation/supplemental/08/human-vibration-weighting-filters-in-labview-sound-and-vibration.html
- [R] Rimell & Mansfield, *Design of digital filters for frequency weightings required for risk assessments of workers exposed to vibration*, Industrial Health 45 (2007) 512–519: https://www.jstage.jst.go.jp/article/indhealth/45/4/45_4_512/_article (digital IIR implementation for any sample rate)
- [E] Directive 2002/44/EC (consolidated): https://eur-lex.europa.eu/eli/dir/2002/44/2019-07-26/eng
- [O] OSHwiki, Vibration: https://oshwiki.osha.europa.eu/en/themes/vibration
- [D] IFA/DGUV, Whole-body and hand-arm vibration: https://www.dguv.de/ifa/fachinfos/vibrationen/index-2.jsp
- [M] ISO 10816-3 zone tables, e.g. https://vibromera.eu/glossary/iso-10816-3/ and https://www.fabrico.io/blog/iso-10816-3-vibration-severity/
- [L] LfU Bayern, Erschütterungen (DIN 4150-2 day/night tables, DIN 4150-3 excerpt): https://www.lfu.bayern.de/laerm/erschuetterungen/index.htm
- [H] HLNUG, Einwirkungen auf Bauwerke: https://www.hlnug.de/themen/erschuetterungen/beurteilung/einwirkungen-auf-bauwerke
- [G] Bundesverband Geothermie, Schadensgrenze: https://www.geothermie.de/bibliothek/lexikon-der-geothermie/s/schadensgrenze-erschuetterungen
- [T] terrana Geophysik, Regelwerke DIN 4150 und LAI: https://www.terrana-geophysik.de/images/Methode_Erschuetterung.htm
