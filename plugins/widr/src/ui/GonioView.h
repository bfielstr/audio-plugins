// A goniometer (vectorscope) of the output and a correlation meter under it. In the goniometer the
// mid is vertical and the side horizontal: a mono signal is a vertical line, a wide one a cloud,
// anything wider than the speakers leans towards the horizontal. The meter shows how similar L and
// R are: +1 mono, 0 unrelated, -1 opposite (it cancels in mono).
#pragma once

#include "../core/Engine.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <vector>

namespace widr {

class GonioView : public VSTGUI::CView
{
public:
    static constexpr double kMeter = 58.0; // the correlation meter at the bottom
    static constexpr int kPoints = 1024;

    using MeterSource = std::function<Meters* ()>; // null while there is no audio engine
    GonioView (const VSTGUI::CRect& r, MeterSource meters);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();

private:
    MeterSource meters;
    std::vector<float> l, r;
    int count = 0;
    float correlation = 1.0f, level = 0.1f;
};

} // namespace widr
