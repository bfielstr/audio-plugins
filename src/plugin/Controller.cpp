#include "Controller.h"

#include "Cids.h"
#include "StateIO.h"
#include "ui/Editor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace simplr {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

class SimplrParameter : public Parameter
{
public:
    explicit SimplrParameter (uint32_t id)
    {
        const auto& p = paramInfo (id);
        info.id = id;
        StringConvert::convert (p.name, info.title);
        StringConvert::convert (p.shortName, info.shortTitle);
        StringConvert::convert ("", info.units);
        info.stepCount = p.stepCount ();
        info.defaultNormalizedValue = defaultNormalized (id);
        info.unitId = kRootUnitId;
        info.flags = ParameterInfo::kCanAutomate;
        if (p.type == PType::Choice)
            info.flags |= ParameterInfo::kIsList;
        valueNormalized = info.defaultNormalizedValue;
    }

    void toString (ParamValue n, String128 string) const override
    {
        StringConvert::convert (toText (info.id, simplr::toPlain (info.id, n)), string);
    }

    bool fromString (const TChar* string, ParamValue& n) const override
    {
        const std::string s = StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (string)));
        double plain;
        if (!fromText (info.id, s, plain))
            return false;
        n = simplr::toNormalized (info.id, plain);
        return true;
    }

    ParamValue toPlain (ParamValue n) const override { return simplr::toPlain (info.id, n); }
    ParamValue toNormalized (ParamValue plain) const override { return simplr::toNormalized (info.id, plain); }
};

int guessWarpBeats (const SampleData& s)
{
    const double secs = s.seconds ();
    if (secs < 0.25)
        return 1;
    for (int beats = 1; beats <= 1024; beats *= 2)
    {
        const double bpm = beats * 60.0 / secs;
        if (bpm >= 80.0 && bpm < 160.0)
            return beats;
    }
    return std::clamp ((int)std::lround (secs * 2.0), 1, 1024); // 120 BPM
}

} // namespace

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    const tresult r = EditController::initialize (context);
    if (r != kResultOk)
        return r;
    for (uint32_t id = 0; id < kNumParams; ++id)
        parameters.addParameter (new SimplrParameter (id));
    const int32 hidden = ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden;
    parameters.addParameter (STR16 ("Pitch Bend"), nullptr, 0, 0.5, hidden, kMidiPitchBend);
    parameters.addParameter (STR16 ("Sustain Pedal"), nullptr, 1, 0.0, hidden, kMidiSustain);
    parameters.addParameter (STR16 ("Mod Wheel"), nullptr, 0, 0.0, hidden, kMidiModWheel);
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate ()
{
    if (bridge)
    {
        bridge->release ();
        bridge = nullptr;
    }
    return EditController::terminate ();
}

tresult PLUGIN_API Controller::setComponentState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    PluginState st;
    if (!readState (stream, st))
        return kResultFalse;
    for (uint32_t id = 0; id < kNumParams; ++id)
        setParamNormalized (id, st.has[id] ? st.norm[id] : defaultNormalized (id));
    pendingPath = st.samplePath;
    return kResultOk;
}

tresult PLUGIN_API Controller::setState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    IBStreamer s (stream, kLittleEndian);
    double v = 1.0;
    if (s.readDouble (v) && v >= 0.5 && v <= 2.0)
        uiScale = v;
    return kResultOk;
}

tresult PLUGIN_API Controller::getState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    IBStreamer s (stream, kLittleEndian);
    return s.writeDouble (uiScale) ? kResultOk : kResultFalse;
}

IPlugView* PLUGIN_API Controller::createView (FIDString name)
{
    if (name && std::strcmp (name, ViewType::kEditor) == 0)
        return new Editor (this);
    return nullptr;
}

tresult PLUGIN_API Controller::notify (IMessage* message)
{
    if (message && std::strcmp (message->getMessageID (), kBridgeMessageId) == 0)
    {
        int64 ptr = 0;
        if (message->getAttributes ()->getInt (kBridgeAttr, ptr) == kResultOk && ptr != 0)
        {
            auto* b = reinterpret_cast<Bridge*> ((intptr_t)ptr);
            if (b != bridge)
            {
                b->retain ();
                if (bridge)
                    bridge->release ();
                bridge = b;
                if (editor)
                    editor->bridgeChanged ();
            }
        }
        return kResultOk;
    }
    return EditController::notify (message);
}

