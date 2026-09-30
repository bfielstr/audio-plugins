// VST3 edit controller base: registers a ParamTable as automatable parameters, keeps the
// editor in sync with host changes, remembers editor-only state (size, tooltips) and handles
// presets (.vstpreset files in the user's preset folder).
#pragma once

#include "pluginkit/ParamTable.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

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
    void setPresetInfo (const Steinberg::FUID& processorClassId, const char* pluginName, const char* formerName = nullptr);
    std::string presetFolder () const;
    const std::string& presetName () const { return presetTitle; }
    bool savePreset (const std::string& path); // the processor answers synchronously in-process
    bool loadPreset (const std::string& path);
    void resetToDefaults (); // every parameter back to its default, as complete gestures

    // Copy / Paste Settings (SettingsText.h, under the plug-in's name from setPresetInfo): every
    // parameter; a paste sets the ones in the text as complete gestures and returns false (changing
    // nothing) when the text is not this plug-in's settings.
    std::string settingsText ();
    bool applySettingsText (const std::string& text);

    double uiScale = 1.0;
    bool uiShowTips = true;

protected:
    void refreshEditor ();
    // The VST3 parameter registered for a table entry (a TableParameter unless overridden).
    virtual Steinberg::Vst::Parameter* makeParameter (uint32_t id);

    // every open editor (a host may show more than one view of a plug-in, or open the next before it
    // closes the last): each one follows the parameters
    std::vector<EditorBase*> editors;
    const ParamTable& tableRef;
    Steinberg::FUID presetClassId;
    std::string presetPlugin, presetTitle, presetFormer; // presetFormer: the name its presets were saved under before a rename
    std::string pendingSavePath;
    bool lastSaveOk = false;
};

} // namespace pk
