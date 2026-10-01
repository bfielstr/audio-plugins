# Detonatr: an explosion designer mirroring the user's explosion chain: Vocoder, Spike, Motion,
# Transient, Limiter, Transient, Comp, Comp, Tape and Limiter stages, in any order.
project(Detonatr VERSION 0.7.0.0)

# each stage's own test program builds the stage directly (so the stages can be worked on apart)
foreach(unit Vocoder Spike Motion Transient Limiter Ttm Tape)
    string(TOLOWER ${unit} lower)
    add_executable(detonatr_${lower}_tests tests/${lower}_tests.cpp src/core/${unit}.cpp)
    target_include_directories(detonatr_${lower}_tests PRIVATE src/core tests ${CMAKE_SOURCE_DIR}/plugins)
    target_link_libraries(detonatr_${lower}_tests PRIVATE pluginkit_core smacheratr_core)
    pk_dsp_settings(detonatr_${lower}_tests)
    add_test(NAME detonatr_${lower} COMMAND detonatr_${lower}_tests)
endforeach()

add_library(detonatr_core STATIC
    src/core/Dsp.h
    src/core/Params.h
    src/core/Params.cpp
    src/core/Vocoder.h
    src/core/Vocoder.cpp
    src/core/Spike.h
    src/core/Spike.cpp
    src/core/Motion.h
    src/core/Motion.cpp
    src/core/Transient.h
    src/core/Transient.cpp
    src/core/Limiter.h
    src/core/Limiter.cpp
    src/core/Ttm.h
    src/core/Ttm.cpp
    src/core/Tape.h
    src/core/Tape.cpp
    src/core/Engine.h
    src/core/Engine.cpp
)
target_include_directories(detonatr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(detonatr_core PUBLIC pluginkit_core smacheratr_core)
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
)
target_include_directories(Detonatr PRIVATE src src/core src/plugin)
target_link_libraries(Detonatr PRIVATE detonatr_core smacheratr_ui)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(detonatr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(detonatr_state_tests PRIVATE src tests)
target_link_libraries(detonatr_state_tests PRIVATE detonatr_core sdk)
if(APPLE)
    target_link_libraries(detonatr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME detonatr_state COMMAND detonatr_state_tests)

pk_add_host_test(detonatr_hosttest
    PLUGIN Detonatr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS detonatr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
