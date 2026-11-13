# Vibration Measurement — User Guide

SPLMeter 3.6 measures vibration with an IEPE accelerometer alongside the microphone. One sensor (triaxial or uniaxial) and the microphone can run at the same time: the SPL meter keeps working on the mic, and a Vibration panel appears below it.

> SPLMeter is not a type-tested vibration meter (ISO 8041-1 / DIN 45669-1). Results are indicative. For legal or contractual assessments, use a certified instrument.

---

## 1. What you need

- An **IEPE accelerometer** (triaxial or uniaxial).
- An audio interface with **IEPE (ICP) supply** and **DC-coupled** or very low-frequency inputs. A normal mic preamp's AC coupling (often 10–20 Hz) cuts off the low frequencies that building and human-exposure measurements need.
- The sensor's datasheet: sensitivity (mV/(m/s²) or mV/g) and usable frequency range — or a handheld calibrator (e.g. 10 m/s² at 159.2 Hz).

---

## 2. Assign the inputs

**Settings → INPUT CHANNELS → Inputs and Sensors…** opens a resizable window with every active input.

1. Set the **Role** of each input: *Mic*, *Accel X*, *Accel Y*, *Accel Z*, *Accel (uniaxial)* or *Off*.
   - Only *Mic* inputs feed the SPL meter, its bandpass and the correction filter.
   - Use either X/Y/Z (one, two or three axes) **or** one uniaxial input. An axis assigned twice, or uniaxial mixed with X/Y/Z, is shown in red and the accelerometer stays inactive.
2. The status line shows the detected sensor, e.g. *"Triaxial accelerometer: X = IN 2, Y = IN 3, Z = IN 4"*.
3. The channel buttons in Settings show the role as a small tag (X / Y / Z / ACC / OFF).

The standalone app opens up to 32 inputs. Plugins (VST3/AU) see the channels the host gives the track (1–8); use a multichannel track for an accelerometer.

---

## 3. Calibrate each axis

Click **Calibrate…** in an accelerometer row. The value is the level in **dB re 1 µm/s²** that a full-scale (0 dBFS) sine would read.

- **Handheld calibrator** (preferred): mount the sensor on the calibrator, choose the reference (10 m/s² RMS at 159.2 Hz = 140 dB, 1 g, or a custom level/frequency) and click **Measure**. SPLMeter settles for 0.5 s, measures for 3 s and checks frequency (±5 %) and stability (±0.2 dB). Click **Apply**.
- **Datasheet**: enter the sensitivity (mV/(m/s²) or mV/g) and the interface's full scale (Vrms or dBu at 0 dBFS). The result updates live; click **Apply**.
- You can also type the value directly into the *Cal* column.

Each axis keeps its own value plus the date and method, which go into the report.

---

## 4. Choose a profile

The **ACCELEROMETER** card below the input table sets everything that applies to all axes. A **Profile** fills in sensible defaults; every value stays editable, and after a change the profile reads *"Custom (based on …)"*.

| Profile | Quantity | Weighting | Main value | Assessment |
|---|---|---|---|---|
| Structure-borne | acceleration | none | L_a in dB re 1 µm/s² | — |
| Machinery (ISO 20816) | velocity | 10–1000 Hz | v_rms (largest axis) | zones A–D |
| Machinery, low speed | velocity | 2–1000 Hz | v_rms | zones A–D |
| Whole-body health (ISO 2631-1) | acceleration | Wd / Wd / Wk, k = 1.4 / 1.4 / 1 | a_w (largest k·axis) | EU / Germany action and limit values |
| Whole-body comfort (ISO 2631-1) | acceleration | Wd / Wd / Wk | a_v (vector sum) | comfort scale (informative) |
| Hand-arm (ISO 5349-1) | acceleration | Wh | a_hv (vector sum) | EU action and limit values |
| Building, perception (DIN 4150-2) | velocity | KB | KB_Fmax | A_u / A_o / A_r |
| Building, structural (DIN 4150-3) | velocity | 1–315 Hz | PPV with dominant frequency | guideline values |

Other settings in the card: quantity, high-pass, RMS time constant, weighting and k-factor per axis, band limits, **main value**, sensor serial, the sensor's **usable range** (from the datasheet) and the **daily exposure time T** for A(8).

---

## 5. Set up the assessment

The **ASSESSMENT** card shows the controls the current profile needs. Selecting a standard, group, zone or line fills in its reference values; you can edit each value, and **Defaults** reloads the standard's values.

