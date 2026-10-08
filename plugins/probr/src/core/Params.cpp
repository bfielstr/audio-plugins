#include "Params.h"

#include <vector>

namespace probr {

using namespace pk;
using namespace pk::make;

const ParamTable& paramTable ()
{
    static const ParamTable t ([] {
        std::vector<ParamInfo> v;
        v.push_back (choice (kRecord, "Record", "Record", {"Off", "Armed"}, kRecordOff));
        v.push_back (choice (kMode, "Mode", "Mode", {"While Playing", "Always"}, kModeWhilePlaying));
        return v;
    }());
    return t;
}

} // namespace probr
