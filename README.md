# SPLMeter

A professional Sound Pressure Level (SPL) meter built with JUCE, available as a macOS/Windows standalone app and as VST3 / AU plugin.

![SPLMeter v2.0 screenshot](screenShot.jpg)

---

## Features

### SPL Measurement
- Broadband SPL in **dB**, **dB(A)**, and **dB(C)** — peak and RMS simultaneously
- IEC 61672-compliant time weighting: **FAST** (125 ms) and **SLOW** (1 s)
- Configurable **calibration offset** (80–140 dB, default 127 dB)
- Adjustable **peak hold time**

### Display
- Large horizontal bargraph meter (20–130 dB SPL scale)
- Numeric SPL readout with peak-hold indicator
- Time-series **log plot** with per-series visibility toggles (dB / dB(A) / dB(C))
- Selectable psychoacoustic overlay: Roughness, Fluctuation Strength, Sharpness, Loudness, or OFF
- Persistent real-time psychoacoustic readout (always visible, independent of log selector)

### FFT Spectrum Analyser
- Toggled via the **FFT** button; displayed as a semi-transparent overlay on the log plot
- Unweighted (raw) signal — A/C weighting is not applied
- **Band resolution:** 1/1, 1/3, 1/6, 1/12, or 1/24 octave (up to ~240 bands)
- **Display modes:** Bars, Area fill, Bars + Peak hold markers
- **Window functions:** Hann, Hamming, Blackman, Flat-top, Rectangular
- **Overlap:** 0 %, 25 %, 50 %, 75 %
- **RTA +3 dB/oct mode** — applies a +3 dB/octave tilt so pink noise appears flat
- **FFT Gain** and **FFT Smooth** rotary knobs in Settings

### Psychoacoustic Metrics
Continuous real-time estimation of:

| Metric | Description |
|---|---|
| Roughness | Perceived roughness / beating (asper) |
| Sharpness | High-frequency spectral centroid (acum) |
| Fluctuation Strength | Slow amplitude modulation (vacil) |
| Loudness | Perceived loudness (sone) |

### Input Modes
- **Real Time** — live microphone / audio interface input, up to 8 channels
- **File** — load and analyse an audio file (WAV, AIFF, …)
- **Monitor button** — speaker icon next to the File button; mutes the output by default (output is silenced unless monitoring is explicitly enabled)

### Export
- **Save JPG** — exports the current view as a JPEG image
- **Save CSV** — exports the full log (timestamps, all SPL values, all psychoacoustic metrics) as a CSV file

### Settings Panel
Opened via the **Settings** button. Contains all configuration options:

| Section | Options |
|---|---|
| Knobs | Calibration, Hold Time, FFT Gain, FFT Smooth |
| FFT | Enable toggle, display mode, band resolution, overlap, window function, peak hold, RTA +3 dB/oct |
| 20-20k Bandpass | 8th-order Butterworth bandpass (48 dB/oct HP at 20 Hz + LP at 20 kHz) |
| Light Mode | Switches the entire UI to a light theme |
| Full Screen | Toggles fullscreen mode |

### MIDI Learn
Right-click any of the three knobs to assign or clear a MIDI CC mapping:
- **Calibration** — dB offset
- **FFT Gain** — FFT input gain
- **Hold Time** — peak hold duration

Assigned CC numbers are shown next to each knob label (e.g. `[CC 4]`).

---

## Controls

| Control | Description |
|---|---|
| FAST / SLOW | IEC 61672 time weighting |
| Real Time / File | Input source |
| Monitor (speaker icon) | Enables audio pass-through output; OFF (muted) by default |
| Settings | Opens the settings panel |
| Calibration | dB offset to convert full-scale to SPL (right-click for MIDI learn) |
| FFT Gain | Input gain for the FFT overlay (right-click for MIDI learn) |
| FFT Smooth | Spectral smoothing factor (0 = off, 0.95 = heavy) |
| Hold Time | Peak hold duration in seconds (right-click for MIDI learn) |
| FFT | Toggle FFT spectrum overlay on/off |
| 20-20k BP | Enable 8th-order Butterworth bandpass filter (20 Hz – 20 kHz, 48 dB/oct) |
| Light Mode | Switch to light theme |
| Log Duration | History length shown in the log plot |
| dB SPL / dBA SPL / dBC SPL | Visibility checkboxes for each SPL series in the log plot |
| Roughness / Fluctuation / Sharpness / Loudness / OFF | Select which psychoacoustic metric is overlaid in the log plot |
| Save JPG | Export current view as JPEG |
| Save CSV | Export full measurement log as CSV |
| Reset | Clear log and reset peak holds |

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

