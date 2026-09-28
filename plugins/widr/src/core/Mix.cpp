#include "Mix.h"

#include <algorithm>
#include <cmath>

namespace widr {

int readPeers (const Registry& reg, const Liveness& live, int selfSlot, int group, Peer* out, int max)
{
    int n = 0;
    for (int i = 0; i < Registry::kSlots && n < max; ++i)
    {
        if (i == selfSlot || !live.alive (i))
            continue;
        const auto& d = reg.slot (i).data;
        if ((int)d.group.load (std::memory_order_relaxed) != group)
            continue;
        Peer& p = out[n++];
        p.id = live.id (i);
        p.slot = i;
        p.group = group;
        p.role = std::clamp ((int)d.role.load (std::memory_order_relaxed), 0, kNumRoles - 1);
        p.width = d.width.load (std::memory_order_relaxed);
        p.space = d.space.load (std::memory_order_relaxed);
        for (int k = 0; k < kBands; ++k)
        {
            p.side[(size_t)k] = d.side[(size_t)k].load (std::memory_order_relaxed);
            p.mid[(size_t)k] = d.mid[(size_t)k].load (std::memory_order_relaxed);
        }
    }
    return n;
}

MixOutcome negotiate (const Peer& self, const Peer* peers, int n, double aware)
{
    MixOutcome o;
    o.peers = n;
    if (n <= 0)
        return o;
    aware = std::clamp (aware, 0.0, 1.0);
    const int role = std::clamp (self.role, 0, kNumRoles - 1);
    // canonical order: by id, so the sums (and their rounding) do not depend on the slot order
    std::array<int, 64> order {};
    const int m = std::min (n, 64);
    for (int i = 0; i < m; ++i)
        order[(size_t)i] = i;
    std::sort (order.begin (), order.begin () + m, [&] (int a, int b) { return peers[a].id < peers[b].id; });

    o.roleScale = (float)(1.0 + aware * (kRoleWidth[role] - 1.0));
    int twinsBefore = 0;
    for (int j = 0; j < m; ++j)
        if (peers[order[(size_t)j]].role == role && peers[order[(size_t)j]].id < self.id)
            ++twinsBefore;
    o.mirror = twinsBefore % 2 ? -1.0f : 1.0f;

    const int mine = rolePriority (role);
    for (int k = 0; k < kBands; ++k)
    {
        double higher = 0.0;
        for (int j = 0; j < m; ++j)
        {
            const Peer& p = peers[order[(size_t)j]];
            if (rolePriority (p.role) > mine)
                higher += p.side[(size_t)k];
        }
        const double share = higher / (higher + self.side[(size_t)k] + 1e-12);
        o.yield[(size_t)k] = (float)(1.0 - aware * share);
    }
    return o;
}

bool MixMember::join ()
{
    if (mySlot >= 0)
        return true;
    myId = reg->newId ();
    mySlot = reg->claim (myId);
    return mySlot >= 0;
}

void MixMember::leave ()
{
    if (mySlot >= 0)
        reg->release (mySlot);
    mySlot = -1;
}

void MixMember::update (Engine& e, int samples)
{
    live.update (*reg, samples, e.sampleRate ());
    if (mySlot < 0)
    {
        e.setMixOutcome (MixOutcome {});
        e.reportMix (0, -1);
        lastPeers = 0;
        return;
    }
    const int group = std::clamp ((int)std::lround (e.param (kGroup)), 1, 8);
    const int role = std::clamp ((int)std::lround (e.param (kRole)), 0, kNumRoles - 1);
    auto& d = reg->slot (mySlot).data;
    d.group.store ((uint32_t)group, std::memory_order_relaxed);
    d.role.store ((uint32_t)role, std::memory_order_relaxed);
    d.width.store ((float)e.effectiveWidth (), std::memory_order_relaxed);
    d.space.store ((float)e.param (kSpace), std::memory_order_relaxed);
    for (int k = 0; k < kBands; ++k)
    {
        d.side[(size_t)k].store (e.sideEnergy ()[(size_t)k], std::memory_order_relaxed);
        d.mid[(size_t)k].store (e.midEnergy ()[(size_t)k], std::memory_order_relaxed);
    }
    reg->beat (mySlot);

    Peer self;
    self.id = myId;
    self.slot = mySlot;
    self.group = group;
    self.role = role;
    self.side = e.desiredSide ();
    self.mid = e.midEnergy ();
    lastPeers = readPeers (*reg, live, mySlot, group, buf.data (), (int)buf.size ());
    e.setMixOutcome (negotiate (self, buf.data (), lastPeers, e.param (kAware)));
    e.reportMix (lastPeers, mySlot);
}

} // namespace widr
