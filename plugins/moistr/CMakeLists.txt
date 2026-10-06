# Moistr: splits a sound into a low-pass, a band-pass and a high-pass band with a gap between the low
# mids and the high end, moves each band's frequency and level slowly (seeded, repeatable), then glues
# them back together with a compressor and soft clipping; optionally a second pass through the bands.
project(Moistr VERSION 0.18.0.0)

add_library(moistr_core STATIC
    src/core/Params.cpp
    src/core/Movement.cpp
    src/core/Engine.cpp
)
target_include_directories(moistr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(moistr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(moistr_core)

add_executable(moistr_tests tests/core_tests.cpp)
target_link_libraries(moistr_tests PRIVATE moistr_core)
add_test(NAME moistr_core COMMAND moistr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Moistr
    BUNDLE_ID com.bfielstr.vst3.moistr
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
        src/ui/BandView.h
        src/ui/BandView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Moistr PRIVATE src)
target_link_libraries(Moistr PRIVATE moistr_core smacheratr_ui)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(moistr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(moistr_state_tests PRIVATE src)
target_link_libraries(moistr_state_tests PRIVATE moistr_core sdk)
if(APPLE)
    target_link_libraries(moistr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME moistr_state COMMAND moistr_state_tests)

pk_add_host_test(moistr_hosttest
    PLUGIN Moistr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS moistr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