tresult PLUGIN_API Controller::setParamNormalized (ParamID tag, ParamValue value)
{
    const tresult r = EditController::setParamNormalized (tag, value);
    if (editor && tag < kNumParams)
        editor->paramChanged (tag);
    return r;
}

tresult PLUGIN_API Controller::getMidiControllerAssignment (int32 busIndex, int16, CtrlNumber ctrl, ParamID& id)
{
    if (busIndex != 0)
        return kResultFalse;
    switch (ctrl)
    {
        case kPitchBend: id = kMidiPitchBend; return kResultTrue;
        case kCtrlSustainOnOff: id = kMidiSustain; return kResultTrue;
        case kCtrlModWheel: id = kMidiModWheel; return kResultTrue;
        default: return kResultFalse;
    }
}

void Controller::editorAttached (EditorView* e) { editor = dynamic_cast<Editor*> (e); }

void Controller::editorRemoved (EditorView* e)
{
    if (editor == e)
        editor = nullptr;
}

void Controller::setFromUI (uint32_t id, double normalized)
{
    normalized = std::clamp (normalized, 0.0, 1.0);
    setParamNormalized (id, normalized);
    performEdit (id, normalized);
}

void Controller::setPlainFromUI (uint32_t id, double plainValue)
{
    beginEdit (id);
    setFromUI (id, simplr::toNormalized (id, plainValue));
    endEdit (id);
}

void Controller::markDirty ()
{
    if (!componentHandler)
        return;
    FUnknownPtr<IComponentHandler2> h2 (componentHandler);
    if (h2)
        h2->setDirty (true);
}

std::string Controller::sampleDisplayName ()
{
    if (!bridge)
        return pendingPath.empty () ? std::string () : pendingPath;
    std::string p = bridge->samplePath ();
    auto slash = p.find_last_of ("/\\");
    return slash == std::string::npos ? p : p.substr (slash + 1);
}

bool Controller::loadSample (const std::string& path, bool isNewFile)
{
    if (!bridge)
        return false;
    std::string err;
    const SampleOps ops = isNewFile ? SampleOps {} : bridge->sampleOps ();
    const bool ok = bridge->loadSample (path, ops, err);
    if (ok && isNewFile)
    {
        bridge->setEdits ({});
        setPlainFromUI (kSampleStart, 0.0);
        setPlainFromUI (kSampleEnd, 1.0);
        if (auto s = bridge->sample ())
            setPlainFromUI (kWarpBeats, guessWarpBeats (*s));
    }
    markDirty ();
    return ok;
}

void Controller::applyOps (const SampleOps& ops, bool remapFlags)
{
    if (!bridge)
        return;
    const std::string path = bridge->samplePath ();
    if (path.empty ())
        return;
    const SampleOps before = bridge->sampleOps ();
    const double fs = plain (kSampleStart), fe = plain (kSampleEnd);
    std::string err;
    if (!bridge->loadSample (path, ops, err))
        return;

    // Keep flags and manual slices pointing at the same audio.
    SliceEdits edits;
    if (auto e = bridge->editsNow ())
        edits = *e;
    auto remap = [&] (std::vector<double>& v) {
        std::vector<double> out;
        for (double x : v)
        {
            double y = x;
            if (before.reverse != ops.reverse)
                y = 1.0 - y;
            if (remapFlags)
            {
                const double a = std::min (fs, fe), b = std::max (fs, fe);
                if (y < a || y > b || b - a <= 0.0)
                    continue;
                y = (y - a) / (b - a);
            }
            out.push_back (y);
        }
        v = out;
    };
    remap (edits.manual);
    remap (edits.suppressed);
    bridge->setEdits (edits);
    if (remapFlags)
    {
        setPlainFromUI (kSampleStart, 0.0);
        setPlainFromUI (kSampleEnd, 1.0);
    }
    else if (before.reverse != ops.reverse)
    {
        setPlainFromUI (kSampleStart, 1.0 - fe);
        setPlainFromUI (kSampleEnd, 1.0 - fs);
    }
    markDirty ();
}

void Controller::clearSample ()
{
    if (bridge)
        bridge->clearSample ();
    pendingPath.clear ();
    markDirty ();
}

} // namespace simplr
