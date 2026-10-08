# Moistr: a SWEEP stage (eight sweeping bell EQs, a High Shelf on a slow orbit, a level-matched saturator), then
# a split into a low band held steady and upper bands that rise and fall slowly (seeded, repeatable), glued back
# together with a compressor and soft clipping; optionally a second pass through the bands.
project(Moistr VERSION 0.27.1.0)

add_library(moistr_core STATIC
    src/core/Params.cpp
    src/core/Movement.cpp
    src/core/Engine.cpp
    src/core/Sweep.cpp
    src/core/Gesture.cpp
    src/core/GestureFile.cpp
)
target_include_directories(moistr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(moistr_core PUBLIC pluginkit_core smacheratr_core smemplr_fxslot) # (the LAB's slots: smemplr's)
pk_dsp_settings(moistr_core)

add_executable(moistr_tests tests/core_tests.cpp)
target_link_libraries(moistr_tests PRIVATE moistr_core)
# (the Liquid preset's statistics are checked on the preset file itself)
target_compile_definitions(moistr_tests PRIVATE MOISTR_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")
add_test(NAME moistr_core COMMAND moistr_tests)
pk_cpu_test(moistr_core) # times the DSP against a CPU budget

# scripts/als_extract.py --moistr on a synthetic Live set (made by the test): Ableton lanes to one gesture of lanes
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME moistr_als_extract
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/als_extract_test.py ${CMAKE_SOURCE_DIR}/scripts/als_extract.py)
endif()

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
        src/ui/SweepView.h
        src/ui/SweepView.cpp
        src/ui/GestureView.h
        src/ui/GestureView.cpp
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
