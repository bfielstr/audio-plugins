#include "Editor.h"

#include "Help.h"
#include "LevelView.h"
#include "plugin/Controller.h"

#include "pluginkit/ui/Theme.h"
#include "pluginkit/vst/PresetBar.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace levlr {

using namespace VSTGUI;
using pk::ActionButton;
using pk::Choice;
using pk::Knob;
using pk::Label;
using pk::NumberBox;
using pk::Segmented;
using pk::Toggle;

namespace {
constexpr double kKnobW = 56, kKnobH = 64;
CRect knobRect (double x, double y) { return CRect (x, y, x + kKnobW, y + kKnobH); }

class Background : public CViewContainer
{
public:
    using CViewContainer::CViewContainer;
    void drawBackgroundRect (CDrawContext* ctx, const CRect&) override
    {
        ctx->setFillColor (pk::theme::kBackground);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), getViewSize ().getHeight ()), kDrawFilled);
        ctx->setFillColor (pk::theme::kHeader);
        ctx->drawRect (CRect (0, 0, getViewSize ().getWidth (), 34), kDrawFilled);
    }
};

std::string hzText (double hz)
{
    char buf[24];
    if (hz < 999.5)
        std::snprintf (buf, sizeof (buf), "%.0f Hz", hz);
    else
        std::snprintf (buf, sizeof (buf), "%.1f kHz", hz / 1000.0);
    return buf;
}

// A band's name in its colour and the range it covers now (from the crossovers as the engine uses them).
class BandHeader : public CView
{
public:
    BandHeader (const CRect& r, pk::ParamHost* h, int b, std::function<double ()> rate)
        : CView (r), host (h), band (b), sampleRate (std::move (rate))
    {
    }
    void draw (CDrawContext* ctx) override
    {
        const CRect r = getViewSize ();
        if (auto p = owned (ctx->createGraphicsPath ()))
        {
            p->addRoundRect (r, 3.0);
            ctx->setFillColor (LevelView::bandColor (band, 34));
            ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
        }
        ctx->setFillColor (LevelView::bandColor (band));
        ctx->drawRect (CRect (r.left, r.top + 3, r.left + 3, r.bottom - 3), kDrawFilled);
        double set[kCrossovers], xo[kCrossovers];
        for (int k = 0; k < kCrossovers; ++k)
            set[k] = host->plainValue (xoverParam (k));
        effectiveCrossovers (set, sampleRate (), xo);
        const int count = bandsOf (host->plainValue (kBandCount)); // (the last band in use goes to the top)
        const double lo = band == 0 ? LevelView::kMinHz : xo[band - 1], hi = band >= count - 1 ? LevelView::kMaxHz : xo[band];
        char name[16];
        std::snprintf (name, sizeof (name), "BAND %d", band + 1);
        ctx->setFont (pk::theme::font (10.5, true));
        ctx->setFontColor (LevelView::bandColor (band));
        ctx->drawString (name, CRect (r.left + 9, r.top, r.right, r.bottom), kLeftText, true);
        ctx->setFont (pk::theme::font (9.5));
        ctx->setFontColor (pk::theme::kText);
        ctx->drawString ((hzText (lo) + " - " + hzText (hi)).c_str (), CRect (r.left, r.top, r.right - 6, r.bottom), kRightText, true);
    }

private:
    pk::ParamHost* host;
    int band;
    std::function<double ()> sampleRate;
};
} // namespace

Editor::Editor (Controller* c) : pk::EditorBase (c, kWidth, kHeight), ctl (c) {}

void Editor::onClose ()
{
    tailDisplays.reset ();
    levels = nullptr;
    headers.clear ();
    for (auto& c : columns)
        c.clear ();
    for (auto& t : driveType)
        t = nullptr;
}

