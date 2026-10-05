#include "pluginkit/Layout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace pk::layout {

namespace fs = std::filesystem;

namespace {

std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size ();
    while (a < b && std::isspace ((unsigned char)s[a]))
        ++a;
    while (b > a && std::isspace ((unsigned char)s[b - 1]))
        --b;
    return s.substr (a, b - a);
}

std::vector<std::string> split (const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
        if (c == sep)
        {
            out.push_back (cur);
            cur.clear ();
        }
        else
            cur += c;
    out.push_back (cur);
    return out;
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char)std::tolower ((unsigned char)c);
    return s;
}

const char* const kToken = "\x01"; // a moved panel's new place, while the old one is taken out
} // namespace

const Panel* Spec::find (const std::string& id) const
{
    for (const auto& p : panels)
        if (p.id == id)
            return &p;
    return nullptr;
}

bool Arrangement::operator== (const Arrangement& o) const
{
    if (rows.size () != o.rows.size ())
        return false;
    for (size_t r = 0; r < rows.size (); ++r)
    {
        if (rows[r].size () != o.rows[r].size ())
            return false;
        for (size_t c = 0; c < rows[r].size (); ++c)
            if (rows[r][c].ids != o.rows[r][c].ids || rows[r][c].width != o.rows[r][c].width)
                return false;
    }
    return true;
}

const Block* Geometry::find (const std::string& id) const
{
    for (const auto& b : blocks)
        if (b.id == id)
            return &b;
    return nullptr;
}

// ---- text ------------------------------------------------------------------------------------------

std::string toString (const Arrangement& a)
{
    std::string out;
    for (size_t r = 0; r < a.rows.size (); ++r)
    {
        if (r > 0)
            out += ';';
        for (size_t c = 0; c < a.rows[r].size (); ++c)
        {
            if (c > 0)
                out += ',';
            const Column& col = a.rows[r][c];
            for (size_t i = 0; i < col.ids.size (); ++i)
                out += (i > 0 ? "/" : "") + col.ids[i];
            if (col.width > 0)
            {
                char buf[24];
                std::snprintf (buf, sizeof (buf), ":%d", (int)std::lround (col.width));
                out += buf;
            }
        }
    }
    return out;
}

bool isDefault (const std::string& text)
{
    const std::string t = lower (trim (text));
    return t.empty () || t == "default";
}

Arrangement parse (const std::string& text)
{
    Arrangement a;
    const std::string t = trim (text);
    if (isDefault (t) || lower (t) == "wide")
        return a;
    for (const auto& rowText : split (t, ';'))
    {
        Row row;
        for (const auto& colText : split (rowText, ','))
        {
            Column col;
            std::string ids = colText;
            if (const size_t colon = ids.find (':'); colon != std::string::npos)
            {
                col.width = std::max (0.0, std::atof (ids.substr (colon + 1).c_str ()));
                ids = ids.substr (0, colon);
            }
            for (const auto& id : split (ids, '/'))
                if (!trim (id).empty ())
                    col.ids.push_back (trim (id));
            if (!col.ids.empty ())
                row.push_back (col);
        }
        if (!row.empty ())
            a.rows.push_back (row);
    }
    return a;
}

Arrangement resolve (const Spec& spec, const std::string& text)
{
    if (spec.empty () || isDefault (text))
        return {};
    if (lower (trim (text)) == "wide")
        return wide (spec);
    return normalize (spec, parse (text));
}

// ---- templates -------------------------------------------------------------------------------------

Arrangement wide (const Spec& spec)
{
    std::map<int, Row> rows;
    std::map<std::pair<int, int>, size_t> stacks; // (row, stack) -> its column in the row
    for (const auto& p : spec.panels)
    {
        Row& row = rows[p.row];
        if (p.stack >= 0)
            if (auto it = stacks.find ({p.row, p.stack}); it != stacks.end ())
            {
                row[it->second].ids.push_back (p.id);
                continue;
            }
        if (p.stack >= 0)
            stacks[{p.row, p.stack}] = row.size ();
        row.push_back (Column {{p.id}, 0});
    }
    Arrangement a;
    for (auto& [r, row] : rows)
        a.rows.push_back (row);
    return normalize (spec, a);
}

