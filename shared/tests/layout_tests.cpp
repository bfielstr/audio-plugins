// Headless tests for the layout model (pluginkit/Layout.h, issue #11): layout texts, the Wide template,
// placing blocks (rows that fill the window, blocks of a row as tall as it, content centred, one gap
// everywhere), dragging them to other places and widths, and the saved layouts file.
// Run: ./pluginkit_layout_tests [filter]
#include "pluginkit/Layout.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace pk::layout;

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        ++gChecks;                                                         \
        if (!(cond))                                                       \
        {                                                                  \
            ++gFailures;                                                   \
            std::printf ("    FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
        }                                                                  \
    } while (0)

struct TestCase
{
    const char* name;
    std::function<void ()> fn;
};
static std::vector<TestCase>& tests ()
{
    static std::vector<TestCase> t;
    return t;
}
struct Reg
{
    Reg (const char* n, std::function<void ()> f) { tests ().push_back ({n, std::move (f)}); }
};
#define TEST(name)                     \
    static void name ();               \
    static Reg reg_##name (#name, name); \
    static void name ()

// An editor like smemplr's, smaller: a display over a row of panels, a column at the right, the rack
// under them. Row 0 holds the instrument (the display and the sample row stacked), row 1 what follows.
static Spec testSpec ()
{
    Spec s;
    s.width = 800;
    s.height = 600;
    s.panels = {
        {"display", "DISPLAY", {8, 38, 600, 200}, 0, 0},
        {"sample", "", {8, 206, 600, 280}, 0, 0},
        {"filter", "", {8, 286, 200, 450}, 0},
        {"env", "", {206, 286, 400, 450}, 0},
        {"rack", "RACK", {8, 456, 600, 592}, 1},
        {"mod", "", {606, 38, 792, 200}, 1},
    };
    return s;
}

static std::map<std::string, int> countIds (const Arrangement& a)
{
    std::map<std::string, int> n;
    for (const auto& row : a.rows)
        for (const auto& col : row)
            for (const auto& id : col.ids)
                ++n[id];
    return n;
}

static bool everyPanelOnce (const Spec& s, const Arrangement& a)
{
    const auto n = countIds (a);
    if (n.size () != s.panels.size ())
        return false;
    for (const auto& p : s.panels)
        if (auto it = n.find (p.id); it == n.end () || it->second != 1)
            return false;
    return true;
}

static bool overlaps (const Box& a, const Box& b) { return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom; }

TEST (textRoundTrip)
{
    for (const char* t : {"display/sample,filter,env;rack,mod", "a", "a,b:300;c/d/e", "display/sample:640,filter;rack"})
        CHECK (toString (parse (t)) == t, "%s -> %s", t, toString (parse (t)).c_str ());
    // spaces and empty parts are tolerated
    CHECK (toString (parse (" a , b ;; c/ /d ")) == "a,b;c/d", "%s", toString (parse (" a , b ;; c/ /d ")).c_str ());
    // the templates' names and the empty text are not arrangements
    CHECK (parse ("").empty () && parse ("default").empty () && parse ("Default").empty () && parse ("classic").empty () && parse ("wide").empty (),
           "names");
    // the Classic layout: "" (0.14's Default), "default" (how it is written) and "classic"
    CHECK (isClassic ("") && isClassic (" Default ") && isClassic ("Classic") && !isClassic ("wide") && !isClassic ("a,b"), "isClassic");
    CHECK (isClassic (kClassicText) && std::string (kDefaultLayout) == "wide", "the texts");
    CHECK (templateName ("") == "Classic" && templateName ("default") == "Classic" && templateName ("Wide") == "Wide" && templateName ("a,b").empty (),
           "template names");
}

TEST (classicLayoutIsTheBuild)
{
    // the Classic layout is no arrangement at all: the editor's content stays where it was built (the
    // draw benchmark compares every editor's Classic rendering with one from before layouts)
    const Spec s = testSpec ();
    CHECK (resolve (s, "").empty () && resolve (s, "default").empty () && resolve (s, "classic").empty (), "Classic resolves to nothing to move");
    // an editor without panels has only the Classic layout, whatever its state says
    CHECK (resolve (Spec {}, "wide").empty () && resolve (Spec {}, "a,b").empty (), "no panels: Classic");
    // and the regions an editor declares do not overlap (each control belongs to one panel)
    for (size_t i = 0; i < s.panels.size (); ++i)
        for (size_t j = i + 1; j < s.panels.size (); ++j)
            CHECK (!overlaps (s.panels[i].region, s.panels[j].region), "%s x %s", s.panels[i].id.c_str (), s.panels[j].id.c_str ());
}

