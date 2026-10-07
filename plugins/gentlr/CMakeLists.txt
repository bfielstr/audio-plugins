# Gentlr: Smacheratr's Gentlr on its own, a gentle dynamic de-muddier / de-harsher: two bands and a
# Sub band that turn their region down when it gets loud, into Smacheratr.
project(Gentlr VERSION 0.22.0.0)

add_library(gentlr_core STATIC
    src/core/Params.h
    src/core/Params.cpp
    src/core/Engine.h
    src/core/Engine.cpp
)
target_include_directories(gentlr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(gentlr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(gentlr_core)

add_executable(gentlr_tests tests/core_tests.cpp)
target_link_libraries(gentlr_tests PRIVATE gentlr_core)
add_test(NAME gentlr_core COMMAND gentlr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Gentlr
    BUNDLE_ID com.bfielstr.vst3.gentlr
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
        src/ui/GentlrView.h
        src/ui/GentlrView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Gentlr PRIVATE src)
target_link_libraries(Gentlr PRIVATE gentlr_core smacheratr_ui)

# the saved state (plugin/State.cpp) on its own: runs everywhere, beside the core tests
add_executable(gentlr_state_tests tests/state_tests.cpp src/plugin/State.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(gentlr_state_tests PRIVATE src)
target_link_libraries(gentlr_state_tests PRIVATE gentlr_core sdk)
if(APPLE)
    target_link_libraries(gentlr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME gentlr_state COMMAND gentlr_state_tests)

pk_add_host_test(gentlr_hosttest
    PLUGIN Gentlr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS gentlr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
