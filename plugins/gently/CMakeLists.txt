# Gently: Smacheratr's Gently on its own, a gentle dynamic de-muddier / de-harsher: two bands that
# turn their region down when it gets loud, into Smacheratr.
project(Gently VERSION 0.1.0.0)

add_library(gently_core STATIC
    src/core/Params.h
    src/core/Params.cpp
    src/core/Engine.h
    src/core/Engine.cpp
)
target_include_directories(gently_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(gently_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(gently_core)

add_executable(gently_tests tests/core_tests.cpp)
target_link_libraries(gently_tests PRIVATE gently_core)
add_test(NAME gently_core COMMAND gently_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Gently
    BUNDLE_ID com.bfielstr.vst3.gently
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
        src/ui/GentlyView.h
        src/ui/GentlyView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Gently PRIVATE src)
target_link_libraries(Gently PRIVATE gently_core smacheratr_ui)

pk_add_host_test(gently_hosttest
    PLUGIN Gently
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS gently_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
