// One effect's codec (HostedState.h), compiled once per hosted effect (plugins/smemplr/CMakeLists.txt:
// smemplr_hosted_<effect>) with:
//   PK_HOSTED_STATE_H  the effect's plugin/State.h      PK_HOSTED_CIDS_H  its plugin/Cids.h
//   PK_HOSTED_NS       its namespace (para)             PK_HOSTED_CODEC   the function (codecPara)
//   PK_HOSTED_PRESETS  the function that registers its factory presets (generated: cmake/EmbedPresets.cmake)
#include PK_HOSTED_STATE_H
#include PK_HOSTED_CIDS_H

#include "HostedState.h" // (beside this file: the effect's own headers come first on its include path)

#include "public.sdk/source/common/memorystream.h"

#include <memory>

void PK_HOSTED_PRESETS ();

namespace smemplr::hosted {

namespace {
namespace fx = PK_HOSTED_NS;

bool readState (const std::vector<char>& component, std::vector<double>& norm)
{
    if (component.empty ())
        return false;
    Steinberg::MemoryStream ms (const_cast<char*> (component.data ()), (Steinberg::TSize)component.size ());
    auto st = std::make_unique<fx::State> ();
    if (!fx::readState (&ms, *st))
        return false;
    norm.assign (st->norm.begin (), st->norm.end ());
    return true;
}

bool writeState (const std::vector<double>& norm, std::vector<char>& component)
{
    auto st = std::make_unique<fx::State> ();
    for (size_t id = 0; id < st->norm.size (); ++id)
    {
        st->has[id] = id < norm.size ();
        st->norm[id] = st->has[id] ? norm[id] : 0.0;
    }
    Steinberg::MemoryStream ms;
    if (!fx::writeState (&ms, *st))
        return false;
    component.assign (ms.getData (), ms.getData () + ms.getSize ());
    return true;
}
} // namespace

const Codec& PK_HOSTED_CODEC ()
{
    PK_HOSTED_PRESETS ();
    static const Codec c {fx::kProcessorUID, fx::State {}.norm.size (), &readState, &writeState};
    return c;
}

} // namespace smemplr::hosted
