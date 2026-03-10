#!/bin/bash
# sign-and-notarize.sh
# Signs and notarizes the SPLMeter macOS build artefacts.
#
# Prerequisites (one-time setup — see README):
#   SIGNING_IDENTITY  — "Developer ID Application: Your Name (TEAMID)"
#   NOTARIZATION_KEY  — path to your .p8 API key file
#   NOTARIZATION_KEY_ID     — App Store Connect key ID (e.g. ABCDEF1234)
#   NOTARIZATION_KEY_ISSUER — App Store Connect issuer UUID
#
# Usage:
#   ./sign-and-notarize.sh [path/to/build]
#
# Build path defaults to: Builds/MacOSX/build/Release

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration — edit these or export as environment variables
# ---------------------------------------------------------------------------
SIGNING_IDENTITY="${SIGNING_IDENTITY:-}"
NOTARIZATION_KEY="${NOTARIZATION_KEY:-}"
NOTARIZATION_KEY_ID="${NOTARIZATION_KEY_ID:-}"
NOTARIZATION_KEY_ISSUER="${NOTARIZATION_KEY_ISSUER:-}"

BUILD_DIR="${1:-Builds/MacOSX/build/Release}"
ENTITLEMENTS="Source/entitlements.plist"

# ---------------------------------------------------------------------------
# Validate
# ---------------------------------------------------------------------------
if [[ -z "$SIGNING_IDENTITY" ]]; then
  echo "ERROR: Set SIGNING_IDENTITY, e.g.:"
  echo "  export SIGNING_IDENTITY=\"Developer ID Application: Your Name (TEAMID)\""
  exit 1
fi
if [[ -z "$NOTARIZATION_KEY" || -z "$NOTARIZATION_KEY_ID" || -z "$NOTARIZATION_KEY_ISSUER" ]]; then
  echo "ERROR: Set NOTARIZATION_KEY, NOTARIZATION_KEY_ID, NOTARIZATION_KEY_ISSUER"
  exit 1
fi

APP="${BUILD_DIR}/SPLMeter.app"
if [[ ! -d "$APP" ]]; then
  echo "ERROR: App not found at ${APP}"
  echo "Build first:  xcodebuild -project Builds/MacOSX/SPLMeter.xcodeproj -scheme \"SPLMeter - App\" -configuration Release"
  exit 1
fi

# ---------------------------------------------------------------------------
# Strip iCloud resource forks (needed when building from iCloud Drive)
# ---------------------------------------------------------------------------
echo "==> Stripping iCloud resource forks..."
find "$APP" -exec xattr -c {} \; 2>/dev/null || true

# ---------------------------------------------------------------------------
# Sign
# ---------------------------------------------------------------------------
echo "==> Signing inner frameworks/dylibs..."
find "$APP" \( -name "*.dylib" -o -name "*.framework" \) | while read -r f; do
  codesign --force --options runtime \
    --sign "$SIGNING_IDENTITY" "$f"
done

echo "==> Signing .app..."
codesign --force --options runtime \
  --entitlements "$ENTITLEMENTS" \
  --sign "$SIGNING_IDENTITY" \
  "$APP"

echo "==> Verifying signature..."
codesign --verify --deep --strict "$APP"
spctl --assess --type execute --verbose "$APP" 2>&1 || true

# ---------------------------------------------------------------------------
# Notarize
# ---------------------------------------------------------------------------
ZIP="/tmp/SPLMeter-notarize.zip"

echo "==> Creating zip for notarization..."
ditto -c -k --keepParent "$APP" "$ZIP"

echo "==> Submitting to Apple Notary Service (this may take a minute)..."
xcrun notarytool submit "$ZIP" \
  --key "$NOTARIZATION_KEY" \
  --key-id "$NOTARIZATION_KEY_ID" \
  --issuer "$NOTARIZATION_KEY_ISSUER" \
  --wait

rm "$ZIP"

# ---------------------------------------------------------------------------
# Staple
# ---------------------------------------------------------------------------
echo "==> Stapling notarization ticket..."
xcrun stapler staple "$APP"

echo ""
echo "Done! Final Gatekeeper check:"
spctl --assess --type execute --verbose "$APP"

# ---------------------------------------------------------------------------
# Package DMG
# ---------------------------------------------------------------------------
DMG="${BUILD_DIR}/SPLMeter-macOS.dmg"
echo ""
echo "==> Creating DMG..."
hdiutil create -volname "SPLMeter" -srcfolder "$APP" \
  -ov -format UDZO "$DMG"
echo "DMG ready: ${DMG}"
