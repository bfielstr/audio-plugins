# Dropr: a transient designer you draw (each hit starts a drawn level shape).
project(Dropr VERSION 0.7.0.0)

add_library(dropr_core STATIC
    src/core/Params.cpp
    src/core/Engine.cpp
)
target_include_directories(dropr_core PUBLIC src/core ${CMAKE_SOURCE_DIR}/plugins)
target_link_libraries(dropr_core PUBLIC pluginkit_core smacheratr_core)
pk_dsp_settings(dropr_core)

add_executable(dropr_tests tests/core_tests.cpp)
target_link_libraries(dropr_tests PRIVATE dropr_core)
add_test(NAME dropr_core COMMAND dropr_tests)

if(NOT PK_BUILD_PLUGINS)
    return()
endif()
