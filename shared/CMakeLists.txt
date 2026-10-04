# pluginkit: code shared by all plug-ins.

# Parameter tables (no SDK dependency, used by DSP tests too).
add_library(pluginkit_core STATIC pluginkit/ParamTable.h pluginkit/ParamTable.cpp pluginkit/RtShared.h pluginkit/ScopeBuffer.h pluginkit/InstanceRegistry.h pluginkit/TailParams.h pluginkit/TailParams.cpp pluginkit/SampleFiles.h pluginkit/CrashDump.h pluginkit/CrashDump.cpp pluginkit/SettingsText.h pluginkit/SettingsText.cpp)
target_include_directories(pluginkit_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_link_libraries(pluginkit_core PUBLIC dbghelp)
endif()
pk_dsp_settings(pluginkit_core)

# The pure helpers' tests (help text wrapping, the info box's titles, the layout overlap check).
add_executable(pluginkit_tests tests/pluginkit_tests.cpp)
target_link_libraries(pluginkit_tests PRIVATE pluginkit_core)
add_test(NAME pluginkit_core COMMAND pluginkit_tests)

if(PK_BUILD_PLUGINS)
    # VST3 controller/editor bases and VSTGUI widgets.
    add_library(pluginkit_vst STATIC
        pluginkit/vst/ControllerBase.h
        pluginkit/vst/ControllerBase.cpp
        pluginkit/vst/EditorBase.h
        pluginkit/vst/EditorBase.cpp
        pluginkit/vst/Presets.h
        pluginkit/vst/Presets.cpp
        pluginkit/vst/PresetBar.h
        pluginkit/vst/PresetBar.cpp
        pluginkit/vst/Clipboard.h
        pluginkit/vst/Clipboard.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstpresetfile.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
        pluginkit/ui/Theme.h
        pluginkit/ui/Widgets.h
        pluginkit/ui/Widgets.cpp
        pluginkit/ui/HelpText.h
        pluginkit/ui/InfoBox.h
        pluginkit/ui/InfoBox.cpp
        pluginkit/ui/LayoutCheck.h
        pluginkit/ui/LayoutCheck.cpp
        pluginkit/ui/ScopeView.h
        pluginkit/ui/DropFiles.h
        pluginkit/ui/ScopeView.cpp
    )
    target_include_directories(pluginkit_vst PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(pluginkit_vst PUBLIC pluginkit_core sdk vstgui_support)
endif()
