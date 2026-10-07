# Smacheratr: waveshaping saturator in the spirit of Live's Saturator.
project(Smacheratr VERSION 0.21.0.0)

add_library(smacheratr_core STATIC
    src/core/Params.cpp
    src/core/Oversampler.cpp
    src/core/Engine.cpp
)
target_include_directories(smacheratr_core PUBLIC src/core)
target_link_libraries(smacheratr_core PUBLIC pluginkit_core)
pk_dsp_settings(smacheratr_core)

add_executable(smacheratr_tests tests/core_tests.cpp)
target_link_libraries(smacheratr_tests PRIVATE smacheratr_core)
add_test(NAME smacheratr_core COMMAND smacheratr_tests)
pk_cpu_test(smacheratr_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

# Smacheratr's displays, and the saturator section at the end of the other plug-ins' chains (TailPanel)
add_library(smacheratr_ui STATIC
    src/ui/ShaperView.h
    src/ui/ShaperView.cpp
    src/ui/ColorView.h
    src/ui/ColorView.cpp
    src/ui/ThresholdSlider.h
    src/ui/ThresholdSlider.cpp
    src/ui/TailPanel.h
    src/ui/TailPanel.cpp
    src/ui/BandPush.h
    src/ui/BandPush.cpp
)
target_include_directories(smacheratr_ui PUBLIC ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(smacheratr_ui PUBLIC smacheratr_core pluginkit_vst)
pk_dsp_settings(smacheratr_ui)

pk_add_plugin(Smacheratr
    BUNDLE_ID com.bfielstr.vst3.smacheratr
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
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Smacheratr PRIVATE src)
target_link_libraries(Smacheratr PRIVATE smacheratr_core smacheratr_ui)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(smacheratr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(smacheratr_state_tests PRIVATE src)
target_link_libraries(smacheratr_state_tests PRIVATE smacheratr_core sdk)
if(APPLE)
    target_link_libraries(smacheratr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME smacheratr_state COMMAND smacheratr_state_tests)

# the editors' glue and No Overlap edits (ui/BandPush.cpp) on a plain parameter host: runs everywhere
add_executable(smacheratr_bandpush_tests tests/bandpush_tests.cpp)
target_include_directories(smacheratr_bandpush_tests PRIVATE src)
target_link_libraries(smacheratr_bandpush_tests PRIVATE smacheratr_ui)
add_test(NAME smacheratr_bandpush COMMAND smacheratr_bandpush_tests)

pk_add_host_test(smacheratr_hosttest
    PLUGIN Smacheratr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS smacheratr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
