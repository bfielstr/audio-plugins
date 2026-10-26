# Sunder: a germanium compressor (a FET compressor with a choice of knees, into a germanium output stage whose
# Feedback sets its gain and its low resonance), measured from the Softube model setting by setting, with a bass
# enhancer ahead of it (bubr's, measured from a commercial bass enhancer).
project(Sunder VERSION 0.32.0.0)

add_library(sunder_core STATIC
    src/core/Params.cpp
    src/core/BassEngine.cpp
    src/core/Engine.cpp
)
target_include_directories(sunder_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
# (sweepr's Dsp.h and pluginkit's dsp/Oversampler4x.h are header-only)
target_link_libraries(sunder_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(sunder_core)

add_executable(sunder_tests tests/core_tests.cpp)
target_link_libraries(sunder_tests PRIVATE sunder_core)
add_test(NAME sunder_core COMMAND sunder_tests)
pk_cpu_test(sunder_core) # times the DSP against a CPU budget

# the bass enhancer on its own, against bubr's measurements of the reference
add_executable(sunder_bass_tests tests/bass_tests.cpp)
target_link_libraries(sunder_bass_tests PRIVATE sunder_core)
add_test(NAME sunder_bass COMMAND sunder_bass_tests)
pk_cpu_test(sunder_bass) # times the DSP against a CPU budget

# sunder_render and sunder_bass_render: a WAV through the engine (or the enhancer alone) at given settings
# (calibrating them against the reference renders)
add_executable(sunder_render tools/render.cpp)
target_link_libraries(sunder_render PRIVATE sunder_core)
add_executable(sunder_bass_render tools/bass_render.cpp)
target_link_libraries(sunder_bass_render PRIVATE sunder_core)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Sunder
    BUNDLE_ID com.bfielstr.vst3.sunder
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
        src/ui/SunderView.h
        src/ui/SunderView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Sunder PRIVATE src)
target_link_libraries(Sunder PRIVATE sunder_core)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(sunder_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(sunder_state_tests PRIVATE src)
target_link_libraries(sunder_state_tests PRIVATE sunder_core sdk)
if(APPLE)
    target_link_libraries(sunder_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME sunder_state COMMAND sunder_state_tests)

pk_add_host_test(sunder_hosttest
    PLUGIN Sunder
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS sunder_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
