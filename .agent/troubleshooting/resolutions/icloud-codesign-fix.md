# Resolution: iCloud CodeSign Fix

**Issue ID:** build-001
**Applies to:** Every build when project is in an iCloud-synced directory

## Symptoms
```
SPLMeter.app: resource fork, Finder information, or similar detritus not allowed
Command CodeSign failed with a nonzero exit code
```

## Root Cause
iCloud CloudDocs (com.apple.CloudDocs) adds extended attributes and resource forks
to files inside the .app bundle. `codesign` rejects these unconditionally.

## Fix
Run after every successful xcodebuild:

```bash
APP="Builds/MacOSX/build/Release/SPLMeter.app"
xattr -cr "$APP"
/usr/bin/codesign --force --sign - "$APP"
open "$APP"
```

- `xattr -cr` — recursively clears all extended attributes
- `codesign --force --sign -` — re-signs with ad-hoc identity

## Permanent Solution
Moving the project out of iCloud Drive would eliminate this entirely,
but is not required — the fix is fast and reliable.
