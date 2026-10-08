// Headless tests for the rack slots' presets (core/RackPresets.h, plugin/RackPresetIO.h): a slot loads
// the factory presets of its effect's own plug-in to the same values the plug-in has after loading them
// (in slot 0 and slot 7, every effect), a preset saved from a slot reads back in the plug-in's own state
// format, a new slot starts from the plug-in's saved default and its Menu > Defaults switches, a project
// loads as saved whatever those are, and what does not fit (unknown IDs, the parameters a slot has no place
// for, another plug-in's file, a folder that cannot be made) is ignored. And the Gentlr page's Threshold
// sliders (Editor::updateGentlrAdvanced): dragging one moves its band's Threshold in that slot, nothing else. Run: ./smemplr_rack_preset_tests
#include "Params.h"
#include "Rack.h"
#include "RackPresets.h"
#include "plugin/RackPresetIO.h"
#include "plugin/StateIO.h"

#include "pluginkit/GentlrDefaults.h"
#include "pluginkit/ui/Widgets.h"
#include "pluginkit/vst/Presets.h"
#include "smacheratr/src/ui/ThresholdSlider.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/vstguiinit.h"

#include "public.sdk/source/common/memorystream.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace smemplr;
namespace fs = std::filesystem;

static int gFailures = 0, gChecks = 0;

// the presets folder override (PK_PRESETS_DIR), on every platform
static void setPresetsDir (const std::string& dir)
{
#if defined(_WIN32)
    _putenv_s ("PK_PRESETS_DIR", dir.c_str ());
#else
    setenv ("PK_PRESETS_DIR", dir.c_str (), 1);
#endif
}
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

static const int kHostedTypes[] = {kFxPara, kFxMultidyn, kFxSmacheratr, kFxWidr, kFxWubr, kFxLevlr, kFxGentlr, kFxSmoothr};

// a new smemplr's values, with `type` in `slot` (its factory defaults, as addFx puts it there)
static std::vector<double> rackWith (int slot, int type)
{
    std::vector<double> n (kNumParams);
    for (uint32_t id = 0; id < kNumParams; ++id)
        n[id] = defaultNormalized (id);
    n[slotParam (slot, kSlotType)] = toNormalized (slotParam (slot, kSlotType), type);
    n[slotParam (slot, kSlotOn)] = 1.0;
    const auto& t = fxBlockTable (type);
    for (uint32_t j = 0; j < kSlotBlockAll; ++j)
        n[slotBlockParam (slot, j)] = j < t.size () ? t.defaultNormalized (j) : 0.0;
    return n;
}

static void applyEdits (std::vector<double>& n, const SlotEdits& e)
{
    for (const auto& [id, v] : e)
        n[id] = v;
}

// every parameter of the plug-in with a place in the slot: the slot's plain value is the plug-in's
static int compareCarried (const std::vector<double>& rack, int slot, int type, const std::vector<double>& plugin, const char* what)
{
    const pk::ParamTable& pt = *hostedPlugin (type)->table;
    const pk::ParamTable& bt = fxBlockTable (type);
    int carried = 0, bad = 0;
    for (uint32_t id = 0; id < pt.size (); ++id)
    {
        const int64_t j = fxBlockOf (type, id);
        if (j < 0)
            continue;
        ++carried;
        const double slotPlain = bt.toPlain ((uint32_t)j, rack[slotBlockParam (slot, (uint32_t)j)]);
        const double plugPlain = pt.toPlain (id, plugin[id]);
        if (slotPlain != plugPlain && bad++ < 3)
            CHECK (false, "%s, %s slot %d: %s is %g in the slot, %g in the plug-in", what, fxName (type), slot, pt.info (id).name, slotPlain,
                   plugPlain);
    }
    CHECK (bad == 0, "%s, %s slot %d: %d of %d carried parameters differ", what, fxName (type), slot, bad, carried);
    return carried;
}

