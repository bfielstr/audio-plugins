#include "TailDisplays.h"

#include "Help.h"

#include "../core/TailExt.h"

#include "vstgui/lib/cviewcontainer.h"

namespace smacheratr {

using namespace VSTGUI;

TailDisplays::TailDisplays (pk::ParamHost* editor, uint32_t b, uint32_t e, ColorView::RateSource r, ColorView::MeterSource m)
    : base (b), extBase (e), rate (std::move (r)), meters (std::move (m))
{
    host = std::make_unique<pk::MappedParamHost> (editor, paramTable (), [b, e] (uint32_t id) -> int64_t {
        const int f = tailFieldOf (id);
        if (f < 0)
            return -1;
        return f < (int)pk::kTailFields ? (int64_t)(b + (uint32_t)f) : (int64_t)(e + (uint32_t)(f - pk::kTailFields));
    });
}

void TailDisplays::add (CViewContainer* parent, const CRect& area)
{
    const double w = std::min (230.0, area.getWidth () * 0.34);
    shaper = new ShaperView (CRect (area.left, area.top, area.left + w, area.bottom), host.get (), meters);
    shaper->setTooltipText (help::kShaperDisplay);
    parent->addView (shaper);
    color = new ColorView (CRect (area.left + w + 8, area.top, area.right, area.bottom), host.get (), rate, meters);
    color->setTooltipText (help::kColorDisplay);
    color->onBandPicked = [this] (int k) {
        if (bandPicked)
            bandPicked (k);
    };
    parent->addView (color);
}

void TailDisplays::idle ()
{
    if (shaper)
        shaper->idle ();
    if (color)
        color->idle ();
}

bool TailDisplays::isTailParam (uint32_t id) const
{
    return (id >= base && id < base + pk::kTailFields) || (id >= extBase && id < extBase + pk::kTailExtFields);
}

void TailDisplays::paramChanged (uint32_t id)
{
    if (!isTailParam (id))
        return;
    if (shaper)
        shaper->invalid ();
    if (color)
        color->invalid ();
}

void TailDisplays::onBandPicked (std::function<void (int)> f) { bandPicked = std::move (f); }

void TailDisplays::closed ()
{
    shaper = nullptr;
    color = nullptr;
}

} // namespace smacheratr
