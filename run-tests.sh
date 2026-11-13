#!/bin/bash
# Build and run the unit + processor regression tests outside iCloud Drive.
#
#   ./run-tests.sh            tolerance mode (same as CI)
#   ./run-tests.sh --exact    bit-identical check against Tests/golden (use for refactors)
#   ./run-tests.sh --update   rewrite the processor golden files (only for intended changes!)
set -euo pipefail

cd "$(dirname "$0")"
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export CMAKE_POLICY_VERSION_MINIMUM=3.5

BUILD_DIR="${SPLMETER_TEST_BUILD_DIR:-${TMPDIR:-/tmp}/splmeter-tests}"
CMAKE=$(command -v cmake || echo /opt/homebrew/bin/cmake)

JUCE_SRC_ARG=()
[ -d build-local/_deps/juce-src ] && JUCE_SRC_ARG=(-DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/build-local/_deps/juce-src")

"$CMAKE" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DSPLMETER_TFLITE=OFF \
         -DSPLMETER_BUILD_TESTS=ON "${JUCE_SRC_ARG[@]}" > /dev/null
"$CMAKE" --build "$BUILD_DIR" --target SPLMeterTests SPLMeterProcessorTests -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

case "${1:-}" in
    --exact)  export SPLMETER_GOLDEN_EXACT=1 ;;
    --update) export SPLMETER_UPDATE_GOLDEN=1 ;;
esac

"$BUILD_DIR/SPLMeterTests"
"$BUILD_DIR/SPLMeterProcessorTests"