static void factoryPresetsLoadIntoSlots ()
{
    for (int type : kHostedTypes)
    {
        const auto& fps = rackio::factoryPresetsOf (type);
        const auto* hp = hostedPlugin (type);
        CHECK (hp && !fps.empty (), "%s: factory presets registered (%zu)", fxName (type), fps.size ());
        CHECK (pk::presets::factoryFilesOf (hp->name).size () == fps.size (), "%s: every factory file parses", fxName (type));
        for (int slot : {0, kRackSlots - 1})
            for (const auto& fp : fps)
            {
                // the plug-in after loading it (ControllerBase::loadFactory: its defaults, then the file's values)
                std::vector<double> plugin (hp->table->size ());
                for (uint32_t id = 0; id < plugin.size (); ++id)
                    plugin[id] = hp->table->defaultNormalized (id);
                for (const auto& [id, v] : fp.values)
                    plugin[id] = v;
                const std::vector<double> before = rackWith (slot, type);
                std::vector<double> rack = before;
                const SlotEdits e = slotEditsFor (slot, type, pluginValuesWith (type, fp.values));
                applyEdits (rack, e);
                const std::string what = "factory preset " + fp.name;
                compareCarried (rack, slot, type, plugin, what.c_str ());
                // nothing else moved: the other slots, the slot's Type and On, smemplr's own parameters
                int moved = 0;
                for (uint32_t id = 0; id < kNumParams; ++id)
                {
                    const bool inSlot = isRackParam (id) && rackField (id).slot == slot && rackField (id).field >= kSlotParams;
                    if (!inSlot && rack[id] != before[id])
                        ++moved;
                }
                CHECK (moved == 0, "%s %s: %d parameters outside slot %d moved", fxName (type), fp.name.c_str (), moved, slot);
            }
    }
    CHECK (hostedPlugin (kFxEmpty) == nullptr && hostedPlugin (kFxMsEq) == nullptr, "Empty and the M/S EQ have no plug-in");
    CHECK (slotEditsFor (0, kFxMsEq, {0.5}).empty (), "no edits for the M/S EQ");
}

static void outsideTheBlockIgnored ()
{
    // Widr's cinema stage and Multidyn's own saturator have no place in a slot: values for them change nothing
    for (int type : kHostedTypes)
    {
        std::vector<double> v (hostedPlugin (type)->table->size () + 5, 0.75); // (and IDs past the plug-in's)
        const SlotEdits e = slotEditsFor (3, type, v);
        bool inSlot = true;
        for (const auto& [id, n] : e)
            inSlot = inSlot && isRackParam (id) && rackField (id).slot == 3 && rackField (id).field >= kSlotParams;
        CHECK (inSlot, "%s: only slot 3's block", fxName (type));
        int carried = 0;
        for (uint32_t id = 0; id < hostedPlugin (type)->table->size (); ++id)
            carried += fxBlockOf (type, id) >= 0 ? 1 : 0;
        CHECK ((int)e.size () == carried, "%s: %zu edits for %d carried parameters", fxName (type), e.size (), carried);
    }
    CHECK (fxBlockOf (kFxWidr, widr::kCinema) < 0 && hostedPlugin (kFxWidr)->table->size () == widr::kNumPluginParams,
           "widr: the cinema stage is the plug-in's, not the slot's");
    CHECK (fxBlockOf (kFxMultidyn, multidyn::kSatOn + 2) < 0, "multidyn: its saturator is not in the slot");
    // a slot's values written for the plug-in: the ones it has no place for at the plug-in's defaults
    const std::vector<double> rack = rackWith (2, kFxWidr);
    const std::vector<double> w = pluginValuesOf (2, kFxWidr, [&] (uint32_t id) { return rack[id]; });
    CHECK (w.size () == widr::kNumPluginParams && w[widr::kCinema] == widr::pluginParamTable ().defaultNormalized (widr::kCinema),
           "widr from a slot: Cinema at its default");
}

static std::string tmpRoot;

