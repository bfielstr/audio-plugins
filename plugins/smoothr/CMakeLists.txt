# Smoothr: a limiter that keeps the low end smooth (slow lows, fast highs), with Smacheratr before it
# and Character, a dip in the low mids.
project(Smoothr VERSION 0.10.0.0)

add_library(smoothr_core STATIC
    src/core/Params.h
    src/core/Params.cpp
    src/core/Limiter.h
    src/core/Limiter.cpp
    src/core/Character.h
    src/core/Engine.h
    src/core/Engine.cpp
)
target_include_directories(smoothr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(smoothr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(smoothr_core)

add_executable(smoothr_tests tests/core_tests.cpp)
target_link_libraries(smoothr_tests PRIVATE smoothr_core)
add_test(NAME smoothr_core COMMAND smoothr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Smoothr
    BUNDLE_ID com.bfielstr.vst3.smoothr
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
        src/ui/HistoryView.h
        src/ui/HistoryView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Smoothr PRIVATE src)
target_link_libraries(Smoothr PRIVATE smoothr_core smacheratr_ui)

pk_add_host_test(smoothr_hosttest
    PLUGIN Smoothr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS smoothr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
