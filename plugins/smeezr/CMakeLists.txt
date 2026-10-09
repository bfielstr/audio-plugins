# Smeezr: a one-knob compressor. Squeeze first moves the spectrum towards the balance of pink noise (ten
# one-octave bands, each pulled towards an equal share of the power), then past the middle adds an
# OTT-style boost on top (upward and downward compression in OTT's three bands, Multidyn's OTT model).
project(Smeezr VERSION 0.29.0.0)

add_library(smeezr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(smeezr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(smeezr_core PUBLIC pluginkit_core smacheratr_core) # (multidyn's Ott.h is header-only)
pk_dsp_settings(smeezr_core)

add_executable(smeezr_tests tests/core_tests.cpp)
target_link_libraries(smeezr_tests PRIVATE smeezr_core)
add_test(NAME smeezr_core COMMAND smeezr_tests)
pk_cpu_test(smeezr_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Smeezr
    BUNDLE_ID com.bfielstr.vst3.smeezr
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
        src/ui/PinkView.h
        src/ui/PinkView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Smeezr PRIVATE src)
target_link_libraries(Smeezr PRIVATE smeezr_core smacheratr_ui)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(smeezr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(smeezr_state_tests PRIVATE src)
target_link_libraries(smeezr_state_tests PRIVATE smeezr_core sdk)
if(APPLE)
    target_link_libraries(smeezr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME smeezr_state COMMAND smeezr_state_tests)

pk_add_host_test(smeezr_hosttest
    PLUGIN Smeezr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS smeezr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