static void savedFromSlotReadsInThePlugin ()
{
    for (int type : kHostedTypes)
    {
        const auto& fps = rackio::factoryPresetsOf (type);
        const int slot = type % kRackSlots;
        std::vector<double> rack = rackWith (slot, type);
        if (!fps.empty ())
            applyEdits (rack, slotEditsFor (slot, type, pluginValuesWith (type, fps.back ().values)));
        // one value off its preset's, so the file is not just a factory preset
        const int64_t j0 = fxBlockOf (type, 1);
        if (j0 >= 0)
            rack[slotBlockParam (slot, (uint32_t)j0)] = 0.37;
        const std::vector<double> out = pluginValuesOf (slot, type, [&] (uint32_t id) { return rack[id]; });
        pk::presets::Meta m;
        m.tags = {"slot", "test"};
        m.category = "From Smemplr";
        const std::string folder = rackio::folderOf (type);
        CHECK (!folder.empty () && fs::path (folder).filename () == hostedPlugin (type)->name, "%s: its own folder %s", fxName (type),
               folder.c_str ());
        const std::string path = (fs::path (folder) / "From Smemplr" / "Slot Save.vstpreset").string ();
        CHECK (rackio::writeValues (type, path, out, m), "%s: saved", fxName (type));
        // the plug-in reads it with its own reader (its class ID, its state format)
        std::vector<char> comp, ctrl;
        CHECK (pk::presets::read (path, rackio::codecOf (type)->classId, comp, ctrl) && !comp.empty () && ctrl.empty (),
               "%s: a .vstpreset of the plug-in (component state only)", fxName (type));
        std::vector<double> back;
        CHECK (rackio::codecOf (type)->read (comp, back) && back.size () == hostedPlugin (type)->table->size (), "%s: the plug-in reads it",
               fxName (type));
        if (back.size () == out.size ())
        {
            int diff = 0;
            for (size_t id = 0; id < out.size (); ++id)
                diff += back[id] != out[id] ? 1 : 0;
            CHECK (diff == 0, "%s: %d values differ after the round trip", fxName (type), diff);
            compareCarried (rack, slot, type, back, "saved from a slot");
        }
        // in the plug-in's menu: its folder, its category, its tags
        bool listed = false;
        for (const auto& it : pk::presets::listUser (folder))
            if (it.name == "Slot Save" && it.category == "From Smemplr" && pk::presets::hasTag (it.tags, "test"))
                listed = true;
        CHECK (listed, "%s: listed as the plug-in's user preset", fxName (type));
        CHECK (pk::presets::readMeta (path).plugin == hostedPlugin (type)->name, "%s: PlugInName", fxName (type));
        // and loads back into another slot as it was
        std::vector<double> loaded;
        CHECK (rackio::readValues (type, path, loaded), "%s: loads", fxName (type));
        std::vector<double> rack2 = rackWith (7 - slot, type);
        applyEdits (rack2, slotEditsFor (7 - slot, type, loaded));
        compareCarried (rack2, 7 - slot, type, out, "loaded from a file");
    }
}

