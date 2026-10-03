# Dropr: a transient designer you draw (each hit starts a drawn level shape).
project(Dropr VERSION 0.8.0.0)

add_library(dropr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(dropr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(dropr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(dropr_core)

add_executable(dropr_tests tests/core_tests.cpp)
target_link_libraries(dropr_tests PRIVATE dropr_core)
add_test(NAME dropr_core COMMAND dropr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Dropr
    BUNDLE_ID com.bfielstr.vst3.dropr
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
        src/ui/ShapeView.h
        src/ui/ShapeView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Dropr PRIVATE src)
target_link_libraries(Dropr PRIVATE dropr_core smacheratr_ui)

pk_add_host_test(dropr_hosttest
    PLUGIN Dropr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS dropr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
