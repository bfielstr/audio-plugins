# Helpers shared by every plug-in in this repository.

set(PK_VST3SDK_TAG "v3.8.1_build_84")

# Common compile settings for the framework-free DSP libraries.
function(pk_dsp_settings target)
    if(WIN32)
        target_compile_definitions(${target} PUBLIC _USE_MATH_DEFINES NOMINMAX)
    endif()
    if(MSVC)
        target_compile_options(${target} PUBLIC /utf-8 /bigobj)
        target_compile_options(${target} PRIVATE /O2 /fp:fast)
    else()
        target_compile_options(${target} PRIVATE -O3 -fcx-limited-range)
    endif()
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

# Fetches (first time only) and adds the Steinberg VST3 SDK.
macro(pk_setup_vst3_sdk)
    if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/cmake/modules/SMTG_VST3_SDK.cmake")
        find_package(Git REQUIRED)
        message(STATUS "pluginkit: fetching VST3 SDK ${PK_VST3SDK_TAG} into ${vst3sdk_SOURCE_DIR} (one-time)")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --depth 1 --branch ${PK_VST3SDK_TAG} --recurse-submodules
                    --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git "${vst3sdk_SOURCE_DIR}"
            RESULT_VARIABLE _pk_clone_result)
        if(NOT _pk_clone_result EQUAL 0 OR NOT EXISTS "${vst3sdk_SOURCE_DIR}/cmake/modules/SMTG_VST3_SDK.cmake")
            message(FATAL_ERROR "Could not fetch the VST3 SDK. Clone it manually:\n"
                "  git clone --depth 1 --branch ${PK_VST3SDK_TAG} --recurse-submodules --shallow-submodules "
                "https://github.com/steinbergmedia/vst3sdk.git external/vst3sdk")
        endif()
    endif()
    # a dropped file before its text, so clips dragged out of a DAW arrive (see PatchVstgui.cmake)
    include(PatchVstgui)
    pk_patch_vstgui("${vst3sdk_SOURCE_DIR}")
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
            message(STATUS "pluginkit: no full Xcode found, building with the Command Line Tools")
            set(ENV{XCODE_VERSION} "16.0")
            set(XCODE_VERSION "16.0")
        endif()
    endif()
    add_subdirectory(${vst3sdk_SOURCE_DIR} ${CMAKE_BINARY_DIR}/vst3sdk)
    smtg_enable_vst3_sdk()
endmacro()

# pk_add_plugin(<target> BUNDLE_ID <id> SOURCES <files...>)
# Creates the VST3 bundle, links pluginkit, bundles licence notices and ad-hoc signs on macOS.
function(pk_add_plugin target)
    cmake_parse_arguments(ARG "" "BUNDLE_ID" "SOURCES" ${ARGN})
    smtg_add_vst3plugin(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE pluginkit_vst sdk vstgui_support)
    smtg_target_configure_version_file(${target})

    set(lic "${CMAKE_BINARY_DIR}/licenses")
    configure_file(${vst3sdk_SOURCE_DIR}/LICENSE.txt ${lic}/VST3_SDK_LICENSE.txt COPYONLY)
    configure_file(${vst3sdk_SOURCE_DIR}/vstgui4/LICENSE ${lic}/VSTGUI_LICENSE.txt COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md ${lic}/THIRD_PARTY_NOTICES.md COPYONLY)
    configure_file(${CMAKE_SOURCE_DIR}/LICENSE ${lic}/LICENSE.txt COPYONLY)
    smtg_target_add_plugin_resources(${target}
        RESOURCES ${lic}/VST3_SDK_LICENSE.txt ${lic}/VSTGUI_LICENSE.txt ${lic}/THIRD_PARTY_NOTICES.md ${lic}/LICENSE.txt)

    if(SMTG_MAC)
        smtg_target_set_bundle(${target} BUNDLE_IDENTIFIER ${ARG_BUNDLE_ID} COMPANY_NAME "bfielstr")
        # The SDK only fills Info.plist for Xcode builds; provide it for Makefile/Ninja builds too
        # (identifier and version are how macOS and hosts tell plug-ins and versions apart).
        if(NOT XCODE)
            set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
                MACOSX_BUNDLE_GUI_IDENTIFIER ${ARG_BUNDLE_ID}
                MACOSX_BUNDLE_BUNDLE_NAME ${target}
                MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
                MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 bfielstr, MIT License")
        endif()
        # Always seal the bundle with an ad-hoc signature (hosts on Apple Silicon need a valid
        # signature, and the SDK only signs when a signing identity is configured).
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:${target}>"
            COMMENT "Ad-hoc signing ${target}.vst3")
    endif()
endfunction()

# pk_add_host_test(<name> PLUGIN <target> SOURCES <files...> LIBS <libs...> INCLUDES <dirs...>)
# macOS: loads the built bundle through the VST3 hosting API (see shared/pluginkit/testing).
function(pk_add_host_test name)
    cmake_parse_arguments(ARG "" "PLUGIN" "SOURCES;LIBS;INCLUDES" ${ARGN})
    if(NOT APPLE)
        return()
    endif()
    add_executable(${name}
        ${ARG_SOURCES}
        ${CMAKE_SOURCE_DIR}/shared/pluginkit/testing/HostRig.mm
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
    # the editors' headers (layout constants used by the tests) pull in VSTGUI headers
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/shared ${ARG_INCLUDES} ${vst3sdk_SOURCE_DIR}/vstgui4)
    target_link_libraries(${name} PRIVATE sdk_hosting ${ARG_LIBS} "-framework Cocoa")
    target_compile_options(${name} PRIVATE -fobjc-arc)
    add_dependencies(${name} ${ARG_PLUGIN})
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-output/${ARG_PLUGIN})
    add_test(NAME ${name}
             COMMAND ${name} ${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/${ARG_PLUGIN}.vst3 ${CMAKE_BINARY_DIR}/test-output/${ARG_PLUGIN})
endfunction()
