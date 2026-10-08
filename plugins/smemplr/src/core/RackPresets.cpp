#include "RackPresets.h"

#include "Rack.h"

#include <algorithm>

namespace smemplr {

const HostedPlugin* hostedPlugin (int type)
{
    static const HostedPlugin plugins[] = {
        {"Para", "", &para::paramTable (), para::kGentlrIds},
        {"Multidyn", "", &multidyn::paramTable (), multidyn::kGentlrIds},
        {"Smacheratr", "", &smacheratr::paramTable (), smacheratr::kGentlrIds},
        {"Widr", "", &widr::pluginParamTable (), widr::kGentlrIds},
        {"Wubr", "", &wubr::paramTable (), wubr::kGentlrIds},
        {"Levlr", "", &levlr::paramTable (), levlr::kGentlrIds},
        {"Gentlr", "Gently", &gentlr::paramTable (), gentlr::kGentlrIds},
        {"Smoothr", "", &smoothr::paramTable (), smoothr::kGentlrIds},
    };
    switch (type)
    {
        case kFxPara: return &plugins[0];
        case kFxMultidyn: return &plugins[1];
        case kFxSmacheratr: return &plugins[2];
        case kFxWidr: return &plugins[3];
        case kFxWubr: return &plugins[4];
        case kFxLevlr: return &plugins[5];
        case kFxGentlr: return &plugins[6];
        case kFxSmoothr: return &plugins[7];
        default: return nullptr; // Empty, the M/S EQ
    }
}

int64_t slotParamOf (int slot, int type, uint32_t id)
{
    if (slot < 0 || slot >= kRackSlots)
        return -1;
    const int64_t j = fxBlockOf (type, id);
    return j < 0 ? -1 : (int64_t)slotBlockParam (slot, (uint32_t)j);
}

SlotEdits slotEditsFor (int slot, int type, const std::vector<double>& values)
{
    SlotEdits out;
    const HostedPlugin* hp = hostedPlugin (type);
    if (!hp || slot < 0 || slot >= kRackSlots)
        return out;
    const pk::ParamTable& t = *hp->table;
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
    {
        const int64_t id = fxIdAt (type, j);
        if (id < 0 || (uint32_t)id >= t.size ())
            continue;
        const double v = (size_t)id < values.size () ? std::clamp (values[(size_t)id], 0.0, 1.0) : t.defaultNormalized ((uint32_t)id);
        out.emplace_back (slotBlockParam (slot, j), v);
    }
    return out;
}

std::vector<double> pluginDefaults (int type)
{
    std::vector<double> out;
    if (const HostedPlugin* hp = hostedPlugin (type))
        for (uint32_t id = 0; id < hp->table->size (); ++id)
            out.push_back (hp->table->defaultNormalized (id));
    return out;
}

std::vector<double> pluginValuesOf (int slot, int type, const std::function<double (uint32_t)>& norm)
{
    std::vector<double> out = pluginDefaults (type);
    if (slot < 0 || slot >= kRackSlots)
        return out;
    for (uint32_t id = 0; id < out.size (); ++id)
    {
        const int64_t j = fxBlockOf (type, id);
        if (j >= 0 && fxIdAt (type, (uint32_t)j) == (int64_t)id)
            out[id] = std::clamp (norm (slotBlockParam (slot, (uint32_t)j)), 0.0, 1.0);
    }
    return out;
}

std::vector<double> pluginValuesWith (int type, const pk::SettingValues& values)
{
    std::vector<double> out = pluginDefaults (type);
    for (const auto& [id, v] : values)
        if (id < out.size ())
            out[id] = std::clamp (v, 0.0, 1.0);
    return out;
}

std::vector<double> newSlotValues (int type, const std::vector<double>* savedDefault, const pk::GentlrDefaults& switches)
{
    const HostedPlugin* hp = hostedPlugin (type);
    if (!hp)
        return {};
    std::vector<double> out = pluginDefaults (type);
    if (savedDefault)
        for (size_t id = 0; id < out.size () && id < savedDefault->size (); ++id)
            out[id] = std::clamp ((*savedDefault)[id], 0.0, 1.0);
    for (const auto& [id, v] : pk::gentlrDefaultValues (hp->gentlrIds, switches))
        if (id < out.size ())
            out[id] = v;
    return out;
}

} // namespace smemplr
