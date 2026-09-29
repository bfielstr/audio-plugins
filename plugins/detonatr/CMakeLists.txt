# Detonatr: an explosion / impact designer: Clean, Tone, Multiband, Transient and Saturator stages in any order.
project(Detonatr VERSION 0.1.0.0)

# each stage's own test program builds the stage directly (so the stages can be worked on apart)
foreach(unit Clean Tone Transient)
    string(TOLOWER ${unit} lower)
    add_executable(detonatr_${lower}_tests tests/${lower}_tests.cpp src/core/${unit}.cpp)
    target_include_directories(detonatr_${lower}_tests PRIVATE src/core tests ${CMAKE_SOURCE_DIR}/plugins)
    target_link_libraries(detonatr_${lower}_tests PRIVATE pluginkit_core smacheratr_core)
    pk_dsp_settings(detonatr_${lower}_tests)
    add_test(NAME detonatr_${lower} COMMAND detonatr_${lower}_tests)
endforeach()

add_library(detonatr_core STATIC
    src/core/Params.h
    src/core/Params.cpp
    src/core/Clean.h
    src/core/Clean.cpp
    src/core/Tone.h
    src/core/Tone.cpp
    src/core/Transient.h
    src/core/Transient.cpp
    src/core/Engine.h
    src/core/Engine.cpp
)
target_include_directories(detonatr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(detonatr_core PUBLIC pluginkit_core multidyn_core smacheratr_core)
pk_dsp_settings(detonatr_core)

add_executable(detonatr_tests tests/core_tests.cpp)
target_include_directories(detonatr_tests PRIVATE tests)
target_link_libraries(detonatr_tests PRIVATE detonatr_core)
add_test(NAME detonatr_core COMMAND detonatr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Detonatr
    BUNDLE_ID com.bfielstr.vst3.detonatr
    SOURCES
        src/plugin/Cids.h
        src/plugin/Version.h
        src/plugin/Bridge.h
        src/plugin/Bridge.cpp
        src/plugin/State.h
        src/plugin/State.cpp
        src/plugin/Processor.h
        src/plugin/Processor.cpp
        src/plugin/Controller.h
        src/plugin/Controller.cpp
        src/plugin/Entry.cpp
        src/ui/Help.h
        src/ui/Views.h
        src/ui/Views.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
        # Multidyn's display, on the Multiband page
        ${CMAKE_SOURCE_DIR}/plugins/multidyn/src/ui/DynDisplay.cpp
)
target_include_directories(Detonatr PRIVATE src src/core src/plugin)
# smempler_core: the suite's audio file decoder (the recordings)
target_link_libraries(Detonatr PRIVATE detonatr_core smempler_core smacheratr_ui)

pk_add_host_test(detonatr_hosttest
    PLUGIN Detonatr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS detonatr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
