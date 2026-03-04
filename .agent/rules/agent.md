# ppkSPLmeter — Agent Rules

These rules apply in every session, every task.

## Project Identity
- App: ppkSPLmeter — professional SPL meter (standalone + VST3 + AU)
- Framework: JUCE (Projucer-managed, NOT CMake on macOS)
- Xcode project: `Builds/MacOSX/SPLMeter.xcodeproj`
- UI size: 960 × 1080 px
- Target: macOS 10.13+, Apple Silicon native, Windows 10+ (CMake/CI)

## Build Rules
- ALWAYS use `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcodebuild ...`
  (default dev dir points to CommandLineTools — builds will fail without this)
- After every build, run the iCloud codesign fix (see `workflows/build.md`)
- NEVER use `cmake` on macOS — macOS builds are Xcode only
- NEVER modify `.xcodeproj` manually — it is managed by Projucer

## Code Rules
- Audio thread: only atomics and lock-free structures (no allocations, no locks)
- GUI thread: polls atomics at 30 Hz via `timerCallback()`
- Log entries pushed every 125 ms, pruned to `logDuration` seconds
- All parameters live in `apvts` (AudioProcessorValueTreeState)
- Do not add parameters without also updating `createParameterLayout()`

## Style Rules
- C++17, JUCE idioms
- No raw owning pointers — use `std::unique_ptr` or JUCE smart types
- No heap allocations on the audio thread
- Keep DSP and UI concerns separated (processor vs editor)

## Communication
- Be concise — no filler, no preamble
- Show file:line references when pointing to code
- Ask before making architectural changes
