# Perrera: a high-pass and a low-pass in parallel that track MIDI, with a split envelope.
project(Perrera VERSION 0.5.0.0)

add_library(perrera_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(perrera_core PUBLIC src/core)
target_link_libraries(perrera_core PUBLIC pluginkit_core)
pk_dsp_settings(perrera_core)

add_executable(perrera_tests tests/core_tests.cpp)
target_link_libraries(perrera_tests PRIVATE perrera_core)
add_test(NAME perrera_core COMMAND perrera_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Perrera
    BUNDLE_ID com.bfielstr.vst3.perrera
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
        src/ui/FilterView.h
        src/ui/FilterView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Perrera PRIVATE src)
target_link_libraries(Perrera PRIVATE perrera_core)

pk_add_host_test(perrera_hosttest
    PLUGIN Perrera
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS perrera_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