static void robustness ()
{
    // another plug-in's file, a missing file, not a preset
    const std::string paraPath = (fs::path (rackio::folderOf (kFxPara)) / "Slot Save.vstpreset").string ();
    std::vector<double> v;
    CHECK (!rackio::readValues (kFxWidr, (fs::path (rackio::folderOf (kFxWidr)) / "Nothing.vstpreset").string (), v), "missing file");
    CHECK (!rackio::readValues (kFxGentlr, paraPath, v), "another plug-in's preset is not read");
    {
        const std::string junk = (fs::path (tmpRoot) / "junk.vstpreset").string ();
        std::ofstream (junk) << "not a preset";
        CHECK (!rackio::readValues (kFxPara, junk, v), "not a preset");
    }
    CHECK (!rackio::readValues (kFxMsEq, paraPath, v) && rackio::codecOf (kFxMsEq) == nullptr, "the M/S EQ has no presets");
    // a newer version's state: a parameter this version does not know is ignored, the others load
    {
        const auto* c = rackio::codecOf (kFxPara);
        std::vector<double> vals = pluginDefaults (kFxPara);
        vals[3] = 0.25;
        std::vector<char> comp;
        CHECK (c->write (vals, comp) && comp.size () >= 12, "written");
        int32_t version = 0, count = 0;
        std::memcpy (&version, comp.data () + 4, 4);
        std::memcpy (&count, comp.data () + 8, 4);
        ++version;
        ++count;
        std::memcpy (comp.data () + 4, &version, 4);
        std::memcpy (comp.data () + 8, &count, 4);
        const uint32_t unknown = 4000;
        const double val = 0.9;
        comp.insert (comp.end (), (const char*)&unknown, (const char*)&unknown + 4);
        comp.insert (comp.end (), (const char*)&val, (const char*)&val + 8);
        const std::string path = (fs::path (rackio::folderOf (kFxPara)) / "Newer.vstpreset").string ();
        CHECK (pk::presets::write (path, c->classId, comp, {}, nullptr), "newer preset written");
        std::vector<double> back;
        CHECK (rackio::readValues (kFxPara, path, back) && back.size () == vals.size () && back[3] == 0.25, "a newer preset loads");
        const SlotEdits e = slotEditsFor (1, kFxPara, back);
        bool ok = true;
        for (const auto& [id, n] : e)
            ok = ok && rackField (id).slot == 1;
        CHECK (ok, "its unknown parameter goes nowhere");
    }
    // a preset folder that cannot be made (under a file): nothing listed, nothing saved, a new slot gets the
    // factory defaults
    {
        const std::string file = (fs::path (tmpRoot) / "a-file").string ();
        std::ofstream (file) << "x";
        const std::string saved = std::getenv ("PK_PRESETS_DIR");
        setPresetsDir ((fs::path (file) / "presets").string ().c_str ());
        CHECK (rackio::folderOf (kFxLevlr).empty (), "no folder");
        CHECK (pk::presets::listUser (rackio::folderOf (kFxLevlr)).empty (), "nothing listed");
        CHECK (!rackio::writeValues (kFxLevlr, (fs::path (file) / "presets" / "Levlr" / "x.vstpreset").string (), pluginDefaults (kFxLevlr), {}),
               "nothing saved");
        bool applied = true;
        CHECK (rackio::newSlotValues (kFxLevlr, &applied) == pluginDefaults (kFxLevlr) && !applied, "a new slot: the factory defaults");
        CHECK (!rackio::factoryPresetsOf (kFxLevlr).empty (), "the factory presets are still there");
        setPresetsDir (saved.c_str ());
    }
}

