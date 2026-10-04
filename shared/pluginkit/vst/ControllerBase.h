// VST3 edit controller base: registers a ParamTable as automatable parameters, keeps the
// editor in sync with host changes, remembers editor-only state (size, tooltips) and handles
// presets (.vstpreset files in the user's preset folder).
#pragma once

#include "pluginkit/ParamTable.h"
#include "pluginkit/PresetStore.h"
#include "pluginkit/SettingsText.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <atomic>
#include <string>
#include <vector>

namespace pk {

class EditorBase;

// A VST3 parameter whose ranges and text come from a ParamTable.
class TableParameter : public Steinberg::Vst::Parameter
{
public:
    TableParameter (const ParamTable& table, uint32_t id);
    void toString (Steinberg::Vst::ParamValue n, Steinberg::Vst::String128 string) const override;
    bool fromString (const Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& n) const override;
    Steinberg::Vst::ParamValue toPlain (Steinberg::Vst::ParamValue n) const override;
    Steinberg::Vst::ParamValue toNormalized (Steinberg::Vst::ParamValue plain) const override;

private:
    const ParamTable& table;
};

class ControllerBase : public Steinberg::Vst::EditController
{
public:
    explicit ControllerBase (const ParamTable& t) : tableRef (t) {}

    // Registers every table entry (all automatable). Subclasses call this first.
    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
                                                      Steinberg::Vst::ParamValue value) override;
    // Editor-only state (interface size, tooltips, preset name).
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) override;
    // Handles the preset messages (see Presets.h); subclasses fall back to this from their own.
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) override;
    void editorAttached (Steinberg::Vst::EditorView* editor) override;
    void editorRemoved (Steinberg::Vst::EditorView* editor) override;

    const ParamTable& table () const { return tableRef; }
    double plain (uint32_t id) { return tableRef.toPlain (id, getParamNormalized (id)); }
    void beginGesture (uint32_t id) { beginEdit (id); }
    void endGesture (uint32_t id) { endEdit (id); }
    void setFromUI (uint32_t id, double normalized);    // inside a gesture
    void setPlainFromUI (uint32_t id, double plainValue); // complete gesture
    void markDirty ();

    // Presets. The processor's state travels in messages, so saving and loading work from the
    // editor without any help from the host. Subclasses call setPresetInfo() in their constructor.
    // See PresetStore.h for the kinds of preset (Init, factory, user, the saved default).
    void setPresetInfo (const Steinberg::FUID& processorClassId, const char* pluginName, const char* formerName = nullptr);
    const std::string& pluginName () const { return presetPlugin; }
    std::string presetFolder () const;
    const std::string& presetName () const { return presetTitle; }
    presets::Kind presetKind () const { return currentKind; }
    const std::string& presetPath () const { return currentPath; } // user file, or factory source path
    // meta: written into the file (nullptr: the current preset's tags and category are kept when
    // `path` is the current user preset, else none)
    bool savePreset (const std::string& path, const presets::Meta* meta = nullptr); // the processor answers synchronously in-process
    bool loadPreset (const std::string& path);
    void resetToDefaults (); // every parameter back to its default, as complete gestures
    bool loadInit ();        // Init: resetToDefaults (and resetExtraState), shown as "Init"
    // Factory presets: the files registered by the build (Presets.h), parsed against this table.
    const std::vector<presets::FactoryPreset>& factoryPresets ();
    bool loadFactory (int index);
    std::vector<presets::Item> factoryItems ();
    std::vector<presets::Item> userItems () const;
    // User presets: <folder>/[category/]name.vstpreset
    std::string userPresetPath (const std::string& name, const std::string& category = {}) const;
    bool saveUserPreset (const std::string& name, const std::string& category, const std::vector<std::string>& tags);
    bool setPresetTags (const std::string& path, const std::vector<std::string>& tags);
    bool renamePreset (const std::string& path, const std::string& newName);
    bool deletePreset (const std::string& path);
    // The saved default: a new instance starts from it (see applyStartupDefault).
    std::string defaultPath () const;
    bool hasDefault () const;
    bool saveAsDefault ();
    bool loadDefault (); // Init when there is none
    bool resetDefault ();
    // The Presets menu (PresetBar) and its tag filter (for this editor session).
    std::vector<presets::MenuEntry> presetMenu (std::vector<presets::Item>* factoryOut = nullptr,
                                                std::vector<presets::Item>* userOut = nullptr);
    std::string tagFilter;

    // Copy / Paste Settings (SettingsText.h, under the plug-in's name from setPresetInfo; a paste also
    // takes texts under its former name): every parameter; a paste sets the ones in the text as complete gestures and returns false (changing
    // nothing) when the text is not this plug-in's settings.
    std::string settingsText ();
    bool applySettingsText (const std::string& text);

    // Latency the processor can change while it runs (a saturator's Oversampling): the processor
    // publishes what it runs at in an atomic (-1: not known yet) shared in-process with the controller,
    // which watches it here and tells the host (restartComponent (kLatencyChanged)) when it moves. The
    // first value seen is what the host was told at activation. Checked on every parameter the
    // controller hears of and 30 times a second while an editor is open (the processor takes a new
    // setting on its next block, so the check that follows a change is the one that sees it). A
    // source must outlive the watch: unwatchLatency () before releasing what holds it.
    void watchLatency (const std::atomic<int>* source);
    void unwatchLatency ();
    void checkLatency ();

    double uiScale = 1.0;
    bool uiShowTips = true;
    // The end saturator's sections (smacheratr::TailPanel): which are open (kTailOpenSaturator,
    // kTailOpenGentlr; -1: not decided yet, so a new instance opens the saturator only when it is on), and
    // which layer of its colour display is in front (Smacheratr's own editor and Smemplr's rack page use it
    // too: 0 Color, 1 Gentlr). Editor state, saved with the controller's (not parameters).
    static constexpr int kTailOpenSaturator = 1, kTailOpenGentlr = 2;
    int uiTailOpen = -1;
    int uiColorLayer = 0;

