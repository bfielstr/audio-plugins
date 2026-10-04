// The editors' side of Gentlr's glue and No Overlap (ui/BandPush.h) on a plain parameter host, as the
// colour display and Gentlr's display drive it: an edge dragged onto a neighbour's snaps and glues, a
// glued border dragged moves both bands, a glued band moved drags its neighbour's edge, a link click
// detaches and glues. Runs everywhere (no window). Run: ./smacheratr_bandpush_tests
#include "ui/BandPush.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using namespace smacheratr;

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

// Smacheratr's parameters, with the gestures counted (a setNorm outside one is an error)
struct Host : pk::ParamHost
{
    std::vector<double> v;
    std::multiset<uint32_t> open;
    int outside = 0;
    Host ()
    {
        for (uint32_t id = 0; id < kNumParams; ++id)
            v.push_back (paramTable ().defaultNormalized (id));
    }
    const pk::ParamTable& table () override { return paramTable (); }
    double norm (uint32_t id) override { return v[id]; }
    double plainValue (uint32_t id) override { return toPlain (id, v[id]); }
    void beginEdit (uint32_t id) override { open.insert (id); }
    void setNorm (uint32_t id, double x) override
    {
        outside += open.count (id) == 0;
        v[id] = x;
    }
    void endEdit (uint32_t id) override
    {
        if (auto it = open.find (id); it != open.end ())
            open.erase (it);
    }
    std::string valueText (uint32_t id) override { return paramTable ().toText (id, plainValue (id)); }
    void set (uint32_t id, double plain) { v[id] = toNormalized (id, plain); }
};

static double lo (Host& h, int k) { return k == kHighBand ? h.plainValue (kGentlrFreqIds[k]) : h.plainValue (kGentlrFreqIds[k]) / std::exp2 (0.5 * h.plainValue (kClarityWidthIds[k])); }
static double hi (Host& h, int k) { return k == kSubBand ? h.plainValue (kGentlrFreqIds[k]) : h.plainValue (kGentlrFreqIds[k]) * std::exp2 (0.5 * h.plainValue (kClarityWidthIds[k])); }
static bool near (double a, double b, double rel = 1e-4) { return std::fabs (a / b - 1.0) <= rel; }

// A display's edge drag of band k (the edge follows the mouse, the band centred: as ColorView does it)
static void dragEdge (Host& h, int k, bool high, const std::vector<double>& hzPath, double snapOct = 0.05)
{
    const auto& bp = smacheratrBandParams ();
    BandPush push;
    const uint32_t w = kClarityWidthIds[k];
    h.beginEdit (w);
    push.begin (&h, bp, k, {w}, high ? BandPush::Grab::HighEdge : BandPush::Grab::LowEdge, snapOct);
    for (double hz : hzPath)
    {
        h.setNorm (w, toNormalized (w, 2.0 * std::fabs (std::log2 (hz / h.plainValue (kClarityFreqIds[k])))));
        push.update (&h, bp);
    }
    push.end (&h, bp);
    h.endEdit (w);
}

// A handle drag of band k sideways (its frequency), as the displays do it
static void dragBody (Host& h, int k, const std::vector<double>& hzPath, double snapOct = 0.05)
{
    const auto& bp = smacheratrBandParams ();
    BandPush push;
    const uint32_t f = kGentlrFreqIds[k], r = kGentlrRangeIds[k];
    h.beginEdit (f);
    h.beginEdit (r);
    push.begin (&h, bp, k, {f, r}, BandPush::Grab::Body, snapOct);
    for (double hz : hzPath)
    {
        h.setNorm (f, toNormalized (f, hz));
        push.update (&h, bp);
    }
    push.end (&h, bp);
    h.endEdit (f);
    h.endEdit (r);
}

