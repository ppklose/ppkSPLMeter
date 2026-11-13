# REW Measurement Sequencer — User Guide

SPLMeter 3.6 can run a list of [Room EQ Wizard](https://www.roomeqwizard.com/) sweep measurements one after the other, each step with its own settings. REW does the measuring; SPLMeter configures REW through its REST API, starts each sweep, waits for the result and moves on. While REW sweeps, SPLMeter measures the same interval with its own microphone, and with an accelerometer also the vibration. These values are saved with the run.

**Tools → REW Measurement Sequencer…** opens the window (standalone app and plugins; not on iOS). Tested on macOS with REW's Java driver; the ASIO parts (input names, second output) follow REW's API description and the automated tests.

---

## 1. What you need

- **REW with its REST API enabled** (developed and tested with REW 5.40, API 0.9.9): in REW, *Preferences → API → Start the API when REW starts*, or start REW with `-api`. The API listens on port 4735 on the same computer only, so REW and SPLMeter must run on the same machine.
- **A REW Pro licence.** REW only lets the API start sweep measurements with Pro. Without it, the first step fails with a message about the licence.
- REW's soundcard, input and output set up and working, as for a manual measurement.

---

## 2. Connection

The top line of the window shows the connection:

| Dot | Meaning |
|---|---|
| green | REW connected, audio ready: `REW 5.40 Beta 147 · API 0.9.9 · Java · 48 kHz` |
| amber | REW reachable, but its audio is not ready (check REW's soundcard settings) |
| red | REW not reachable on the port. Start REW and enable its API |
| grey | still looking |

SPLMeter asks REW every 5 s while the window is open; **Refresh** asks now. Change the **Port** if REW runs its API on another one (`-port`).

Once connected, the lists in the step editor come from REW: your outputs, the sweep lengths, the timing references and, with ASIO, the input names.

---

## 3. Build a sequence

### Steps

The table lists the steps. A **—** means the step leaves that REW setting as it is. **+ Add**, **Duplicate**, **Delete**, **▲ ▼** edit the list; click **✓** to switch a step off without deleting it. A row tinted red has an error (see the line under the table).

The editor on the right sets the selected step. Every REW setting has its own **set** box; unticked, REW keeps its current value.

| Setting | What it sets in REW |
|---|---|
| Name | the measurement's title, exactly as entered (empty: sequence name + step number) |
| Notes | the measurement's notes |
| Output | the output channel (Java: `L`, `R`, `L+R`, `3` …; ASIO: output names) |
| 2nd output | **On** + output: a second output plays the sweep too (ASIO only); **Invert** inverts the second output. With the Java driver use the output `L+R` for two channels; only *Invert* applies there |
| Input | first and last input. A range (REW Pro multi-input) gives one measurement per input |
| Level | the sweep level and its unit (dBFS, dBu, dBV, V) |
| Sweep | start and end frequency, and length (64k … 4M) |
| Repetitions | sweeps averaged into one measurement |
| Timing ref. | None, Loopback, Loopback cal, Acoustic, Wired |
| Protection | abort on **clipping** and/or above an **SPL limit** |
| REW group | the REW group the measurement goes into (created if missing) |
| Wait for me | the run stops before this step and shows the prompt (e.g. "Move the microphone to position 3.") |
| Delay before | extra time before the sweep starts |

**Add positions…** turns the selected step into N microphone positions: *Pos 1 … Pos N*, each waiting for you with "Move the microphone to position k."

### Sequence settings

| Setting | Meaning |
|---|---|
| Sequence | name; also the base of the results file names |
| Loops | run the whole list N times (repeatability) |
| Gap between | pause between two steps |
| On error | **Stop the run**, **Skip the step**, or **Retry once, then skip** |
| Restore REW settings | put every REW setting the run changed back afterwards (default on) |
| Timeline markers | one marker per sweep on SPLMeter's timeline |
| REW file | optional: REW's *Save all* writes every measurement to this .mdat file at the end |
| Results | folder for SPLMeter's results (default `Documents/SPLMeter/REW runs`) |

**Load… / Save…** read and write sequences as `*.rewseq.json`. The last sequence is also kept with SPLMeter's settings and comes back when you reopen the window.

Before a run, SPLMeter checks the sequence. Errors (red) block **Run**: for example a level above 0 dBFS, a sweep that does not rise or ends above half the sample rate, an output REW does not have, or a second output with the Java driver. Warnings (amber), such as two steps with the same name, do not.

---

## 4. Run

| Control | Action |
|---|---|
| **▶ Run** | runs the sequence |
| **Dry run** | sends every step's settings to REW but starts no sweep — to check routing and settings |
| **Pause / Resume** | holds the run after the current step |
| **■ Abort** | stops now: cancels the sweep in REW and restores REW's settings |

The status line shows the position and what happens, e.g. `Step 3/8 · loop 1/2 · L Pos 3 · Measuring 0:07 / ~0:14`; the bar shows the progress over all sweeps; the step being measured is highlighted green. During a run the sequence cannot be edited.

At a **Wait for me** step the window comes to the front with the prompt: **Continue** measures the step, **Skip step** leaves it out.

How a step runs:

1. SPLMeter sends the step's settings (only those that are set), the title and the notes.
2. It starts the sweep and waits for REW's new measurement. REW errors (e.g. clipping) end the step as *failed*; REW warnings (e.g. low signal-to-noise) are logged and the step counts as done.
3. If nothing arrives in time (REW's start delay + repetitions × sweep length + 15 s), SPLMeter cancels the sweep and the step fails (*timeout*).
4. The measurement goes into the step's REW group.

At the end — also after an abort or an error — SPLMeter saves REW's measurements to the REW file (if set), restores REW's settings, and switches REW's *blocking* mode back on if it was on (the run needs it off).

The run belongs to SPLMeter, not to the window: closing the window or the plugin editor does not stop it, and reopening the window shows where it is. Quitting SPLMeter aborts a run and restores REW.

---

## 5. Results

The **run log** lists every step with its result, the number of REW measurements and the duration, REW's messages, and SPLMeter's own values:

```
14:02:11  #3   L Pos 3     ok         1 measurement (13.2 s)
            SPLMeter: LAeq 78.3 dB, LAFmax 84.0 dB, LCpeak 96.1 dB; vibration Σ RMS 1.2 mm/s
```

SPLMeter measures from the start of the sweep until REW has the measurement (this includes REW's start delay and the silence around the sweep):

- **Microphone:** L_Aeq, L_Ceq, L_Zeq, L_AFmax, L_CSmax, L_Cpeak, with the current calibration. L_AFmax and L_CSmax use their own FAST and SLOW detectors, whatever the meter is set to.
- **Accelerometer** (if one is set up): RMS and peak of X, Y, Z and Σ, in the vibration panel's unit. The profile's main value (A(8), VDV, KB_FTr …) is not given per step: it accumulates over a whole measurement.
- Your own readings, log and peaks are not reset. Paused time (Pause, DAW sync) is not measured; the step is marked *partly paused*.

After every run that measured, SPLMeter writes two files to the results folder; **Show results** opens it:

- `<sequence>_<date>_<time>.csv` — header lines with the SPLMeter and REW versions, the microphone calibration, the vibration profile and the accelerometer calibration; then one row per step: step, loop, name, status, start, duration, messages, REW measurement IDs, output, second output, input, level, LAeq … LCpeak, metered and paused time, vibration unit, X/Y/Z/Σ RMS and peak.
- `<same name>.json` — the same data plus the complete sequence, so a run can be traced later.

Dry runs write no results. With **Timeline markers** on, SPLMeter's timeline shows `REW 3/8 L Pos 3` at the start of each sweep.

---

## 6. Tips

- Start with a **Dry run** to check outputs and inputs, then a short run with one step.
- For multi-position measurements: one step with the speaker, level and sweep set, then **Add positions…**, and a **REW group** per speaker.
- Set **Protection → Clipping** so a clipped sweep ends the step instead of producing a bad measurement; choose **Retry once** if you expect occasional disturbances.
- REW's own measurement naming is overridden during the run (title exactly as entered) and restored afterwards.
- Windows with ASIO: the second output (with *On*) plays the sweep on two outputs at once, e.g. for a stereo pair or a sub plus main; *Invert* inverts the second one.
