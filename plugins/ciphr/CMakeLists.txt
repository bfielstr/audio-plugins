# Ciphr: an 8-voice instrument. Clusters of wavetable oscillators (Timbre scans each one's list of
# waves and pitches, Cross modulates neighbours), a morphing filter and envelopes per voice, then a
# processor that goes from multi-tap delay to diffuse reverb with a frequency shifter in its feedback.
project(Ciphr VERSION 0.27.1.0)

add_library(ciphr_core STATIC
    src/core/Params.cpp
    src/core/Wavetable.cpp
    src/core/Variant.cpp
    src/core/Voice.cpp
    src/core/SpaceFx.cpp
    src/core/Disperse.cpp
    src/core/Engine.cpp
)
target_include_directories(ciphr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(ciphr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(ciphr_core)

add_executable(ciphr_tests tests/core_tests.cpp)
target_link_libraries(ciphr_tests PRIVATE ciphr_core)
target_compile_definitions(ciphr_tests PRIVATE CIPHR_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")
add_test(NAME ciphr_core COMMAND ciphr_tests)
pk_cpu_test(ciphr_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Ciphr
    BUNDLE_ID com.bfielstr.vst3.ciphr
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
        src/ui/CipherView.h
        src/ui/CipherView.cpp
        src/ui/DisperseView.h
        src/ui/DisperseView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Ciphr PRIVATE src)
target_link_libraries(Ciphr PRIVATE ciphr_core smacheratr_ui)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(ciphr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(ciphr_state_tests PRIVATE src)
target_link_libraries(ciphr_state_tests PRIVATE ciphr_core sdk)
if(APPLE)
    target_link_libraries(ciphr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME ciphr_state COMMAND ciphr_state_tests)

pk_add_host_test(ciphr_hosttest
    PLUGIN Ciphr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS ciphr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
