# Simplr: a sampler modelled on Ableton Live's Simpler.
project(Simplr VERSION 0.4.0.0)

add_library(simplr_core STATIC
    src/core/Params.cpp
    src/core/SampleData.cpp
    src/core/Slices.cpp
    src/core/Warp.cpp
    src/core/Engine.cpp
)
# Other plug-ins reuse the warp engines as "simplr/src/core/Warp.h"; Simplr's own code includes src/core directly.
target_include_directories(simplr_core PUBLIC ${CMAKE_SOURCE_DIR}/plugins ${CMAKE_SOURCE_DIR}/third_party/dr_libs PRIVATE src/core)
target_link_libraries(simplr_core PUBLIC pluginkit_core)
pk_dsp_settings(simplr_core)

add_executable(simplr_tests tests/core_tests.cpp)
target_include_directories(simplr_tests PRIVATE src/core)
target_link_libraries(simplr_tests PRIVATE simplr_core)
add_test(NAME simplr_core COMMAND simplr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Simplr
    BUNDLE_ID com.bfielstr.vst3.simplr
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
)
target_include_directories(Simplr PRIVATE src src/core)
target_link_libraries(Simplr PRIVATE simplr_core)

pk_add_host_test(simplr_hosttest
    PLUGIN Simplr
    SOURCES tests/host_test.mm src/plugin/StateIO.cpp
    LIBS simplr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