int main ()
{
    // band 1 at 250 Hz, 2 octaves (125 - 500 Hz), band 2 at 1 kHz, an octave (707 - 1414 Hz); the Sub and High
    // bands at work too (40 Hz, 7 kHz)
    auto fresh = [] {
        Host h;
        h.set (kClarity, 1.0);
        h.set (kClarity2Range, 6.0);
        h.set (kClarity2Freq, 1000.0);
        h.set (kClarity2Width, 1.0);
        h.set (kClaritySubRange, 6.0);
        h.set (kClarityHighRange, 6.0);
        return h;
    };
    // glue on touch: band 1's high edge dragged to just short of band 2's low edge snaps onto it, and the
    // two glue when the drag ends; dragged short of the snap, nothing glues
    {
        Host h = fresh ();
        dragEdge (h, 0, true, {550.0, 650.0, 700.0});
        CHECK (h.plainValue (kClarityGlue12) >= 0.5 && near (hi (h, 0), lo (h, 1)), "snapped and glued: %.1f / %.1f Hz", hi (h, 0), lo (h, 1));
        CHECK (near (h.plainValue (kClarityFreq), 250.0) && h.open.empty () && h.outside == 0, "centred, every gesture closed");
        Host far = fresh ();
        dragEdge (far, 0, true, {550.0, 600.0});
        CHECK (far.plainValue (kClarityGlue12) < 0.5 && near (hi (far, 0), 600.0, 1e-3), "not near: no glue (%.1f Hz)", hi (far, 0));
        GlueBorder b[kGentlrBands];
        CHECK (linkBorders (&h, smacheratrBandParams (), b) == 1 && b[0].glued && b[0].pair == kGlue12, "a lit link on the border");
    }
    // a glued border dragged: band 1's low edge stays, band 2's high edge stays, the border moves for both
    {
        Host h = fresh ();
        dragEdge (h, 0, true, {700.0}); // glued at 707 Hz
        const double lo1 = lo (h, 0), hi2 = hi (h, 1);
        dragEdge (h, 0, true, {800.0, 900.0});
        CHECK (near (hi (h, 0), 900.0, 1e-3) && near (lo (h, 1), 900.0, 1e-3) && near (lo (h, 0), lo1) && near (hi (h, 1), hi2),
               "the border at %.1f / %.1f Hz, the outer edges kept (%.1f, %.1f)", hi (h, 0), lo (h, 1), lo (h, 0), hi (h, 1));
        // and from band 2's side, down again
        dragEdge (h, 1, false, {600.0});
        CHECK (near (hi (h, 0), 600.0, 1e-3) && near (lo (h, 1), 600.0, 1e-3) && near (lo (h, 0), lo1) && near (hi (h, 1), hi2),
               "band 2's low edge dragged: the border at %.1f Hz", lo (h, 1));
        CHECK (h.open.empty () && h.outside == 0, "every gesture closed");
    }
    // a glued band moved: its neighbour's edge comes along
    {
        Host h = fresh ();
        dragEdge (h, 0, true, {700.0});
        const double hi2 = hi (h, 1);
        dragBody (h, 0, {280.0, 200.0});
        CHECK (near (hi (h, 0), lo (h, 1)) && near (hi (h, 1), hi2) && near (h.plainValue (kClarityWidth), 3.0), "band 1 moved down: band 2 widens to it");
    }
    // the Sub band: its handle dragged to band 1's low edge snaps and glues; then its Freq is the border
    {
        Host h = fresh ();
        h.set (kClarityFreq, 200.0); // 100 - 400 Hz
        dragBody (h, kSubBand, {60.0, 90.0, 98.0});
        CHECK (h.plainValue (kClarityGlueSub1) >= 0.5 && near (hi (h, kSubBand), lo (h, 0)), "Sub glued to band 1 at %.1f Hz", lo (h, 0));
        const double hi1 = hi (h, 0);
        dragBody (h, kSubBand, {70.0});
        CHECK (near (lo (h, 0), 70.0, 1e-3) && near (hi (h, 0), hi1), "the Sub band's Freq moved: band 1's low edge with it (%.1f Hz)", lo (h, 0));
    }
    // link clicks: detach (the bands stay), glue again (they touch)
    {
        Host h = fresh ();
        dragEdge (h, 0, true, {700.0});
        GlueBorder b[kGentlrBands];
        linkBorders (&h, smacheratrBandParams (), b);
        const double f1 = h.plainValue (kClarityFreq), f2 = h.plainValue (kClarity2Freq);
        toggleGlue (&h, smacheratrBandParams (), b[0]);
        CHECK (h.plainValue (kClarityGlue12) < 0.5 && h.plainValue (kClarityFreq) == f1 && h.plainValue (kClarity2Freq) == f2, "detached, in place");
        CHECK (linkBorders (&h, smacheratrBandParams (), b) == 1 && !b[0].glued, "an unlit link: they still touch");
        // detached, band 1 moves on its own
        dragBody (h, 0, {200.0}, 0.0);
        CHECK (near (h.plainValue (kClarity2Freq), f2) && linkBorders (&h, smacheratrBandParams (), b) == 0, "detached: band 2 stays, no link");
        dragBody (h, 0, {250.0}, 0.0);
        linkBorders (&h, smacheratrBandParams (), b);
        toggleGlue (&h, smacheratrBandParams (), b[0]);
        CHECK (h.plainValue (kClarityGlue12) >= 0.5 && near (hi (h, 0), lo (h, 1)), "glued again by a click");
        CHECK (h.open.empty () && h.outside == 0, "every gesture closed");
    }
    // with No Overlap on: a glued band pushed into its neighbour narrows it (as No Overlap does), pulled
    // away it widens it; an unglued neighbour is pushed but not pulled
    {
        Host h = fresh ();
        h.set (kClarityNoOverlap, 1.0);
        dragEdge (h, 0, true, {700.0});
        CHECK (h.plainValue (kClarityGlue12) >= 0.5, "glued with No Overlap on");
        dragBody (h, 0, {320.0, 200.0});
        CHECK (near (hi (h, 0), lo (h, 1)) && !bandsOverlap (shownLayout (&h, smacheratrBandParams ())), "moved: glued and apart");
        // band 2 and the High band (7 kHz) unglued: band 2 moved up pushes the High band
        const double highWas = h.plainValue (kClarityHighFreq);
        dragBody (h, 1, {4000.0, 6000.0}, 0.0);
        CHECK (h.plainValue (kClarityHighFreq) > highWas * 1.01, "unglued High pushed: %.0f Hz", h.plainValue (kClarityHighFreq));
        CHECK (h.open.empty () && h.outside == 0, "every gesture closed");
    }
    std::printf ("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
