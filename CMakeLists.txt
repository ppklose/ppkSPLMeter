cmake_minimum_required(VERSION 3.22)
project(SPLMeter VERSION 3.3.0)

set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Fetch JUCE 8.0.7 (matches the Projucer version used for the macOS build)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# ASIO SDK — Windows only
#
# Priority order:
#   1. CMake cache variable ASIO_SDK_DIR (set by user / CI: -DASIO_SDK_DIR=...)
#   2. Vendor/asiosdk/ inside the project root (any nesting depth)
#   3. Auto-download from Steinberg CDN via FetchContent
#
# The ASIO SDK headers (asio.h, iasiodrv.h …) live in the "common/" sub-folder
# of the SDK root.  CMake locates that folder automatically regardless of the
# archive structure.
# ---------------------------------------------------------------------------
if(WIN32)
    set(ASIO_SDK_DIR "" CACHE PATH "Path to the ASIO SDK 'common' directory (containing asio.h)")

    # --- 1. User-provided path (already contains asio.h) ---
    if(ASIO_SDK_DIR AND EXISTS "${ASIO_SDK_DIR}/iasiodrv.h")
        message(STATUS "ASIO SDK (user-supplied): ${ASIO_SDK_DIR}")

    # --- 2. Vendor directory (any depth) ---
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/asiosdk")
        file(GLOB_RECURSE _ASIO_HEADER
             "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/asiosdk/*/iasiodrv.h")
        if(_ASIO_HEADER)
            list(GET _ASIO_HEADER 0 _ASIO_HEADER)
            get_filename_component(ASIO_SDK_DIR "${_ASIO_HEADER}" DIRECTORY)
            message(STATUS "ASIO SDK (Vendor/): ${ASIO_SDK_DIR}")
        endif()

    # --- 3. Auto-download from Steinberg ---
    else()
        message(STATUS "ASIO SDK not found locally — downloading from Steinberg …")
        FetchContent_Declare(
            asiosdk
            URL                      https://download.steinberg.net/sdk_downloads/asiosdk_2.3.3_2019-06-14.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(asiosdk)

        # Locate iasiodrv.h inside the extracted tree
        file(GLOB_RECURSE _ASIO_HEADER "${asiosdk_SOURCE_DIR}/*/iasiodrv.h")
        if(_ASIO_HEADER)
            list(GET _ASIO_HEADER 0 _ASIO_HEADER)
            get_filename_component(ASIO_SDK_DIR "${_ASIO_HEADER}" DIRECTORY)
            message(STATUS "ASIO SDK (downloaded): ${ASIO_SDK_DIR}")
        else()
            message(WARNING "ASIO SDK downloaded but iasiodrv.h not found — ASIO disabled")
        endif()
    endif()

    if(ASIO_SDK_DIR)
        message(STATUS "ASIO support ENABLED: ${ASIO_SDK_DIR}")
    else()
        message(STATUS "ASIO support DISABLED (WASAPI/WDM only)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
# Format selection: iOS → Standalone only; macOS → Standalone + VST3 + AU; Windows/Linux → Standalone + VST3
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(SPLMETER_FORMATS Standalone)
elseif(APPLE)
    set(SPLMETER_FORMATS Standalone VST3 AU)
else()
    set(SPLMETER_FORMATS Standalone VST3)
endif()

# ---------------------------------------------------------------------------
# Linux system dependencies required by JUCE
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA       REQUIRED IMPORTED_TARGET alsa)
    pkg_check_modules(FREETYPE   REQUIRED IMPORTED_TARGET freetype2)
    pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
    pkg_check_modules(X11        REQUIRED IMPORTED_TARGET x11)
    pkg_check_modules(GTK3       REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(GL         REQUIRED IMPORTED_TARGET gl)
endif()

juce_add_plugin(SPLMeter
    VERSION                     3.3.0
    PLUGIN_MANUFACTURER_CODE    Manu
    PLUGIN_CODE                 Splm
    FORMATS                     ${SPLMETER_FORMATS}
    PRODUCT_NAME                "SPLMeter"
    COMPANY_NAME                "Philipp Paul Klose Audio Engineering"
    BUNDLE_ID                   com.PhilippPaulKloseAudioEngineering.SPLMeter
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Source/icon_512.png"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    # iOS microphone access
    NEEDS_MICROPHONE_INPUT      TRUE
    MICROPHONE_PERMISSION_TEXT  "SPLMeter needs microphone access for real-time SPL measurement."
    PLIST_TO_MERGE              "<plist><dict><key>NSMicrophoneUsageDescription</key><string>SPLMeter needs microphone access for real-time SPL measurement.</string><key>UIBackgroundModes</key><array><string>audio</string></array></dict></plist>"
)

juce_generate_juce_header(SPLMeter)

target_sources(SPLMeter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/MeterComponent.cpp
    Source/LogComponent.cpp
)

# SoundDetective: Apple ML backend (macOS + iOS only)
if(APPLE)
    target_sources(SPLMeter PRIVATE Source/SoundDetectiveApple.mm)
    find_library(SOUND_ANALYSIS_FRAMEWORK SoundAnalysis)
    find_library(AV_FOUNDATION_FRAMEWORK  AVFoundation  REQUIRED)
    find_library(CORE_MEDIA_FRAMEWORK     CoreMedia     REQUIRED)
    if(SOUND_ANALYSIS_FRAMEWORK)
        target_link_libraries(SPLMeter PRIVATE ${SOUND_ANALYSIS_FRAMEWORK})
    else()
        # Weak-link so the binary still runs on older OS versions
        target_link_libraries(SPLMeter PRIVATE "-weak_framework SoundAnalysis")
    endif()
    target_link_libraries(SPLMeter PRIVATE
        ${AV_FOUNDATION_FRAMEWORK}
        ${CORE_MEDIA_FRAMEWORK}
    )
endif()

# ---------------------------------------------------------------------------
# TFLite cross-platform ML backend
# Fetches only the tensorflow/lite sub-tree via SOURCE_SUBDIR.
# First build is slow (~5–10 min); subsequent builds use cached deps.
# ---------------------------------------------------------------------------
option(SPLMETER_TFLITE "Enable TFLite ML inference backend (all platforms)" ON)

if(SPLMETER_TFLITE)
    message(STATUS "TFLite enabled — fetching TensorFlow Lite 2.16.2 …")

    # Suppress warnings from TFLite's own headers
    if(MSVC)
        add_compile_options(/wd4996 /wd4244 /wd4267)
    endif()

    set(TFLITE_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)
    set(TFLITE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)

    # flatbuffers' CMakeLists tries to install 'flatc' as MACOSX_BUNDLE which
    # requires BUNDLE DESTINATION — fails on iOS.  We only need the runtime lib.
    set(FLATBUFFERS_BUILD_FLATC  OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_INSTALL      OFF CACHE BOOL "" FORCE)

    # CMake 3.31 tightened the git update step: it now runs "git rev-parse HEAD"
    # before updating, which fails for any FetchContent git source that was cloned
    # shallow or whose .git directory is absent (e.g. extracted from a tarball cache).
    # JUCE (GIT_SHALLOW clone) and TFLite's sub-deps farmhash/eigen/abseil-cpp
    # (all fetched via GIT_REPOSITORY inside TFLite's OverridableFetchContent) all
    # exhibit this failure.  Setting per-content DISCONNECTED suppresses the update
    # step for each one individually without affecting the initial download.
    # Belt-and-suspenders: set the global flag in addition to per-content overrides.
    # The per-content flags don't reliably propagate through TFLite's
    # OverridableFetchContent_Populate path (e.g. gemmlowp 2026-05 CI failure).
    set(FETCHCONTENT_UPDATES_DISCONNECTED              ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_JUCE         ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_FARMHASH     ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_EIGEN        ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_ABSEIL-CPP   ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_NEON2SSE     ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_FLATBUFFERS  ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_FFT2D        ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_GEMMLOWP     ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_RUY          ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_CPUINFO      ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_PTHREADPOOL  ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_FXDIV        ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_FP16         ON CACHE BOOL "" FORCE)
    set(FETCHCONTENT_UPDATES_DISCONNECTED_XNNPACK      ON CACHE BOOL "" FORCE)

    # Abseil's check_cxx_compiler_flag(-msse4.1) returns true on arm64-apple-darwin
    # because AppleClang accepts the flag syntactically, but actual compilation then
    # fails with "unsupported option '-msse4.1' for target 'arm64-apple-darwin'".
    # Pre-setting these cache vars (BOOL, matching check_cxx_compiler_flag output
    # type) to OFF prevents the flag from being applied.  We also block
    # ABSL_RANDOM_RANDEN_HWAES_IMPL_COPTS directly so randen_hwaes_impl never
    # receives -msse4.1/-maes regardless of how cmake evaluates the checks above.
    if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(ABSL_HAVE_MSSE4_1 OFF CACHE BOOL "SSE4.1 not available on ARM64" FORCE)
        set(ABSL_HAVE_MAES    OFF CACHE BOOL "AES-NI not available on ARM64"  FORCE)
        set(ABSL_RANDOM_RANDEN_HWAES_IMPL_COPTS "" CACHE STRING
            "No x86 HWAES flags on ARM64" FORCE)
    endif()

    # neon2sse sub-project uses cmake_minimum_required(VERSION 2.x) which was
    # removed in CMake 3.31 — set the policy minimum floor to suppress the error.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

    FetchContent_Declare(
        tensorflow_lite
        URL https://github.com/tensorflow/tensorflow/archive/refs/tags/v2.16.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR  "tensorflow/lite"
    )
    FetchContent_MakeAvailable(tensorflow_lite)

    target_compile_definitions(SPLMeter PUBLIC SPLMETER_HAS_TFLITE=1)
    target_link_libraries(SPLMeter PRIVATE tensorflow-lite)
    # TF root must be on the include path so <tensorflow/lite/c/c_api.h> resolves
    target_include_directories(SPLMeter SYSTEM PRIVATE ${tensorflow_lite_SOURCE_DIR})
endif()

target_include_directories(SPLMeter PRIVATE
    Source
    $<$<BOOL:${ASIO_SDK_DIR}>:${ASIO_SDK_DIR}>
)

target_compile_definitions(SPLMeter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<BOOL:${ASIO_SDK_DIR}>:JUCE_ASIO=1>
)

target_link_libraries(SPLMeter
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Strip iCloud resource forks before Xcode codesigns the app bundle.
# This is needed because the project lives in iCloud Drive and CloudDocs
# adds resource forks to files, which codesign rejects.
if(APPLE AND TARGET SPLMeter_Standalone)
    add_custom_command(TARGET SPLMeter_Standalone POST_BUILD
        COMMAND find "$<TARGET_BUNDLE_DIR:SPLMeter_Standalone>" -exec xattr -c {} ";" 2>/dev/null || true
        COMMENT "Strip iCloud resource forks before codesign"
        VERBATIM
    )
endif()

# ViSQOL static library — enabled when the pre-built lib is present in Vendor/
#   macOS:   Vendor/visqol/lib/libvisqol_ffi.a
#   Windows: Vendor/visqol/lib/visqol_ffi.lib
if(WIN32)
    set(_VISQOL_LIB "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/visqol/lib/visqol_ffi.lib")
elseif(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_VISQOL_LIB "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/visqol/lib/libvisqol_ffi.a")
else()
    set(_VISQOL_LIB "")
endif()

if(_VISQOL_LIB AND EXISTS "${_VISQOL_LIB}")
    message(STATUS "ViSQOL enabled: ${_VISQOL_LIB}")
    target_compile_definitions(SPLMeter PUBLIC SPLMETER_HAS_VISQOL=1)
    target_link_libraries(SPLMeter PRIVATE "${_VISQOL_LIB}")
    if(APPLE)
        find_library(VISQOL_CORE_FOUNDATION  CoreFoundation       REQUIRED)
        find_library(VISQOL_SECURITY         Security             REQUIRED)
        find_library(VISQOL_SYSCONFIGURATION SystemConfiguration  REQUIRED)
        target_link_libraries(SPLMeter PRIVATE
            ${VISQOL_CORE_FOUNDATION}
            ${VISQOL_SECURITY}
            ${VISQOL_SYSCONFIGURATION}
        )
    endif()
else()
    message(STATUS "ViSQOL disabled (pre-built lib not found at ${_VISQOL_LIB})")
endif()

# ---------------------------------------------------------------------------
# Unit tests (Catch2)
# ---------------------------------------------------------------------------
option(SPLMETER_BUILD_TESTS "Build unit tests" OFF)

if(SPLMETER_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(SPLMeterTests
        Tests/TestMain.cpp
        Tests/AWeightingFilterTests.cpp
        Tests/CWeightingFilterTests.cpp
        Tests/RoughnessEstimatorTests.cpp
        Tests/SharpnessEstimatorTests.cpp
        Tests/FluctuationEstimatorTests.cpp
        Tests/ImpulsivenessEstimatorTests.cpp
        Tests/TonalityEstimatorTests.cpp
        Tests/SPLCalculationTests.cpp
        Tests/LoudnessAnnoyanceTests.cpp
        Tests/StateSerializationTests.cpp
        Tests/MarkerTests.cpp
        Tests/LeqTests.cpp
    )

    target_include_directories(SPLMeterTests PRIVATE Source)
    target_link_libraries(SPLMeterTests PRIVATE Catch2::Catch2WithMain)

    # State serialization tests need JUCE
    target_link_libraries(SPLMeterTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    )
    target_compile_definitions(SPLMeterTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_UNIT_TESTS=1
    )

    include(CTest)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(SPLMeterTests)
endif()
