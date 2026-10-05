# Stretchr: clip pitch/time editor with REAPER-style stretch algorithms.
project(Stretchr VERSION 0.14.0.0)

add_library(stretchr_core STATIC
    src/core/Params.cpp
    src/core/Clip.cpp
    src/core/Render.cpp
    src/core/Wav.cpp
    src/core/Session.cpp
)
target_include_directories(stretchr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
# Reuses Smemplr's warp engines, sample loading and FFT (included as "smemplr/src/core/...").
find_package(Threads REQUIRED)
target_link_libraries(stretchr_core PUBLIC pluginkit_core smacheratr_core smemplr_core Threads::Threads)
pk_dsp_settings(stretchr_core)

add_executable(stretchr_tests tests/core_tests.cpp)
target_link_libraries(stretchr_tests PRIVATE stretchr_core)
add_test(NAME stretchr_core COMMAND stretchr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Stretchr
    BUNDLE_ID com.bfielstr.vst3.stretchr
    SOURCES
        src/plugin/Cids.h
        src/plugin/Version.h
        src/plugin/State.h
        src/plugin/State.cpp
        src/plugin/Processor.h
        src/plugin/Processor.cpp
        src/plugin/Controller.h
        src/plugin/Controller.cpp
        src/plugin/Entry.cpp
        src/ui/Help.h
        src/ui/ClipView.h
        src/ui/ClipView.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Stretchr PRIVATE src)
target_link_libraries(Stretchr PRIVATE stretchr_core smacheratr_ui)

pk_add_host_test(stretchr_hosttest
    PLUGIN Stretchr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS stretchr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
