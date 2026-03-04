# Skill: UI / JUCE Editor

## Layout
- Window: 960 × 1080 px (set in `PluginEditor` constructor)
- Title bar row: logo, title, mode buttons (Real Time / File), calibration, hold time, reset
- Meter row: `MeterComponent` (horizontal bargraph + SPL readout + FAST/SLOW)
- Psychoacoustic readout row: Roughness, Sharpness, Fluctuation, Loudness
- Log plot: `LogComponent` (time-series, ~60% of height)
- Footer: version + build date, log duration slider, Spectrogram checkbox, gain slider
- `SpectrogramComponent` overlays the log plot area

## Component Hierarchy
```
PluginEditor
├── MeterComponent       — bargraph + numeric SPL
├── LogComponent         — time-series plot + metric selector tabs
│   └── SpectrogramComponent (overlay)
├── Buttons: Save JPG, Reset, Real Time, File, FAST, SLOW
├── Sliders: calSlider, holdSlider
└── TooltipWindow
```

## Timer / Repaint Pattern
```cpp
// timerCallback() at 30 Hz — GUI thread only
void timerCallback() override {
    meter.updateValues(audioProcessor.getPeakSPL(), ...);
    meter.repaint();
    log.addEntry(audioProcessor.getLogEntries());
    log.repaint();
}
```

## Colour Palette
- Background: dark near-black (~0xFF1A1A1A)
- Meter fill: green → yellow → red gradient
- SPL text: white, large bold font
- Log plot lines: blue (dB SPL), orange (dBA), grey (dBC), highlight colour for selected metric
- Psychoacoustic values: light grey labels, white values

## Adding a UI Control
1. Declare in `PluginEditor.h` (Slider/Button + Label + Attachment)
2. Set up in constructor (addAndMakeVisible, range, style, attachment)
3. Position in `resized()` using `getLocalBounds()` / `removeFrom*()` layout
4. Attach to `apvts` via `SliderAttachment` or `ButtonAttachment`

## Save JPG
- Renders the full editor component to an `Image`
- Saves via `FileChooser` → JPEG format
- Triggered by `saveButton`

## FAST/SLOW Toggle
- Buttons are right-aligned in the SPL readout row of `MeterComponent`
- On click: set `splTimeWeight` parameter via `apvts`
- Processor detects change and resets smoothing state
