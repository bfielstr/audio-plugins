# Widr: trailer-width stereo widener that keeps the mono fold, aware of the other Widrs in the session.
project(Widr VERSION 0.5.0.0)

add_library(widr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
    src/core/Mix.cpp
)
target_include_directories(widr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(widr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(widr_core)

add_executable(widr_tests tests/core_tests.cpp)
target_link_libraries(widr_tests PRIVATE widr_core)
add_test(NAME widr_core COMMAND widr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Widr
    BUNDLE_ID com.bfielstr.vst3.widr
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
        src/ui/StageView.h
        src/ui/StageView.cpp
        src/ui/GonioView.h
        src/ui/GonioView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Widr PRIVATE src)
target_link_libraries(Widr PRIVATE widr_core smacheratr_ui)

pk_add_host_test(widr_hosttest
    PLUGIN Widr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS widr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
