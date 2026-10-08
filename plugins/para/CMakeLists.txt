# Para: a high-pass and a low-pass in parallel that track MIDI, with a split envelope.
project(Para VERSION 0.27.0.0)

add_library(para_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(para_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(para_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(para_core)

add_executable(para_tests tests/core_tests.cpp)
target_link_libraries(para_tests PRIVATE para_core)
add_test(NAME para_core COMMAND para_tests)
pk_cpu_test(para_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Para
    BUNDLE_ID com.bfielstr.vst3.para
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
target_include_directories(Para PRIVATE src)
target_link_libraries(Para PRIVATE para_core smacheratr_ui)

pk_add_host_test(para_hosttest
    PLUGIN Para
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS para_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
