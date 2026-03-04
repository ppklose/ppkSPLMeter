# Workflow: Implement a Feature

## Before You Start
1. Read the relevant source files — never modify code you haven't read
2. Identify which layer the change lives in:
   - **DSP / audio thread** → `PluginProcessor.cpp`
   - **Parameters** → `createParameterLayout()` in `PluginProcessor.cpp`
   - **UI layout** → `PluginEditor.cpp` / `resized()`
   - **Meter display** → `MeterComponent.cpp`
   - **Log/time-series plot** → `LogComponent.cpp`
   - **Spectrogram** → `SpectrogramComponent.h`
3. Check `rules/agent.md` for audio-thread safety rules

## Implementation Steps
1. Read affected files
2. Make minimal, focused changes
3. Build (see `workflows/build.md`)
4. Fix iCloud codesign issue
5. Launch and verify visually / aurally
6. Commit

## Audio Thread Safety Checklist
- [ ] No `new` / `delete` on the audio thread
- [ ] No `std::mutex` — use `juce::SpinLock` or `juce::AbstractFifo`
- [ ] Values passed to GUI via `std::atomic<float>`
- [ ] Spectrogram fed via `spectroFifo` (lock-free AbstractFifo)
- [ ] Log entries pushed via `SpinLock`-guarded `logEntries` deque

## Adding a New Parameter
1. Add to `createParameterLayout()` in `PluginProcessor.cpp`
2. Add atomic + accessor if needed for GUI polling
3. Add UI control in `PluginEditor` (`resized()` + attachment)
4. Update `MEMORY.md` Parameters section

## Adding a New Psychoacoustic Metric
1. Implement estimator class in `Source/` (see `RoughnessEstimator.h` as pattern)
2. Instantiate in `PluginProcessor.h`
3. Call in `processBlock()` — keep it lightweight
4. Add `std::atomic<float>` for GUI
5. Add `LogEntry` field
6. Add selector button in `LogComponent`