double naturalWidth (const Spec& spec, const Column& c)
{
    double w = 0;
    for (const auto& id : c.ids)
        if (const Panel* p = spec.find (id))
            w = std::max (w, p->region.width ());
    return w;
}

double maxWidth (const Spec& spec, const Column& c) { return 2.0 * naturalWidth (spec, c); }

Arrangement normalize (const Spec& spec, Arrangement a)
{
    std::set<std::string> seen;
    Arrangement out;
    for (auto& row : a.rows)
    {
        Row kept;
        for (auto& col : row)
        {
            Column c;
            c.width = col.width;
            for (const auto& id : col.ids)
                if (spec.find (id) && seen.insert (id).second)
                    c.ids.push_back (id);
            if (!c.ids.empty ())
                kept.push_back (c);
        }
        if (!kept.empty ())
            out.rows.push_back (kept);
    }
    for (const auto& p : spec.panels)
        if (!seen.count (p.id))
        {
            if (out.rows.empty ())
                out.rows.emplace_back ();
            out.rows.back ().push_back (Column {{p.id}, 0});
        }
    for (auto& row : out.rows)
        for (auto& col : row)
        {
            const double nat = naturalWidth (spec, col);
            col.width = col.width <= nat + 0.5 ? 0.0 : std::round (std::min (col.width, maxWidth (spec, col)));
        }
    return out;
}

// ---- geometry --------------------------------------------------------------------------------------

Geometry place (const Spec& spec, const Arrangement& a, const std::map<std::string, double>& heights)
{
    Geometry g;
    double y = spec.top, right = 0;
    for (size_t r = 0; r < a.rows.size (); ++r)
    {
        double x = kMargin, bottom = y;
        std::vector<Box> cols;
        for (size_t c = 0; c < a.rows[r].size (); ++c)
        {
            const Column& col = a.rows[r][c];
            const double w = std::max (col.width, naturalWidth (spec, col));
            double cy = y;
            for (size_t i = 0; i < col.ids.size (); ++i)
            {
                const Panel* p = spec.find (col.ids[i]);
                if (!p)
                    continue;
                double h = p->region.height ();
                if (auto it = heights.find (p->id); it != heights.end ())
                    h = std::clamp (it->second, 0.0, h);
                Block b;
                b.id = p->id;
                b.row = (int)r;
                b.column = (int)c;
                b.index = (int)i;
                b.box = {x, cy, x + w, cy + kStrip + h};
                const double cx = x + std::floor ((w - p->region.width ()) / 2);
                b.content = {cx, cy + kStrip, cx + p->region.width (), cy + kStrip + h};
                g.blocks.push_back (b);
                cy = b.box.bottom + kGap;
            }
            const double colBottom = cy - kGap;
            cols.push_back ({x, y, x + w, colBottom});
            bottom = std::max (bottom, colBottom);
            x += w + kGap;
        }
        right = std::max (right, x - kGap);
        g.rows.push_back ({kMargin, y, x - kGap, bottom});
        g.columns.push_back (cols);
        y = bottom + kGap;
    }
    g.width = std::max (spec.width, right + kMargin);
    g.height = (a.rows.empty () ? spec.top : y - kGap) + kMargin;
    return g;
}

// ---- moving and resizing ---------------------------------------------------------------------------

