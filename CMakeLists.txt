cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")
# Universal by default so the plug-in loads in both native and Rosetta hosts.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk" CACHE PATH "VST3 SDK")

project(Simplr
    VERSION 0.1.0.0
    DESCRIPTION "Simplr - a Simpler-style sampler instrument"
    LANGUAGES C CXX OBJC OBJCXX
)

option(SIMPLR_BUILD_PLUGIN "Build the VST3 plug-in (needs the VST3 SDK)" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Some Command Line Tools installs leave a stale, nearly empty libc++ header directory inside the
# toolchain that shadows the SDK's headers ("'algorithm' file not found"). Point the compiler at
# the SDK's libc++ headers explicitly when that happens.
if(APPLE AND EXISTS "/Library/Developer/CommandLineTools/usr/include/c++/v1"
         AND NOT EXISTS "/Library/Developer/CommandLineTools/usr/include/c++/v1/algorithm")
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _simplr_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "Simplr: working around stale CommandLineTools libc++ headers")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-nostdinc++;-isystem;${_simplr_sdk}/usr/include/c++/v1>")
endif()

if(WIN32)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    add_compile_options(/utf-8 /bigobj)
endif()

# ---------------------------------------------------------------------------
# Framework-free DSP core
add_library(simplr_core STATIC
    src/core/Params.cpp
    src/core/SampleData.cpp
    src/core/Slices.cpp
    src/core/Warp.cpp
    src/core/Engine.cpp
)
target_include_directories(simplr_core PUBLIC src/core third_party/dr_libs)
if(MSVC)
    target_compile_options(simplr_core PRIVATE /O2 /fp:fast)
else()
    target_compile_options(simplr_core PRIVATE -O3 -fcx-limited-range)
endif()
set_target_properties(simplr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(simplr_tests tests/core_tests.cpp)
target_link_libraries(simplr_tests PRIVATE simplr_core)

enable_testing()
add_test(NAME core COMMAND simplr_tests)

if(NOT SIMPLR_BUILD_PLUGIN)
    return()
endif()

# ---------------------------------------------------------------------------
# VST3 plug-in
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE) # builds the SDK validator
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_BUILD_UNIVERSAL_BINARY OFF CACHE BOOL "" FORCE)
set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")

# The SDK insists on detecting a full Xcode install; with only the Command Line Tools
# `xcodebuild -version` fails, so provide a version explicitly.
if(APPLE AND NOT DEFINED ENV{XCODE_VERSION})
    execute_process(COMMAND xcodebuild -version OUTPUT_VARIABLE _xc ERROR_QUIET RESULT_VARIABLE _xc_res)
    if(NOT _xc_res EQUAL 0)
        message(STATUS "Simplr: no full Xcode found, building with the Command Line Tools")
        set(ENV{XCODE_VERSION} "16.0")
        set(XCODE_VERSION "16.0")
    endif()
endif()

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(Simplr
    src/plugin/Bridge.h
    src/plugin/Bridge.cpp
    src/plugin/Cids.h
    src/plugin/Processor.h
    src/plugin/Processor.cpp
    src/plugin/Controller.h
    src/plugin/Controller.cpp
    src/plugin/Entry.cpp
    src/plugin/StateIO.h
    src/plugin/StateIO.cpp
    src/plugin/Version.h
    src/ui/Theme.h
    src/ui/Widgets.h
    src/ui/Widgets.cpp
    src/ui/WaveformView.h
    src/ui/WaveformView.cpp
    src/ui/Displays.h
    src/ui/Displays.cpp
    src/ui/Editor.h
    src/ui/Editor.cpp
)
target_include_directories(Simplr PRIVATE src)
target_link_libraries(Simplr PRIVATE simplr_core sdk vstgui_support)
smtg_target_configure_version_file(Simplr)

# Ship the third-party licence notices inside the bundle (before it gets signed).
configure_file(${vst3sdk_SOURCE_DIR}/LICENSE.txt ${PROJECT_BINARY_DIR}/licenses/VST3_SDK_LICENSE.txt COPYONLY)
configure_file(${vst3sdk_SOURCE_DIR}/vstgui4/LICENSE ${PROJECT_BINARY_DIR}/licenses/VSTGUI_LICENSE.txt COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md ${PROJECT_BINARY_DIR}/licenses/THIRD_PARTY_NOTICES.md COPYONLY)
smtg_target_add_plugin_resources(Simplr
    RESOURCES
        ${PROJECT_BINARY_DIR}/licenses/VST3_SDK_LICENSE.txt
        ${PROJECT_BINARY_DIR}/licenses/VSTGUI_LICENSE.txt
        ${PROJECT_BINARY_DIR}/licenses/THIRD_PARTY_NOTICES.md
)

if(SMTG_MAC)
    smtg_target_set_bundle(Simplr
        BUNDLE_IDENTIFIER com.simplr.vst3.simplr
        COMPANY_NAME "Simplr"
    )
endif()

# Loads the built bundle through the VST3 hosting API, plays notes and checks the output.
if(APPLE)
    add_executable(simplr_hosttest
        tests/host_test.mm
        src/plugin/StateIO.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
    )
    target_include_directories(simplr_hosttest PRIVATE src)
    target_link_libraries(simplr_hosttest PRIVATE sdk_hosting simplr_core "-framework Cocoa")
    target_compile_options(simplr_hosttest PRIVATE -fobjc-arc)
    add_dependencies(simplr_hosttest Simplr)
    file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/test-output)
    add_test(NAME host
             COMMAND simplr_hosttest ${PROJECT_BINARY_DIR}/VST3/$<CONFIG>/Simplr.vst3 ${PROJECT_BINARY_DIR}/test-output)
endif()
