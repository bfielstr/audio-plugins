cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")
# Universal by default so the plug-ins load in both native and Rosetta hosts.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk" CACHE PATH "VST3 SDK")

project(AudioPlugins
    VERSION 0.29.0
    DESCRIPTION "Smemplr, Multidyn, Locus, Stretchr, Smacheratr, Para, Widr, Wubr, Levlr, Deepr, Smoothr, Gentlr, Dropr, Orbitr, Ciphr, Moistr, Smeezr and Probr VST3 plug-ins"
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

# Every plug-in, in the order they are added.
set(PK_ALL_PLUGINS smemplr multidyn locus stretchr smacheratr para widr wubr levlr deepr dropr gentlr smoothr
    orbitr ciphr moistr smeezr probr)

# -DPK_ONLY_PLUGINS="orbitr;ciphr" builds and tests only those plug-ins (pull request CI uses it for the
# plug-ins a change touches); empty (the default) builds all of them. The plug-ins a selected one links
# (smacheratr_core, smacheratr_ui, ...) are added too, but only their libraries are built and their
# tests are disabled. The shared tests always run; factory_presets checks only the selected plug-ins.
set(PK_ONLY_PLUGINS "" CACHE STRING "Build only these plug-ins (a list of folder names under plugins/); empty builds all")
if(PK_ONLY_PLUGINS)
    if(CMAKE_VERSION VERSION_LESS 3.28)
        message(FATAL_ERROR "PK_ONLY_PLUGINS needs CMake 3.28 or newer (set_property(TEST ... DIRECTORY))")
    endif()
    foreach(p IN LISTS PK_ONLY_PLUGINS)
        if(NOT p IN_LIST PK_ALL_PLUGINS)
            message(FATAL_ERROR "PK_ONLY_PLUGINS: no plug-in called '${p}' (known: ${PK_ALL_PLUGINS})")
        endif()
    endforeach()
    # the plug-ins whose <name>_core, <name>_ui or <name>_fxslot targets a needed plug-in's CMakeLists.txt names, until
    # nothing new turns up
    set(_pk_needed ${PK_ONLY_PLUGINS})
    set(_pk_todo ${PK_ONLY_PLUGINS})
    while(_pk_todo)
        list(POP_FRONT _pk_todo p)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${p}/CMakeLists.txt" _pk_text)
        foreach(q IN LISTS PK_ALL_PLUGINS)
            if(NOT q IN_LIST _pk_needed AND _pk_text MATCHES "(^|[^a-z_])${q}_(core|ui|fxslot)([^a-z_]|$)")
                list(APPEND _pk_needed ${q})
                list(APPEND _pk_todo ${q})
            endif()
        endforeach()
    endwhile()
    message(STATUS "pluginkit: building only ${PK_ONLY_PLUGINS} (libraries of: ${_pk_needed})")
endif()

foreach(p IN LISTS PK_ALL_PLUGINS)
    if(NOT PK_ONLY_PLUGINS OR p IN_LIST PK_ONLY_PLUGINS)
        add_subdirectory(plugins/${p})
    elseif(p IN_LIST _pk_needed)
        # only what a selected plug-in links gets built; its own tests are not built, so not run
        add_subdirectory(plugins/${p} EXCLUDE_FROM_ALL)
        get_property(_pk_tests DIRECTORY plugins/${p} PROPERTY TESTS)
        if(_pk_tests)
            set_property(TEST ${_pk_tests} DIRECTORY plugins/${p} PROPERTY DISABLED TRUE)
        endif()
    endif()
endforeach()

# Every plug-in's factory presets (plugins/<plugin>/presets) parse against its parameters.
add_executable(factory_presets_tests shared/tests/factory_presets_tests.cpp)
target_compile_definitions(factory_presets_tests PRIVATE PK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(factory_presets_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
if(PK_ONLY_PLUGINS)
    # only the selected plug-ins' presets (PK_WITH_<name> in the test)
    target_compile_definitions(factory_presets_tests PRIVATE PK_PRESET_SUBSET)
    foreach(p IN LISTS PK_ONLY_PLUGINS)
        target_compile_definitions(factory_presets_tests PRIVATE PK_WITH_${p})
        target_link_libraries(factory_presets_tests PRIVATE ${p}_core)
    endforeach()
    target_link_libraries(factory_presets_tests PRIVATE pluginkit_core)
else()
    target_link_libraries(factory_presets_tests PRIVATE pluginkit_core ciphr_core deepr_core dropr_core gentlr_core levlr_core locus_core multidyn_core
        moistr_core orbitr_core para_core smacheratr_core smemplr_core smoothr_core stretchr_core widr_core wubr_core
        smeezr_core probr_core)
endif()
add_test(NAME factory_presets COMMAND factory_presets_tests)
