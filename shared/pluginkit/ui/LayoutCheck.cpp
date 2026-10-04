#include "pluginkit/ui/LayoutCheck.h"

#include "pluginkit/ui/HelpText.h"
#include "pluginkit/ui/InfoBox.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cstring.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/platform/iplatformfont.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <typeinfo>
#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>
#include <cxxabi.h>
#endif

namespace pk {

using namespace VSTGUI;

std::string typeName (const std::type_info& t)
{
    const char* raw = t.name ();
    std::string name = raw;
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    if (char* d = abi::__cxa_demangle (raw, nullptr, nullptr, &status))
    {
        name = d;
        std::free (d);
    }
#endif
    const size_t colons = name.rfind ("::");
    return colons == std::string::npos ? name : name.substr (colons + 2);
}

namespace {

// The width of `s` in the theme's font: measured by the platform's font (no drawing context needed on
// macOS, Windows or Linux), or estimated when there is none.
double textWidth (const std::string& s, double size, bool bold)
{
    auto font = theme::font (size, bold);
    if (auto pf = font->getPlatformFont ())
        if (auto* painter = pf->getPainter ())
        {
            UTF8String str (s.c_str ());
            if (auto* ps = str.getPlatformString ())
                return painter->getStringWidth (nullptr, ps, true);
        }
    return (bold ? 0.62 : 0.56) * size * (double)helptext::chars (s);
}

std::vector<TextSpot> spotsOf (CView* v)
{
    if (auto* k = dynamic_cast<Knob*> (v))
        return k->textSpots ();
    if (auto* x = dynamic_cast<HSlider*> (v))
        return x->textSpots ();
    if (auto* x = dynamic_cast<NumberBox*> (v))
        return x->textSpots ();
    if (auto* x = dynamic_cast<Toggle*> (v))
        return x->textSpots ();
    if (auto* x = dynamic_cast<Segmented*> (v))
        return x->textSpots ();
    if (auto* x = dynamic_cast<ViewSwitch*> (v))
        return x->textSpots ();
    if (auto* x = dynamic_cast<Choice*> (v))
        return x->textSpots ();
    if (auto* x = dynamic_cast<ActionButton*> (v))
        return x->textSpots ();
    if (auto* x = dynamic_cast<Label*> (v))
        return x->textSpots ();
    return {};
}

std::string nameOf (CView* v)
{
    if (auto* pv = dynamic_cast<ParamView*> (v))
        return pv->paramName ();
    if (auto* l = dynamic_cast<Label*> (v))
        return "\"" + l->getText () + "\"";
    if (auto* b = dynamic_cast<ActionButton*> (v))
        return "\"" + b->label () + "\"";
    const HelpInfo h = helpFor (v);
    return !h.title.empty () ? h.title : h.text.substr (0, 32);
}

std::string rectText (const CRect& r)
{
    char buf[80];
    std::snprintf (buf, sizeof (buf), "[%g %g %g %g]", r.left, r.top, r.right, r.bottom);
    return buf;
}

struct Found
{
    helptext::Box box; // the view's rectangle grown to the ink of its texts
    bool label = false;
};

// The visible leaves under `v` in window coordinates (offset by ox, oy), each grown to its texts'
// ink; texts wider than the room their widget keeps for them go to `spills`.
void collect (CView* v, double ox, double oy, const CRect& rootSize, std::vector<Found>& out, std::vector<std::string>& spills)
{
    if (!v->isVisible ())
        return;
    CRect r = v->getViewSize ();
    r.offset (ox, oy);
    if (auto* c = v->asViewContainer ())
    {
        if (auto* p = dynamic_cast<Panel*> (c); p && !p->titleText ().empty ())
        {
            std::string t = p->titleText ();
            for (auto& ch : t)
                ch = (char)std::toupper ((unsigned char)ch);
            const double w = textWidth (t, 9.5, true);
            const CRect tr (r.left + 8, r.top + 3, r.left + 8 + w, r.top + 18);
            out.push_back ({{tr.left, tr.top, tr.right, tr.bottom, "Panel title \"" + t + "\" " + rectText (tr)}, true});
        }
        c->forEachChild ([&] (CView* child) { collect (child, r.left, r.top, rootSize, out, spills); });
        return;
    }
    // overlays over the whole window (Smemplr's modulation layer) cover everything by design
    if (r.getWidth () >= rootSize.getWidth () * 0.9 && r.getHeight () >= rootSize.getHeight () * 0.9)
        return;
    const bool isLabel = dynamic_cast<Label*> (v) != nullptr;
    const std::string name = typeName (typeid (*v)) + " " + nameOf (v);
    // a label counts only where its text is (its rectangle is often generous), a knob where its dial
    // and its texts are (the corners of its rectangle are empty); other views count with their whole
    // rectangle, grown to any text that reaches out of it
    CRect ink = isLabel ? CRect () : r;
    bool any = !isLabel;
    if (auto* k = dynamic_cast<Knob*> (v))
    {
        ink = k->dialRect ();
        ink.offset (r.left - v->getViewSize ().left, r.top - v->getViewSize ().top);
    }
    double worstOver = 0;
    std::string worstText;
    for (const auto& s : spotsOf (v))
    {
        if (s.text.empty ())
            continue;
        CRect a = s.area;
        a.offset (r.left - v->getViewSize ().left, r.top - v->getViewSize ().top);
        const double w = textWidth (s.text, s.size, s.bold);
        const double room = a.getWidth () - s.pad;
        if (w - room > worstOver)
        {
            worstOver = w - room;
            worstText = s.text;
        }
        double l = a.left + s.pad / 2, rt = l + w;
        if (s.align == 1)
            l = a.getCenter ().x - w / 2, rt = l + w;
        else if (s.align == 2)
            rt = a.right - s.pad / 2, l = rt - w;
        const CRect t (l, a.top, rt, a.bottom);
        ink = any ? CRect (std::min (ink.left, t.left), std::min (ink.top, t.top), std::max (ink.right, t.right), std::max (ink.bottom, t.bottom)) : t;
        any = true;
    }
    if (worstOver > 0.5)
    {
        char buf[48];
        std::snprintf (buf, sizeof (buf), " needs %.0f px more", worstOver);
        spills.push_back ("spill:   " + name + " " + rectText (r) + ": \"" + worstText + "\"" + buf);
    }
    if (any)
        out.push_back ({{ink.left, ink.top, ink.right, ink.bottom, name + " " + rectText (r)}, isLabel});
}

} // namespace

std::vector<std::string> layoutReport (CView* root)
{
    if (!root)
        return {};
    std::vector<Found> found;
    std::vector<std::string> lines;
    collect (root, -root->getViewSize ().left, -root->getViewSize ().top, root->getViewSize (), found, lines);
    std::vector<helptext::Box> boxes;
    for (const auto& f : found)
        boxes.push_back (f.box);
    for (const auto& [i, j] : helptext::overlaps (boxes, 1.0))
    {
        const auto &a = boxes[i], &b = boxes[j];
        const bool shared = a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
        // a label's text touching something is how labels sit over their controls: only overlaps count
        if (!shared && (found[i].label || found[j].label))
            continue;
        // a control wholly inside another view sits on it by design (a readout or a switch placed on a
        // display): only partial overlaps are clutter
        auto inside = [] (const helptext::Box& in, const helptext::Box& out) {
            return in.left >= out.left && in.right <= out.right && in.top >= out.top && in.bottom <= out.bottom;
        };
        if (inside (a, b) || inside (b, a))
            continue;
        lines.push_back (std::string (shared ? "overlap: " : "touch:   ") + a.name + "  x  " + b.name);
    }
    return lines;
}

} // namespace pk
