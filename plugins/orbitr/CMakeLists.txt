# Orbitr: Detonatr's Motion stage on its own, a Doppler swarm in the spirit of Tonsturm SpinTracer
# (not affiliated), "Liquid Debris"-like by default.
project(Orbitr VERSION 0.28.0.0)

add_library(orbitr_core STATIC
    src/core/Params.cpp
    src/core/Motion.cpp
    src/core/Engine.cpp
)
target_include_directories(orbitr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(orbitr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(orbitr_core)

add_executable(orbitr_tests tests/core_tests.cpp)
target_link_libraries(orbitr_tests PRIVATE orbitr_core)
add_test(NAME orbitr_core COMMAND orbitr_tests)
pk_cpu_test(orbitr_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Orbitr
    BUNDLE_ID com.bfielstr.vst3.orbitr
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
        src/ui/OrbView.h
        src/ui/OrbView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Orbitr PRIVATE src)
target_link_libraries(Orbitr PRIVATE orbitr_core smacheratr_ui)

pk_add_host_test(orbitr_hosttest
    PLUGIN Orbitr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS orbitr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
