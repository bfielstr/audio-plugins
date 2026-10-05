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

bool isClassic (const std::string& text)
{
    const std::string t = lower (trim (text));
    return t.empty () || t == "default" || t == "classic";
}

bool isDefault (const std::string& text) { return isClassic (text); }

std::string templateName (const std::string& text)
{
    if (isClassic (text))
        return "Classic";
    return lower (trim (text)) == "wide" ? "Wide" : std::string ();
}

Arrangement parse (const std::string& text)
{
    Arrangement a;
    const std::string t = trim (text);
    if (isClassic (t) || lower (t) == "wide")
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
    if (spec.empty () || isClassic (text))
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

namespace {
// `total` shared among columns of natural widths `pref`: each gets a part of what is over in proportion to
// its natural width, held to its `cap` while the others can take the rest (when every one is at its cap
// and some is still over, in proportion again past the caps: the row always fills). Whole pixels, adding
// up to `total` exactly. (A row at its natural width keeps it.)
std::vector<double> share (const std::vector<double>& pref, const std::vector<double>& cap, double total)
{
    const size_t n = pref.size ();
    std::vector<double> w = pref;
    double sum = 0;
    for (double p : pref)
        sum += p;
    double extra = total - sum;
    std::vector<bool> held (n, false);
    for (size_t pass = 0; pass <= n && extra > 0.01; ++pass)
    {
        double base = 0;
        for (size_t i = 0; i < n; ++i)
            if (!held[i])
                base += pref[i];
        if (base <= 0)
            break;
        // the columns that would pass their cap take it and leave the round; the others share the rest
        bool capped = false;
        for (size_t i = 0; i < n; ++i)
            if (!held[i] && w[i] + extra * pref[i] / base >= cap[i])
            {
                extra -= std::max (0.0, cap[i] - w[i]);
                w[i] = std::max (w[i], cap[i]);
                held[i] = true;
                capped = true;
            }
        if (capped)
            continue;
        for (size_t i = 0; i < n; ++i)
            if (!held[i])
                w[i] += extra * pref[i] / base;
        extra = 0;
    }
    if (extra > 0.01) // every column at its cap: past them, in proportion
        for (size_t i = 0; i < n; ++i)
            w[i] += extra * pref[i] / std::max (1e-9, sum);
    // whole pixels by their running total (the last edge at `total` exactly)
    std::vector<double> out (n);
    double run = 0, edge = 0;
    for (size_t i = 0; i < n; ++i)
    {
        run += w[i];
        const double next = i + 1 == n ? std::round (total) : std::round (run);
        out[i] = next - edge;
        edge = next;
    }
    return out;
}
} // namespace

Geometry place (const Spec& spec, const Arrangement& a, const std::map<std::string, double>& heights)
{
    // each column's panels and their heights now, its natural width, its range's end and its height
    struct Col
    {
        std::vector<const Panel*> panels;
        std::vector<double> heights;
        double pref = 0, cap = 0, height = 0;
    };
    std::vector<std::vector<Col>> cols (a.rows.size ());
    double inner = std::max (0.0, std::round (spec.width) - 2 * kMargin);
    for (size_t r = 0; r < a.rows.size (); ++r)
    {
        double rowWidth = 0;
        for (const Column& column : a.rows[r])
        {
            Col c;
            for (const auto& id : column.ids)
                if (const Panel* p = spec.find (id))
                {
                    double h = p->region.height ();
                    if (auto it = heights.find (p->id); it != heights.end ())
                        h = std::clamp (it->second, 0.0, h);
                    c.height += (c.panels.empty () ? 0.0 : kGap) + kStrip + h;
                    c.panels.push_back (p);
                    c.heights.push_back (h);
                }
            c.pref = std::round (std::max (column.width, naturalWidth (spec, column)));
            c.cap = std::max (c.pref, std::round (maxWidth (spec, column)));
            rowWidth += (cols[r].empty () ? 0.0 : kGap) + c.pref;
            cols[r].push_back (c);
        }
        inner = std::max (inner, rowWidth);
    }

    Geometry g;
    g.width = inner + 2 * kMargin;
    double y = spec.top;
    for (size_t r = 0; r < a.rows.size (); ++r)
    {
        const auto& row = cols[r];
        std::vector<double> pref, cap;
        double rowHeight = 0;
        for (const auto& c : row)
        {
            pref.push_back (c.pref);
            cap.push_back (c.cap);
            rowHeight = std::max (rowHeight, c.height);
        }
        const std::vector<double> widths = share (pref, cap, inner - kGap * (double)(row.empty () ? 0 : row.size () - 1));
        double x = kMargin;
        std::vector<Box> boxes;
        for (size_t c = 0; c < row.size (); ++c)
        {
            const double w = widths[c];
            const size_t n = row[c].panels.size ();
            // the row's height shared out: each block of a stack taller by an even part of what is over
            // (the last one reaches the row's bottom)
            const double spare = rowHeight - row[c].height;
            double cy = y;
            for (size_t i = 0; i < n; ++i)
            {
                const Panel* p = row[c].panels[i];
                const double h = row[c].heights[i];
                const double add = i + 1 == n ? (y + rowHeight) - (cy + kStrip + h) : std::floor (spare / (double)n);
                Block b;
                b.id = p->id;
                b.row = (int)r;
                b.column = (int)c;
                b.index = (int)i;
                b.box = {x, cy, x + w, cy + kStrip + h + add};
                // the content centred under the strip, both ways (a filling panel: as wide as the block)
                const double cw = p->fill ? w : std::min (w, p->region.width ());
                const double cx = x + std::floor ((w - cw) / 2);
                const double top = cy + kStrip + std::floor (add / 2);
                b.content = {cx, top, cx + cw, top + h};
                g.blocks.push_back (b);
                cy = b.box.bottom + kGap;
            }
            boxes.push_back ({x, y, x + w, y + rowHeight});
            x += w + kGap;
        }
        g.rows.push_back ({kMargin, y, kMargin + inner, y + rowHeight});
        g.columns.push_back (boxes);
        y += rowHeight + kGap;
    }
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
    Arrangement b = normalize (spec, a);
    for (size_t r = 0; r < b.rows.size (); ++r)
        for (size_t c = 0; c < b.rows[r].size (); ++c)
        {
            Column& col = b.rows[r][c];
            if (std::find (col.ids.begin (), col.ids.end (), id) == col.ids.end ())
                continue;
            // the natural width that shows it `width` wide (a wider natural width shows it wider: a search)
            const double lo = naturalWidth (spec, col), hi = maxWidth (spec, col);
            auto shown = [&] (double natural) {
                col.width = natural;
                return place (spec, b).columns[r][c].width ();
            };
            double natural = lo;
            if (shown (hi) <= width)
                natural = hi;
            else if (shown (lo) < width)
            {
                double l = lo, h = hi;
                for (int i = 0; i < 40 && h - l > 0.25; ++i)
                {
                    const double m = 0.5 * (l + h);
                    (shown (m) < width ? l : h) = m;
                }
                natural = std::round (h);
            }
            col.width = natural;
            return normalize (spec, b);
        }
    return b;
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
    return l != "default" && l != "classic" && l != "wide";
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
