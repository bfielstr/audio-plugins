# Levlr: the spectrum in four touching bands (movable Linkwitz-Riley crossovers, their phase shift
# kept), a level for each, into Smacheratr.
project(Levlr VERSION 0.20.0.0)

add_library(levlr_core STATIC
    src/core/Params.h
    src/core/Params.cpp
    src/core/Crossover.h
    src/core/Drive.h
    src/core/Engine.h
    src/core/Engine.cpp
)
target_include_directories(levlr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(levlr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(levlr_core)

add_executable(levlr_tests tests/core_tests.cpp)
target_link_libraries(levlr_tests PRIVATE levlr_core)
add_test(NAME levlr_core COMMAND levlr_tests)
pk_cpu_test(levlr_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Levlr
    BUNDLE_ID com.bfielstr.vst3.levlr
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
        src/ui/LevelView.h
        src/ui/LevelView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Levlr PRIVATE src)
target_link_libraries(Levlr PRIVATE levlr_core smacheratr_ui)

pk_add_host_test(levlr_hosttest
    PLUGIN Levlr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS levlr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
