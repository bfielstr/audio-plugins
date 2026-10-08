# Probr: an analysis probe. Passes the audio through untouched and, while armed, records what passes (a
# 32-bit float WAV per take), the host's transport (a time map in ppq) and the MIDI it gets, off the audio
# thread, into one session folder shared by every probe (scripts/probr_align.py lines them up).
project(Probr VERSION 0.28.0.0)

add_library(probr_core STATIC
    src/core/Params.cpp
    src/core/Probe.cpp
    src/core/Writer.cpp
    src/core/Session.cpp
    src/core/FileSystem.cpp
)
target_include_directories(probr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(probr_core PUBLIC pluginkit_core)
find_package(Threads REQUIRED)
target_link_libraries(probr_core PUBLIC Threads::Threads)
pk_dsp_settings(probr_core)
# (written into each take's JSON)
target_compile_definitions(probr_core PUBLIC PROBR_VERSION="${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

add_executable(probr_tests tests/core_tests.cpp)
target_link_libraries(probr_tests PRIVATE probr_core)
add_test(NAME probr_core COMMAND probr_tests)
# the CPU budget on its own, so it runs alone (the other core tests run in parallel)
add_test(NAME probr_cpu COMMAND probr_tests cpu_budget)
pk_cpu_test(probr_cpu)

# scripts/probr_align.py on generated sessions, and on one the writer made (needs Python 3 with numpy;
# skipped without numpy)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    set(_probr_session ${CMAKE_CURRENT_BINARY_DIR}/align-session)
    add_test(NAME probr_make_session COMMAND probr_tests --make-session ${_probr_session})
    set_tests_properties(probr_make_session PROPERTIES FIXTURES_SETUP probr_session)
    add_test(NAME probr_align
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/align_test.py ${CMAKE_SOURCE_DIR}/scripts/probr_align.py
                     ${CMAKE_CURRENT_BINARY_DIR}/align-test ${_probr_session})
    set_tests_properties(probr_align PROPERTIES FIXTURES_REQUIRED probr_session SKIP_RETURN_CODE 77)
endif()

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

pk_add_plugin(Probr
    BUNDLE_ID com.bfielstr.vst3.probr
    SOURCES
        src/plugin/Cids.h
        src/plugin/Version.h
        src/plugin/Shared.h
        src/plugin/State.h
        src/plugin/State.cpp
        src/plugin/Processor.h
        src/plugin/Processor.cpp
        src/plugin/Controller.h
        src/plugin/Controller.cpp
        src/plugin/Entry.cpp
        src/ui/Help.h
        src/ui/ProbeViews.h
        src/ui/ProbeViews.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
)
target_include_directories(Probr PRIVATE src)
target_link_libraries(Probr PRIVATE probr_core)

# the saved state (plugin/State.cpp) and the processor's pass-through on their own: run everywhere, beside
# the core tests
add_executable(probr_state_tests tests/state_tests.cpp src/plugin/State.cpp src/plugin/Processor.cpp)
target_include_directories(probr_state_tests PRIVATE src)
target_link_libraries(probr_state_tests PRIVATE probr_core pluginkit_vst sdk_hosting sdk)
if(APPLE)
    target_link_libraries(probr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME probr_state COMMAND probr_state_tests)

pk_add_host_test(probr_hosttest
    PLUGIN Probr
    SOURCES tests/host_test.mm src/plugin/State.cpp
    LIBS probr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
