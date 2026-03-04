# Workflow: Debug

## Layers to Check

### Audio Thread Issues (crackling, wrong values, crashes)
- Check `processBlock()` in `PluginProcessor.cpp`
- Verify alpha calculation: `alpha = 1 - exp(-1 / (tau * sampleRate))`
- Check FAST/SLOW switch resets: `rmsSmoothedRaw`, `rmsSmoothedA`, `rmsSmoothedC`
- Confirm no allocations or locks on audio thread

### GUI Not Updating
- Check `timerCallback()` in `PluginEditor.cpp` — polls at 30 Hz
- Check atomic stores in `processBlock()` (must call `.store()`)
- Check `repaint()` is called after updating display state

### Spectrogram Invisible / Black
- Check `SpectrogramComponent` is not covered by an opaque `fillAll()`
- Check `spectroFifo` is being written to in `processBlock()`
- Check `spectroGain` parameter value

### Log Plot Wrong / Empty
- Check `logSampleCounter` increments and triggers `pushLogEntry()`
- Check `pruneLog()` isn't over-pruning (verify `logDuration` param value)
- Check `LogComponent::paint()` — verify scale mapping

### Peak Hold Not Working
- Check `peakHoldCounterRaw` / `peakHoldCounterA` / `peakHoldCounterC`
- Check `peakHoldTime` parameter is being read in `processBlock()`

### File Mode Issues
- Check `fileModeActive` atomic
- Check `transportSource` state and `readerSource` validity
- Verify `prepareToPlay()` is called on `transportSource` after loading

## Debug Build
Add `-configuration Debug` to the xcodebuild command.
Use Xcode's debugger for breakpoints and runtime inspection.

## Logging
Use `DBG("message")` for temporary debug output (JUCE macro, stdout in standalone).
Remove all `DBG()` calls before committing.

## Known Issues
See `troubleshooting/known-issues.yaml` for documented issues and resolutions.
