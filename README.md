# SPLMeter

A professional Sound Pressure Level (SPL) meter built with JUCE, available as a macOS/Windows/Linux standalone app and as VST3 / AU plugin.

![SPLMeter v3.4 screenshot](screenShot.png)

---

## Features

### SPL Measurement
- Broadband SPL in **dB**, **dB(A)**, and **dB(C)** — peak and RMS simultaneously
- IEC 61672-compliant time weighting: **FAST** (125 ms) and **SLOW** (1 s)
- Configurable **calibration offset** (80–140 dB, default 127 dB)
- Adjustable **peak hold time**
- Optional **20 Hz – 20 kHz bandpass** filter (8th-order Butterworth, 48 dB/oct)

### Display
- Large horizontal bargraph meter (20–130 dB SPL scale)
- Numeric SPL readout with peak-hold indicator
- **Basic / Advanced mode** toggle — Basic shows only the SPL meter; Advanced adds the full log plot and psychoacoustic overlay
- Time-series **log plot** with per-series visibility toggles (Left y-axis: dB / dB(A) / dB(C))
- Selectable psychoacoustic overlay on the right y-axis: Roughness, Fluctuation Strength, Sharpness, Specific Loudness, Psychoacoustic Annoyance

### SoundDetective
- ML-powered sound event detection; opened via the **Tools…** menu
- **Apple platforms (macOS / iOS):** uses Apple SoundAnalysis framework for on-device neural classification
- **All platforms:** optional user-supplied **TFLite model** (`.tflite`) loaded at runtime — supports any 1-D audio input model (e.g. YAMNet); auto-detects a matching `_labels.txt` file in the same directory; configurable model sample rate (default 16 kHz)
- Falls back to a heuristic amplitude-onset detector when no ML framework or model is available
- Configurable **minimum confidence threshold** (0.10 – 0.90)
- Scrollable **event log** — timestamp, label, confidence for each detected event; auto-trims to 500 events
- **Timeline markers** — detected events appear as labelled vertical markers on the log plot
- **Clear** button resets both the event log and the timeline markers simultaneously
- **Save Events CSV** export

### Spectrogram
- Opened via the **Tools…** menu; floating window with configurable controls
- **Frequency scale:** Log (default) or **Mel**
- Gain and time-resolution controls

### FFT Spectrum Analyser
- Toggled via the **FFT** button; displayed as a semi-transparent overlay on the log plot
- Unweighted (raw) signal — A/C weighting is not applied
- **Band resolution:** 1/1, 1/3, 1/6, 1/12, or 1/24 octave (up to ~240 bands)
- **Display modes:** Bars, Area fill, Bars + Peak hold markers
- **Window functions:** Hann, Hamming, Blackman, Flat-top, Rectangular
- **Overlap:** 0 %, 25 %, 50 %, 75 %
- **RTA +3 dB/oct mode** — applies a +3 dB/octave tilt so pink noise appears flat
- Frequency axis labels (20 Hz – 20 kHz) along the FFT x-axis

### Psychoacoustic Metrics
Continuous real-time estimation of:

| Metric | Description |
|---|---|
| Roughness | Perceived roughness / beating (15–300 Hz AM range) |
| Sharpness | High-frequency spectral centroid (acum, Zwicker/Aures model) |
| Fluctuation Strength | Slow amplitude modulation (0.5–20 Hz range) |
| Specific Loudness | Perceived loudness approximation (sone) |
| Psychoacoustic Annoyance | Combined annoyance index (Zwicker & Fastl 1999): N × (1 + √(w_S² + w_FR²)) |

### Input Modes
- **Real Time** — live microphone / audio interface input, up to **32 channels**
- **File** — load and analyse an audio file (WAV, AIFF, FLAC, MP3, OGG)
- **Monitor button** — speaker icon; output is muted by default, click to enable pass-through

