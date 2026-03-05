# SPLMeter

A professional Sound Pressure Level (SPL) meter built with JUCE, available as a macOS/Windows standalone app and as VST3 / AU plugin.

![SPLMeter v1.1 screenshot](screenShot.jpg)

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

### 1/3-Octave FFT Overlay
- 31-band ISO 266 analysis (20 Hz – 20 kHz), toggled via the **FFT** button
- Bars drawn as a semi-transparent green overlay on the log plot
- **FFT Gain** rotary knob controls the input signal level into the FFT
- Unweighted (raw) signal — A/C weighting is not applied

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

### Export
- **Save JPG** — exports the current view as a JPEG image
- **Save CSV** — exports the full log (timestamps, all SPL values, all psychoacoustic metrics) as a CSV file

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
| Calibration | dB offset to convert full-scale to SPL (right-click for MIDI learn) |
| FFT Gain | Input gain for the 1/3-octave FFT overlay (right-click for MIDI learn) |
| Hold Time | Peak hold duration in seconds (right-click for MIDI learn) |
| FFT | Toggle 1/3-octave FFT overlay on/off |
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

**ASIO support (optional):** Download the ASIO SDK from https://www.steinberg.net/asiosdk, then copy the contents of the `asiosdk_x.x.x/common/` folder into `Vendor/asiosdk/common/` inside the project root. CMake will detect the headers automatically and enable ASIO — no extra flags needed:

```
ppkSPLmeter/
  Vendor/
    asiosdk/
      common/
        asio.h
        iasiodrv.h
        ...
```

Without `Vendor/asiosdk/common/`, the build completes normally with WASAPI/WDM support only.

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
