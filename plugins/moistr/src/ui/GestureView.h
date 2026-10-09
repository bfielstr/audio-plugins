// Moistr's gesture display (0.30): the one gesture's lanes stacked, one row per lane (its target named at the
// left, its curve across the gesture, 0 at the bottom of its row and 1 at the top), the beat grid of the length it
// plays at, and while it runs one playhead through every lane (they share one clock) with a dot at each lane's
// value now. The title names the gesture; the footer its mode and length, and whether 0.27 slots (from an older
// project) play beside it.
//
// It repaints only when what it shows changed: the gesture, its settings, or the playhead (in small steps); with
// Gesture None it asks for no repaints. (Loop Lock's window and region: LoopView.)
#pragma once

#include "Engine.h"
#include "Params.h"

#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cview.h"

#include <array>
#include <functional>
#include <string>

namespace moistr {

class GestureView : public VSTGUI::CView
{
public:
    using MeterSource = std::function<const Meters* ()>;
    using UserSource = std::function<const Scene* ()>; // the user gesture as the engine plays it (nullptr: none)
    using NameSource = std::function<std::string ()>;  // the user gesture's name
    GestureView (const VSTGUI::CRect& r, pk::ParamHost* host, MeterSource meters, UserSource user, NameSource userName);
    void draw (VSTGUI::CDrawContext* ctx) override;
    void idle ();
    // a short note in its footer (a gesture file that could not be read) until the gesture changes
    void setNote (const std::string& text)
    {
        note = text;
        noteFor = shown;
        invalid ();
    }

    // the gesture the Gesture choice picks (nullptr: None, or User with none loaded)
    static const Scene* sceneOf (pk::ParamHost* host, const Scene* user);
    // whether any 0.27 slot plays (its Target is not Off)
    static bool slotsOn (pk::ParamHost* host);

private:
    struct Now
    {
        int choice = 0;
        const Scene* user = nullptr;
        bool slots = false;
        double pos = -1.0, pull = 0.0; // (rounded: the playhead's steps)
        std::array<float, kMaxSceneLanes> value {};
        bool operator== (const Now& o) const = default;
    };
    Now now () const;
    pk::ParamHost* host;
    MeterSource meters;
    UserSource user;
    NameSource userName;
    Now shown, noteFor;
    std::string note;
};

} // namespace moistr