### Export
Accessible via the **Save…** button (popup menu):
- **Save CSV** — exports the full measurement log (timestamps, all SPL values, all psychoacoustic metrics) as a CSV file
- **Save WAV** — saves the last *Keep Last* seconds of Input 01 and 02 as a stereo 24-bit WAV file
- **Save Screenshot** — exports the current view as a JPEG image
- **Save All** — saves CSV, WAV, and screenshot in one step into a chosen folder

### Analysis Tools
Accessible via the **Tools…** button (popup menu):
- **Spectrogram** — opens the spectrogram floating window (Log or Mel frequency scale)
- **SoundDetective** — ML-powered sound event detection (Apple SoundAnalysis on Apple platforms; optional TFLite model on all platforms; heuristic fallback)
- **ViSQOL** *(macOS / Windows when pre-built lib is present)* — perceptual audio quality analysis (MOS-LQO + per-band NSIM)
- **Correction Filter** — load a frequency-response correction curve (.txt / .csv: Hz + dB pairs); applied as a linear-phase FIR filter before metering (configured in Settings)
- **Graph Overlay** — load a reference curve and display it as a dashed blue line in the FFT view (configured in Settings)

### Title Bar Utilities
- **Live clock** — auto-updating date/time display (updates every second)
- **Note field** — three-line free-text field for session annotations

### Settings Panel
Opened via the **Settings** button. Organised into four sections:

| Section | Contents |
|---|---|
| **General** | Calibration fader, Hold Time fader, Light Mode, 94 dB Reference Line, 20–20k Bandpass, Full Screen |
| **FFT** | FFT enable toggle, FFT Gain fader, FFT Smooth fader, Band Resolution, Display Mode, Window Function, Overlap, Peak Hold, RTA +3 dB/oct |
| **Analysis** | Correction Filter (enable / load / clear), Graph Overlay (enable / load / clear) |
| **Input Channels** | Per-channel mute buttons for IN01 – IN32 |

### MIDI Learn
Right-click any of the three faders to assign or clear a MIDI CC mapping:
- **Calibration** — dB offset
- **FFT Gain** — FFT input gain
- **Hold Time** — peak hold duration

Assigned CC numbers are shown next to each fader label (e.g. `[CC 4]`).

---

## Controls

| Control | Description |
|---|---|
| Basic Mode / Advanced Mode | Toggle between compact meter-only view and full advanced view |
| FAST / SLOW | IEC 61672 time weighting |
| Real Time / File | Input source |
| Monitor (speaker icon) | Enables audio pass-through output; OFF by default |
| Settings… | Opens the settings panel |
| Calibration | dB offset to convert full-scale to SPL (right-click for MIDI learn) |
| Hold Time | Peak hold duration in seconds (right-click for MIDI learn) |
| FFT Gain | Input gain for the FFT overlay (right-click for MIDI learn) |
| FFT Smooth | Spectral smoothing factor (0 = off, 0.95 = heavy) |
| FFT | Toggle FFT spectrum overlay on/off |
| 20-20k BP | Enable 8th-order Butterworth bandpass filter |
| 94 dB Line | Draw a dashed reference line at 94 dB SPL in the log plot |
| Light Mode | Switch to light theme |
| Left y axis checkboxes | Visibility of dB SPL / dBA SPL / dBC SPL series |
| Right y axis checkboxes | Psychoacoustic overlay: Roughness / Fluctuation / Sharpness / Specific Loudness / Annoyance |
| Save… | Popup menu: Save CSV / Save WAV / Save Screenshot / Save All |
| Tools… | Popup menu: Spectrogram / SoundDetective / ViSQOL (macOS/Windows) |
| Reset | Clear log and reset peak holds |
| Clock | Live date/time (updates every second) |
| Note field | Free-text annotation field (3 lines) |

---

