// VST3 edit controller base: registers a ParamTable as automatable parameters, keeps the
// editor in sync with host changes and remembers editor-only state (size, tooltips).
#pragma once

#include "pluginkit/ParamTable.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

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
    // Editor-only state (interface size, tooltips).
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) override;
    void editorAttached (Steinberg::Vst::EditorView* editor) override;
    void editorRemoved (Steinberg::Vst::EditorView* editor) override;

    const ParamTable& table () const { return tableRef; }
    double plain (uint32_t id) { return tableRef.toPlain (id, getParamNormalized (id)); }
    void beginGesture (uint32_t id) { beginEdit (id); }
    void endGesture (uint32_t id) { endEdit (id); }
    void setFromUI (uint32_t id, double normalized);    // inside a gesture
    void setPlainFromUI (uint32_t id, double plainValue); // complete gesture
    void markDirty ();

    double uiScale = 1.0;
    bool uiShowTips = true;

protected:
    EditorBase* editor = nullptr;
    const ParamTable& tableRef;
};

} // namespace pk
