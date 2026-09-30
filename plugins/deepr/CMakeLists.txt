# Deepr: makes a bass sound deeper by contrast (the low mids dipped while the sub plays, the sub in mono).
project(Deepr VERSION 0.5.0.0)

add_library(deepr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(deepr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(deepr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(deepr_core)

add_executable(deepr_tests tests/core_tests.cpp)
target_link_libraries(deepr_tests PRIVATE deepr_core)
add_test(NAME deepr_core COMMAND deepr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()