static void newSlotDefaults ()
{
    // no saved default and no switches: the factory defaults
    for (int type : kHostedTypes)
    {
        bool applied = true;
        CHECK (rackio::newSlotValues (type, &applied) == pluginDefaults (type) && !applied, "%s: factory defaults", fxName (type));
    }
    // Smacheratr's Save as Default (written as the slot's Save as Default writes it), then Gentlr and Advanced
    // on in its Menu > Defaults
    std::vector<double> def = pluginDefaults (kFxSmacheratr);
    def[smacheratr::kDrive] = 0.81;
    def[smacheratr::kClarity] = 0.0;
    def[smacheratr::kClarityAdvanced] = 0.0;
    pk::presets::Meta m;
    m.name = pk::presets::kDefaultName;
    CHECK (!rackio::folderOf (kFxSmacheratr).empty () && rackio::writeValues (kFxSmacheratr, rackio::defaultPathOf (kFxSmacheratr), def, m),
           "default saved");
    CHECK (rackio::hasDefault (kFxSmacheratr) && fs::path (rackio::defaultPathOf (kFxSmacheratr)).filename () == pk::presets::kDefaultFile,
           "the plug-in's .default.vstpreset");
    bool applied = false;
    std::vector<double> v = rackio::newSlotValues (kFxSmacheratr, &applied);
    CHECK (applied && v[smacheratr::kDrive] == 0.81 && v[smacheratr::kClarity] == 0.0, "the saved default (%g)", v[smacheratr::kDrive]);
    pk::GentlrDefaults sw;
    sw.gentlrOn = true;
    sw.advancedOn = true;
    CHECK (pk::writeGentlrDefaults (rackio::folderOf (kFxSmacheratr), sw), "switches written");
    v = rackio::newSlotValues (kFxSmacheratr, &applied);
    CHECK (applied && v[smacheratr::kDrive] == 0.81 && v[smacheratr::kClarity] == 1.0 && v[smacheratr::kClarityAdvanced] == 1.0,
           "the switches over the saved default");
    // in the slot (Editor::addFx: the factory defaults, then these)
    std::vector<double> rack = rackWith (4, kFxSmacheratr);
    applyEdits (rack, slotEditsFor (4, kFxSmacheratr, v));
    CHECK (rack[slotBlockParam (4, smacheratr::kDrive)] == 0.81 && rack[slotBlockParam (4, smacheratr::kClarityAdvanced)] == 1.0,
           "a new slot gets them");
    // Gentlr: Advanced off in its switches, no saved default
    sw = {};
    sw.advancedOn = false;
    CHECK (pk::writeGentlrDefaults (rackio::folderOf (kFxGentlr), sw), "gentlr switches written");
    std::vector<double> g = rackio::newSlotValues (kFxGentlr, &applied);
    CHECK (!applied && g[gentlr::kAdvanced] == 0.0, "gentlr: Advanced off");
    sw.advancedOn = true;
    pk::writeGentlrDefaults (rackio::folderOf (kFxGentlr), sw);
    g = rackio::newSlotValues (kFxGentlr, &applied);
    CHECK (g[gentlr::kAdvanced] == 1.0, "gentlr: Advanced on");
    // Para's switches set its end saturator, which a Para slot carries (and does not run)
    sw = {};
    sw.gentlrOn = true;
    pk::writeGentlrDefaults (rackio::folderOf (kFxPara), sw);
    const std::vector<double> p = rackio::newSlotValues (kFxPara);
    CHECK (p[(size_t)para::kGentlrIds.saturator] == 1.0 && p[(size_t)para::kGentlrIds.gentlr] == 1.0, "para: the saturator's switches");

    // a project loads as saved, whatever the plug-in's default and switches say
    PluginState st;
    for (uint32_t id = 0; id < kNumParams; ++id)
    {
        st.norm[id] = defaultNormalized (id);
        st.has[id] = true;
    }
    st.norm[slotParam (2, kSlotType)] = toNormalized (slotParam (2, kSlotType), kFxSmacheratr);
    st.norm[slotBlockParam (2, smacheratr::kDrive)] = 0.2;
    st.norm[slotBlockParam (2, smacheratr::kClarity)] = 0.0;
    st.norm[slotBlockParam (2, smacheratr::kClarityAdvanced)] = 0.0;
    Steinberg::MemoryStream s;
    PluginState back;
    CHECK (writeState (&s, st), "project written");
    s.seek (0, Steinberg::IBStream::kIBSeekSet, nullptr);
    CHECK (readState (&s, back), "project read");
    CHECK (back.norm[slotBlockParam (2, smacheratr::kDrive)] == 0.2 && back.norm[slotBlockParam (2, smacheratr::kClarity)] == 0.0 &&
               back.norm[slotBlockParam (2, smacheratr::kClarityAdvanced)] == 0.0,
           "the project's slot as saved");
}

// smemplr's parameters, recording the edits (the editor's host, as the slot pages see it)
struct RackHost : pk::ParamHost
{
    std::vector<double> n;
    std::vector<uint32_t> began, ended;
    const pk::ParamTable& table () override { return paramTable (); }
    double norm (uint32_t id) override { return n[id]; }
    double plainValue (uint32_t id) override { return paramTable ().toPlain (id, n[id]); }
    void beginEdit (uint32_t id) override { began.push_back (id); }
    void setNorm (uint32_t id, double v) override { n[id] = v; }
    void endEdit (uint32_t id) override { ended.push_back (id); }
    std::string valueText (uint32_t) override { return {}; }
};