| Profile | Choices | Values |
|---|---|---|
| Whole-body health | EU 2002/44/EC or Germany (LärmVibrationsArbSchV); criterion A(8) or VDV | action 0.5 m/s²; limit 1.15 m/s² (Germany: Z 0.8 m/s²); VDV 9.1 / 21 m/s^1.75 |
| Hand-arm | — | action 2.5 m/s², limit 5 m/s² A(8) |
| Comfort | — | informative scale, no pass/fail |
| Machinery | machine group 1/2, rigid/flexible foundation | zone boundaries A/B, B/C, C/D |
| Building, perception | zone (industrial … hospitals), day/night | A_u, A_o, A_r, exposure time T_e |
| Building, structural | line 1/2/3, short-/long-term, foundation / top floor / floor slab | values at 1–10, 50, 100 Hz or one value |

How the result is found:

- **Whole-body health**: A(8) = k·a_w · √(T / 8 h) of each axis against the action value (amber) and the limit value of that axis (red). With *VDV*: the largest k·VDV, scaled to the daily exposure.
- **Hand-arm**: A(8) of a_hv.
- **Machinery**: the largest axis velocity RMS over the measurement → zone A/B (green), C (amber), D (red).
- **DIN 4150-2**: KB_Fmax ≤ A_u → met; KB_Fmax > A_o → not met; otherwise KB_FTr ≤ A_r → met, where KB_FTr = KB_FTm · √(T_e / T_r) (T_r = 16 h by day, 8 h at night).
- **DIN 4150-3**: the peak velocity (PPV) of each axis against the value at its dominant frequency (foundation, interpolated between 10, 50 and 100 Hz), or one value for the top floor (X/Y) or a floor slab (Z).

Rail-specific rules of DIN 4150-2:2025 and the construction-site table are not built in — enter the values yourself in those cases.

---

## 6. Measure

The **Vibration panel** below the SPL meter shows one row per axis plus the vector sum Σ: a log-scale bar with the running RMS, the interval peak and the maximum since reset, the readout and the unit selector (linear, g, or dB re ISO 1683).

- The header shows the profile, quantity, weightings and time constant, and the main value on the right.
- The results line underneath shows the measured time and the profile's secondary values (A(8), VDV, MTVV, crest factor; KB_FTr; or PPV axis and f_dom), and on the right a **status dot** with the assessment: green ok, amber warning, red exceeded, blue informative.
- After a start or a settings change, the filters settle for a moment (0.8 s ÷ high-pass, 0.5–10 s); the readout shows "--" meanwhile.
- **Reset** restarts all accumulated values (a_w, MTVV, VDV, PPV, KB values, the spectrum) together with the session peaks. **Pause** and DAW sync hold them, like the SPL log.

In Advanced mode the **timeline** gets *Vib X · Y · Z · Σ* toggles; vibration traces use the right axis.

---

## 7. Record and export

- **Save WAV** with an accelerometer records every non-Off input (up to 8 tracks, 24-bit) and stores each track's role and calibration in the file. Playing the file back (File mode) uses that information, so the Vibration panel, timeline and CSV work during playback.
- **Save CSV** adds the columns `vibX_rms … vibSum_pk, vib_main, vib_unit, vib_profile` in the display unit.
- **Save Settings / Load Settings** include the input roles, calibration, accelerometer settings and assessment values.
- **Save Vibration Report…** (Ex-/Import menu) writes one HTML file that opens in any browser and prints to PDF. A dialog asks for project, location, operator, sensor mounting and notes (remembered for the next report). The report contains:
  - measurement and setup (interface, sensor, roles, calibration, profile, filters, k-factors),
  - a results table per axis and Σ,
  - the assessment with the reference values used,
  - the time history, the 1/3-octave spectrum per axis (1 Hz – 1250 Hz, averaged over the measurement), and for DIN 4150-3 a PPV over frequency chart with the guideline lines,
  - markers and pauses, the microphone's LAeq / LCeq / LAFmax,
  - the disclaimer.

  The time history and sound levels cover the log period (log duration); the accumulated results cover the time since the last reset. The report states both.

---

## 8. Tips

- Mount the sensor rigidly (stud or glue); a magnet or a handheld probe lowers the usable upper frequency.
- Set the sensor's **usable range** from the datasheet; the report shades the spectrum outside it.
- For building and whole-body measurements the interface must pass low frequencies: check that a slow movement of the sensor shows up in the 1–2 Hz bands.
- Whole-body and hand-arm exposure use the daily exposure time **T** from the accelerometer card; building perception uses **T_e** from the assessment card.
