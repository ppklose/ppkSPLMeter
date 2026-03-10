cmake_minimum_required(VERSION 3.22)
project(SPLMeter VERSION 2.2.2)

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
    VERSION                     2.2.2
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
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
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

# ViSQOL static library + required Apple frameworks — macOS desktop only
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_library(VISQOL_CORE_FOUNDATION  CoreFoundation       REQUIRED)
    find_library(VISQOL_SECURITY         Security             REQUIRED)
    find_library(VISQOL_SYSCONFIGURATION SystemConfiguration  REQUIRED)
    target_link_libraries(SPLMeter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/visqol/lib/libvisqol_ffi.a
        ${VISQOL_CORE_FOUNDATION}
        ${VISQOL_SECURITY}
        ${VISQOL_SYSCONFIGURATION}
    )
endif()
