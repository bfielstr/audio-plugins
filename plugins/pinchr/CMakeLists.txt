# Pinchr: a germanium compressor (a FET compressor with a choice of knees, into a germanium output stage whose
# Feedback sets its gain and its low resonance), measured from the Softube model setting by setting.
project(Pinchr VERSION 0.31.0.0)

add_library(pinchr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(pinchr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
# (sweepr's Dsp.h and para's Oversampler.h are header-only)
target_link_libraries(pinchr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(pinchr_core)

add_executable(pinchr_tests tests/core_tests.cpp)
target_link_libraries(pinchr_tests PRIVATE pinchr_core)
add_test(NAME pinchr_core COMMAND pinchr_tests)
pk_cpu_test(pinchr_core) # times the DSP against a CPU budget

# pinchr_render: a WAV through the engine at given settings (calibrating it against the reference renders)
add_executable(pinchr_render tools/render.cpp)
target_link_libraries(pinchr_render PRIVATE pinchr_core)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Pinchr
    BUNDLE_ID com.bfielstr.vst3.pinchr
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
        src/ui/PinchView.h
        src/ui/PinchView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Pinchr PRIVATE src)
target_link_libraries(Pinchr PRIVATE pinchr_core)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(pinchr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(pinchr_state_tests PRIVATE src)
target_link_libraries(pinchr_state_tests PRIVATE pinchr_core sdk)
if(APPLE)
    target_link_libraries(pinchr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME pinchr_state COMMAND pinchr_state_tests)

pk_add_host_test(pinchr_hosttest
    PLUGIN Pinchr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS pinchr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
