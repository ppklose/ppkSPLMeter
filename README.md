# SPLMeter

A professional Sound Pressure Level (SPL) meter built with JUCE, available as a macOS/Windows standalone app and as VST3 / AU plugin.

![SPLMeter v2.0 screenshot](screenShot.png)

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
- Selectable psychoacoustic overlay on the right y-axis: Roughness, Fluctuation Strength, Sharpness, Loudness

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
| Loudness | Perceived loudness approximation (sone) |

### Input Modes
- **Real Time** — live microphone / audio interface input, up to **32 channels**
- **File** — load and analyse an audio file (WAV, AIFF, FLAC, MP3, OGG)
- **Monitor button** — speaker icon; output is muted by default, click to enable pass-through

### Export
- **Save JPG** — exports the current view as a JPEG image
- **Save CSV** — exports the full measurement log (timestamps, all SPL values, all psychoacoustic metrics) as a CSV file
- **Save WAV** — saves the last *Keep Last* seconds of Input 01 and 02 as a stereo 24-bit WAV file

### Analysis Tools
- **Correction Filter** — load a frequency-response correction curve (.txt / .csv: Hz + dB pairs); applied as a linear-phase FIR filter before metering
- **Graph Overlay** — load a reference curve and display it as a dashed blue line in the FFT view

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
| Settings | Opens the settings panel |
| Calibration | dB offset to convert full-scale to SPL (right-click for MIDI learn) |
| Hold Time | Peak hold duration in seconds (right-click for MIDI learn) |
| FFT Gain | Input gain for the FFT overlay (right-click for MIDI learn) |
| FFT Smooth | Spectral smoothing factor (0 = off, 0.95 = heavy) |
| FFT | Toggle FFT spectrum overlay on/off |
| 20-20k BP | Enable 8th-order Butterworth bandpass filter |
| 94 dB Line | Draw a dashed reference line at 94 dB SPL in the log plot |
| Light Mode | Switch to light theme |
| Left y axis checkboxes | Visibility of dB SPL / dBA SPL / dBC SPL series |
| Right y axis checkboxes | Psychoacoustic overlay: Roughness / Fluctuation / Sharpness / Loudness |
| Save JPG | Export current view as JPEG |
| Save CSV | Export full measurement log as CSV |
| Save WAV | Save the last *Keep Last* seconds of IN01 + IN02 as a stereo WAV |
| Reset | Clear log and reset peak holds |
| Clock | Live date/time (updates every second) |
| Note field | Free-text annotation field (3 lines) |

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
| **Zwicker & Fastl, *Psychoacoustics: Facts and Models* (3rd ed., Springer, 2007)** | Theoretical basis for the Roughness, Sharpness, Fluctuation Strength, and Loudness estimators. |

---

## Changelog

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

© Philipp Paul Klose. All rights reserved.
