#!/usr/bin/env bash
# Build SPLMeter Standalone for iPad (iOS Simulator or Device).
#
# Usage:
#   ./build-ios.sh            — build for iPad Simulator (no signing needed)
#   ./build-ios.sh device     — build for a connected iPad (requires signing)
#
# Prerequisites:
#   • Xcode installed at /Applications/Xcode.app
#   • CMake 3.22+ on PATH  (brew install cmake)
#   • For device builds: a valid Apple Developer account and connected iPad

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-ios"
TARGET="${1:-simulator}"

export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer

if [ "$TARGET" = "device" ]; then
    echo "==> Configuring for iPad device (arm64)..."
    cmake -B "$BUILD_DIR" \
        -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="iPhone Developer" \
        "$SCRIPT_DIR"

    echo "==> Building for device..."
    cmake --build "$BUILD_DIR" \
        --config Debug \
        --target SPLMeter_Standalone \
        -- -sdk iphoneos
else
    echo "==> Configuring for iPad Simulator (arm64)..."
    cmake -B "$BUILD_DIR" \
        -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        "$SCRIPT_DIR"

    echo "==> Building for simulator..."
    cmake --build "$BUILD_DIR" \
        --config Debug \
        --target SPLMeter_Standalone \
        -- -sdk iphonesimulator

    echo ""
    echo "==> Build complete."
    echo "    To run in Simulator, open Xcode and use:"
    echo "    open \"$BUILD_DIR/SPLMeter.xcodeproj\""
    echo "    Then select an iPad simulator and press Run."
fi
