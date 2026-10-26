# Bubr: a bass enhancer (a band at Freq into a clipper, a gate, Dry/Wet with Low Cut, an output limiter),
# BOOM Interactive's Uberbass measured setting by setting, as a 1:1 model to start from.
project(Bubr VERSION 0.32.0.0)

add_library(bubr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(bubr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
# (sweepr's Dsp.h and para's Oversampler.h are header-only)
target_link_libraries(bubr_core PUBLIC pluginkit_core)
pk_dsp_settings(bubr_core)

add_executable(bubr_tests tests/core_tests.cpp)
target_link_libraries(bubr_tests PRIVATE bubr_core)
add_test(NAME bubr_core COMMAND bubr_tests)
pk_cpu_test(bubr_core) # times the DSP against a CPU budget

# bubr_render: a WAV through the engine at given settings (calibrating it against the reference renders)
add_executable(bubr_render tools/render.cpp)
target_link_libraries(bubr_render PRIVATE bubr_core)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Bubr
    BUNDLE_ID com.bfielstr.vst3.bubr
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
        src/ui/BubrView.h
        src/ui/BubrView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Bubr PRIVATE src)
target_link_libraries(Bubr PRIVATE bubr_core)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(bubr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(bubr_state_tests PRIVATE src)
target_link_libraries(bubr_state_tests PRIVATE bubr_core sdk)
if(APPLE)
    target_link_libraries(bubr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME bubr_state COMMAND bubr_state_tests)

pk_add_host_test(bubr_hosttest
    PLUGIN Bubr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS bubr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
