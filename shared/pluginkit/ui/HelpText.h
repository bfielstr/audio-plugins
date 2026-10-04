// Pure text and layout helpers for the editors' help (no VSTGUI, so the core tests cover them): the
// floating tooltips wrapped to lines, the info box's title split from a help text, and the overlap
// check over control rectangles that the host tests run on every editor.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace pk::helptext {

// The number of characters (UTF-8 code points, not bytes) in s[from, to).
inline size_t chars (const std::string& s, size_t from = 0, size_t to = std::string::npos)
{
    to = std::min (to, s.size ());
    size_t n = 0;
    for (size_t i = from; i < to; ++i)
        if (((unsigned char)s[i] & 0xC0) != 0x80) // not a continuation byte
            ++n;
    return n;
}

// `text` broken into lines of at most `width` characters, at spaces (a floating tooltip shows one
// line per '\n' on macOS and Windows; without breaks a long help text is one line across the screen).
// Line breaks already in the text are kept; a word longer than a line stays whole on a line of its
// own. Runs of spaces at a break are dropped.
inline std::string wrap (const std::string& text, size_t width = 50)
{
    if (width == 0)
        return text;
    std::string out;
    size_t lineStart = 0; // where the current output line starts, in `out`
    size_t i = 0;
    while (i < text.size ())
    {
        if (text[i] == '\n')
        {
            out += '\n';
            lineStart = out.size ();
            ++i;
            continue;
        }
        if (text[i] == ' ')
        {
            // a space: kept only between words on one line (added before the next word below)
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size () && text[j] != ' ' && text[j] != '\n')
            ++j;
        const size_t wordLen = chars (text, i, j);
        const size_t lineLen = chars (out, lineStart);
        const bool hadSpace = i > 0 && text[i - 1] == ' ';
        if (lineLen > 0 && lineLen + 1 + wordLen > width)
        {
            out += '\n';
            lineStart = out.size ();
        }
        else if (lineLen > 0 && hadSpace)
            out += ' ';
        out.append (text, i, j - i);
        i = j;
    }
    return out;
}

// A help text's title and the rest, for the info box: "Presets: pick one ..." gives {"Presets",
// "Pick one ..."}. Only a short lead-in before ": " counts as a title (at most 24 characters and three
// words, no full stop, comma or bracket: "The stage from above: ..." is a sentence, not a title); otherwise the title is empty and the text is returned whole. The first letter of
// the rest is upper-cased (ASCII), since it now starts a line of its own.
inline std::pair<std::string, std::string> splitTitle (const std::string& help)
{
    const size_t colon = help.find (": ");
    if (colon == std::string::npos || colon == 0 || chars (help, 0, colon) > 24 ||
        help.find_first_of (".,(\n", 0) < colon || std::count (help.begin (), help.begin () + (long)colon, ' ') > 2)
        return {std::string (), help};
    std::string rest = help.substr (colon + 2);
    if (!rest.empty () && rest[0] >= 'a' && rest[0] <= 'z')
        rest[0] = (char)(rest[0] - 'a' + 'A');
    return {help.substr (0, colon), rest};
}

// --- the overlap check (docs/THEME.md, "Layout"): controls, labels and value boxes in one container
// must not overlap or touch, or their outlines and texts run into each other.
struct Box
{
    double left = 0, top = 0, right = 0, bottom = 0;
    std::string name; // for the report
};

// Every pair (i, j), i < j, of boxes closer than `gap` px (gap 0: only overlapping boxes, with a
// shared area; gap 1: touching edges count too). Empty boxes are ignored.
inline std::vector<std::pair<size_t, size_t>> overlaps (const std::vector<Box>& boxes, double gap = 1.0)
{
    std::vector<std::pair<size_t, size_t>> out;
    for (size_t i = 0; i < boxes.size (); ++i)
    {
        const Box& a = boxes[i];
        if (a.right <= a.left || a.bottom <= a.top)
            continue;
        for (size_t j = i + 1; j < boxes.size (); ++j)
        {
            const Box& b = boxes[j];
            if (b.right <= b.left || b.bottom <= b.top)
                continue;
            if (a.left < b.right + gap && b.left < a.right + gap && a.top < b.bottom + gap && b.top < a.bottom + gap)
                out.push_back ({i, j});
        }
    }
    return out;
}

} // namespace pk::helptext
