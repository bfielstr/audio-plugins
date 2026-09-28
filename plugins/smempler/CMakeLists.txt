# Smempler: a sampler modelled on Ableton Live's Simpler.
project(Smempler VERSION 0.5.0.0)

add_library(smempler_core STATIC
    src/core/Params.cpp
    src/core/SampleData.cpp
    src/core/Slices.cpp
    src/core/Warp.cpp
    src/core/Engine.cpp
)
# Other plug-ins reuse the warp engines as "smempler/src/core/Warp.h"; Smempler's own code includes src/core directly.
target_include_directories(smempler_core PUBLIC ${CMAKE_SOURCE_DIR}/plugins ${CMAKE_SOURCE_DIR}/third_party/dr_libs PRIVATE src/core)
target_link_libraries(smempler_core PUBLIC pluginkit_core perrera_core multidyn_core) # the built-in effects
pk_dsp_settings(smempler_core)

add_executable(smempler_tests tests/core_tests.cpp)
target_include_directories(smempler_tests PRIVATE src/core)
target_link_libraries(smempler_tests PRIVATE smempler_core)
add_test(NAME smempler_core COMMAND smempler_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Smempler
    BUNDLE_ID com.bfielstr.vst3.smempler
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
target_include_directories(Smempler PRIVATE src src/core)
target_link_libraries(Smempler PRIVATE smempler_core)

pk_add_host_test(smempler_hosttest
    PLUGIN Smempler
    SOURCES tests/host_test.mm src/plugin/StateIO.cpp
    LIBS smempler_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
