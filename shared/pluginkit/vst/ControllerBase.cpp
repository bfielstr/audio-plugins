#include "ControllerBase.h"

#include "EditorBase.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>

namespace pk {

using namespace Steinberg;
using namespace Steinberg::Vst;

TableParameter::TableParameter (const ParamTable& t, uint32_t id) : table (t)
{
    const auto& p = t.info (id);
    info.id = id;
    StringConvert::convert (p.name, info.title);
    StringConvert::convert (p.shortName, info.shortTitle);
    StringConvert::convert ("", info.units);
    info.stepCount = p.stepCount ();
    info.defaultNormalizedValue = t.defaultNormalized (id);
    info.unitId = kRootUnitId;
    info.flags = ParameterInfo::kCanAutomate;
    if (p.type == PType::Choice)
        info.flags |= ParameterInfo::kIsList;
    valueNormalized = info.defaultNormalizedValue;
}

void TableParameter::toString (ParamValue n, String128 string) const
{
    StringConvert::convert (table.toText (info.id, table.toPlain (info.id, n)), string);
}

bool TableParameter::fromString (const TChar* string, ParamValue& n) const
{
    const std::string s = StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (string)));
    double plainValue;
    if (!table.fromText (info.id, s, plainValue))
        return false;
    n = table.toNormalized (info.id, plainValue);
    return true;
}

ParamValue TableParameter::toPlain (ParamValue n) const { return table.toPlain (info.id, n); }
ParamValue TableParameter::toNormalized (ParamValue p) const { return table.toNormalized (info.id, p); }

tresult PLUGIN_API ControllerBase::initialize (FUnknown* context)
{
    const tresult r = EditController::initialize (context);
    if (r != kResultOk)
        return r;
    for (uint32_t id = 0; id < tableRef.size (); ++id)
        parameters.addParameter (new TableParameter (tableRef, id));
    return kResultOk;
}

tresult PLUGIN_API ControllerBase::setParamNormalized (ParamID tag, ParamValue value)
{
    const tresult r = EditController::setParamNormalized (tag, value);
    if (editor && tag < tableRef.size ())
        editor->paramChanged (tag);
    return r;
}

tresult PLUGIN_API ControllerBase::setState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    IBStreamer s (stream, kLittleEndian);
    double v = 1.0;
    if (s.readDouble (v) && v >= 0.5 && v <= 2.0)
        uiScale = v;
    bool tips = true;
    if (s.readBool (tips))
        uiShowTips = tips;
    return kResultOk;
}

tresult PLUGIN_API ControllerBase::getState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    IBStreamer s (stream, kLittleEndian);
    return s.writeDouble (uiScale) && s.writeBool (uiShowTips) ? kResultOk : kResultFalse;
}

void ControllerBase::editorAttached (EditorView* e) { editor = dynamic_cast<EditorBase*> (e); }

void ControllerBase::editorRemoved (EditorView* e)
{
    if (editor == e)
        editor = nullptr;
}

void ControllerBase::setFromUI (uint32_t id, double normalized)
{
    normalized = std::clamp (normalized, 0.0, 1.0);
    setParamNormalized (id, normalized);
    performEdit (id, normalized);
}

void ControllerBase::setPlainFromUI (uint32_t id, double plainValue)
{
    beginEdit (id);
    setFromUI (id, tableRef.toNormalized (id, plainValue));
    endEdit (id);
}

void ControllerBase::markDirty ()
{
    if (!componentHandler)
        return;
    FUnknownPtr<IComponentHandler2> h2 (componentHandler);
    if (h2)
        h2->setDirty (true);
}

} // namespace pk
