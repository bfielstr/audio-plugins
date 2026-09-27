# Lowfocus: low-end contrast processor in the spirit of Ozone's Low End Focus.
project(Lowfocus VERSION 0.2.0.0)

add_library(lowfocus_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(lowfocus_core PUBLIC src/core)
target_link_libraries(lowfocus_core PUBLIC pluginkit_core)
pk_dsp_settings(lowfocus_core)

add_executable(lowfocus_tests tests/core_tests.cpp)
target_link_libraries(lowfocus_tests PRIVATE lowfocus_core)
add_test(NAME lowfocus_core COMMAND lowfocus_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Lowfocus
    BUNDLE_ID com.bfielstr.vst3.lowfocus
    SOURCES
        src/plugin/Cids.h
        src/plugin/Version.h
        src/plugin/SharedSpectrum.h
        src/plugin/State.h
        src/plugin/State.cpp
        src/plugin/Processor.h
        src/plugin/Processor.cpp
        src/plugin/Controller.h
        src/plugin/Controller.cpp
        src/plugin/Entry.cpp
        src/ui/Help.h
        src/ui/SpectrumView.h
        src/ui/SpectrumView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Lowfocus PRIVATE src)
target_link_libraries(Lowfocus PRIVATE lowfocus_core)

pk_add_host_test(lowfocus_hosttest
    PLUGIN Lowfocus
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS lowfocus_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
