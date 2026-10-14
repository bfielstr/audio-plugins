# Tapr: a tape saturator (Drive into a fast leveller and a sine clipper, then a high shelf that follows Speed)
# with a three-band Cut EQ, each of the leveller and the EQ before or after the curve.
project(Tapr VERSION 0.31.0.0)

add_library(tapr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(tapr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
# (sweepr's TapeShape.h, BellShape.h and Dsp.h and para's Oversampler.h are header-only)
target_link_libraries(tapr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(tapr_core)

add_executable(tapr_tests tests/core_tests.cpp)
target_link_libraries(tapr_tests PRIVATE tapr_core)
add_test(NAME tapr_core COMMAND tapr_tests)
pk_cpu_test(tapr_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Tapr
    BUNDLE_ID com.bfielstr.vst3.tapr
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
        src/ui/TapeView.h
        src/ui/TapeView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Tapr PRIVATE src)
target_link_libraries(Tapr PRIVATE tapr_core smacheratr_ui)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(tapr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(tapr_state_tests PRIVATE src)
target_link_libraries(tapr_state_tests PRIVATE tapr_core sdk)
if(APPLE)
    target_link_libraries(tapr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME tapr_state COMMAND tapr_state_tests)

pk_add_host_test(tapr_hosttest
    PLUGIN Tapr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS tapr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