TEST (wideHasEveryPanelOnceByRow)
{
    const Spec s = testSpec ();
    const Arrangement w = wide (s);
    CHECK (everyPanelOnce (s, w), "every panel exactly once");
    CHECK (toString (w) == "display/sample,filter,env;rack,mod", "%s", toString (w).c_str ());
    CHECK (resolve (s, "wide") == w && resolve (s, "Wide") == w, "the text \"wide\" is the template");
    // the rows by purpose, whatever order the spec lists them in
    Spec r = s;
    std::swap (r.panels[0], r.panels[4]);
    const Arrangement w2 = wide (r);
    CHECK (w2.rows.size () == 2 && w2.rows[1].size () == 2 && (w2.rows[0].front ().ids == std::vector<std::string> {"sample", "display"}), "%s", toString (w2).c_str ());
}

TEST (normalizeKeepsEveryPanelOnce)
{
    const Spec s = testSpec ();
    // unknown and repeated ids dropped, missing ones added at the end of the last row
    const Arrangement a = normalize (s, parse ("ghost,display;env,display/filter"));
    CHECK (everyPanelOnce (s, a), "%s", toString (a).c_str ());
    CHECK (toString (a) == "display;env,filter,sample,rack,mod", "%s", toString (a).c_str ());
    // a layout text from a version with other panels still shows all of this one's
    CHECK (everyPanelOnce (s, resolve (s, "x/y;z")), "%s", toString (resolve (s, "x/y;z")).c_str ());
    // widths: the natural width is 0, held to twice the widest panel
    const Arrangement b = normalize (s, parse ("display:10,filter:192,env:5000;rack,mod,sample"));
    CHECK (b.rows[0][0].width == 0 && b.rows[0][1].width == 0 && b.rows[0][2].width == 388, "%s", toString (b).c_str ());
}

// The rules every arranged layout's geometry keeps: each row from margin to margin with one gap between
// its columns, its columns and their blocks as tall as the row, each block's content centred in it under
// its strip (a filling panel as wide as its block), nothing overlapping, and the window around the rows.
static void checkTidy (const Spec& s, const Arrangement& a, const Geometry& g, const char* what)
{
    CHECK (g.rows.size () == a.rows.size (), "%s: rows", what);
    double y = s.top;
    for (size_t r = 0; r < g.rows.size (); ++r)
    {
        const Box& row = g.rows[r];
        CHECK (row.left == kMargin && row.right == g.width - kMargin, "%s: row %zu from margin to margin", what, r);
        CHECK (row.top == y, "%s: row %zu one gap under the last", what, r);
        y = row.bottom + kGap;
        const auto& cols = g.columns[r];
        CHECK (!cols.empty () && cols.front ().left == row.left && cols.back ().right == row.right, "%s: row %zu filled", what, r);
        for (size_t c = 0; c < cols.size (); ++c)
        {
            CHECK (cols[c].top == row.top && cols[c].bottom == row.bottom, "%s: row %zu column %zu as tall as the row", what, r, c);
            CHECK (cols[c].width () == std::round (cols[c].width ()), "%s: whole pixels", what);
            if (c > 0)
                CHECK (cols[c].left - cols[c - 1].right == kGap, "%s: row %zu one gap between columns", what, r);
        }
    }
    CHECK (g.height == (g.rows.empty () ? s.top : g.rows.back ().bottom) + kMargin, "%s: the window under the last row", what);
    CHECK (g.width >= s.width, "%s: never narrower than Classic", what);
    for (const auto& b : g.blocks)
    {
        const Panel* p = s.find (b.id);
        const Box& col = g.columns[(size_t)b.row][(size_t)b.column];
        CHECK (b.box.left == col.left && b.box.right == col.right, "%s: %s as wide as its column", what, b.id.c_str ());
        CHECK (b.box.top >= col.top && b.box.bottom <= col.bottom, "%s: %s in its column", what, b.id.c_str ());
        // the content: the region's size (a filling panel: the block's width), centred both ways under the strip
        CHECK (b.content.height () <= p->region.height () && b.content.width () == (p->fill ? b.box.width () : p->region.width ()), "%s: %s content size",
               what, b.id.c_str ());
        const double l = b.content.left - b.box.left, rr = b.box.right - b.content.right;
        const double t = b.content.top - (b.box.top + kStrip), bt = b.box.bottom - b.content.bottom;
        CHECK (l >= 0 && rr >= 0 && std::fabs (l - rr) <= 1, "%s: %s centred across (%g, %g)", what, b.id.c_str (), l, rr);
        CHECK (t >= 0 && bt >= 0 && std::fabs (t - bt) <= 1, "%s: %s centred down (%g, %g)", what, b.id.c_str (), t, bt);
        for (const auto& o : g.blocks)
            if (&o != &b)
                CHECK (!overlaps (b.box, o.box), "%s: %s x %s", what, b.id.c_str (), o.id.c_str ());
    }
    // the last block of each column reaches the row's bottom, the others one gap over the next
    for (const auto& b : g.blocks)
    {
        const Block* next = nullptr;
        for (const auto& o : g.blocks)
            if (o.row == b.row && o.column == b.column && o.index == b.index + 1)
                next = &o;
        CHECK (next ? next->box.top - b.box.bottom == kGap : b.box.bottom == g.rows[(size_t)b.row].bottom, "%s: %s stacked", what, b.id.c_str ());
    }
}

