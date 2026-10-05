// The editors' layout model (issue #11), without VSTGUI so the core tests can run it.
//
// An editor declares its panels: rectangles of its content, each with an id, a title and the row of the
// Wide template it belongs to (a Spec). The editor still builds its content as it always has, at fixed
// coordinates; a panel's region is where that content is in the build (the Classic layout, or the
// arranged build an editor makes for the other layouts: smemplr's modulation column splits in two).
//
// The Classic layout (called Default before 0.15) is the editor as built: nothing moves, so it is the same
// as before layouts, pixel for pixel. Every other layout is an Arrangement: rows from top to bottom, each a
// run of columns from left to right, each column a stack of panels (most hold one). EditorBase moves each
// panel's content into a block placed where the arrangement puts it (place ()): a title strip to drag it
// by, then the content, centred in its block both ways (a panel that fills its block is built at the
// block's width instead: Panel::fill).
//
// Rows fill the window: the window is as wide as the widest row at its natural width (at least the Classic
// width: the header's controls keep their room), and every row's columns share that width, each stretched
// in proportion to its natural width (held to its range while that can fill the row). The blocks of a row
// are all as tall as its tallest column (in a stack, the extra height is shared among its blocks). One gap
// (kGap) between blocks and rows, one margin (kMargin) at the window's sides and under the last row.
// A column's natural width is its widest panel's, or more when the user drags its edge (up to twice that):
// that width is kept in the layout text and decides its share of the row.
//
// Layouts as text (saved in the controller's state and in the layouts file):
//   "" , "default" or "classic"           the Classic layout (written as "default", which 0.14 reads too)
//   "wide"                                the Wide template (wide (spec)), the layout a new instance has
//   "waveform/sample,filter:400,env;fx"   rows split by ';', columns by ',', a column's stacked panels by
//                                         '/'; ":<px>" after a column's last panel is its natural width
// A text naming panels the editor does not have (an older or newer version) drops them; panels it does not
// name are added at the end (normalize ()), so every panel is always shown exactly once.
//
// Which layout an instance has: a new instance has the user's default layout (Use as Default Layout, the
// layouts file) or else Wide (kDefaultLayout). A project or preset state that carries the layout field
// (0.14 and later) keeps the layout it saved, "" included (0.14 wrote "" for its Default: the Classic
// layout); a state without the field (from before 0.14) opens in Wide. So "no field" and "explicitly
// Classic" never mix: the first has no text at all, the second always has one ("" or "default").
#pragma once

#include <map>
#include <string>
#include <vector>

