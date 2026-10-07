# Wubr: two bands whose gain and centre drawn LFOs (or envelopes) move, into Smacheratr.
project(Wubr VERSION 0.21.0.0)

add_library(wubr_core STATIC
    src/core/Params.h
    src/core/Params.cpp
    src/core/Shape.h
    src/core/Engine.h
    src/core/Engine.cpp
)
target_include_directories(wubr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(wubr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(wubr_core)

add_executable(wubr_tests tests/core_tests.cpp)
target_link_libraries(wubr_tests PRIVATE wubr_core)
add_test(NAME wubr_core COMMAND wubr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Wubr
    BUNDLE_ID com.bfielstr.vst3.wubr
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
        src/ui/BandView.h
        src/ui/BandView.cpp
        src/ui/ShapeView.h
        src/ui/ShapeView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Wubr PRIVATE src)
target_link_libraries(Wubr PRIVATE wubr_core smacheratr_ui)

pk_add_host_test(wubr_hosttest
    PLUGIN Wubr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS wubr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
