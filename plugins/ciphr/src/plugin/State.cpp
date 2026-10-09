#include "State.h"

#include "smacheratr/src/core/TailExt.h"

#include "base/source/fstreamer.h"

#include <algorithm>

namespace ciphr {

using namespace Steinberg;

namespace {
constexpr int32 kMagic = 0x52485043; // 'CPHR'
// 1: the first. A later version that changes what a saved value means converts older states in
// readState (as the other plug-ins do), so projects keep sounding the same.
constexpr int32 kVersion = 3;
// 2: the end saturator and its Gentlr on by default, Gentlr's Slope Signature (older states keep the
// old defaults where they lack them)
constexpr int32 kNewDefaults = 2;
// 3: Gentlr's Slope has a fourth choice, Alt Signature: a Slope saved before (three choices) is read as
// the same choice
constexpr int32 kAltSignature = 3;
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
    for (int32 i = 0; i < count; ++i)
    {
        uint32 id = 0;
        double v = 0.0;
        if (!s.readInt32u (id) || !s.readDouble (v))
            return false;
        if (id < kNumParams) // (a newer build's parameters are skipped)
        {
            st.norm[id] = std::clamp (v, 0.0, 1.0);
            st.has[id] = true;
        }
    }
    // the end saturator's Gentlr Slope had three choices before Alt Signature: a value saved then is read
    // as the same choice (first: the conversions below set the Slope as it is now)
    if (version < kAltSignature)
        smacheratr::tailSlopeFromThreeChoices (st.norm, st.has, kTailExt3Base);
    // the defaults were the end saturator off, its Gentlr off and Gentlr's Slope 12 / 12: a state saved
    // then keeps them where it lacks them
    if (version < kNewDefaults)
        smacheratr::tailOldDefaults (st.norm, st.has, kTailBase, kTailExtBase, kTailExt3Base);
    return true;
}

} // namespace ciphr
