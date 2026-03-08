#!/usr/bin/env bash
# Build SPLMeter Standalone (Debug) and launch it.
# The iCloud Drive codesign issue (resource fork error) is handled automatically.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
XCPROJ="$SCRIPT_DIR/Builds/MacOSX/SPLMeter.xcodeproj"
APP="$SCRIPT_DIR/Builds/MacOSX/build/Debug/SPLMeter.app"

export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer

echo "==> Building SPLMeter Standalone (Debug)..."
BUILD_OUTPUT=$(xcodebuild \
  -project "$XCPROJ" \
  -scheme "SPLMeter - Standalone Plugin" \
  -configuration Debug \
  build 2>&1)

# Check for real compile/link errors (not the iCloud codesign issue)
REAL_ERRORS=$(echo "$BUILD_OUTPUT" | grep "error:" | grep -v "resource fork\|detritus\|CodeSign" || true)
if [ -n "$REAL_ERRORS" ]; then
    echo "$REAL_ERRORS"
    echo "BUILD FAILED — fix errors above."
    exit 1
fi

echo "==> Stripping iCloud extended attributes and signing..."
xattr -cr "$APP"
codesign --force --sign - "$APP"

echo "==> Launching SPLMeter..."
open "$APP"
