# Widr: trailer-width stereo widener that keeps the mono fold, aware of the other Widrs in the session.
project(Widr VERSION 0.5.0.0)

add_library(widr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
    src/core/Mix.cpp
)
target_include_directories(widr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(widr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(widr_core)

add_executable(widr_tests tests/core_tests.cpp)
target_link_libraries(widr_tests PRIVATE widr_core)
add_test(NAME widr_core COMMAND widr_tests)
