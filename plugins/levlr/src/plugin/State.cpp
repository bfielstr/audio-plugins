#include "State.h"

#include "smacheratr/src/core/TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace levlr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x4C45564C; // 'LEVL'
// 2: Slope has eight choices (12 .. 96 dB/oct) instead of three; 3: Bands and the bands' drives; 4: the
// end saturator's Sub and High bands without buttons; 5: the end saturator's Gentlr Slope; 6: Oversampling
// (the drives' and the end saturator's); 7: the end saturator and its Gentlr on by default, Gentlr's Slope
// Signature (below)
constexpr int32 kVersion = kStateVersion;
static_assert (kVersion == 7, "a new state version needs its migration (migrateState)");
} // namespace

bool writeState (IBStream* stream, const State& st)
{
    IBStreamer s (stream, kLittleEndian);
    int32 present = 0;
    for (uint32 id = 0; id < kNumParams; ++id)
        present += st.has[id] ? 1 : 0;
    bool ok = s.writeInt32 (kMagic) && s.writeInt32 (kVersion) && s.writeInt32 (present);
    for (uint32 id = 0; ok && id < kNumParams; ++id)
        if (st.has[id])
            ok = s.writeInt32u (id) && s.writeDouble (st.norm[id]);
    return ok;
}

bool readState (IBStream* stream, State& st)
{
    IBStreamer s (stream, kLittleEndian);
    int32 magic = 0, version = 0, count = 0;
    if (!s.readInt32 (magic) || magic != kMagic || !s.readInt32 (version) || version < 1 || !s.readInt32 (count) ||
        count < 0 || count > 100000)
        return false;
    for (uint32 id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalized (id);
        st.has[id] = false;
    }
    // IDs this version doesn't know (from a newer one) are skipped; missing ones keep their defaults
    for (int32 i = 0; i < count; ++i)
    {
        uint32 id = 0;
        double v = 0.0;
        if (!s.readInt32u (id) || !s.readDouble (v))
            return false;
        if (id < kNumParams)
        {
            st.norm[id] = std::clamp (v, 0.0, 1.0);
            st.has[id] = true;
        }
    }
    // the defaults were the end saturator off, its Gentlr off and Gentlr's Slope 12 / 12: a state saved then
    // keeps them where it lacks them (before migrateState, which gives the Slope Classic before version 5)
    if (version < kNewDefaultsVersion)
        smacheratr::tailOldDefaults (st.norm, st.has, kTailBase, kTailExtBase, kTailExt3Base);
    migrateState (version, st.norm.data (), st.has.data ());
    return true;
}

} // namespace levlr