namespace pk::layout {

struct Box
{
    double left = 0, top = 0, right = 0, bottom = 0;
    double width () const { return right - left; }
    double height () const { return bottom - top; }
    bool contains (double x, double y) const { return x >= left && x < right && y >= top && y < bottom; }
    bool operator== (const Box& o) const { return left == o.left && top == o.top && right == o.right && bottom == o.bottom; }
};

struct Panel
{
    std::string id;    // short and stable (it is saved): letters, digits and '-'
    std::string title; // the block's title strip in an arranged layout ("": only the grip)
    Box region;        // its content in the build (editor coordinates)
    int row = 0;       // the Wide template's row (by purpose: 0 the instrument or the effect, 1 what follows)
    int stack = -1;    // Wide: panels of a row with the same stack (>= 0) share one column, top to bottom
    bool fill = false; // its content can be built at any width from the region's up: an arranged build makes
                       // it as wide as its block (EditorBase::layoutRegion), so it fills the block
};

struct Spec
{
    double width = 0, height = 0; // the Classic content (the window without the info strip)
    double headerHeight = 34;     // the header band (title, presets, Menu): stays at the top
    double headerSplit = 0;       // header controls from this x move with the window's right edge (0: half the width)
    double top = 40;              // an arranged layout's first row
    std::vector<Panel> panels;    // in the Wide template's order, row by row
    std::string fallback;         // the panel that takes views with no place yet (an empty rectangle: the editor places them later)
    const Panel* find (const std::string& id) const;
    bool empty () const { return panels.empty (); }
};

struct Column
{
    std::vector<std::string> ids; // stacked top to bottom
    double width = 0;             // 0: as wide as its widest panel
};
using Row = std::vector<Column>;
struct Arrangement
{
    std::vector<Row> rows;
    bool empty () const { return rows.empty (); }
    bool operator== (const Arrangement& o) const;
};

// The layout a new instance has when the user has not picked a default of their own, and the text the
// Classic layout is written as.
constexpr const char* kDefaultLayout = "wide";
constexpr const char* kClassicText = "default";

// spacing in an arranged layout
constexpr double kMargin = 8;  // at the window's sides and under the last row
constexpr double kGap = 8;     // between blocks and between rows
constexpr double kStrip = 16;  // a block's title strip (the handle it is dragged by)
constexpr double kEdge = 5;    // a block's right edge, dragged to change its column's width

// ---- text
std::string toString (const Arrangement& a);
Arrangement parse (const std::string& text); // (empty for "", "default", "classic" and "wide": see resolve)
bool isClassic (const std::string& text);    // "", "default" or "classic" (any case)
bool isDefault (const std::string& text);    // (the same: the name before 0.15)
// The name the Layout menu shows a template's text by ("Classic", "Wide"; "" for an arrangement).
std::string templateName (const std::string& text);
// The arrangement a layout text stands for, normalized (empty: the Classic layout).
Arrangement resolve (const Spec& spec, const std::string& text);

// ---- templates
// Wide: the panels by row, each in a column of its own (or its stack's), in the spec's order.
Arrangement wide (const Spec& spec);
// Every panel of the spec exactly once (unknown and repeated ids dropped, missing ones added to the last
// row), no empty columns or rows, widths within their column's range (0 when it is the natural width).
Arrangement normalize (const Spec& spec, Arrangement a);

// ---- geometry
struct Block
{
    std::string id;
    Box box;          // the block: its strip, then its content
    Box content;      // where the panel's region goes (as large as the region, centred in the block under its
                      // strip; a filling panel's as wide as the block)
    int row = 0, column = 0, index = 0;
};
struct Geometry
{
    double width = 0, height = 0; // the content (the window without the info strip)
    std::vector<Block> blocks;
    std::vector<Box> rows;                 // each row's extent (margin to margin, as tall as its tallest column)
    std::vector<std::vector<Box>> columns; // each row's columns (as tall as the row)
    const Block* find (const std::string& id) const;
};
// heights: panels shown shorter than their region now (a folded end saturator), by id.
Geometry place (const Spec& spec, const Arrangement& a, const std::map<std::string, double>& heights = {});
// A column's natural width range: its widest panel .. twice that.
double naturalWidth (const Spec& spec, const Column& c);
double maxWidth (const Spec& spec, const Column& c);

// ---- moving and resizing
struct Drop
{
    enum Kind
    {
        None,
        NewColumn, // a column of its own, before column `column` of row `row` (== the row's count: at its end)
        Stack,     // into column `column` of row `row`, at `index` in its stack
        NewRow,    // a row of its own, before row `row` (== the count: at the bottom)
    };
    Kind kind = None;
    int row = 0, column = 0, index = 0;
    Box mark; // where the drop indicator goes (editor coordinates)
};
// Where a block dragged to (x, y) would land: near a column's sides, a column of its own beside it; over
// its middle, into its stack (above or below the panel there); above the first row or under the last, a
// row of its own.
Drop dropAt (const Arrangement& a, const Geometry& g, double x, double y);
// The panel moved where the drop says (normalized again; unchanged when it is not in the arrangement).
Arrangement move (const Spec& spec, const Arrangement& a, const std::string& id, const Drop& d);
// The column holding the panel shown `width` wide, as near as its natural width's range allows (the
// natural width that, placed, gives it that width: the other columns of its row share what is left).
Arrangement resize (const Spec& spec, const Arrangement& a, const std::string& id, double width);

// ---- saved layouts: a small text file per plug-in beside its presets (<preset folder>/.layouts.txt)
//   # comment
//   name = layout text           one saved layout per line (names keep their case; '=' is not allowed)
//   @default = layout text       the layout a new instance starts with (Use as Default Layout)
struct Named
{
    std::string name, layout;
};
struct Saved
{
    std::vector<Named> layouts;
    std::string defaultLayout; // (when hasDefault) "" or "default": the Classic layout
    bool hasDefault = false;
    const Named* find (const std::string& name) const;
    void put (const std::string& name, const std::string& layout); // adds or replaces (same name, any case)
    bool remove (const std::string& name);
};
Saved parseSaved (const std::string& text);
std::string savedText (const Saved& s);
// A name a user may give a layout: not empty, no '=', no line breaks, not a template's name.
bool validName (const std::string& name);
std::string savedPath (const std::string& presetFolder);
Saved readSaved (const std::string& presetFolder);
bool writeSaved (const std::string& presetFolder, const Saved& s);

} // namespace pk::layout
