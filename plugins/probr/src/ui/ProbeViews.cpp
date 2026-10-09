#include "ProbeViews.h"

#include "Status.h"

#include "pluginkit/ui/Theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace probr {

using namespace VSTGUI;
namespace theme = pk::theme;

namespace {
void drawText (CDrawContext* ctx, const std::string& s, const CRect& r, const CColor& c, double size, bool bold,
               CHoriTxtAlign align = kCenterText)
{
    ctx->setFont (theme::font (size, bold));
    ctx->setFontColor (c);
    ctx->drawString (s.c_str (), r, align, true);
}
} // namespace

//==============================================================================
RecordButton::RecordButton (const CRect& r, pk::ParamHost* h, uint32_t id, std::function<int ()> s)
: ParamView (r, h, id), state (std::move (s))
{
}

void RecordButton::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const int s = state ? state () : (host->norm (param) >= 0.5 ? kStateArmed : kStateOff);
    shown = s;
    // the record dot left of the text; Recording fills the button in cinnabar (the suite's red)
    const CRect inner (r.left + 0.5, r.top + 0.5, r.right - 0.5, r.bottom - 0.5);
    const char* text = "Record";
    CColor tc = theme::kCopperPale, dot = theme::kEnergyIdle, edge = theme::kCopper;
    switch (s)
    {
        case kStateArmed:
            text = "Armed";
            tc = theme::kText;
            dot = theme::kEnergyLive;
            edge = theme::kEnergyLive;
            break;
        case kStateRecording:
            text = "Recording";
            tc = theme::kText;
            dot = theme::kEnergyPeak;
            edge = theme::kEnergyPeak;
            ctx->setFillColor (theme::kEnergyLive);
            ctx->drawRect (inner, kDrawFilled);
            break;
        case kStateStopped:
            text = "Stopped";
            tc = theme::kEnergyPeak;
            dot = theme::kEnergyIdle;
            edge = theme::kEnergyLive;
            break;
        default: break;
    }
    pk::draw::outline (ctx, r, edge);
    const double d = std::min (12.0, r.getHeight () * 0.32);
    const double cx = r.left + 14 + d / 2, cy = r.getCenter ().y;
    ctx->setFillColor (dot);
    ctx->drawEllipse (CRect (cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2), kDrawFilled);
    drawText (ctx, text, CRect (r.left + 20 + d, r.top, r.right - 6, r.bottom), tc, 13.0, s != kStateOff);
    setDirty (false);
}

void RecordButton::onMouseDownEvent (MouseDownEvent& e)
{
    if (resetOnRightClick (e))
        return;
    if (!e.buttonState.isLeft ())
        return;
    host->setOnce (param, host->norm (param) >= 0.5 ? 0.0 : 1.0);
    invalid ();
    e.consumed = true;
    e.ignoreFollowUpMoveAndUpEvents (true);
}

void RecordButton::idle ()
{
    if (state && state () != shown)
    {
        shown = state ();
        invalid ();
    }
}

//==============================================================================
LevelMeter::LevelMeter (const CRect& r, std::function<void (float&, float&)> p) : CView (r), peaks (std::move (p)) {}

void LevelMeter::idle ()
{
    float l = 0.0f, r = 0.0f;
    if (peaks)
        peaks (l, r);
    const float in[2] = {l, r};
    bool changed = false;
    for (int c = 0; c < 2; ++c)
    {
        const double now = in[c] > 1e-6f ? 20.0 * std::log10 ((double)in[c]) : -100.0;
        db[c] = std::max (now, db[c] - 1.0); // falls 30 dB a second
        if (db[c] < -70.0)
            db[c] = -100.0;
        if (std::fabs (db[c] - drawn[c]) > 0.25)
            changed = true;
    }
    if (changed)
    {
        drawn[0] = db[0];
        drawn[1] = db[1];
        invalid ();
    }
}

void LevelMeter::draw (CDrawContext* ctx)
{
    const CRect r = getViewSize ();
    const double h = std::floor ((r.getHeight () - 3) / 2);
    for (int c = 0; c < 2; ++c)
    {
        const CRect bed (r.left, r.top + c * (h + 3), r.right, r.top + c * (h + 3) + h);
        ctx->setFillColor (theme::kWell);
        ctx->drawRect (bed, kDrawFilled);
        const double frac = std::clamp ((db[c] + 60.0) / 60.0, 0.0, 1.0);
        if (frac > 0)
        {
            ctx->setFillColor (db[c] > -0.1 ? theme::kEnergyPeak : theme::kEnergyLive);
            ctx->drawRect (CRect (bed.left + 1, bed.top + 1, bed.left + 1 + frac * (bed.getWidth () - 2), bed.bottom - 1), kDrawFilled);
        }
        // a tick every 12 dB
        ctx->setFrameColor (theme::kLineDim);
        for (int t = 1; t < 5; ++t)
        {
            const double x = std::round (bed.left + bed.getWidth () * t / 5.0) + 0.5;
            ctx->drawLine (CPoint (x, bed.top), CPoint (x, bed.top + 3));
        }
        pk::draw::outline (ctx, bed, theme::kLineDim, 0.0);
    }
    setDirty (false);
}

//==============================================================================
void MessageText::set (const std::string& t, bool w)
{
    if (t == msg && w == warn)
        return;
    msg = t;
    warn = w;
    invalid ();
}

void MessageText::draw (CDrawContext* ctx)
{
    drawText (ctx, msg, getViewSize (), warn ? theme::kEnergyPeak : theme::kText, 11.0, warn, kLeftText);
    setDirty (false);
}

//==============================================================================
void TextField::Listener::valueChanged (CControl* c)
{
    if (auto* t = dynamic_cast<CTextEdit*> (c); t && commit)
        commit (t->getText ().getString ());
}

TextField::TextField (const CRect& r, std::function<void (const std::string&)> c) : CTextEdit (r, &listener, -1)
{
    listener.commit = std::move (c);
    setBackColor (theme::kWell);
    setFrameColor (theme::kCopper);
    setFontColor (theme::kText);
    setFont (theme::font (12.0));
    setHoriAlign (kLeftText);
    setTextInset (CPoint (6, 0));
    setFrameWidth (1.0);
    setStyle (getStyle () & ~(kNoFrame | kRoundRectStyle));
    setImmediateTextChange (false);
}

//==============================================================================
std::string fitText (const std::string& text, size_t maxChars)
{
    // (counted in UTF-8 characters)
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size (); ++i)
        if (((unsigned char)text[i] & 0xC0) != 0x80)
            starts.push_back (i);
    if (starts.size () <= maxChars || maxChars < 5)
        return text;
    const size_t keep = maxChars - 3, head = keep / 3, tail = keep - head;
    return text.substr (0, starts[head]) + "..." + text.substr (starts[starts.size () - tail]);
}

} // namespace probr
