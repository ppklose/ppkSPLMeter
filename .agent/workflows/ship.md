# Workflow: Ship / Release

## Pre-Release Checklist
- [ ] All features working in standalone
- [ ] VST3 loads in DAW (Logic / Reaper)
- [ ] AU loads in Logic Pro
- [ ] No `DBG()` calls left in source
- [ ] Version number updated in Projucer (`JucePlugin_VersionString`)
- [ ] README up to date
- [ ] Screenshot current

## macOS Build
```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcodebuild -project Builds/MacOSX/SPLMeter.xcodeproj \
             -scheme "SPLMeter - Standalone Plugin" \
             -configuration Release build

# Then for each plugin format:
# -scheme "SPLMeter - VST3"
# -scheme "SPLMeter - AU"

# iCloud codesign fix after each:
xattr -cr Builds/MacOSX/build/Release/SPLMeter.app
codesign --force --sign - Builds/MacOSX/build/Release/SPLMeter.app
```

## Windows Build
Push to `main` — GitHub Actions triggers Windows standalone build automatically.

## Commit & Tag
```bash
git add -A
git commit -m "Release vX.Y.Z"
git tag vX.Y.Z
git push origin main --tags
```

## Version Bump Location
- Projucer project file: `SPLMeter.jucer` → `VERSION` field
- Build date shown in UI footer is injected at compile time via `__DATE__`
