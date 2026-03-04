# Workflow: Build & Run

## Build Standalone (macOS)

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcodebuild \
  -project "Builds/MacOSX/SPLMeter.xcodeproj" \
  -scheme "SPLMeter - Standalone Plugin" \
  -configuration Release \
  build
```

## iCloud Codesign Fix (always required after build)

iCloud CloudDocs adds resource forks to the .app bundle, causing CodeSign to fail.
Run this every time after a successful build:

```bash
APP="Builds/MacOSX/build/Release/SPLMeter.app"
xattr -cr "$APP"
/usr/bin/codesign --force --sign - "$APP"
open "$APP"
```

## Build VST3 / AU

Change `-scheme` to:
- `"SPLMeter - VST3"` for VST3
- `"SPLMeter - AU"` for Audio Unit

## Build for Windows (CI only)

Windows builds run via GitHub Actions using CMake. Do not run CMake locally on macOS.

## Common Build Failures

| Error | Cause | Fix |
|---|---|---|
| `resource fork … not allowed` | iCloud xattrs on .app | Run iCloud codesign fix above |
| `xcode-select` errors | Wrong DEVELOPER_DIR | Prefix command with `DEVELOPER_DIR=...` |
| CodeSign identity missing | No local signing cert | Use `--sign -` (ad-hoc) |
