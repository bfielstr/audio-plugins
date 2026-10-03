#include "Modulation.h"

#include "Rack.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace smemplr {

bool canModulate (uint32_t id, int slotType)
{
    if (!isValidParam (id) || isModLfoParam (id) || isTailParam (id) || id == kLoopLen)
        return false;
    if (id >= kFxParaOn && id < kRackBase)
        return false; // the fixed effects of 0.5 (moved into the rack when a project loads)
    if (isRackParam (id))
    {
        const uint32_t field = rackField (id).field;
        if (field < kSlotParams || slotType <= kFxEmpty || slotType >= kNumFxTypes)
            return false;
        const uint32_t j = field - kSlotParams;
        const auto& t = fxBlockTable (slotType);
        if (j >= t.size () || fxIdAt (slotType, j) < 0)
            return false;
        const PType type = t.info (j).type;
        return type == PType::Float || type == PType::Int;
    }
    const PType type = paramInfo (id).type;
    return type == PType::Float || type == PType::Int;
}

bool addMapping (ModMap& m, int lfo, uint32_t target, double depth, int fxType)
{
    if (lfo < 0 || lfo >= kModLfos || !isValidParam (target) || (int)m.list.size () >= kMaxModMappings)
        return false;
    for (const auto& x : m.list)
        if (x.lfo == lfo && x.target == target)
            return false;
    m.list.push_back ({lfo, target, std::clamp (depth, -1.0, 1.0), fxType});
    return true;
}

bool removeMapping (ModMap& m, size_t index)
{
    if (index >= m.list.size ())
        return false;
    m.list.erase (m.list.begin () + (std::ptrdiff_t)index);
    return true;
}

void remapSlots (ModMap& m, const std::array<int, kRackSlots>& newSlot)
{
    std::vector<ModMapping> out;
    for (ModMapping x : m.list)
    {
        if (isRackParam (x.target))
        {
            const RackField rf = rackField (x.target);
            const int to = newSlot[(size_t)rf.slot];
            if (to < 0 || to >= kRackSlots)
                continue;
            if (rf.field >= kSlotParams)
                x.target = slotBlockParam (to, rf.field - kSlotParams);
            else
                x.target = slotParam (to, rf.field);
        }
        out.push_back (x);
    }
    m.list = std::move (out);
}

namespace {
constexpr int32_t kModFormat = 1;
// lfo (int32), target (uint32), depth (float64), fxType (int32)
constexpr int32_t kEntryBytes = 4 + 4 + 8 + 4;

void put32 (std::vector<uint8_t>& b, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back ((uint8_t)(v >> (8 * i)));
}
void put64 (std::vector<uint8_t>& b, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        b.push_back ((uint8_t)(v >> (8 * i)));
}
uint32_t get32 (const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint64_t get64 (const uint8_t* p) { return (uint64_t)get32 (p) | ((uint64_t)get32 (p + 4) << 32); }
} // namespace

std::vector<uint8_t> encodeModMap (const ModMap& m)
{
    std::vector<uint8_t> b;
    put32 (b, (uint32_t)kModFormat);
    put32 (b, (uint32_t)m.list.size ());
    put32 (b, (uint32_t)kEntryBytes);
    for (const auto& x : m.list)
    {
        uint64_t depthBits;
        std::memcpy (&depthBits, &x.depth, sizeof (depthBits));
        put32 (b, (uint32_t)x.lfo);
        put32 (b, x.target);
        put64 (b, depthBits);
        put32 (b, (uint32_t)x.fxType);
    }
    return b;
}

bool decodeModMap (const uint8_t* data, size_t size, ModMap& m)
{
    m.list.clear ();
    if (size < 12 || !data)
        return false;
    const int32_t format = (int32_t)get32 (data), count = (int32_t)get32 (data + 4), entry = (int32_t)get32 (data + 8);
    if (format < 1 || count < 0 || entry < kEntryBytes || (size_t)count * (size_t)entry > size - 12)
        return false;
    for (int32_t i = 0; i < count; ++i)
    {
        const uint8_t* e = data + 12 + (size_t)i * (size_t)entry;
        const int lfo = (int)(int32_t)get32 (e);
        const uint32_t target = get32 (e + 4);
        const uint64_t depthBits = get64 (e + 8);
        double depth;
        std::memcpy (&depth, &depthBits, sizeof (depth));
        const int fxType = (int)(int32_t)get32 (e + 16);
        if (!std::isfinite (depth))
            continue;
        addMapping (m, lfo, target, depth, fxType < kFxEmpty || fxType >= kNumFxTypes ? -1 : fxType);
    }
    return true;
}

float modShape (int shape, double phase, float lastRandom, float random)
{
    const float p = (float)(phase - std::floor (phase));
    switch (shape)
    {
        case kModSine: return std::sin (2.0f * (float)M_PI * p);
        case kModTriangle: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        case kModSawUp: return 2.0f * p - 1.0f;
        case kModSawDown: return 1.0f - 2.0f * p;
        case kModSquare: return p < 0.5f ? 1.0f : -1.0f;
        case kModRandom: return random;
        default: // Smooth Random: a cosine glide from the last value to this cycle's
            return lastRandom + (random - lastRandom) * (0.5f - 0.5f * std::cos ((float)M_PI * p));
    }
}

void Modulator::nextRandom (Lfo& l)
{
    seed = seed * 1664525u + 1013904223u;
    l.last = l.random;
    l.random = (float)((seed >> 8) & 0xFFFFFF) / 8388607.5f - 1.0f;
}

void Modulator::reset ()
{
    for (auto& l : lfos)
    {
        l = Lfo {};
        nextRandom (l);
        nextRandom (l);
    }
    notePending = false;
}

void Modulator::advance (const double* p, int n, double bpm, double ppq, bool songPlaying)
{
    if (bpm <= 0.0)
        bpm = 120.0;
    const bool note = notePending;
    notePending = false;
    for (int i = 0; i < kModLfos; ++i)
    {
        Lfo& l = lfos[(size_t)i];
        const int shape = std::clamp ((int)std::lround (p[modLfoParam (i, kModShape)]), 0, kNumModShapes - 1);
        const int sync = (int)std::lround (p[modLfoParam (i, kModSync)]);
        const double offset = p[modLfoParam (i, kModPhase)] / 360.0;
        const bool retrig = p[modLfoParam (i, kModRetrig)] >= 0.5;
        if (note && retrig)
        {
            l.run = 0.0;
            nextRandom (l);
        }
        double cycles; // per sample
        if (sync > 0)
        {
            const double beats = syncDivisionBeats (sync - 1);
            cycles = bpm / 60.0 / beats / sr;
            if (songPlaying && !retrig)
            {
                // where the song is in the note length (a jump or a new cycle: the next random value)
                double at = ppq / beats;
                at -= std::floor (at);
                if (at < l.run - 0.5 || at > l.run + 0.5)
                    nextRandom (l);
                l.run = at;
            }
        }
        else
            cycles = std::max (0.0, p[modLfoParam (i, kModRate)]) / sr;
        l.shown = l.run + offset;
        l.shown -= std::floor (l.shown);
        // (the random shapes change value where a cycle starts: they glide over the cycle itself)
        l.value = modShape (shape, shape >= kModRandom ? l.run : l.shown, l.last, l.random);
        l.run += cycles * n;
        if (l.run >= 1.0)
        {
            l.run -= std::floor (l.run);
            nextRandom (l);
        }
    }
}

} // namespace smemplr
