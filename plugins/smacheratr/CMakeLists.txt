# Smacheratr: waveshaping saturator in the spirit of Live's Saturator.
project(Smacheratr VERSION 0.5.0.0)

add_library(smacheratr_core STATIC
    src/core/Params.cpp
    src/core/Oversampler.cpp
    src/core/Engine.cpp
)
target_include_directories(smacheratr_core PUBLIC src/core)
target_link_libraries(smacheratr_core PUBLIC pluginkit_core)
pk_dsp_settings(smacheratr_core)

add_executable(smacheratr_tests tests/core_tests.cpp)
target_link_libraries(smacheratr_tests PRIVATE smacheratr_core)
add_test(NAME smacheratr_core COMMAND smacheratr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Smacheratr
    BUNDLE_ID com.bfielstr.vst3.smacheratr
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
        src/ui/ShaperView.h
        src/ui/ShaperView.cpp
        src/ui/ColorView.h
        src/ui/ColorView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Smacheratr PRIVATE src)
target_link_libraries(Smacheratr PRIVATE smacheratr_core)

pk_add_host_test(smacheratr_hosttest
    PLUGIN Smacheratr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS smacheratr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
