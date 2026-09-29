#include "Rack.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace smempler {

namespace mseq {
const pk::ParamTable& paramTable ()
{
    using namespace pk;
    using namespace pk::make;
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (real (kSideHp, "Side High-Pass", "Side HP", 20.0, 2000.0, 150.0, Curve::Log, Disp::Hz));
        v.push_back (choice (kSlope, "Side High-Pass Slope", "Slope", {"6 dB", "12 dB", "24 dB"}, 2));
        v.push_back (real (kSideGain, "Side Gain", "Side", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        v.push_back (real (kMidGain, "Mid Gain", "Mid", -24.0, 12.0, 0.0, Curve::Linear, Disp::Db));
        return v;
    }());
    return t;
}
} // namespace mseq

static_assert (kSlotBlock >= multidyn::kNumParams && kSlotBlock >= para::kNumParams && kSlotBlock >= widr::kNumParams &&
                   kSlotBlock >= smacheratr::kNumParams && kSlotBlock >= mseq::kNumParams,
               "a slot's block must hold every effect's parameters");

const char* fxName (int type)
{
    switch (type)
    {
        case kFxPara: return "para";
        case kFxMultidyn: return "multidyn";
        case kFxMsEq: return "m/s eq";
        case kFxSmacheratr: return "smacheratr";
        case kFxWidr: return "widr";
        default: return "";
    }
}

const pk::ParamTable& fxTable (int type)
{
    static const pk::ParamTable empty (std::vector<pk::ParamInfo> {});
    switch (type)
    {
        case kFxPara: return para::paramTable ();
        case kFxMultidyn: return multidyn::paramTable ();
        case kFxMsEq: return mseq::paramTable ();
        case kFxSmacheratr: return smacheratr::paramTable ();
        case kFxWidr: return widr::paramTable ();
        default: return empty;
    }
}

Rack::Rack ()
{
    for (auto& s : slots)
        s = std::make_unique<Slot> ();
}

void Rack::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    for (auto& s : slots)
    {
        s->para.prepare (sr, maxBlock);
        s->multidyn.prepare (sr, maxBlock);
        s->ms.prepare (sr);
        s->sat.prepare (sr, maxBlock);
        s->widr.prepare (sr, maxBlock);
        applyAll (*s);
    }
}

void Rack::reset ()
{
    for (auto& s : slots)
    {
        s->para.reset ();
        s->multidyn.reset ();
        s->ms.reset ();
        s->sat.reset ();
        s->widr.reset ();
    }
}

void Rack::setMeters (RackMeters* m)
{
    meters = m;
    for (int i = 0; i < kRackSlots; ++i)
    {
        Slot& s = *slots[(size_t)i];
        s.para.setMeters (m ? &m->para[(size_t)i] : nullptr);
        s.sat.setMeters (m ? &m->sat[(size_t)i] : nullptr);
        s.widr.setMeters (m ? &m->widr[(size_t)i] : nullptr);
    }
}

void Rack::apply (Slot& s, uint32_t j)
{
    const auto& t = fxTable (s.type);
    if (j >= t.size ())
        return;
    const double v = t.toPlain (j, std::clamp (s.norm[j], 0.0, 1.0));
    switch (s.type)
    {
        case kFxPara: s.para.setParam (j, v); break;
        case kFxMultidyn: s.multidyn.setParam (j, v); break;
        case kFxSmacheratr:
            // off: fully dry (it keeps its latency)
            s.sat.setParam (j, j == smacheratr::kDryWet && !s.on ? 0.0 : v);
            break;
        case kFxWidr: s.widr.setParam (j, v); break;
        default: break; // the M/S EQ reads its values when it runs
    }
}

void Rack::applyAll (Slot& s)
{
    for (uint32_t j = 0; j < kSlotBlock; ++j)
        apply (s, j);
    s.multidyn.setBypass (!s.on);
    switch (s.type)
    {
        case kFxPara: s.para.reset (); break;
        case kFxMultidyn: s.multidyn.reset (); break;
        case kFxMsEq: s.ms.reset (); break;
        case kFxSmacheratr: s.sat.reset (); break;
        case kFxWidr: s.widr.reset (); break;
        default: break;
    }
}

void Rack::setParam (uint32_t id, double plain)
{
    if (!isRackParam (id))
        return;
    const uint32_t rel = id - kRackBase;
    Slot& s = *slots[rel / kSlotSize];
    const uint32_t field = rel % kSlotSize;
    if (field == kSlotType)
    {
        const int t = std::clamp ((int)std::lround (plain), 0, kNumFxTypes - 1);
        if (t != s.type)
        {
            s.type = t;
            applyAll (s);
        }
    }
    else if (field == kSlotOn)
    {
        s.on = plain >= 0.5;
        s.multidyn.setBypass (!s.on);
        if (s.type == kFxSmacheratr)
            apply (s, smacheratr::kDryWet);
    }
    else
    {
        const uint32_t j = field - kSlotParams;
        s.norm[j] = plain;
        apply (s, j);
    }
}

int Rack::latency () const
{
    int l = 0;
    for (const auto& s : slots)
        if (s->type == kFxMultidyn)
            l += s->multidyn.latency ();
        else if (s->type == kFxSmacheratr)
            l += s->sat.latency ();
    return l;
}

void Rack::noteOn (int note)
{
    for (auto& s : slots)
        if (s->type == kFxPara)
            s->para.noteOn (note);
}

void Rack::setPitchBend (float bipolar)
{
    for (auto& s : slots)
        s->para.setPitchBend (bipolar);
}

void Rack::process (float* L, float* R, int n)
{
    for (int i = 0; i < kRackSlots; ++i)
    {
        Slot& s = *slots[(size_t)i];
        switch (s.type)
        {
            case kFxPara:
                if (s.on)
                    s.para.process (L, R, L, R, n);
                break;
            case kFxMultidyn: s.multidyn.process (L, R, nullptr, nullptr, L, R, n); break; // bypassed: delay only
            case kFxMsEq:
            {
                const auto& t = mseq::paramTable ();
                auto plain = [&] (uint32_t j) { return t.toPlain (j, std::clamp (s.norm[j], 0.0, 1.0)); };
                if (s.on)
                    s.ms.process (L, R, n, plain (mseq::kSideHp), (int)std::lround (plain (mseq::kSlope)),
                                  plain (mseq::kSideGain), plain (mseq::kMidGain));
                else
                    s.ms.measure (L, R, n);
                break;
            }
            case kFxSmacheratr: s.sat.process (L, R, L, R, n); break; // off: fully dry, same latency
            case kFxWidr:
                if (s.on)
                    s.widr.process (L, R, L, R, n);
                break;
            default: break;
        }
        publish (i);
    }
}

void Rack::publish (int i)
{
    if (!meters)
        return;
    Slot& s = *slots[(size_t)i];
    if (s.type == kFxMultidyn)
        for (int b = 0; b < multidyn::kNumBands; ++b)
        {
            const auto& m = s.multidyn.meter (b);
            meters->multidyn[(size_t)i].inputDb[(size_t)b].store (m.inputDb, std::memory_order_relaxed);
            meters->multidyn[(size_t)i].outputDb[(size_t)b].store (m.outputDb, std::memory_order_relaxed);
            meters->multidyn[(size_t)i].gainDb[(size_t)b].store (m.gainDb, std::memory_order_relaxed);
        }
    else if (s.type == kFxMsEq)
    {
        meters->msMid[(size_t)i].store (s.ms.midPeak, std::memory_order_relaxed);
        meters->msSide[(size_t)i].store (s.ms.sidePeak, std::memory_order_relaxed);
    }
}

} // namespace smempler
