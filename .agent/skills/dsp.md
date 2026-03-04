# Skill: DSP & Audio Processing

## Architecture
- `processBlock()` runs on the audio thread — strict real-time constraints apply
- Sample rate available as `currentSampleRate` (set in `prepareToPlay()`)
- Block size varies — never assume a fixed size

## IEC 61672 Time Weighting
Exponential moving average of mean-square signal:

```cpp
rmsSmoothed += alpha * (x * x - rmsSmoothed);
```

- FAST: τ = 0.125 s → `alpha = 1 - exp(-1.0 / (0.125 * sampleRate))`
- SLOW: τ = 1.0 s  → `alpha = 1 - exp(-1.0 / (1.0   * sampleRate))`
- Recalculate `alpha` in `prepareToPlay()` and on mode switch
- On mode switch: reset `rmsSmoothedRaw/A/C` and peak holds to avoid transients

## SPL Conversion
```cpp
float spl = 20.0f * std::log10(std::sqrt(rmsSmoothed)) + calOffset;
```
Where `calOffset` maps digital full-scale to acoustic SPL (default 127 dB).

## Weighting Filters
- A-weighting: `AWeightingFilter` (`Source/AWeightingFilter.h`)
- C-weighting: `CWeightingFilter` (`Source/CWeightingFilter.h`)
- Both are biquad cascades, process left channel only for simplicity
- Reset filter state on `prepareToPlay()`

## Psychoacoustic Estimators
- `RoughnessEstimator` — modulation depth in 300 Hz–range
- `SharpnessEstimator` — spectral centroid weighting (acum)
- `FluctuationStrengthEstimator` — slow AM detection
- All live in `Source/` and are called in `processBlock()`
- Keep computation lightweight — these run on every block

## Spectrogram FIFO
```cpp
// In processBlock():
int start1, size1, start2, size2;
spectroFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
// copy samples to spectroBuffer at start1/start2
spectroFifo.finishedWrite(size1 + size2);
```

## Log Entries
- Pushed every 125 ms (`logIntervalSamples = sampleRate * 0.125`)
- Contains: peak SPL, peak dBA, peak dBC, RMS SPL, RMS dBA, RMS dBC,
  roughness, fluctuation, sharpness, loudnessSone
- Pruned to `logDuration` seconds on each push

## File Mode
- `AudioTransportSource` + `AudioFormatReaderSource`
- `fileModeActive` atomic controls routing in `processBlock()`
- File audio replaces microphone input, same processing chain applies