static void gentlrThresholdSliders ()
{
    using namespace VSTGUI;
    // (VSTGUI's platform: its mouse events are stamped with its clock)
#if defined(__APPLE__)
    VSTGUI::init (CFBundleGetMainBundle ());
#elif defined(_WIN32)
    VSTGUI::init (GetModuleHandle (nullptr));
#else
    VSTGUI::init (dlopen (nullptr, RTLD_LAZY));
#endif
    for (int slot : {0, 3, kRackSlots - 1})
        for (int k = 0; k < gentlr::kAllBands; ++k)
        {
            // the Gentlr page's chain: the slot's host (Editor::hostFor), then Smacheratr's IDs on Gentlr's
            RackHost rack;
            rack.n = rackWith (slot, kFxGentlr);
            pk::MappedParamHost slotHost (&rack, fxTable (kFxGentlr), [slot] (uint32_t id) { return slotParamOf (slot, kFxGentlr, id); });
            pk::MappedParamHost sliderHost (&slotHost, smacheratr::paramTable (), [] (uint32_t id) { return gentlr::fromSmacheratr (id); });
            auto* slider = new smacheratr::ThresholdSlider (CRect (0, 0, 24, 196), &sliderHost, k, nullptr);
            int picked = -1;
            slider->onPicked = [&] (int b) { picked = b; };
            const std::vector<double> before = rack.n;
            MouseDownEvent down (CPoint (12, 120), MouseButton::Left);
            down.clickCount = 1;
            slider->onMouseDownEvent (down);
            MouseMoveEvent move;
            move.mousePosition = CPoint (12, 70);
            move.buttonState.set (MouseButton::Left);
            slider->onMouseMoveEvent (move);
            MouseUpEvent up;
            up.mousePosition = move.mousePosition;
            slider->onMouseUpEvent (up);
            const int64_t want = slotParamOf (slot, kFxGentlr, gentlr::thresholdParam (k));
            CHECK (want >= 0 && rack.n[(size_t)want] > before[(size_t)want], "slot %d band %d: its Threshold went up (%g -> %g)", slot, k,
                   want >= 0 ? before[(size_t)want] : -1.0, want >= 0 ? rack.n[(size_t)want] : -1.0);
            int moved = 0;
            for (size_t id = 0; id < rack.n.size (); ++id)
                moved += (int64_t)id != want && rack.n[id] != before[id] ? 1 : 0;
            CHECK (moved == 0, "slot %d band %d: %d other parameters moved", slot, k, moved);
            CHECK (rack.began.size () == 1 && rack.ended.size () == 1 && (int64_t)rack.began[0] == want && (int64_t)rack.ended[0] == want,
                   "slot %d band %d: one gesture on it", slot, k);
            CHECK (picked == k, "slot %d band %d: grabbing it picks its band", slot, k);
            // and it shows that slot's value (drawn from it)
            CHECK (sliderHost.norm (smacheratr::kGentlrThresholdIds[k]) == rack.n[(size_t)want], "slot %d band %d: reads its slot", slot, k);
            slider->forget ();
        }
    VSTGUI::exit ();
}

int main ()
{
    tmpRoot = (fs::temp_directory_path () / ("smemplr_rack_presets_" + std::to_string ((long)std::rand ()) + std::to_string ((long)time (nullptr))))
                  .string ();
    std::error_code ec;
    fs::create_directories (tmpRoot, ec);
    setPresetsDir ((fs::path (tmpRoot) / "presets").string ().c_str ());
    struct T
    {
        const char* name;
        void (*fn) ();
    } tests[] = {
        {"factory presets load into slots 0 and 7", factoryPresetsLoadIntoSlots},
        {"values outside the block are ignored", outsideTheBlockIgnored},
        {"saved from a slot, read by the plug-in", savedFromSlotReadsInThePlugin},
        {"robustness", robustness},
        {"a new slot's defaults; a project loads as saved", newSlotDefaults},
        {"the Gentlr page's Threshold sliders move their slot's Thresholds", gentlrThresholdSliders},
    };
    for (const auto& t : tests)
    {
        const int before = gFailures;
        t.fn ();
        std::printf ("%s %s\n", gFailures == before ? "  ok  " : "  FAIL", t.name);
    }
    fs::remove_all (tmpRoot, ec);
    std::printf ("rack preset tests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