void Editor::buildUI (CFrame* f)
{
    auto* root = new Background (CRect (0, 0, kWidth, kHeight));
    f->addView (root);
    root->addView (new Label (CRect (12, 6, 200, 28), "levlr", 14.0, true));
    root->addView (new pk::PresetBar (CRect (580, 6, 776, 28), ctl));
    auto* helpBtn = new ActionButton (CRect (784, 6, 806, 28), "?", [this] { setTooltipsEnabled (!tooltipsEnabled ()); },
                                      [this] { return tooltipsEnabled (); });
    helpBtn->setTooltipText ("Show or hide these help tooltips.");
    root->addView (helpBtn);
    root->addView (new ActionButton (CRect (812, 6, 892, 28), "Menu", [this] { showMenu (CPoint (812, 28)); }));

    auto metersOf = [c = ctl] () -> const Meters* {
        auto* s = c->getShared ();
        return s ? &s->meters : nullptr;
    };
    auto rateOf = [c = ctl] {
        auto* s = c->getShared ();
        return s ? s->sampleRate.load () : 48000.0;
    };
    levels = new LevelView (CRect (kViewLeft, kViewTop, kViewRight, kViewBottom), this, metersOf);
    levels->setTooltipText (help::kDisplay);
    root->addView (levels);

    // each band: its name and range, level, mute and solo; under them its drive and the drive's type
    headers.clear ();
    for (int b = 0; b < kBands; ++b)
    {
        const double x = kViewLeft + b * kColumnW;
        auto& col = columns[b];
        col.clear ();
        auto* h = new BandHeader (CRect (x, kRowTop, x + kColumnW - 8, kRowTop + 18), this, b, rateOf);
        root->addView (h);
        headers.push_back (h);
        col.push_back (h);
        col.push_back (bind (root, new Knob (knobRect (x, kRowTop + 24), this, bandParam (b, kGain), nullptr, true)));
        col.push_back (
            bind (root, new Toggle (CRect (x + 64, kRowTop + 32, x + kColumnW - 8, kRowTop + 52), this, bandParam (b, kMute), "Mute")));
        col.push_back (
            bind (root, new Toggle (CRect (x + 64, kRowTop + 58, x + kColumnW - 8, kRowTop + 78), this, bandParam (b, kSolo), "Solo")));
        col.push_back (bind (root, new Knob (knobRect (x, kDriveTop), this, driveParam (b, kDriveDb))));
        driveType[b] = bind (root, new Choice (CRect (x + 64, kDriveTop + 12, x + kColumnW - 8, kDriveTop + 47), this,
                                               driveParam (b, kDriveType), "Type"));
        col.push_back (driveType[b]);
    }

    // the crossovers, the slope, the output
    const double cx = kViewLeft + kBands * kColumnW + 4; // 612
    root->addView (new Label (CRect (cx, kRowTop, cx + 200, kRowTop + 18), "Crossovers", 10.5, true));
    const char* between[kCrossovers] = {"1 | 2", "2 | 3", "3 | 4"};
    for (int k = 0; k < kCrossovers; ++k)
    {
        const double x = cx + k * 66;
        bind (root, new NumberBox (CRect (x, kRowTop + 24, x + 62, kRowTop + 42), this, xoverParam (k)));
        root->addView (new Label (CRect (x, kRowTop + 44, x + 62, kRowTop + 56), between[k], 9.5, false, 1));
    }
    root->addView (new Label (CRect (cx, kRowTop + 62, cx + 40, kRowTop + 80), "Slope", 10.5, false));
    bind (root, new Choice (CRect (cx + 42, kRowTop + 61, cx + 194, kRowTop + 81), this, kSlope));
    root->addView (new Label (CRect (cx, kBandsTop + 1, cx + 40, kBandsTop + 19), "Bands", 10.5, false));
    bind (root, new Segmented (CRect (kBandsLeft, kBandsTop, kBandsRight, kBandsTop + 20), this, kBandCount, {"1", "2", "3", "4"}));
    bind (root, new Knob (knobRect (kViewRight - kKnobW - 4, kRowTop + 16), this, kOutput));

    // the saturator at the end of the chain, with Smacheratr's displays above its controls
    auto* tailPanel =
        addTailPanel (root, CRect (kViewLeft, kTailTop, kViewRight, kTailTop + 78 + smacheratr::TailDisplays::kHeight), kTailBase, kTailExtBase, kTailExt2Base);
    tailDisplays = std::make_unique<smacheratr::TailDisplays> (
        this, kTailBase, kTailExtBase, kTailExt2Base, rateOf,
        [c = ctl] () -> const smacheratr::Meters* {
            auto* s = c->getShared ();
            return s ? &s->tailMeters : nullptr;
        });
    tailDisplays->add (tailPanel, CRect (10, 24, 874, 24 + smacheratr::TailDisplays::kHeight - 22));
    tailDisplays->onBandPicked ([this] (int k) { showTailBand (k); });

    applyParamTooltips (&help::forParam);
    updateBands ();
    idle ();
}

void Editor::updateBands ()
{
    const int count = bandsOf (plainValue (kBandCount));
    for (int b = 0; b < kBands; ++b)
    {
        for (auto* v : columns[b])
            if (v->isVisible () != (b < count))
            {
                v->setVisible (b < count);
                if (auto* parent = v->getParentView ())
                    parent->invalidRect (v->getViewSize ());
            }
        if (driveType[b])
            driveType[b]->setEnabledLook (plainValue (driveParam (b, kDriveDb)) > 0.0);
    }
}

void Editor::paramChanged (uint32_t id)
{
    pk::EditorBase::paramChanged (id);
    if (tailDisplays)
        tailDisplays->paramChanged (id);
    if (isTailParam (id))
        return;
    if (levels)
        levels->invalid ();
    if ((id >= kXover && id < kXover + kCrossovers) || id == kBandCount)
        for (auto* h : headers)
            h->invalid ();
    if (id == kBandCount || (id >= kDriveBase && id < kNumParams))
        updateBands ();
}

void Editor::idle ()
{
    if (tailDisplays)
        tailDisplays->idle ();
    if (levels)
        levels->idle ();
}

void Editor::showMenu (CPoint where)
{
    if (!frame)
        return;
    auto menu = makeOwned<COptionMenu> ();
    std::vector<double> sizes {0.75, 1.0, 1.25, 1.5, 2.0};
    for (double s : sizes)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "Interface Size %d%%", (int)std::lround (s * 100));
        menu->addEntry (buf, -1, std::fabs (currentScale () - s) < 0.01 ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    }
    menu->popup (frame, where, [this, sizes, menu] (COptionMenu* m) {
        const int32_t r = m->getLastResult ();
        if (r >= 0 && r < (int32_t)sizes.size ())
            resizeTo (sizes[(size_t)r]);
    });
}

} // namespace levlr
