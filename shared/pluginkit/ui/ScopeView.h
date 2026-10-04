// An oscilloscope of a stereo signal: left bright (text colour), right dim (copper), the 0 dBFS lines
// in cinnabar so clipping and squared-off peaks stand out. Click to change the time span; spans up to
// 50 ms trigger on a rising zero crossing so periodic waves stand still.
#pragma once

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>
#include <vector>

namespace pk {

class ScopeView : public VSTGUI::CView
{
public:
    // Fills the last n samples (oldest first) and returns how many are real.
    using Reader = std::function<int (float* l, float* r, int n)>;
    static constexpr int kSpans = 4;
    static constexpr double kSpanMs[kSpans] = {10.0, 50.0, 200.0, 1000.0};

    ScopeView (const VSTGUI::CRect& r, Reader reader, std::function<double ()> sampleRate, int capacity,
               std::string title = "OUTPUT");
    void draw (VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& e) override;
    void idle () { invalid (); }
    int span () const { return spanIndex; }

private:
    Reader reader;
    std::function<double ()> rate;
    int capacity;
    std::string title;
    int spanIndex = 1;
    std::vector<float> l, r, hi, lo;
};

} // namespace pk