TEST (placeBlocks)
{
    const Spec s = testSpec ();
    const Arrangement w = wide (s);
    const Geometry g = place (s, w);
    CHECK (g.blocks.size () == s.panels.size (), "%zu blocks", g.blocks.size ());
    checkTidy (s, w, g, "wide");
    // the first row: the display and the sample row stacked, the filter and envelope beside them
    const Block* d = g.find ("display");
    const Block* sm = g.find ("sample");
    const Block* f = g.find ("filter");
    CHECK (d && sm && f && sm->box.top == d->box.bottom + kGap && f->box.left == d->box.right + kGap && f->box.top == d->box.top, "stack");
    // the widest row at its natural width sets the window: 592 + 192 + 194 and two gaps, and the margins
    CHECK (g.width == 592 + 192 + 194 + 2 * kGap + 2 * kMargin, "window %g", g.width);
    CHECK (f->box.width () == 192 && g.find ("env")->box.width () == 194 && d->box.width () == 592, "the widest row as it is");
    // wide and short: wider than Classic, not taller
    CHECK (g.width > s.width && g.height <= s.height, "%g x %g", g.width, g.height);
    // the second row stretched to the first one's width, its columns in proportion to their natural widths
    const Block* rack = g.find ("rack");
    const Block* mod = g.find ("mod");
    CHECK (rack->box.top == g.rows[0].bottom + kGap, "row 2");
    CHECK (rack->box.width () + mod->box.width () + kGap == g.width - 2 * kMargin, "row 2 fills");
    CHECK (std::fabs (rack->box.width () / mod->box.width () - 592.0 / 186.0) < 0.02, "in proportion (%g, %g)", rack->box.width (), mod->box.width ());
    // the blocks of a row as tall as it: the filter (164) beside the display/sample stack (276 with strips)
    CHECK (f->box.height () == g.rows[0].height () && f->box.height () == 2 * kStrip + 162 + 74 + kGap, "filter %g", f->box.height ());
    CHECK (mod->box.height () == rack->box.height (), "row 2 heights");
    // a panel shown shorter (a folded section) moves what is under it up
    const Arrangement fa = parse ("filter;rack;display/sample,env,mod");
    const Geometry folded = place (s, fa, {{"rack", 40.0}});
    CHECK (folded.find ("rack")->box.height () == kStrip + 40 && folded.find ("display")->box.top == folded.find ("rack")->box.bottom + kGap,
           "folded");
    checkTidy (s, fa, folded, "folded");
    // never narrower than Classic (the header's controls keep their room): the rows stretched to it
    const Arrangement narrow = parse ("filter;env;display;sample;rack;mod");
    const Geometry ng = place (s, narrow);
    CHECK (ng.width == s.width && ng.find ("filter")->box.width () == s.width - 2 * kMargin, "min width");
    checkTidy (s, narrow, ng, "narrow");
}

