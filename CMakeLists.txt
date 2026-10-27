cmake_minimum_required(VERSION 3.24)

# A host process only dlopens a slice matching the architecture it was launched as, so an
# Intel-only bundle is refused outright by a native Apple Silicon host. Pinning the slices here
# keeps that from depending on the shell that ran configure.
if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures to build" FORCE)
endif()

project(Spiral303 VERSION 0.37.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found in vendor/JUCE.\n"
        "Fetch it once with:\n"
        "  git clone --depth 1 --branch 8.0.14 https://github.com/juce-framework/JUCE.git vendor/JUCE")
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/clap-juce-extensions/CMakeLists.txt")
    message(FATAL_ERROR
        "clap-juce-extensions not found in vendor/clap-juce-extensions.\n"
        "Fetch it once with:\n"
        "  git clone https://github.com/dsp56300/clap-juce-extensions.git vendor/clap-juce-extensions\n"
        "  git -C vendor/clap-juce-extensions checkout e1f67893cc409a40c1154fa2e78c97046da24ce0\n"
        "  git -C vendor/clap-juce-extensions submodule update --init --recursive")
endif()

add_subdirectory(vendor/JUCE)

# CLAP format support on top of JUCE. Adds the Spiral303_CLAP target, which builds
# a .clap alongside the VST3/AU/Standalone artefacts.
add_subdirectory(vendor/clap-juce-extensions EXCLUDE_FROM_ALL)

# Factory presets and famous lines. Drop a .json into presets/ and it becomes a program
# on the next build - the file name prefix sets the order, see presets/README.md.
file(GLOB spiral303_presets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json")
list(SORT spiral303_presets)
if(NOT spiral303_presets)
    message(FATAL_ERROR "presets/ holds no .json factory preset.")
endif()

juce_add_binary_data(PresetBank
    NAMESPACE PresetBank
    HEADER_NAME "PresetBankData.h"
    SOURCES ${spiral303_presets}
)

juce_add_plugin(Spiral303
    COMPANY_NAME "Spiralfractal"
    PLUGIN_MANUFACTURER_CODE Sp30
    PLUGIN_CODE Spr3
    FORMATS VST3 AU LV2 Standalone
    PRODUCT_NAME "Spiral303"
    BUNDLE_ID "com.spiral303.plugin"
    LV2URI "https://plinken.org/lv2"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
)

if (APPLE)
    set_property(TARGET Spiral303 PROPERTY JUCE_COPY_PLUGIN_AFTER_BUILD TRUE)
    juce_enable_copy_plugin_step(Spiral303)
endif()

target_sources(Spiral303 PRIVATE
    Source/PluginProcessor.cpp
    Source/CreateEditor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp
    Source/dsp/Sequence.cpp
    Source/dsp/WavExport.cpp
)

target_link_libraries(Spiral303 PRIVATE
    PresetBank
    juce::juce_audio_utils
    juce::juce_dsp
)

target_compile_definitions(Spiral303 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    SPIRAL303_VERSION="${PROJECT_VERSION}"
)

# Without this MSVC maps the \u escapes in the UI labels ('▾', '◀▶') into the system
# code page and emits '?' instead of the glyph.
if (MSVC)
    target_compile_options(Spiral303 PRIVATE /utf-8)
    # /fp:fast was tried here and broke the loudness calibration of the
    # saturator modes (tanh precision), so Release stays at the default /O2.
else()
    target_compile_options (Spiral303 PRIVATE -Wall -Wextra)
    target_compile_options (Spiral303 PRIVATE $<$<AND:$<CONFIG:Release>,$<OR:$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>>:-O3>)
endif()

juce_generate_juce_header(Spiral303)

# CLAP: same shared code as VST3/AU, wrapped by clap-juce-extensions. CLAP_ID matches BUNDLE_ID,
# features come from https://github.com/free-audio/clap/blob/main/include/clap/plugin-features.h.
clap_juce_extensions_plugin(TARGET Spiral303
    CLAP_ID "com.spiral303.plugin"
    CLAP_FEATURES instrument synth monophonic bass lead "virtual analog" acid
)

# Offline render test binary (no audio device required)
juce_add_console_app(Spiral303Tests)

target_sources(Spiral303Tests PRIVATE
    tests/TestRender.cpp
    tests/TestCoverage.cpp
    tests/TestGui.cpp
    tests/TestStability.cpp
    tests/TestCoreParity.cpp
    tests/CoreParityCore.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/CreateEditor.cpp
    Source/Presets.cpp
    Source/dsp/Sequence.cpp
    Source/dsp/WavExport.cpp
)

if (NOT MSVC)
    target_compile_options (Spiral303Tests PRIVATE -Wall -Wextra)
    target_compile_options (Spiral303Tests PRIVATE $<$<AND:$<CONFIG:Release>,$<OR:$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>>:-O3>)
endif()

target_link_libraries(Spiral303Tests PRIVATE
    PresetBank
    juce::juce_audio_utils
    juce::juce_dsp
)

target_compile_definitions(Spiral303Tests PRIVATE
    SPIRAL303_TESTS=1
    SPIRAL303_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

if (MSVC)
    target_compile_options(Spiral303Tests PRIVATE /utf-8)
endif()

juce_generate_juce_header(Spiral303Tests)
