# Smatcheratr: waveshaping saturator in the spirit of Live's Saturator.
project(Smatcheratr VERSION 0.5.0.0)

add_library(smatcheratr_core STATIC
    src/core/Params.cpp
    src/core/Oversampler.cpp
    src/core/Engine.cpp
)
target_include_directories(smatcheratr_core PUBLIC src/core)
target_link_libraries(smatcheratr_core PUBLIC pluginkit_core)
pk_dsp_settings(smatcheratr_core)

add_executable(smatcheratr_tests tests/core_tests.cpp)
target_link_libraries(smatcheratr_tests PRIVATE smatcheratr_core)
add_test(NAME smatcheratr_core COMMAND smatcheratr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Smatcheratr
    BUNDLE_ID com.bfielstr.vst3.smatcheratr
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
target_include_directories(Smatcheratr PRIVATE src)
target_link_libraries(Smatcheratr PRIVATE smatcheratr_core)

pk_add_host_test(smatcheratr_hosttest
    PLUGIN Smatcheratr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS smatcheratr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
