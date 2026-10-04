#include "Controller.h"
#include "Rack.h"

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

namespace smemplr {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

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

namespace {
// A value in a rack slot's block: shown and typed in the units of the slot's current effect.
class SlotParameter : public pk::TableParameter
{
public:
    SlotParameter (const pk::ParamTable& t, uint32_t id, Controller* c, int s, uint32_t j)
        : pk::TableParameter (t, id), ctl (c), slot (s), index (j)
    {
    }
    void toString (ParamValue n, String128 string) const override
    {
        const auto& t = fxBlockTable (ctl->slotType (slot));
        if (index >= t.size ())
        {
            pk::TableParameter::toString (n, string);
            return;
        }
        StringConvert::convert (t.toText (index, t.toPlain (index, n)), string);
    }
    bool fromString (const TChar* string, ParamValue& n) const override
    {
        const auto& t = fxBlockTable (ctl->slotType (slot));
        if (index >= t.size ())
            return pk::TableParameter::fromString (string, n);
        double v;
        if (!t.fromText (index, StringConvert::convert (std::u16string (reinterpret_cast<const char16_t*> (string))), v))
            return false;
        n = t.toNormalized (index, v);
        return true;
    }

private:
    Controller* ctl;
    int slot;
    uint32_t index;
};
} // namespace

Parameter* Controller::makeParameter (uint32_t id)
{
    // the hidden MIDI parameters (their places in the table: Params.h), as they always were: hidden,
    // automatable (for the hosts that send the controllers that way), never shown
    if (isMidiParam (id))
    {
        const int32 hidden = ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden;
        if (id == kMidiPitchBend)
            return new Parameter (STR16 ("Pitch Bend"), id, nullptr, 0.5, 0, hidden);
        if (id == kMidiSustain)
            return new Parameter (STR16 ("Sustain Pedal"), id, nullptr, 0.0, 1, hidden);
        return new Parameter (STR16 ("Mod Wheel"), id, nullptr, 0.0, 0, hidden);
    }
    if (isRackParam (id))
    {
        const RackField rf = rackField (id);
        if (rf.field >= kSlotParams)
            return new SlotParameter (tableRef, id, this, rf.slot, rf.field - kSlotParams);
    }
    return pk::ControllerBase::makeParameter (id);
}

int Controller::slotType (int slot)
{
    return std::clamp ((int)std::lround (plain (slotParam (slot, kSlotType))), 0, kNumFxTypes - 1);
}

void Controller::retitleSlot (int slot)
{
    const int type = slotType (slot);
    if (titledType[(size_t)slot] == type)
        return;
    titledType[(size_t)slot] = type;
    const auto& t = fxBlockTable (type);
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
        if (auto* prm = parameters.getParameter (slotBlockParam (slot, j)))
        {
            const std::string name = "FX " + std::to_string (slot + 1) + " " +
                                     (j < t.size () ? std::string (fxName (type)) + " " + t.info (j).name : std::to_string (j + 1));
            StringConvert::convert (name, prm->getInfo ().title);
        }
    if (componentHandler)
        componentHandler->restartComponent (kParamTitlesChanged);
}

tresult PLUGIN_API Controller::setParamNormalized (ParamID tag, ParamValue value)
{
    const tresult r = pk::ControllerBase::setParamNormalized (tag, value);
    if (isRackParam (tag) && rackField (tag).field == kSlotType)
        retitleSlot (rackField (tag).slot);
    return r;
}

void Controller::checkLatency ()
{
    if (!bridge)
        return;
    const int l = bridge->latency.load (std::memory_order_relaxed);
    if (reportedLatency < 0)
        reportedLatency = l; // what the host asked for at activation
    else if (l != reportedLatency)
    {
        reportedLatency = l;
        if (componentHandler)
            componentHandler->restartComponent (kLatencyChanged);
    }
}

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    const tresult r = pk::ControllerBase::initialize (context);
    if (r != kResultOk)
        return r;
    // (the hidden MIDI parameters are in the table's range: makeParameter registered them)
    // a new Smemplr's first slot has an effect: its parameters take its names from the start
    for (int s = 0; s < kRackSlots; ++s)
        retitleSlot (s);
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate ()
{
    if (bridge)
    {
        bridge->release ();
        bridge = nullptr;
    }
    return pk::ControllerBase::terminate ();
}

tresult PLUGIN_API Controller::setComponentState (IBStream* stream)
{
    if (!stream)
        return kInvalidArgument;
    PluginState st;
    if (!readState (stream, st))
        return kResultFalse;
    for (uint32_t id = 0; id < kNumParams; ++id)
        if (isValidParam (id)) // (the MIDI ones are not in a state)
            setParamNormalized (id, st.has[id] ? st.norm[id] : defaultNormalized (id));
    pendingPath = st.samplePath;
    return kResultOk;
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
                for (auto* ed : editors)
                    if (auto* e = dynamic_cast<Editor*> (ed))
                        e->bridgeChanged ();
            }
        }
        return kResultOk;
    }
    return pk::ControllerBase::notify (message);
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

} // namespace smemplr