TEST (rowsFillEvenly)
{
    const Spec s = testSpec ();
    // columns past their range's end: the others take the rest; when every one is at its end, past them
    const Arrangement a = parse ("display,rack;filter,env,mod");
    const Geometry g = place (s, a);
    checkTidy (s, a, g, "two rows");
    CHECK (g.width == 592 + 592 + kGap + 2 * kMargin, "%g", g.width);
    const double row2 = g.find ("filter")->box.width () + g.find ("env")->box.width () + g.find ("mod")->box.width () + 2 * kGap;
    CHECK (row2 == g.width - 2 * kMargin, "row 2 fills (%g)", row2);
    // a column's natural width (dragged wider) is its share's weight
    const Arrangement b = parse ("display,rack;filter:300,env,mod");
    const Geometry gb = place (s, b);
    checkTidy (s, b, gb, "weighted");
    CHECK (gb.find ("filter")->box.width () > g.find ("filter")->box.width (), "a wider column takes more");
    // a filling panel: its content as wide as its block, wherever it is
    Spec f = s;
    for (auto& p : f.panels)
        if (p.id == "rack")
            p.fill = true;
    for (const char* text : {"wide", "display,rack;filter,env,mod", "rack;display/sample,filter,env,mod"})
    {
        const Arrangement fa = resolve (f, text);
        const Geometry fg = place (f, fa);
        checkTidy (f, fa, fg, text);
        const Block* r = fg.find ("rack");
        CHECK (r->content.left == r->box.left && r->content.right == r->box.right, "%s: rack fills", text);
    }
}

TEST (dragToReorder)
{
    const Spec s = testSpec ();
    const Arrangement w = wide (s);
    const Geometry g = place (s, w);
    const Block* env = g.find ("env");
    const Block* filter = g.find ("filter");
    // dropped on the filter's left side: a column of its own before it
    Drop d = dropAt (w, g, filter->box.left + 4, filter->box.top + 30);
    CHECK (d.kind == Drop::NewColumn && d.row == 0 && d.column == 1, "kind %d row %d column %d", (int)d.kind, d.row, d.column);
    CHECK (d.mark.width () <= 2 && d.mark.top == g.rows[0].top, "a vertical mark");
    Arrangement m = move (s, w, "env", d);
    CHECK (toString (m) == "display/sample,env,filter;rack,mod", "%s", toString (m).c_str ());
    CHECK (everyPanelOnce (s, m), "once");
    // over the middle of the filter, low: stacked under it
    d = dropAt (w, g, (filter->box.left + filter->box.right) / 2, filter->box.bottom - 4);
    CHECK (d.kind == Drop::Stack && d.column == 1 && d.index == 1, "kind %d column %d index %d", (int)d.kind, d.column, d.index);
    CHECK (d.mark.height () <= 2, "a horizontal mark");
    m = move (s, w, "env", d);
    CHECK (toString (m) == "display/sample,filter/env;rack,mod", "%s", toString (m).c_str ());
    // between rows: into the second row, at its end
    const Block* mod = g.find ("mod");
    d = dropAt (w, g, mod->box.right + 30, mod->box.top + 10);
    CHECK (d.kind == Drop::NewColumn && d.row == 1 && d.column == 2, "kind %d row %d column %d", (int)d.kind, d.row, d.column);
    m = move (s, w, "env", d);
    CHECK (toString (m) == "display/sample,filter;rack,mod,env", "%s", toString (m).c_str ());
    // under the last row: a row of its own
    d = dropAt (w, g, 100, g.rows.back ().bottom + 20);
    CHECK (d.kind == Drop::NewRow && d.row == 2, "kind %d row %d", (int)d.kind, d.row);
    m = move (s, w, "display", d);
    CHECK (toString (m) == "sample,filter,env;rack,mod;display", "%s", toString (m).c_str ());
    // above the first row: a row of its own at the top
    d = dropAt (w, g, 100, s.top - 6);
    CHECK (d.kind == Drop::NewRow && d.row == 0, "kind %d row %d", (int)d.kind, d.row);
    // dropped where it is: nothing changes
    d = dropAt (w, g, (env->box.left + env->box.right) / 2, env->box.top + 4);
    CHECK (move (s, w, "env", d) == w, "%s", toString (move (s, w, "env", d)).c_str ());
    // a panel that is not there, or no drop: unchanged
    CHECK (move (s, w, "ghost", d) == w && move (s, w, "env", Drop {}) == w, "no-op");
    // a column moved alone keeps its width
    const Arrangement wide2 = resize (s, w, "filter", 300);
    const Geometry g2 = place (s, wide2);
    d = dropAt (wide2, g2, g2.find ("mod")->box.right + 30, g2.find ("mod")->box.top + 10);
    CHECK (toString (move (s, wide2, "filter", d)) == "display/sample,env;rack,mod,filter:300", "%s", toString (move (s, wide2, "filter", d)).c_str ());
}

