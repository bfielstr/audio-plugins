#include "ControllerBase.h"

#include "EditorBase.h"
#include "Presets.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <cstring>

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
        parameters.addParameter (makeParameter (id));
    return kResultOk;
}

Parameter* ControllerBase::makeParameter (uint32_t id) { return new TableParameter (tableRef, id); }

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
    if (char8* name = s.readStr8 ())
    {
        presetTitle = name;
        delete[] name;
    }
    refreshEditor ();
    return kResultOk;
}

tresult PLUGIN_API ControllerBase::getState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    IBStreamer s (stream, kLittleEndian);
    return s.writeDouble (uiScale) && s.writeBool (uiShowTips) && s.writeStr8 (presetTitle.c_str ()) ? kResultOk
                                                                                                     : kResultFalse;
}

void ControllerBase::editorAttached (EditorView* e) { editor = dynamic_cast<EditorBase*> (e); }

void ControllerBase::editorRemoved (EditorView* e)
{
    if (editor == e)
        editor = nullptr;
}

void ControllerBase::refreshEditor ()
{
    if (editor)
        editor->refresh ();
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

//------------------------------------------------------------------------------------------------
void ControllerBase::setPresetInfo (const FUID& processorClassId, const char* pluginName)
{
    presetClassId = processorClassId;
    presetPlugin = pluginName ? pluginName : "";
}

std::string ControllerBase::presetFolder () const
{
    return presetPlugin.empty () ? std::string () : presets::userFolder (presetPlugin.c_str ());
}

bool ControllerBase::savePreset (const std::string& path)
{
    if (path.empty () || !presetClassId.isValid ())
        return false;
    // the processor answers with its state (see notify), which writes the file
    pendingSavePath = path;
    lastSaveOk = false;
    if (IMessage* msg = allocateMessage ())
    {
        msg->setMessageID (presets::kMsgGetState);
        sendMessage (msg);
        msg->release ();
    }
    return lastSaveOk;
}

bool ControllerBase::loadPreset (const std::string& path)
{
    std::vector<char> component, controllerState;
    if (!presetClassId.isValid () || !presets::read (path, presetClassId, component, controllerState))
        return false;
    if (!component.empty ())
    {
        // the processor first (it stores the state and reloads it in the audio thread), then us
        if (IMessage* msg = allocateMessage ())
        {
            msg->setMessageID (presets::kMsgSetState);
            msg->getAttributes ()->setBinary (presets::kAttrData, component.data (), (uint32)component.size ());
            sendMessage (msg);
            msg->release ();
        }
        MemoryStream ms (component.data (), (TSize)component.size ());
        setComponentState (&ms);
    }
    if (!controllerState.empty ())
    {
        MemoryStream ms (controllerState.data (), (TSize)controllerState.size ());
        setState (&ms);
    }
    presetTitle = presets::nameOf (path);
    if (componentHandler)
        componentHandler->restartComponent (kParamValuesChanged);
    markDirty ();
    refreshEditor ();
    return true;
}

void ControllerBase::resetToDefaults ()
{
    for (uint32_t id = 0; id < tableRef.size (); ++id)
    {
        const double def = tableRef.defaultNormalized (id);
        beginEdit (id);
        setParamNormalized (id, def);
        performEdit (id, def);
        endEdit (id);
    }
    presetTitle.clear ();
    markDirty ();
    refreshEditor ();
}

tresult PLUGIN_API ControllerBase::notify (IMessage* message)
{
    if (!message || !message->getMessageID ())
        return EditController::notify (message);
    const char* id = message->getMessageID ();
    if (std::strcmp (id, presets::kMsgState) == 0)
    {
        const void* data = nullptr;
        uint32 size = 0;
        if (!pendingSavePath.empty () && message->getAttributes ()->getBinary (presets::kAttrData, data, size) == kResultOk)
        {
            std::vector<char> component ((const char*)data, (const char*)data + size);
            MemoryStream own;
            getState (&own);
            std::vector<char> controllerState ((const char*)own.getData (), (const char*)own.getData () + own.getSize ());
            lastSaveOk = presets::write (pendingSavePath, presetClassId, component, controllerState);
            if (lastSaveOk)
            {
                presetTitle = presets::nameOf (pendingSavePath);
                markDirty ();
                refreshEditor ();
            }
        }
        pendingSavePath.clear ();
        return kResultOk;
    }
    if (std::strcmp (id, presets::kMsgSave) == 0 || std::strcmp (id, presets::kMsgLoad) == 0)
    {
        TChar buf[1024] = {0};
        if (message->getAttributes ()->getString (presets::kAttrPath, buf, sizeof (buf)) == kResultOk)
        {
            const std::string path = StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (buf)));
            if (std::strcmp (id, presets::kMsgSave) == 0)
                savePreset (path);
            else
                loadPreset (path);
        }
        return kResultOk;
    }
    return EditController::notify (message);
}

} // namespace pk
