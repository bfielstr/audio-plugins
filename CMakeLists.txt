cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")
# Universal by default so the plug-ins load in both native and Rosetta hosts.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk" CACHE PATH "VST3 SDK")

project(AudioPlugins
    VERSION 0.17.0
    DESCRIPTION "Smemplr, Multidyn, Locus, Stretchr, Smacheratr, Para, Widr, Wubr, Levlr, Deepr, Smoothr, Gentlr, Dropr, Orbitr and Ciphr VST3 plug-ins"
    LANGUAGES C CXX
)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

option(PK_BUILD_PLUGINS "Build the VST3 plug-ins (needs the VST3 SDK); OFF builds only DSP + tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Some Command Line Tools installs leave a stale, nearly empty libc++ header directory inside the
# toolchain that shadows the SDK's headers ("'algorithm' file not found"). Point the compiler at
# the SDK's libc++ headers explicitly when that happens.
if(APPLE AND EXISTS "/Library/Developer/CommandLineTools/usr/include/c++/v1"
         AND NOT EXISTS "/Library/Developer/CommandLineTools/usr/include/c++/v1/algorithm")
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _pk_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "pluginkit: working around stale CommandLineTools libc++ headers")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-nostdinc++;-isystem;${_pk_sdk}/usr/include/c++/v1>")
endif()

enable_testing()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PluginKit)

if(PK_BUILD_PLUGINS)
    pk_setup_vst3_sdk()
endif()

add_subdirectory(shared)
add_subdirectory(plugins/smemplr)
add_subdirectory(plugins/multidyn)
add_subdirectory(plugins/locus)
add_subdirectory(plugins/stretchr)
add_subdirectory(plugins/smacheratr)
add_subdirectory(plugins/para)
add_subdirectory(plugins/widr)
add_subdirectory(plugins/wubr)
add_subdirectory(plugins/levlr)
add_subdirectory(plugins/deepr)
add_subdirectory(plugins/dropr)
add_subdirectory(plugins/gentlr)
add_subdirectory(plugins/smoothr)
add_subdirectory(plugins/orbitr)
add_subdirectory(plugins/ciphr)

# Every plug-in's factory presets (plugins/<plugin>/presets) parse against its parameters.
add_executable(factory_presets_tests shared/tests/factory_presets_tests.cpp)
target_compile_definitions(factory_presets_tests PRIVATE PK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(factory_presets_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_link_libraries(factory_presets_tests PRIVATE pluginkit_core ciphr_core deepr_core dropr_core gentlr_core levlr_core locus_core multidyn_core
    orbitr_core para_core smacheratr_core smemplr_core smoothr_core stretchr_core widr_core wubr_core)
add_test(NAME factory_presets COMMAND factory_presets_tests)
