cmake_minimum_required(VERSION 3.24)

# The WebAssembly build of Spiral303: a CLAP plugin compiled to wasm32, packed
# into a .wclap bundle (module.wasm plus the interface files).
#
# It is a project of its own because the toolchain is different: the native
# build uses the host compiler, this one uses the wasi-sdk. Configure it with
#
#   cmake -S wclap -B build-wclap \
#         -DCMAKE_TOOLCHAIN_FILE=vendor/wasi-sdk-XX/share/cmake/wasi-sdk-p1.cmake
#
# tools/build_wclap.sh does that, downloads the sdk and packs the result.

# One version source: the root project's VERSION. tools/build_wclap.sh names the
# bundle from that same line, so the module cannot report something else.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" SPIRAL303_ROOT_CMAKE)
string(REGEX MATCH "project\\(Spiral303 VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" _unused "${SPIRAL303_ROOT_CMAKE}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Cannot read project(Spiral303 VERSION ...) from ../CMakeLists.txt.")
endif()
project(Spiral303Wclap VERSION "${CMAKE_MATCH_1}" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    message(FATAL_ERROR
        "This project builds a WebAssembly module and needs the wasi-sdk toolchain.\n"
        "Use tools/build_wclap.sh, or point CMAKE_TOOLCHAIN_FILE at the sdk's\n"
        "share/cmake/wasi-sdk-p1.cmake.")
endif()

get_filename_component(SPIRAL303_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# The CLAP headers: the ones vendored with clap-juce-extensions.
set(CLAP_INCLUDE_DIR "${SPIRAL303_ROOT}/vendor/clap-juce-extensions/clap-libs/clap/include")
if(NOT EXISTS "${CLAP_INCLUDE_DIR}/clap/clap.h")
    message(FATAL_ERROR
        "The CLAP headers are not in vendor/clap-juce-extensions.\n"
        "Fetch it with:\n"
        "  git clone https://github.com/dsp56300/clap-juce-extensions.git vendor/clap-juce-extensions\n"
        "  git -C vendor/clap-juce-extensions checkout e1f67893cc409a40c1154fa2e78c97046da24ce0\n"
        "  git -C vendor/clap-juce-extensions submodule update --init --recursive")
endif()

# The factory presets, embedded into the module: the native build gets them
# from JUCE's binary data, which does not exist here.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

file(GLOB spiral303_presets CONFIGURE_DEPENDS "${SPIRAL303_ROOT}/presets/*.json")
list(SORT spiral303_presets)
if(NOT spiral303_presets)
    message(FATAL_ERROR "presets/ holds no .json factory preset.")
endif()

set(PRESET_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/FactoryPresetData.h")

add_custom_command(
    OUTPUT "${PRESET_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_presets.py"
            "${SPIRAL303_ROOT}/presets" "${PRESET_HEADER}"
    DEPENDS ${spiral303_presets} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_presets.py"
    COMMENT "Embedding the factory presets into the module"
    VERBATIM
)

# The interface files, embedded as well: the plugin serves the page from its
# own memory.
set(WEB_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/WebAssets.h")

file(GLOB spiral303_web_files CONFIGURE_DEPENDS "${SPIRAL303_ROOT}/web/*")
list(SORT spiral303_web_files)

add_custom_command(
    OUTPUT "${WEB_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_web.py"
            "${SPIRAL303_ROOT}/web" "${WEB_HEADER}"
    DEPENDS ${spiral303_web_files} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_web.py"
    COMMENT "Embedding the interface files into the module"
    VERBATIM
)

add_executable(Spiral303_wclap
    "${SPIRAL303_ROOT}/Source/wclap/ClapEntry.cpp"
    "${SPIRAL303_ROOT}/Source/wclap/Spiral303Clap.cpp"
    "${SPIRAL303_ROOT}/Source/wclap/ProcessorCore.cpp"
    "${SPIRAL303_ROOT}/Source/wclap/ParamTable.cpp"
    "${SPIRAL303_ROOT}/Source/wclap/PresetCore.cpp"
    "${SPIRAL303_ROOT}/Source/wclap/JsonLite.cpp"
    "${SPIRAL303_ROOT}/Source/wclap/StateXml.cpp"
    "${SPIRAL303_ROOT}/Source/wclap/WebView.cpp"
    "${SPIRAL303_ROOT}/Source/dsp/Sequence.cpp"
    "${PRESET_HEADER}"
    "${WEB_HEADER}"
)

target_include_directories(Spiral303_wclap PRIVATE
    "${CLAP_INCLUDE_DIR}"
    "${SPIRAL303_ROOT}/Source/wclap"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(Spiral303_wclap PRIVATE
    SPIRAL303_CORE_ONLY=1
    SPIRAL303_VERSION="${PROJECT_VERSION}"
)

target_compile_options(Spiral303_wclap PRIVATE -O3 -fno-exceptions -msimd128)

# The WebCLAP rules: a reactor module that exports the entry point, the
# allocator and a growable function table, with a memory that can grow to 2 GB.
target_link_options(Spiral303_wclap PRIVATE
    -mexec-model=reactor
    "-Wl,--max-memory=2147483648,--export-table,--growable-table,--export=malloc,--export=clap_entry"
)

# The bundle: Spiral303.wclap/module.wasm, with the interface files beside it.
set_target_properties(Spiral303_wclap PROPERTIES
    OUTPUT_NAME "Spiral303"
    PREFIX ""
    SUFFIX ".wclap/module.wasm"
)

if(EXISTS "${SPIRAL303_ROOT}/web")
    add_custom_command(TARGET Spiral303_wclap POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${SPIRAL303_ROOT}/web" "$<TARGET_FILE_DIR:Spiral303_wclap>"
        COMMENT "Copying the interface files into the bundle"
        VERBATIM
    )
endif()

# The bundle directory has to exist before the linker writes into it.
add_custom_command(TARGET Spiral303_wclap PRE_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:Spiral303_wclap>"
    VERBATIM
)

# The host half of the test harness: the callbacks the plugin calls back into.
# A WebAssembly table only holds WebAssembly functions, so a host that wants to
# hand the plugin an event list has to bring a module of its own.
add_executable(host-half "${CMAKE_CURRENT_SOURCE_DIR}/host/host-half.cpp")
target_compile_options(host-half PRIVATE -O2 -fno-exceptions)
target_link_options(host-half PRIVATE
    -mexec-model=reactor
    "LINKER:--export-memory"
    "LINKER:--export=host_events_begin"
    "LINKER:--export=host_event_size"
    "LINKER:--export=host_event_get"
    "LINKER:--export=host_get_extension"
    "LINKER:--export=host_register_id"
    "LINKER:--export=host_set_extension_struct"
    "LINKER:--export=host_params_rescan"
    "LINKER:--export=host_params_clear"
    "LINKER:--export=host_params_request_flush"
    "LINKER:--export=host_webview_send"
    "LINKER:--export=host_last_message_ptr"
    "LINKER:--export=host_last_message_size"
    "LINKER:--export=host_stream_write"
    "LINKER:--export=host_last_write_ptr"
    "LINKER:--export=host_last_write_size"
    "LINKER:--export=host_param_hovered_update"
    "LINKER:--export=host_last_hovered_param"
)
set_target_properties(host-half PROPERTIES OUTPUT_NAME "host-half" PREFIX "" SUFFIX ".wasm")
