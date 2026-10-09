# Smemplr: a sampler modelled on Ableton Live's Simpler.
project(Smemplr VERSION 0.29.0.0)

add_library(smemplr_core STATIC
    src/core/Params.cpp
    src/core/SampleData.cpp
    src/core/Slices.cpp
    src/core/Warp.cpp
    src/core/Engine.cpp
    src/core/Rack.cpp
    src/core/RackPresets.cpp
    src/core/Modulation.cpp
)
# Other plug-ins reuse the warp engines as "smemplr/src/core/Warp.h"; Smemplr's own code includes src/core directly.
target_include_directories(smemplr_core PUBLIC ${CMAKE_SOURCE_DIR}/plugins ${CMAKE_SOURCE_DIR}/third_party/dr_libs PRIVATE src/core)
target_link_libraries(smemplr_core PUBLIC pluginkit_core para_core multidyn_core smacheratr_core widr_core wubr_core levlr_core smoothr_core gentlr_core) # the rack
pk_dsp_settings(smemplr_core)

add_executable(smemplr_tests tests/core_tests.cpp)
target_include_directories(smemplr_tests PRIVATE src/core)
target_link_libraries(smemplr_tests PRIVATE smemplr_core)
add_test(NAME smemplr_core COMMAND smemplr_tests)
pk_cpu_test(smemplr_core) # times the DSP against a CPU budget

if(NOT PK_BUILD_PLUGINS)
    return()
endif()

# the saved state (plugin/StateIO.cpp) on its own: runs everywhere, beside the core tests
add_executable(smemplr_state_tests tests/state_tests.cpp src/plugin/StateIO.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp)
target_include_directories(smemplr_state_tests PRIVATE src src/core)
target_link_libraries(smemplr_state_tests PRIVATE smemplr_core sdk_common)
if(APPLE)
    target_link_libraries(smemplr_state_tests PRIVATE "-framework CoreFoundation") # the SDK's base library uses it
endif()
add_test(NAME smemplr_state COMMAND smemplr_state_tests)

# The rack slots' presets are the presets of the effects' own plug-ins (src/plugin/RackPresetIO.h): each
# effect's state format (its plug-in's State.cpp, with its migrations) and factory presets, compiled in
# once per effect beside src/plugin/HostedState.cpp (which sees only that effect's headers).
set(smemplr_hosted_libs)
foreach(p IN ITEMS para multidyn smacheratr widr wubr levlr gentlr smoothr)
    string(SUBSTRING ${p} 0 1 _first)
    string(SUBSTRING ${p} 1 -1 _rest)
    string(TOUPPER ${_first} _first)
    set(_name "${_first}${_rest}") # Para: the plug-in's name (its preset folder's)
    set(_lib smemplr_hosted_${p})
    add_library(${_lib} STATIC ${CMAKE_SOURCE_DIR}/plugins/${p}/src/plugin/State.cpp src/plugin/HostedState.cpp)
    target_compile_definitions(${_lib} PRIVATE
        PK_HOSTED_STATE_H="${CMAKE_SOURCE_DIR}/plugins/${p}/src/plugin/State.h"
        PK_HOSTED_CIDS_H="${CMAKE_SOURCE_DIR}/plugins/${p}/src/plugin/Cids.h"
        PK_HOSTED_NS=${p} PK_HOSTED_CODEC=codec${_name} PK_HOSTED_PRESETS=smemplr_hosted_presets_${p})
    target_link_libraries(${_lib} PUBLIC ${p}_core pluginkit_core sdk_common)
    set_target_properties(${_lib} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    pk_embed_hosted_presets(${_lib} PLUGIN ${p} NAME ${_name} FUNCTION smemplr_hosted_presets_${p})
    list(APPEND smemplr_hosted_libs ${_lib})
endforeach()

# the rack slots' presets: the mapping, the effects' files and Save as Default / Menu > Defaults for a new slot;
# and the Gentlr page's Threshold sliders on a slot's parameters
add_executable(smemplr_rack_preset_tests tests/rack_preset_tests.cpp src/plugin/RackPresetIO.cpp src/plugin/StateIO.cpp)
target_include_directories(smemplr_rack_preset_tests PRIVATE src src/core)
target_link_libraries(smemplr_rack_preset_tests PRIVATE smemplr_core ${smemplr_hosted_libs} pluginkit_vst smacheratr_ui)
if(APPLE)
    target_link_libraries(smemplr_rack_preset_tests PRIVATE "-framework CoreFoundation")
endif()
add_test(NAME smemplr_rack_presets COMMAND smemplr_rack_preset_tests)

pk_add_plugin(Smemplr
    BUNDLE_ID com.bfielstr.vst3.smemplr
    SOURCES
        src/plugin/Bridge.h
        src/plugin/Bridge.cpp
        src/plugin/Cids.h
        src/plugin/Processor.h
        src/plugin/Processor.cpp
        src/plugin/Controller.h
        src/plugin/Controller.cpp
        src/plugin/Entry.cpp
        src/plugin/StateIO.h
        src/plugin/StateIO.cpp
        src/plugin/HostedState.h
        src/plugin/RackPresetIO.h
        src/plugin/RackPresetIO.cpp
        src/plugin/SlotPresets.h
        src/plugin/SlotPresets.cpp
        src/plugin/Version.h
        src/ui/UiKit.h
        src/ui/Help.h
        src/ui/WaveformView.h
        src/ui/WaveformView.cpp
        src/ui/Displays.h
        src/ui/Displays.cpp
        src/ui/Editor.h
        src/ui/Editor.cpp
        src/ui/MsView.h
        src/ui/MsView.cpp
        src/ui/ModView.h
        src/ui/ModView.cpp
        # the other plug-ins' displays, shown in the effects strip
        ${CMAKE_SOURCE_DIR}/plugins/para/src/ui/FilterView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/multidyn/src/ui/DynDisplay.cpp
        ${CMAKE_SOURCE_DIR}/plugins/widr/src/ui/GonioView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/wubr/src/ui/BandView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/wubr/src/ui/ShapeView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/levlr/src/ui/LevelView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/smoothr/src/ui/HistoryView.cpp
        ${CMAKE_SOURCE_DIR}/plugins/gentlr/src/ui/GentlrView.cpp
)
target_include_directories(Smemplr PRIVATE src src/core)
target_link_libraries(Smemplr PRIVATE smemplr_core smacheratr_ui ${smemplr_hosted_libs})

pk_add_host_test(smemplr_hosttest
    PLUGIN Smemplr
    CPU_BUDGET # times the processing against a CPU budget
    SOURCES tests/host_test.mm src/plugin/StateIO.cpp
    LIBS smemplr_core
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