## Real-Time Audio Signal Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ppkSPLmeter v3.4 — Real-Time Audio Signal Flow           │
└─────────────────────────────────────────────────────────────────────────────┘

  ┌──────────────┐     ┌──────────────────────┐
  │  DAW / Host  │     │  File Transport       │
  │  Input Bus   │     │  AudioTransportSource │
  │  (≤32 ch)    │     │  (file playback mode) │
  └──────┬───────┘     └──────────┬───────────┘
         │                        │
         └──────────┬─────────────┘
                    │  source selection
                    ▼
          ┌──────────────────┐
          │  Channel Muting  │  (per-channel on/off, up to 32 ch)
          └────────┬─────────┘
                   │
                   ├──────────────────────────────────────────────────────────┐
                   │                                                           │
                   ▼                                                    WAV Capture
        ┌──────────────────────┐                                        (pre-EQ)│
        │  WAV Circular Buffer │ ◄── Ch0 + Ch1, pre-filter                     │
        │  (lock-free ring buf)│                                               │
        │  → Save WAV feature  │                                               │
        └──────────────────────┘                                               │
                   │                                                           │
                   ▼                                                           │
        ┌──────────────────────────────────────┐                              │
        │  Bandpass Filter (optional)           │                              │
        │  8th-order Butterworth 20Hz – 20kHz  │                              │
        │  4× cascaded biquad (HP + LP stages) │                              │
        │  per active channel                  │                              │
        └──────────────────┬───────────────────┘                              │
                           │                                                   │
                           ▼                                                   │
        ┌──────────────────────────────────────┐                              │
        │  FIR Correction Filter (optional)     │                              │
        │  4096-tap linear-phase               │                              │
        │  juce::dsp::Convolution              │                              │
        │  (inverted measured SPL curve)       │                              │
        └──────────────────┬───────────────────┘                              │
                           │                                                   │
                           ▼                                                   │
                 ┌─────────────────┐                                           │
                 │   Mono Mix      │  Σ(all active ch) / N                    │
                 └────────┬────────┘                                           │
                          │                                                    │
          ┌───────────────┼─────────────────────────────────────┐             │
          │               │                 │                   │             │
          ▼               ▼                 ▼                   ▼             │
  ┌──────────────┐ ┌─────────────┐  ┌───────────────┐  ┌────────────────┐   │
  │  A-Weighting │ │ C-Weighting │  │  FFT Circular │  │ Psychoacoustic │   │
  │  IIR filter  │ │  IIR filter │  │  Buffer       │  │  Estimators    │   │
  │  (IEC 61672) │ │  (IEC 61672)│  │  8192 samples │  │                │   │
  └──────┬───────┘ └──────┬──────┘  │  lock-free    │  │  • Roughness   │   │
         │                │         │  atomic write │  │    (AM 15-300Hz│   │
         │                │         └───────┬───────┘  │  • Sharpness   │   │
         │                │                 │           │    (Zwicker/   │   │
         │                │                 │           │     Aures 8-bd)│   │
         │                │                 │           │  • Fluctuation │   │
         │                │                 │           │    (0.5-20 Hz) │   │
         │                │                 │           └───────┬────────┘   │
         │                │                 │                   │             │
         └───────┬─────── ┘                 │                   │             │
                 │                          │                   │             │
                 ▼                          ▼                   ▼             │
        ┌─────────────────────┐    ┌────────────────┐  ┌───────────────────┐ │
        │  Peak Tracking      │    │  FFT spectrum  │  │  Psychoacoustic   │ │
        │  with Hold          │    │  → GUI display │  │  readouts         │ │
        │  (raw, A, C)        │    │  (30 Hz poll)  │  │  → GUI atomics    │ │
        │  configurable hold  │    └────────────────┘  └───────────────────┘ │
        └──────────┬──────────┘                                               │
                   │                                                           │
                   ▼                                                           │
        ┌──────────────────────────────────────┐                              │
        │  Exponential RMS Smoothing            │                              │
        │  IEC 61672 time weighting            │                              │
        │  FAST  τ = 125 ms                    │                              │
        │  SLOW  τ = 1000 ms                   │                              │
        │  (raw, A-weighted, C-weighted)       │                              │
        └──────────────────┬───────────────────┘                              │
                           │                                                   │
                           ▼                                                   │
        ┌──────────────────────────────────────┐                              │
        │  Atomic Readout Update (every 125ms)  │ ◄── also pushes LogEntry   │
        │  • SPL raw/A/C (fast+slow)           │     to deque               │
        │  • Peak raw/A/C                      │     (pruned to logDuration) │
        │  • Psychoacoustic values             │                              │
        └──────────────────┬───────────────────┘                              │
                           │                                                   │
                           ▼                                                   │
                  ┌─────────────────┐                                          │
                  │  Monitor Mute   │  buffer.clear() if monitor off           │
                  └────────┬────────┘                                          │
                           │                                                   │
                           ▼                                                   │
                  ┌─────────────────┐                                          │
                  │  DAW / Host     │                                          │
                  │  Output Bus     │                                          │
                  └─────────────────┘                                          │
                                                                               │
  ┌────────────────────────────────────────────────────────────────────────────┘
  │  GUI Timer (30 Hz)
  │  ┌──────────────────────────────────────────┐
  └─►│  timerCallback()                         │
     │  • reads atomics → MeterComponent        │
     │  • reads FFT ring buf → spectrum display │
     │  • updates Log / CSV export              │
     │  • MIDI CC learn / apply                 │
     └──────────────────────────────────────────┘
```

---

## Building

### macOS (Xcode)

Requires Xcode and the JUCE framework.

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcodebuild -project Builds/MacOSX/SPLMeter.xcodeproj \
             -scheme "SPLMeter - Standalone Plugin" \
             -configuration Release build
```

After building in an iCloud-synced directory, re-sign before launching:

```bash
xattr -cr Builds/MacOSX/build/Release/SPLMeter.app
codesign --force --sign - Builds/MacOSX/build/Release/SPLMeter.app
open Builds/MacOSX/build/Release/SPLMeter.app
```

### Windows (CMake)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**ASIO support** is enabled automatically — CMake downloads the Steinberg ASIO SDK 2.3.3 during configuration if it is not already present locally.

To use a local copy instead, place the unzipped SDK root at `Vendor/asiosdk/` inside the project root, or pass the path explicitly:

```bash
cmake -B build -DASIO_SDK_DIR="C:/path/to/asiosdk/common" -DCMAKE_BUILD_TYPE=Release
```

### CI

GitHub Actions workflows build the standalone for macOS and Windows on every push.

---

## Requirements

- macOS 10.13+ (Apple Silicon native) or Windows 10+
- Audio input device for Real Time mode

---

## Third-Party References

### Frameworks & SDKs

| Library | Version | License | URL |
|---|---|---|---|
| **JUCE** | 8.0.7 | ISC / GPL-3.0 | https://github.com/juce-framework/JUCE |
| **Steinberg VST3 SDK** (incl. ASIO SDK) | latest | Steinberg VST3 License | https://github.com/steinbergmedia/vst3sdk |

### Standards & Psychoacoustic Models

| Reference | Description |
|---|---|
| **IEC 61672-1:2013** | Electroacoustics — Sound level meters. Defines FAST (125 ms) and SLOW (1 s) time weighting and A/C frequency weighting curves. |
| **ISO 266:1997** | Preferred frequencies — defines the 31 standard 1/3-octave band centre frequencies (20 Hz – 20 kHz). |
| **Zwicker & Fastl, *Psychoacoustics: Facts and Models* (3rd ed., Springer, 2007)** | Theoretical basis for the Roughness, Sharpness, Fluctuation Strength, Specific Loudness, and Psychoacoustic Annoyance estimators. |

---

## Changelog

### [v3.4.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v3.4.0)
- **TA Laerm compliance lane** added next to DIN 15905-5 and NIOSH on the bottom strip — Beurteilungspegel `Lr` (≈ LAeq) compared against day (06–22) / night (22–06) limits for the selected German land-use category, with an OK / WARN / LIMIT pill of its own. Categories: Industriegebiet, Gewerbegebiet, Misch-/Dorf-/Kerngebiet, Allg. Wohngebiet (default), Reines Wohngebiet, Kurgebiet/Krankenhaus
- **Settings → General**: TA Laerm Category dropdown
- **Hover tooltips** on each compliance section (DIN 15905-5 / NIOSH / TA Laerm) explain what the values measure, the relevant limits, and the assumed measurement position. `MeterComponent` now implements `juce::TooltipClient`, capturing per-section bounds during paint and returning region-aware tooltip text

### [v3.3.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v3.3.0)
- **Main meter now displays time-weighted RMS** (IEC 61672 SPL) instead of held sample peak. Bargraph hold tick captures the maximum recent RMS reading (Lmax-style). Calibration with a 94 dB / 1 kHz reference now reads 94 dB on the meter as expected
- **NIOSH REL readout** alongside DIN 15905-5 in the same meter strip — cumulative 8-hour noise dose (3 dB exchange rate, 85 dB(A) criterion, 80 dB(A) threshold) with its own OK / WARN / LIMIT pill (50 % WARN, 100 % LIMIT, plus 140 dB(C) LCpeak ceiling); same dose readout in the Long-term SPL window after offline analysis
- **DIN 15905-5 strip is Advanced-mode only** and now sits below the Annoyance row of the psychoacoustic table; "LAeq,30min" relabelled to **"LAeq(30min)"**
- **"Cal to Input" mirrored in the Long-term SPL window** — calibrates `calOffset` so the loaded file's LAeq reads 94 dB(A) and auto-runs Refresh
- **Long-term SPL: spacebar play/pause** kept (now also closes file mode cleanly when window is dismissed): closing the window stops playback and reverts the meter to the live input
- **Title-bar Real Time / File buttons reflect file-mode state** — light up automatically when Long-term SPL playback starts, revert to Real Time on stop, window close, or end-of-file
- **First-launch defaults**: only **dBA SPL** trace selected; psychoacoustic overlay starts off
- **Output VU meter** vertically aligned to the volume knob's textbox baseline

### [v3.2.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v3.2.0)
- **DIN 15905-5 compliance readout** — new strip in the main meter showing the sliding 30-min A-weighted equivalent continuous level (LAeq,30min) and the session-long C-weighted peak (LCpeak), each color-coded against the standard's thresholds (green / yellow at 95 dB(A) / 130 dB(C) / red at 99 dB(A) / 135 dB(C)) plus an OK / WARN / LIMIT compliance pill
- **DIN 15905-5 in Long-term SPL** — after offline file analysis, the worst-case sliding-30-min LAeq and the overall LCpeak (max |x_C|) are computed and shown beneath the existing stats line, with the same color-coded compliance status
- **Session LCpeak** — `peakDBCSPL` is now also accumulated as a session-long maximum (independent of the regular peak-hold time) and reset by the Reset button or a FAST/SLOW switch

### [v3.1.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v3.1.0)
- **Output VU meter** — vertical L/R peak bargraph next to the master volume control; -60..+6 dBFS scale with green / yellow / orange / red zones, fast attack and ~250 ms release smoothing, 0 dBFS reference tick
- **Master volume as 270° rotary knob** — replaces the linear fader in the title bar; maximum gain raised from +12 dB to **+32 dB**
- **Ctrl/Cmd+M shortcut** — toggles the monitor (output) mute from anywhere in the main window
- **Cal to Input button** *(Settings → General)* — one-click calibration: averages the energy of the last ~2 s of input and sets the Calibration offset so it reads 94 dB SPL; intended for users without adjustable hardware input gain or who prefer not to use the Calibration slider directly
- **Long-term SPL: Refresh button** — re-runs the analysis on the loaded file with the current Calibration value, without reloading the file
- **Long-term SPL: Spacebar toggles play/pause** — works regardless of which transport button was last clicked
- **Long-term SPL: file audio routed to the device output** — pressing Play in the Long-term SPL window now actually plays the loaded file through the configured output (previously the live input was sent to the output during file analysis)

### [v3.0.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v3.0.0)
- **Long-term SPL** — new sub-app (Tools menu) for offline analysis of WAV / BWF files; computes A-, C-, and Z-weighted IEC 61672 FAST SPL across the full file with a waveform overview, zoomable time and dB axes, transport with playhead, click-to-seek, BWF timecode support, and CSV export; shares the main monitor controls
- **Impulse Fidelity** — new sub-app (Tools menu) for testing the transient response of a device under test using 1/3-octave tone bursts (100 Hz – 10 kHz); shows envelope comparison plots and computes attack, release, and overshoot metrics with a rating system

### [v2.9.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v2.9.0)
- **L_FFT spectrum analyzer** — new sub-app (Tools menu) showing a real-time 1/N octave band spectrum with log-frequency axis and dB SPL y-axis; reads all FFT settings from the Settings pane (window type, band resolution, smoothing, gain, RTA mode, display mode, peak hold, freq range)
- **FFT snapshot storage** — capture the current spectrum with the ✎ button; snapshots are listed in a sidebar with timestamp names, colour dots, and visibility toggles; click a snapshot to show/hide, right-click or Delete to remove; Cmd+Z undoes deletions
- **Per-snapshot styling** — gear icon on each snapshot opens a settings panel to change the overlay colour (10 swatches) and line style (solid, dashed, dotted, dash-dot)
- **FFT cycle averaging** — editable "Avg N" field (1–999) in the Settings FFT section; averages N consecutive FFT frames before computing band energies for a smoother display; applied in both the main FFT view and L_FFT
- **FFT frequency range in Settings** — f Lower / f Upper sliders moved from the right-click callout into the FFT section as vertical sliders; upper frequency limit extended to 100 kHz
- **L_FFT Y-axis zoom** — magnifying glass opens a callout to set the dB SPL display range (own parameters, independent of the main SPL plot)
- **Hold time editing** — clicking the hold time label in the title bar now shows only the numeric value in the editor (not the full "Hold time: X s" text)
- **Settings pane widened** to 720px; bandpass button renamed to "20Hz–20kHz Bandpass"

### [v2.8.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v2.8.0)
- **Fix crash when loading correction filter** — the convolution engine was prepared for 8 channels but the audio buffer could have 18+, causing a null-pointer dereference on the audio thread; also fixed a race condition where the IR was used before async background processing completed
- **FFT crosshair cursor** — click in the FFT plot to show a crosshair snapping to the nearest octave band with frequency and level readout; follows the mouse, adapts to light/dark mode
- **Right Y-axis zoom for psychoacoustic metrics** — each of the 7 metrics has independent min/max range parameters; magnifying glass icon next to the unit label opens a popup to adjust the displayed range; ranges saved/restored with plugin state
- **Pause button tooltip** — shows "Pause / Resume (M)" on hover

### [v2.7.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v2.7.0)
- **Marker system** — red diamond button in the title bar (Shift+M shortcut) to place named markers on the timeline; markers included in CSV export
- **LAeq / LCeq** — energy-averaged dBA/dBC over the log duration window, displayed in the meter readout and CSV export
- **Unit test infrastructure** — Catch2 v3.7.1 with 93 test cases covering DSP filters, psychoacoustic estimators, SPL calculations, state serialization, markers, and Leq
- **Pause button & DAW sync** moved to advanced-mode only

### [v2.6.1](https://github.com/ppklose/ppkSPLMeter/releases/tag/v2.6.1)
- **SPL Y-axis zoom** — magnifying glass icon below the "dB SPL" label opens a callout to set the minimum and maximum displayed dB SPL range; grid lines and labels update dynamically
- **X-axis duration & FFT freq popup** — the "Keep last (s)" bottom control is replaced by a magnifying glass at the centre of the time axis; clicking it opens a combined callout with time range (s), FFT lower frequency (Hz), and FFT upper frequency (Hz)

### [v2.6.0](https://github.com/ppklose/ppkSPLMeter/releases/tag/v2.6.0)
- **FFT adjustable frequency range** — lower and upper frequency limits (10–20 kHz) configurable via right-click on the FFT button in Settings; displayed range and axis labels update accordingly; persisted in APVTS and JSON settings
- **Pause timeline markers** — pausing and resuming analysis leaves a yellow dashed vertical line on the timeline with a flag showing the pause duration (e.g. "5s pause"); clock label now shows the wall-clock time of the last recorded data point and freezes while paused
- **Input channel rename** — right-click any channel button in Settings to rename it; names are saved with the project state
- **Ex-/Import menu** — Save Settings… and Load Settings… options export/import all parameters, UI state, channel names, and MIDI CCs as a human-readable JSON file
- **Last-used folder memory** — all file pickers remember the last-used folder
- **Maximize button** — standalone window now has a third title-bar button for full-screen zoom
- **MIT license** added

### v2.5.0
- **SoundDetective** — new ML sound event detection panel (via **Tools…** menu):
  - Apple platforms use the on-device **Apple SoundAnalysis** neural classifier
  - All platforms support loading a user-supplied **TFLite model** (`.tflite`) at runtime; auto-detects matching `_labels.txt`; configurable model sample rate
  - Heuristic amplitude-onset fallback when no ML framework or model is available
  - Configurable minimum confidence threshold; scrollable event log with timestamp, label, and confidence; auto-trims at 500 events
  - **Timeline markers** — events appear as vertical markers on the log plot
  - **Clear** button resets the event log and the timeline markers simultaneously
  - **Save Events CSV** export
- **TFLite cross-platform ML backend** — TensorFlow Lite 2.16.2 fetched and linked via CMake on all platforms; defines `SPLMETER_HAS_TFLITE`; model input window size adapted automatically from the tensor shape
- **ViSQOL cross-platform** — CMake now detects `Vendor/visqol/lib/visqol_ffi.lib` on Windows and enables ViSQOL there too; compile definition changed from hard-coded `JUCE_MAC` to `SPLMETER_HAS_VISQOL` (with `|| JUCE_MAC` fallback for Xcode builds)
- **Build fix** — AVFoundation and CoreMedia are now explicitly linked in the CMake build, fixing linker errors introduced by SoundDetective's Apple SoundAnalysis backend

### v2.4.0
- **Psychoacoustic Annoyance** — real-time Zwicker & Fastl annoyance index (PA) added as a momentary readout in the meter bar and as a selectable trace on the right y-axis of the log plot; included in CSV export
- **Specific Loudness** — renamed from "Loudness" throughout the UI and CSV output for clarity
- **Mel frequency scale** — spectrogram now offers a Log / Mel toggle; Mel scale compresses high frequencies and expands low frequencies for perceptually-uniform display
- **Save… menu** — Save CSV, Save WAV, Save Screenshot, and Save All grouped under a single popup button; Save All exports all three formats in one step to a chosen folder
- **Tools… menu** — Spectrogram and ViSQOL (macOS) grouped under a single popup button
- **ASIO level fix** — level readings with ASIO drivers (e.g. 32-channel interfaces) were up to 30 dB too low because the mono mix divided by total hardware channel count; fix counts only channels carrying signal
- **Settings… button** — renamed from "Settings" for standard macOS/Windows UI convention

### v2.3.0
- **Persistent user settings** — all settings (monitor level, FFT parameters, calibration, hold time, bandpass, 94 dB line, light mode, etc.) are saved to disk and restored on the next launch
- **Light mode persistence** — light/dark theme is remembered across launches
- **Correction filter auto-enable** — loading a correction curve automatically enables it
- **Correction file metadata parsing** — if the first line of a correction `.txt` contains `Sens Factor` and `SERNO:` (e.g. from a calibrated measurement microphone data sheet), the calibration offset is set to `94 + |Sens Factor|` and the serial number is filled into the notes field
- **Windows build provenance** — replaced expensive Azure Trusted Signing with free GitHub Artifact Attestations (SLSA); binaries are cryptographically linked to the CI workflow and verifiable with `gh attestation verify`
- **macOS microphone permission** — added `NSMicrophoneUsageDescription` to the app bundle so macOS shows the permission dialog on first launch
- **Light mode note field** — the notes text field now correctly follows the light/dark theme

### v2.2.2
- **Developer ID codesigning** — macOS builds are signed with a Developer ID Application certificate and notarized with the Apple Notary Service; Gatekeeper passes without warnings on all Macs
- **macOS distribution as DMG** — the Standalone app is packaged as a `.dmg` so the notarization ticket is preserved through download and the bundle structure is intact
- **Windows codesigning** — Azure Trusted Signing integrated into CI; `.exe` and `.vst3` are signed with a Microsoft-trusted certificate via the `azure/trusted-signing-action`
- **Linux support** — new CI job builds Standalone + VST3 on Ubuntu; required JUCE system dependencies (ALSA, X11, GL, Freetype, GTK3) resolved via `pkg_check_modules`
- **Persistent Basic / Advanced mode** — the app remembers the last used mode across launches; first launch defaults to Basic mode

### v2.1.0
- **ViSQOL integration** — perceptual audio quality analysis (MOS-LQO + per-band NSIM) available as a floating panel via the ViSQOL button (Advanced mode only)
- **Automatic audio conversion** — WAV files are automatically resampled, converted to 16-bit, and mixed to mono before ViSQOL analysis (GPL-compatible, JUCE-only pipeline); conversion details shown in the result panel
- **Default window width** increased to 1800 px
- **Real-time signal flow diagram** added to documentation

### v2.0.0
- **Basic / Advanced mode** — new mode toggle in the header; app starts in Basic mode (compact SPL meter only); Advanced mode adds the full log plot and psychoacoustic overlay
- **32-channel input** — channel count extended from 8 to 32; Settings panel shows 4 rows of 8 per-channel mute buttons (IN01 – IN32)
- **Save WAV** — records the last *Keep Last* seconds of IN01 + IN02 as a stereo 24-bit WAV file
- **Psychoacoustic axis checkboxes** — Roughness, Fluctuation, Sharpness, Loudness now have individual ToggleButton checkboxes on the right y-axis (radio-button behaviour); replaced the old click-on-name selector
- **Left / Right y-axis labels** — "Left y axis:" and "Right y axis:" section labels added above the respective checkbox rows
- **FFT frequency axis** — centre-frequency labels (20 Hz – 20 kHz) added along the FFT x-axis
- **Settings panel sections** — reorganised into four labelled sections: General, FFT, Analysis, Input Channels; thin separator lines between sections
- **Calibration, Hold Time, FFT Gain, FFT Smooth** — controls changed from rotary knobs to short vertical faders
- **Roughness colour** — distinct teal colour (was similar to dBA orange)
- **Live clock** — auto-updating date/time label in the title bar (updates every second)
- **Note field** — three-line free-text annotation field in the title bar
- **Reset button** — text colour changed to red (button background unchanged)
- **Window width** — default width increased to 1500 px; extended height 900 px

### v1.2.0
- **Settings panel** — floating settings window replaces individual toolbar controls
- **FFT overhaul** — band resolution up to 1/24 octave (~240 bands); display modes: Bars, Area, Bars+Peak; window functions: Hann, Hamming, Blackman, Flat-top, Rect; overlap: 0/25/50/75 %; FFT smoothing; peak hold toggle
- **RTA +3 dB/oct mode**
- **Light mode**
- **Monitor/mute button**
- **20-20k Bandpass filter**

### v1.1.0
- **Resizable window** (default 1400×900)
- **SPL series visibility toggles**
- **Persistent psychoacoustic readout**
- **Save CSV**
- **1/3-octave FFT overlay**
- **FFT Gain fader**
- **MIDI learn** for Calibration, FFT Gain, Hold Time
- **Windows ASIO support**

### v1.0.0
- Initial release
- Broadband SPL measurement in dB, dB(A), dB(C) — peak and RMS
- IEC 61672 FAST / SLOW time weighting
- Horizontal bargraph meter (20–130 dB SPL scale)
- Time-series log plot with configurable history length
- Real-time psychoacoustic metrics
- Real Time and File input modes
- Save JPG export
- Standalone, VST3, and AU formats

---

## License

MIT License — © 2026 Philipp Paul Klose. See [LICENSE](LICENSE) for the full text.
