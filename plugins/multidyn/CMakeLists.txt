# Multidyn: three-band upward/downward compressor-expander modelled on Live's Multiband Dynamics.
project(Multidyn VERSION 0.6.0.0)

add_library(multidyn_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(multidyn_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(multidyn_core PUBLIC pluginkit_core smacheratr_core) # the built-in saturator
pk_dsp_settings(multidyn_core)

add_executable(multidyn_tests tests/core_tests.cpp)
target_link_libraries(multidyn_tests PRIVATE multidyn_core)
add_test(NAME multidyn_core COMMAND multidyn_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Multidyn
    BUNDLE_ID com.bfielstr.vst3.multidyn
    SOURCES
        src/plugin/Cids.h
        src/plugin/Version.h
        src/plugin/Meters.h
        src/plugin/State.h
        src/plugin/State.cpp
        src/plugin/Processor.h
        src/plugin/Processor.cpp
        src/plugin/Controller.h
        src/plugin/Controller.cpp
        src/plugin/Entry.cpp
        src/ui/Help.h
        src/ui/DynDisplay.h
        src/ui/DynDisplay.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Multidyn PRIVATE src)
target_link_libraries(Multidyn PRIVATE multidyn_core smacheratr_ui)

pk_add_host_test(multidyn_hosttest
    PLUGIN Multidyn
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS multidyn_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