protected:
    void refreshEditor ();
    // The VST3 parameter registered for a table entry (a TableParameter unless overridden).
    virtual Steinberg::Vst::Parameter* makeParameter (uint32_t id);
    // Init and factory presets set every parameter; a plug-in with state beyond its parameters that a
    // fresh instance does not have (smemplr's LFO mappings) clears it here.
    virtual void resetExtraState () {}
    // Called at the end of initialize(): the saved default, if there is one, as a new instance's
    // settings. A host loading a project then calls setComponentState / setState, which win.
    void applyStartupDefault ();
    // A table entry that is a setting (reset to its default, copied and pasted as settings): all of them
    // unless overridden (Smemplr's hidden MIDI parameters are not).
    virtual bool isSetting (uint32_t) const { return true; }

    // every open editor (a host may show more than one view of a plug-in, or open the next before it
    // closes the last): each one follows the parameters
    std::vector<EditorBase*> editors;
    const ParamTable& tableRef;
    Steinberg::FUID presetClassId;
    std::string presetPlugin, presetTitle, presetFormer; // presetFormer: the name its presets were saved under before a rename
    presets::Kind currentKind = presets::Kind::None;
    std::string currentPath;
    std::string pendingSavePath;
    presets::Meta pendingMeta;
    bool pendingHasMeta = false, pendingIsDefault = false;
    bool lastSaveOk = false;
    std::vector<presets::FactoryPreset> factory;
    bool factoryParsed = false;

    std::vector<const std::atomic<int>*> latencySources;
    std::vector<int> latencySeen; // per source: the value the host knows (-1: none yet)

private:
    void applyValues (const SettingValues& values); // every parameter: the default unless in `values`
};

} // namespace pk
