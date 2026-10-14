# Sweepr: two bells whose frequencies an LFO each sweeps (synced to the song or free), into a tape stage
# (drive into a fast leveller and a sine clipper, then a high shelf that follows Speed).
project(Sweepr VERSION 0.30.1.0)

add_library(sweepr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(sweepr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(sweepr_core PUBLIC pluginkit_core smacheratr_core) # (para's Oversampler.h is header-only)
pk_dsp_settings(sweepr_core)

add_executable(sweepr_tests tests/core_tests.cpp)
target_include_directories(sweepr_tests PRIVATE tools)
target_link_libraries(sweepr_tests PRIVATE sweepr_core)
add_test(NAME sweepr_core COMMAND sweepr_tests)
pk_cpu_test(sweepr_core) # times the DSP against a CPU budget

# sweepr_analyze: a recording's response against another per 1/12 octave and their null depth, or a
# reference EQ's render against sweepr's at given settings (calibrating the bells: src/core/BellShape.h)
add_executable(sweepr_analyze tools/analyze.cpp)
target_link_libraries(sweepr_analyze PRIVATE sweepr_core)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Sweepr
    BUNDLE_ID com.bfielstr.vst3.sweepr
    SOURCES
        src/plugin/Cids.h
        src/plugin/Version.h
        src/plugin/SharedMeters.h
        src/plugin/State.h
        src/plugin/State.cpp
        src/plugin/Processor.h
        src/plugin/Processor.cpp
        src/plugin/Controller.h
        src/plugin/Controller.cpp
        src/plugin/Entry.cpp
        src/ui/Help.h
        src/ui/CurveView.h
        src/ui/CurveView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Sweepr PRIVATE src)
target_link_libraries(Sweepr PRIVATE sweepr_core smacheratr_ui)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(sweepr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(sweepr_state_tests PRIVATE src)
target_link_libraries(sweepr_state_tests PRIVATE sweepr_core sdk)
if(APPLE)
    target_link_libraries(sweepr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME sweepr_state COMMAND sweepr_state_tests)

pk_add_host_test(sweepr_hosttest
    PLUGIN Sweepr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS sweepr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