Drop dropAt (const Arrangement& a, const Geometry& g, double x, double y)
{
    Drop d;
    auto rowMark = [&] (double ym) { return Box {kMargin, ym - 1, g.width - kMargin, ym + 1}; };
    if (a.rows.empty () || g.rows.size () != a.rows.size ())
    {
        d.kind = Drop::NewRow;
        d.mark = rowMark (g.height);
        return d;
    }
    // above the first row, under the last: a row of its own
    if (y < g.rows.front ().top - kGap / 2)
    {
        d.kind = Drop::NewRow;
        d.row = 0;
        d.mark = rowMark (g.rows.front ().top - kGap / 2);
        return d;
    }
    if (y >= g.rows.back ().bottom + kGap / 2)
    {
        d.kind = Drop::NewRow;
        d.row = (int)a.rows.size ();
        d.mark = rowMark (g.rows.back ().bottom + kGap / 2);
        return d;
    }
    size_t r = 0;
    while (r + 1 < g.rows.size () && y >= g.rows[r].bottom + kGap / 2)
        ++r;
    const Box& row = g.rows[r];
    const auto& cols = g.columns[r];
    auto columnMark = [&] (size_t c) {
        const double xm = c == 0 ? cols.front ().left - kGap / 2 : c >= cols.size () ? cols.back ().right + kGap / 2 : (cols[c - 1].right + cols[c].left) / 2;
        return Box {xm - 1, row.top, xm + 1, row.bottom};
    };
    d.row = (int)r;
    // beside the row's columns, or near a column's sides: a column of its own
    size_t c = 0;
    while (c + 1 < cols.size () && x >= cols[c].right + kGap / 2)
        ++c;
    const Box& col = cols[c];
    const double frac = col.width () > 0 ? (x - col.left) / col.width () : 0.5;
    if (frac < 0.25 || frac > 0.75)
    {
        d.kind = Drop::NewColumn;
        d.column = (int)(frac < 0.25 ? c : c + 1);
        d.mark = columnMark ((size_t)d.column);
        return d;
    }
    // over its middle: into its stack, above the first panel whose middle is under the mouse
    std::vector<const Block*> stack;
    for (const auto& b : g.blocks)
        if (b.row == (int)r && b.column == (int)c)
            stack.push_back (&b);
    std::sort (stack.begin (), stack.end (), [] (const Block* p, const Block* q) { return p->index < q->index; });
    size_t i = 0;
    while (i < stack.size () && (stack[i]->box.top + stack[i]->box.bottom) / 2 < y)
        ++i;
    d.kind = Drop::Stack;
    d.column = (int)c;
    d.index = (int)i;
    const double ym = stack.empty () ? col.top
                      : i == 0       ? stack.front ()->box.top - kGap / 2
                      : i >= stack.size () ? stack.back ()->box.bottom + kGap / 2
                                           : (stack[i - 1]->box.bottom + stack[i]->box.top) / 2;
    d.mark = {col.left, ym - 1, col.right, ym + 1};
    return d;
}

Arrangement move (const Spec& spec, const Arrangement& a, const std::string& id, const Drop& d)
{
    // where it is now (and its column's width, kept when it moves alone into a column of its own)
    double width = 0;
    bool found = false;
    for (const auto& row : a.rows)
        for (const auto& col : row)
            for (const auto& i : col.ids)
                if (i == id)
                {
                    found = true;
                    width = col.ids.size () == 1 ? col.width : 0.0;
                }
    if (!found || d.kind == Drop::None)
        return a;
    // its new place first (the drop's indices are the arrangement's as it is), then the old one out
    Arrangement b = a;
    auto clampTo = [] (int v, size_t n) { return (size_t)std::clamp (v, 0, (int)n); };
    if (d.kind == Drop::NewRow)
        b.rows.insert (b.rows.begin () + (long)clampTo (d.row, b.rows.size ()), Row {Column {{kToken}, width}});
    else
    {
        if (d.row < 0 || d.row >= (int)b.rows.size ())
            return a;
        Row& row = b.rows[(size_t)d.row];
        if (d.kind == Drop::NewColumn)
            row.insert (row.begin () + (long)clampTo (d.column, row.size ()), Column {{kToken}, width});
        else
        {
            if (d.column < 0 || d.column >= (int)row.size ())
                return a;
            auto& ids = row[(size_t)d.column].ids;
            ids.insert (ids.begin () + (long)clampTo (d.index, ids.size ()), kToken);
        }
    }
    for (auto& row : b.rows)
        for (auto& col : row)
            col.ids.erase (std::remove (col.ids.begin (), col.ids.end (), id), col.ids.end ());
    for (auto& row : b.rows)
        for (auto& col : row)
            for (auto& i : col.ids)
                if (i == kToken)
                    i = id;
    return normalize (spec, b);
}

