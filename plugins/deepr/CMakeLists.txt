# Deepr: makes a bass sound deeper by contrast (the low mids dipped while the sub plays, the sub in mono).
project(Deepr VERSION 0.8.0.0)

add_library(deepr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(deepr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(deepr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(deepr_core)

add_executable(deepr_tests tests/core_tests.cpp)
target_link_libraries(deepr_tests PRIVATE deepr_core)
add_test(NAME deepr_core COMMAND deepr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Deepr
    BUNDLE_ID com.bfielstr.vst3.deepr
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
        src/ui/DeeprView.h
        src/ui/DeeprView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Deepr PRIVATE src)
target_link_libraries(Deepr PRIVATE deepr_core smacheratr_ui)

pk_add_host_test(deepr_hosttest
    PLUGIN Deepr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS deepr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