**ASIO support** is enabled automatically — CMake downloads the Steinberg ASIO SDK 2.3.3 during configuration if it is not already present locally. No manual steps required.

To use a local copy instead (e.g. to avoid the download or to use a different SDK version), place the unzipped SDK root at `Vendor/asiosdk/` inside the project root, or pass the path to the `common/` header directory explicitly:

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

JUCE is used for the entire audio and UI framework (AudioProcessor, AudioProcessorValueTreeState, DSP module, Graphics, etc.).
The ASIO SDK (part of the Steinberg VST3 SDK) is fetched automatically on Windows builds to enable low-latency ASIO driver support.

### Standards & Psychoacoustic Models

| Reference | Description |
|---|---|
| **IEC 61672-1:2013** | Electroacoustics — Sound level meters. Defines FAST (125 ms) and SLOW (1 s) time weighting and A/C frequency weighting curves. |
| **ISO 266:1997** | Preferred frequencies — defines the 31 standard 1/3-octave band centre frequencies (20 Hz – 20 kHz) used for the FFT overlay. |
| **Zwicker & Fastl, *Psychoacoustics: Facts and Models* (3rd ed., Springer, 2007)** | Theoretical basis for the Roughness, Sharpness (Zwicker/Aures model), Fluctuation Strength, and Loudness (sone) estimators implemented in this plugin. |

---

## Changelog

### v1.2.0
- **Settings panel** — floating settings window replaces individual toolbar controls; groups all configuration options in one place
- **FFT overhaul** — band resolution up to 1/24 octave (~240 bands); display modes: Bars, Area, Bars+Peak; window functions: Hann, Hamming, Blackman, Flat-top, Rect; overlap: 0/25/50/75 %; FFT smoothing knob; FFT peak hold toggle
- **RTA +3 dB/oct mode** — FFT tilt so pink noise appears flat
- **Light mode** — full light theme switchable via Settings; propagated across all UI components
- **Monitor/mute button** — speaker icon in the header; output is muted by default, click to enable pass-through monitoring
- **20-20k Bandpass filter** — 8th-order Butterworth bandpass (48 dB/oct) at 20 Hz and 20 kHz; applied per-channel before metering; toggled in Settings

### v1.1.0
- **Resizable window** — drag to any size between 480×500 and 3840×2160 (default 1400×900)
- **SPL series visibility toggles** — individual checkboxes for dB, dB(A), and dB(C) series in the log plot
- **Persistent psychoacoustic readout** — Roughness, Sharpness, Fluctuation, and Loudness values are always shown regardless of the log selector state
- **Save CSV** — export the full measurement log (timestamps, all SPL and psychoacoustic values) to a CSV file
- **1/3-octave FFT overlay** — 31-band ISO 266 analysis (20 Hz–20 kHz) displayed as a green semi-transparent overlay in the log plot; toggled via the FFT button
- **FFT Gain knob** — rotary control to adjust the input level fed into the FFT
- **MIDI learn** — right-click Calibration, FFT Gain, or Hold Time to assign or clear a MIDI CC mapping; assigned CC numbers are shown in the knob label
- **Windows ASIO support** — ASIO SDK is fetched automatically via CMake FetchContent from the Steinberg VST3 SDK repository
- **Spectrogram removed** — replaced by the 1/3-octave FFT overlay in the log plot

### v1.0.0
- Initial release
- Broadband SPL measurement in dB, dB(A), dB(C) — peak and RMS
- IEC 61672 FAST / SLOW time weighting
- Horizontal bargraph meter (20–130 dB SPL scale)
- Time-series log plot with configurable history length
- Real-time psychoacoustic metrics: Roughness, Sharpness, Fluctuation Strength, Loudness
- Real Time and File input modes
- Calibration offset and peak hold time controls
- Save JPG export
- Standalone, VST3, and AU formats

---

## License

© Philipp Paul Klose. All rights reserved.
