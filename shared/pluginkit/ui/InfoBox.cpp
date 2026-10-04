#include "pluginkit/ui/InfoBox.h"

#include "pluginkit/ui/HelpText.h"
#include "pluginkit/ui/Theme.h"
#include "pluginkit/ui/Widgets.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cviewcontainer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

namespace pk {

using namespace VSTGUI;

namespace {

// View attributes beside VSTGUI's tooltip ('cvtt'): the help text as written (unwrapped), the wrapped
// copy last set as the tooltip (to tell a tooltip set again since), and the info box title.
constexpr CViewAttributeID kHelpTextAttr = 'pkht';
constexpr CViewAttributeID kHelpWrappedAttr = 'pkhw';
constexpr CViewAttributeID kHelpTitleAttr = 'pkit';

std::string attribute (const CView* v, CViewAttributeID id)
{
    uint32_t size = 0;
    if (!v->getAttributeSize (id, size) || size == 0)
        return {};
    std::string s (size, '\0');
    if (!v->getAttribute (id, size, s.data (), size))
        return {};
    s.resize (std::strlen (s.c_str ())); // stored with its terminating zero
    return s;
}

void setAttribute (CView* v, CViewAttributeID id, const std::string& s)
{
    v->setAttribute (id, (uint32_t)s.size () + 1, s.c_str ());
}

// `text` broken into lines that fit `width` px in the context's current font, at spaces; at most
// `maxLines`, the last one ending in an ellipsis when the text goes on.
std::vector<std::string> fitLines (CDrawContext* ctx, const std::string& text, double width, int maxLines)
{
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    auto fits = [&] (const std::string& s) { return ctx->getStringWidth (s.c_str ()) <= width; };
    while (i < text.size ())
    {
        while (i < text.size () && (text[i] == ' ' || text[i] == '\n'))
            ++i;
        size_t j = i;
        while (j < text.size () && text[j] != ' ' && text[j] != '\n')
            ++j;
        if (j == i)
            break;
        const std::string word = text.substr (i, j - i);
        const std::string tryLine = line.empty () ? word : line + " " + word;
        if (line.empty () || fits (tryLine))
            line = tryLine;
        else
        {
            lines.push_back (line);
            line = word;
            if ((int)lines.size () == maxLines)
            {
                line.clear ();
                // more text than lines: the last line ends in an ellipsis that fits with it
                std::string& last = lines.back ();
                while (!last.empty () && !fits (last + "\xE2\x80\xA6"))
                {
                    const size_t sp = last.rfind (' ');
                    last.resize (sp == std::string::npos ? last.size () - 1 : sp);
                }
                last += "\xE2\x80\xA6";
                return lines;
            }
        }
        i = j;
    }
    if (!line.empty ())
        lines.push_back (line);
    return lines;
}

} // namespace

void setHelp (CView* v, const char* title, const char* text)
{
    if (!v)
        return;
    v->setTooltipText (text);
    if (title && *title)
        setAttribute (v, kHelpTitleAttr, title);
    else
        v->removeAttribute (kHelpTitleAttr);
    prepareTooltip (v);
}

void prepareTooltip (CView* v)
{
    if (!v)
        return;
    const std::string tip = attribute (v, kCViewTooltipAttribute);
    if (tip.empty ())
        return;
    if (attribute (v, kHelpWrappedAttr) == tip)
        return; // wrapped already, and not set again since
    const std::string wrapped = helptext::wrap (tip, kTooltipChars);
    setAttribute (v, kHelpTextAttr, tip);
    setAttribute (v, kHelpWrappedAttr, wrapped);
    if (wrapped != tip)
        v->setTooltipText (wrapped.c_str ());
}

void prepareTooltips (CView* root)
{
    if (!root)
        return;
    prepareTooltip (root);
    if (auto* c = root->asViewContainer ())
        c->forEachChild ([] (CView* child) { prepareTooltips (child); });
}

HelpInfo helpFor (CView* v)
{
    for (; v; v = v->getParentView ())
    {
        prepareTooltip (v);
        const std::string text = attribute (v, kCViewTooltipAttribute).empty () ? std::string () : attribute (v, kHelpTextAttr);
        auto* pv = dynamic_cast<ParamView*> (v);
        if (text.empty () && !pv)
            continue;
        HelpInfo h;
        h.text = text;
        if (pv)
            h.title = pv->paramName ();
        if (h.title.empty ())
            h.title = attribute (v, kHelpTitleAttr);
        if (h.title.empty ())
        {
            auto [t, rest] = helptext::splitTitle (text);
            if (!t.empty ())
            {
                h.title = t;
                h.text = rest;
            }
        }
        // a push button names itself ("Load", "Menu"), unless its label is a symbol ("<"); the
        // headers' "?" is the help button
        if (h.title.empty ())
            if (auto* b = dynamic_cast<ActionButton*> (v))
            {
                if (b->label () == "?")
                    h.title = "Help";
                else if (std::any_of (b->label ().begin (), b->label ().end (), [] (char ch) { return std::isalpha ((unsigned char)ch); }))
                    h.title = b->label ();
            }
        return h;
    }
    return {};
}

InfoBox::InfoBox (const CRect& r) : CView (r)
{
    setMouseEnabled (false); // only shows: the mouse over it is over the window, and it clears
}

void InfoBox::showFor (CView* v)
{
    HelpInfo h = helpFor (v);
    if (h.title != info.title || h.text != info.text)
    {
        info = std::move (h);
        invalid ();
    }
}

void InfoBox::draw (CDrawContext* ctx)
{
    // repainted when a neighbour's meter or a hover changes it: the text is fitted word by word, so it
    // is kept in a bitmap until the help shown changes
    layer.draw (ctx, getViewSize (), LayerKey ().add (info.title, info.text), [this] (CDrawContext* c) { paint (c); });
}

void InfoBox::paint (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    // a recessed field like the displays (a well in a dim hairline): the title in the text colour at
    // the left, the help in text dim to the right of it, wrapped to the box (docs/THEME.md, "Info box")
    ctx->setFillColor (theme::kWell);
    ctx->drawRect (r, kDrawFilled);
    draw::outline (ctx, r, theme::kLineDim, 0);
    const double pad = 8.0, lineH = 11.5; // four lines of help in the 52 px box
    const int maxLines = std::max (1, (int)std::floor ((r.getHeight () - 6.0) / lineH));
    const double textLeft = r.left + pad + kTitleWidth + pad;
    if (info.title.empty () && info.text.empty ())
    {
        ctx->setFont (theme::font (9.5));
        ctx->setFontColor (theme::kTextDim);
        ctx->drawString ("Point at a control to see what it does here.", CRect (r.left + pad, r.top + 3, r.right - pad, r.top + 3 + lineH),
                         kLeftText, true);
        return;
    }
    ctx->setFont (theme::font (10.5, true));
    ctx->setFontColor (theme::kText);
    const auto titleLines = fitLines (ctx, info.title, kTitleWidth, maxLines);
    for (size_t i = 0; i < titleLines.size (); ++i)
    {
        const double y = r.top + 3 + lineH * (double)i;
        ctx->drawString (titleLines[i].c_str (), CRect (r.left + pad, y, r.left + pad + kTitleWidth, y + lineH), kLeftText, true);
    }
    ctx->setFont (theme::font (9.5));
    ctx->setFontColor (theme::kTextDim);
    const auto lines = fitLines (ctx, info.text, r.right - pad - textLeft, maxLines);
    for (size_t i = 0; i < lines.size (); ++i)
    {
        const double y = r.top + 3 + lineH * (double)i;
        ctx->drawString (lines[i].c_str (), CRect (textLeft, y, r.right - pad, y + lineH), kLeftText, true);
    }
}

} // namespace pk
