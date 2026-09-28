// Mix awareness: every Widr publishes what it does in the process-wide registry
// (pk::InstanceRegistry) and reads the other Widrs of its Group, so a mix full of Widrs shares the
// stereo field instead of stacking width in the same place.
//
// The negotiation is a pure function of the published data, and independent of the order of the
// peers (they are sorted by id before anything is summed), so every instance reaches the same
// view whatever order the host processes them in:
//   - Roles have priority: Anchor > Support > Wide > Ambient. With others in the group the role
//     also pulls the width (Anchor narrower, it claims the centre; Wide and Ambient wider), by
//     Mix Aware.
//   - Per band, where higher-priority instances already generate side energy, this instance gives
//     way in proportion to their share of the side in that band, by up to Mix Aware (0 % ignores
//     the others, 100 % yields fully). Instances only yield to higher priorities, so nothing loops.
//   - Two instances with the same role widen in opposite directions (the later one mirrored), so
//     they spread instead of widening identically.
// The engine smooths what comes out over 200 ms, so nothing pumps.
#pragma once

#include "Bands.h"
#include "Engine.h"
#include "Params.h"

#include "pluginkit/InstanceRegistry.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace widr {

// What one instance publishes (atomics only: see pk::InstanceRegistry).
struct MixSlot
{
    std::atomic<uint32_t> group {1}, role {kSupport};
    std::atomic<float> width {0.0f}, space {0.0f};
    std::array<std::atomic<float>, kBands> side {}; // generated side energy per band (after its own gains)
    std::array<std::atomic<float>, kBands> mid {};  // mid energy per band
    void clear ()
    {
        group.store (1);
        role.store (kSupport);
        width.store (0.0f);
        space.store (0.0f);
        for (auto& v : side)
            v.store (0.0f);
        for (auto& v : mid)
            v.store (0.0f);
    }
};

using Registry = pk::InstanceRegistry<MixSlot, 64>;
using Liveness = pk::Liveness<64>;

// A plain copy of one instance's published data.
struct Peer
{
    uint64_t id = 0;
    int slot = -1, group = 1, role = kSupport;
    float width = 0.0f, space = 0.0f;
    std::array<float, kBands> side {}, mid {};
};

// The live instances of `group` other than `selfSlot`, copied into out[0 .. max); returns how many.
int readPeers (const Registry& reg, const Liveness& live, int selfSlot, int group, Peer* out, int max);

// self.side is what this instance would generate (before yielding); the peers' is what they do.
MixOutcome negotiate (const Peer& self, const Peer* peers, int n, double aware);

// Role multipliers of the width with others around (at Mix Aware 100 %).
constexpr float kRoleWidth[kNumRoles] = {0.4f, 0.75f, 1.0f, 1.15f};
inline int rolePriority (int role) { return kNumRoles - role; } // Anchor highest

// An Engine taking part in a registry: claims a slot, publishes once per block and applies the
// negotiation. The plug-in's processor owns one; the tests use private registries.
class MixMember
{
public:
    explicit MixMember (Registry& r = Registry::global ()) : reg (&r) {}
    ~MixMember () { leave (); }
    MixMember (const MixMember&) = delete;
    MixMember& operator= (const MixMember&) = delete;

    bool join ();  // claims a slot (not on the audio thread); false when the registry is full
    void leave (); // frees it
    int slot () const { return mySlot; }
    uint64_t id () const { return myId; }

    // Once per block, after the engine processed it: publishes the engine's state and hands it the
    // negotiated outcome for the next block.
    void update (Engine& e, int samples);
    int peers () const { return lastPeers; }

private:
    Registry* reg;
    int mySlot = -1;
    uint64_t myId = 0;
    Liveness live;
    std::array<Peer, 64> buf;
    int lastPeers = 0;
};

} // namespace widr
