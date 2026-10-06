# Smemplr: a sampler modelled on Ableton Live's Simpler.
project(Smemplr VERSION 0.18.0.0)

add_library(smemplr_core STATIC
    src/core/Params.cpp
    src/core/SampleData.cpp
    src/core/Slices.cpp
    src/core/Warp.cpp
    src/core/Engine.cpp
    src/core/Rack.cpp
    src/core/Modulation.cpp
)
# Other plug-ins reuse the warp engines as "smemplr/src/core/Warp.h"; Smemplr's own code includes src/core directly.
target_include_directories(smemplr_core PUBLIC ${CMAKE_SOURCE_DIR}/plugins ${CMAKE_SOURCE_DIR}/third_party/dr_libs PRIVATE src/core)
target_link_libraries(smemplr_core PUBLIC pluginkit_core para_core multidyn_core smacheratr_core widr_core wubr_core levlr_core smoothr_core gentlr_core) # the rack
pk_dsp_settings(smemplr_core)

add_executable(smemplr_tests tests/core_tests.cpp)
target_include_directories(smemplr_tests PRIVATE src/core)
target_link_libraries(smemplr_tests PRIVATE smemplr_core)
add_test(NAME smemplr_core COMMAND smemplr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

# the saved state (plugin/StateIO.cpp) on its own: runs everywhere, beside the core tests
add_executable(smemplr_state_tests tests/state_tests.cpp src/plugin/StateIO.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(smemplr_state_tests PRIVATE src src/core)
target_link_libraries(smemplr_state_tests PRIVATE smemplr_core sdk_common)
if(APPLE)
    target_link_libraries(smemplr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME smemplr_state COMMAND smemplr_state_tests)

pk_add_plugin(Smemplr
    BUNDLE_ID com.bfielstr.vst3.smemplr
    SOURCES
        src/plugin/Bridge.h
        src/plugin/Bridge.cpp
        src/plugin/Cids.h
        src/plugin/Processor.h
        src/plugin/Processor.cpp
        src/plugin/Controller.h
        src/plugin/Controller.cpp
        src/plugin/Entry.cpp
        src/plugin/StateIO.h
        src/plugin/StateIO.cpp
        src/plugin/Version.h
        src/ui/UiKit.h
        src/ui/Help.h
        src/ui/WaveformView.h
        src/ui/WaveformView.cpp
        src/ui/Displays.h
        src/ui/Displays.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
        src/ui/MsView.h
        src/ui/MsView.cpp
        src/ui/ModView.h
        src/ui/ModView.cpp
        # the other plug-ins' displays, shown in the effects strip
        ${CMAKE_SOURCE_DIR}/plugins/para/src/ui/FilterView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/multidyn/src/ui/DynDisplay.cpp
        ${CMAKE_SOURCE_DIR}/plugins/widr/src/ui/GonioView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/wubr/src/ui/BandView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/wubr/src/ui/ShapeView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/levlr/src/ui/LevelView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/smoothr/src/ui/HistoryView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/gentlr/src/ui/GentlrView.cpp
)
target_include_directories(Smemplr PRIVATE src src/core)
target_link_libraries(Smemplr PRIVATE smemplr_core smacheratr_ui)

pk_add_host_test(smemplr_hosttest
    PLUGIN Smemplr
    SOURCES tests/host_test.mm src/plugin/StateIO.cpp
    LIBS smemplr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