Arrangement resize (const Spec& spec, const Arrangement& a, const std::string& id, double width)
{
    Arrangement b = a;
    for (auto& row : b.rows)
        for (auto& col : row)
            if (std::find (col.ids.begin (), col.ids.end (), id) != col.ids.end ())
                col.width = std::clamp (width, naturalWidth (spec, col), maxWidth (spec, col));
    return normalize (spec, b);
}

// ---- saved layouts ---------------------------------------------------------------------------------

namespace {
constexpr const char* kDefaultKey = "@default";
}

const Named* Saved::find (const std::string& name) const
{
    for (const auto& n : layouts)
        if (lower (n.name) == lower (trim (name)))
            return &n;
    return nullptr;
}

void Saved::put (const std::string& name, const std::string& layout)
{
    for (auto& n : layouts)
        if (lower (n.name) == lower (trim (name)))
        {
            n.name = trim (name);
            n.layout = layout;
            return;
        }
    layouts.push_back ({trim (name), layout});
}

bool Saved::remove (const std::string& name)
{
    const size_t before = layouts.size ();
    layouts.erase (std::remove_if (layouts.begin (), layouts.end (), [&] (const Named& n) { return lower (n.name) == lower (trim (name)); }),
                   layouts.end ());
    return layouts.size () != before;
}

Saved parseSaved (const std::string& text)
{
    Saved s;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        line = trim (line);
        if (line.empty () || line[0] == '#')
            continue;
        const size_t eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const std::string name = trim (line.substr (0, eq)), value = trim (line.substr (eq + 1));
        if (name == kDefaultKey)
        {
            s.defaultLayout = value;
            s.hasDefault = true;
        }
        else if (validName (name))
            s.put (name, value);
    }
    return s;
}

std::string savedText (const Saved& s)
{
    std::string out = "# Saved editor layouts (Menu > Layout). name = layout\n";
    if (s.hasDefault)
        out += std::string (kDefaultKey) + " = " + s.defaultLayout + "\n";
    for (const auto& n : s.layouts)
        out += n.name + " = " + n.layout + "\n";
    return out;
}

bool validName (const std::string& name)
{
    const std::string t = trim (name);
    if (t.empty () || t[0] == '@' || t[0] == '#' || t.size () > 64)
        return false;
    for (char c : t)
        if (c == '=' || c == '\n' || c == '\r')
            return false;
    const std::string l = lower (t);
    return l != "default" && l != "wide";
}

std::string savedPath (const std::string& presetFolder)
{
    return presetFolder.empty () ? std::string () : (fs::path (presetFolder) / ".layouts.txt").string ();
}

Saved readSaved (const std::string& presetFolder)
{
    const std::string path = savedPath (presetFolder);
    if (path.empty ())
        return {};
    std::ifstream f (path, std::ios::binary);
    if (!f)
        return {};
    std::stringstream ss;
    ss << f.rdbuf ();
    return parseSaved (ss.str ());
}

bool writeSaved (const std::string& presetFolder, const Saved& s)
{
    const std::string path = savedPath (presetFolder);
    if (path.empty ())
        return false;
    std::error_code ec;
    fs::create_directories (fs::path (path).parent_path (), ec);
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f << savedText (s);
    return (bool)f;
}

} // namespace pk::layout