TEST (resizeWithinLimits)
{
    const Spec s = testSpec ();
    const Arrangement w = wide (s);
    // the filter is 192 wide: 192 .. 384
    CHECK (toString (resize (s, w, "filter", 250)) == "display/sample,filter:250,env;rack,mod", "%s", toString (resize (s, w, "filter", 250)).c_str ());
    CHECK (resize (s, w, "filter", 100) == w, "no narrower than its panel");
    CHECK (toString (resize (s, w, "filter", 1000)) == "display/sample,filter:384,env;rack,mod", "held to twice");
    // a stacked column: its widest panel's range
    CHECK (toString (resize (s, w, "sample", 700)) == "display/sample:700,filter,env;rack,mod", "%s", toString (resize (s, w, "sample", 700)).c_str ());
    // the width travels in the text
    const Arrangement r = resize (s, w, "env", 300);
    CHECK (resolve (s, toString (r)) == r, "round trip with widths");
    CHECK (place (s, r).find ("env")->box.width () == 300, "placed at its width");
    // in a stretched row the width asked for is the width shown: its natural width is what gives it that
    const double shownNow = place (s, w).find ("mod")->box.width ();
    const Arrangement m = resize (s, w, "mod", 300);
    CHECK (std::fabs (place (s, m).find ("mod")->box.width () - 300) <= 1, "mod shown %g", place (s, m).find ("mod")->box.width ());
    CHECK (place (s, m).width == place (s, w).width, "the window as it was (its row is not the widest)");
    // narrower than it is shown at its natural width: as narrow as it goes (its natural width)
    CHECK (resize (s, w, "mod", shownNow - 20) == w, "no narrower than its share");
}

TEST (savedLayouts)
{
    Saved s;
    s.put ("Mixing", "a,b;c");
    s.put ("Tracking", "c;a,b");
    s.put ("mixing", "b,a;c"); // the same name in another case replaces it
    CHECK (s.layouts.size () == 2 && s.find ("MIXING") && s.find ("MIXING")->layout == "b,a;c", "put");
    s.defaultLayout = "wide";
    s.hasDefault = true;
    const Saved back = parseSaved (savedText (s));
    CHECK (back.layouts.size () == 2 && back.layouts[0].name == "mixing" && back.layouts[1].layout == "c;a,b" && back.hasDefault &&
               back.defaultLayout == "wide",
           "%s", savedText (back).c_str ());
    CHECK (back.find ("tracking") && !back.find ("nothing"), "find");
    Saved r = back;
    CHECK (r.remove ("Tracking") && !r.remove ("Tracking") && r.layouts.size () == 1, "remove");
    // comments, blank and broken lines, and names a user may not give are skipped
    const Saved p = parseSaved ("# c\n\nno equals\n Wide = a\nDefault = b\n@other = c\n ok = a,b \n");
    CHECK (p.layouts.size () == 1 && p.layouts[0].name == "ok" && p.layouts[0].layout == "a,b" && !p.hasDefault, "%s", savedText (p).c_str ());
    CHECK (validName ("My layout") && !validName ("") && !validName ("  ") && !validName ("a=b") && !validName ("wide") && !validName ("DEFAULT") &&
               !validName ("Classic") &&
               !validName ("@x") && !validName ("two\nlines"),
           "names");
    // the file beside the presets
    const auto dir = std::filesystem::temp_directory_path () / ("pk-layout-test-" + std::to_string ((long)std::rand ()));
    CHECK (readSaved (dir.string ()).layouts.empty (), "no file yet: none");
    CHECK (writeSaved (dir.string (), back), "write");
    const Saved f = readSaved (dir.string ());
    CHECK (f.layouts.size () == 2 && f.defaultLayout == "wide", "read back");
    CHECK (savedPath (dir.string ()).find (".layouts.txt") != std::string::npos && savedPath ("").empty (), "path");
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
}

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : tests ())
    {
        if (filter && std::string (t.name).find (filter) == std::string::npos)
            continue;
        const int before = gFailures;
        std::printf ("%s\n", t.name);
        t.fn ();
        std::printf ("  %s\n", gFailures == before ? "ok" : "FAILED");
        ++ran;
    }
    std::printf ("\n%d tests, %d checks, %d failures\n", ran, gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
