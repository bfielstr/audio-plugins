// Moistr's gesture display: the picked slot's curve over its whole gesture (0 .. 1 across, its value up), the
// beat grid of the length it plays at, and while it runs the playhead (where in the gesture it is) with a dot
// at the value the target is pulled towards. The title names the slot, its gesture and its target.
//
// It repaints only when what it shows changed: the slot, its settings, the gesture, or the playhead (in small
// steps); with the slot's Target Off it asks for no repaints.
#pragma once

#include "Engine.h"
#include "GestureFile.h"
#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace moistr {

class GestureView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    using SlotSource = std::function<int ()>;                  // the picked slot
    using UserSource = std::function<const GestureData* (int)>; // a slot's user gesture (nullptr: none)
    GestureView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters, SlotSource slot, UserSource user);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();
    // a short note in its footer (a gesture file that could not be read) until the slot or its gesture changes
    void setNote (const std::string& text)
    {
        note = text;
        noteFor = shown;
        invalid ();
    }

    // the curve the picked slot plays (a factory gesture's points, or its user gesture's) and its name
    static GestureData curveOf (pk::ParamHost* host, int slot, const GestureData* user);

private:
    struct Now
    {
        int slot = 0, target = 0, choice = 0;
        double pos = -1.0, value = 0.0, pull = 0.0; // (rounded: the playhead's steps)
        size_t userPoints = 0;
        bool operator== (const Now& o) const = default;
    };
    Now now () const;
    pk::ParamHost* host;
    MeterSource meters;
    SlotSource slot;
    UserSource user;
    Now shown, noteFor;
    std::string note;
};

} // namespace moistr
