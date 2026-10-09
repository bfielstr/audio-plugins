# Locus: low-end contrast processor in the spirit of Ozone's Low End Focus.
project(Locus VERSION 0.30.0.0)

add_library(locus_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(locus_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(locus_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(locus_core)

add_executable(locus_tests tests/core_tests.cpp)
target_link_libraries(locus_tests PRIVATE locus_core)
add_test(NAME locus_core COMMAND locus_tests)
pk_cpu_test(locus_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Locus
    BUNDLE_ID com.bfielstr.vst3.locus
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
target_include_directories(Locus PRIVATE src)
target_link_libraries(Locus PRIVATE locus_core smacheratr_ui)

pk_add_host_test(locus_hosttest
    PLUGIN Locus
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS locus_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
