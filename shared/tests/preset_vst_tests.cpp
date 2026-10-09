// The preset system with the SDK, on a small processor / controller pair connected the way a host
// connects them (in a temporary preset folder, $PK_PRESETS_DIR): .vstpreset round trips with tags,
// files without metadata (saved before 0.13), Init, factory presets, the tag filter, Save as Default
// (a new instance starts from it, a project's state still wins), Reset Default, rename / delete and
// the menu message the macOS host tests use; the editor's layout in the controller's state (Wide for a new
// instance and for older states without it, an explicit Classic kept, presets keeping the layout, the
// user's default layout winning over Wide); the Gentlr defaults for new instances (Menu > Defaults: on top
// of the saved default preset, never over a project or a loaded preset). Run: ./pluginkit_preset_tests
#include "pluginkit/vst/ControllerBase.h"
#include "pluginkit/vst/Presets.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace fs = std::filesystem;

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

// what the editor support code expects from a plug-in module (a test is not one)
void* moduleHandle = nullptr;
extern "C" SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory () { return nullptr; }

namespace {

const FUID kProcId (0x11111111, 0x22222222, 0x33333333, 0x44444444);
const FUID kOtherId (0x55555555, 0x22222222, 0x33333333, 0x44444444);
constexpr const char* kName = "PresetTestPlug";
constexpr uint32_t kN = 7;
// the parameters Menu > Defaults switches on (an end saturator's on switch, its Gentlr and Advanced)
constexpr pk::GentlrIds kIds {4, 5, 6};

const pk::ParamTable& testTable ()
{
    static const pk::ParamTable t ({
        pk::make::real (0, "Frequency", "Freq", 20.0, 20000.0, 250.0, pk::Curve::Log, pk::Disp::Hz),
        pk::make::real (1, "Range", "Range", 0.0, 24.0, 8.0, pk::Curve::Linear, pk::Disp::Db),
        pk::make::choice (2, "Mode", "Mode", {"Soft", "Hard"}, 0),
        pk::make::percent (3, "Mix", "Mix", 1.0),
        pk::make::toggle (4, "Saturator", "Saturator", false),
        pk::make::toggle (5, "Saturator Gentlr", "Gentlr", false),
        pk::make::toggle (6, "Saturator Gentlr Advanced", "Advanced", false),
    });
    return t;
}

// the state: a magic number, then every value
bool writeValues (IBStream* s, const std::array<double, kN>& v)
{
    IBStreamer st (s, kLittleEndian);
    bool ok = st.writeInt32 (0x50525354);
    for (double x : v)
        ok = ok && st.writeDouble (x);
    return ok;
}
bool readValues (IBStream* s, std::array<double, kN>& v)
{
    IBStreamer st (s, kLittleEndian);
    int32 magic = 0;
    if (!st.readInt32 (magic) || magic != 0x50525354)
        return false;
    for (auto& x : v)
        if (!st.readDouble (x))
            return false;
    return true;
}
std::array<double, kN> defaults ()
{
    std::array<double, kN> d;
    for (uint32_t i = 0; i < kN; ++i)
        d[i] = testTable ().defaultNormalized (i);
    return d;
}

class Proc : public AudioEffect
{
public:
    std::array<double, kN> v = defaults ();
    tresult PLUGIN_API initialize (FUnknown* context) override
    {
        const tresult r = AudioEffect::initialize (context);
        if (r == kResultOk)
            pk::presets::applyDefault (*this, kProcId, kName, kIds, [this] (uint32_t id, double n) { v[id] = n; });
        return r;
    }
    tresult PLUGIN_API setState (IBStream* s) override { return readValues (s, v) ? kResultOk : kResultFalse; }
    tresult PLUGIN_API getState (IBStream* s) override { return writeValues (s, v) ? kResultOk : kResultFalse; }
    tresult PLUGIN_API notify (IMessage* m) override
    {
        return pk::presets::handleProcessorMessage (*this, m) ? kResultOk : AudioEffect::notify (m);
    }
};

class Ctrl : public pk::ControllerBase
{
public:
    Ctrl () : pk::ControllerBase (testTable ())
    {
        setPresetInfo (kProcId, kName);
        setGentlrIds (kIds);
    }
    int extraResets = 0;
    tresult PLUGIN_API setComponentState (IBStream* s) override
    {
        std::array<double, kN> v;
        if (!readValues (s, v))
            return kResultFalse;
        for (uint32_t i = 0; i < kN; ++i)
            setParamNormalized (i, v[i]);
        return kResultOk;
    }

protected:
    void resetExtraState () override { ++extraResets; }
};

// a processor and a controller, initialized and connected as a host does
struct Instance
{
    IPtr<Proc> proc;
    IPtr<Ctrl> ctrl;
    explicit Instance (FUnknown* host)
    {
        proc = owned (new Proc ());
        ctrl = owned (new Ctrl ());
        proc->initialize (host);
        ctrl->initialize (host);
        proc->connect (ctrl);
        ctrl->connect (proc);
    }
    ~Instance ()
    {
        proc->disconnect (ctrl);
        ctrl->disconnect (proc);
        ctrl->terminate ();
        proc->terminate ();
    }
    // a host edit: both halves
    void set (uint32_t id, double n)
    {
        ctrl->setParamNormalized (id, n);
        proc->v[id] = n;
    }
    double ctl (uint32_t id) { return ctrl->getParamNormalized (id); }
};

bool near (double a, double b) { return std::fabs (a - b) < 1e-9; }

std::vector<char> stateOf (const std::array<double, kN>& v)
{
    MemoryStream ms;
    writeValues (&ms, v);
    return std::vector<char> (ms.getData (), ms.getData () + ms.getSize ());
}

const pk::presets::Item* findItem (const std::vector<pk::presets::Item>& items, const std::string& name)
{
    for (const auto& i : items)
        if (i.name == name)
            return &i;
    return nullptr;
}

std::vector<std::string> menuViaMessage (Ctrl* c)
{
    auto msg = owned (new HostMessage ());
    msg->setMessageID (pk::presets::kMsgMenu);
    c->notify (msg);
    const void* data = nullptr;
    uint32 size = 0;
    std::vector<std::string> out;
    if (msg->getAttributes ()->getBinary (pk::presets::kAttrItems, data, size) != kResultOk || !data)
        return out;
    std::string s ((const char*)data, size), cur;
    for (char ch : s)
        if (ch == '\n')
        {
            out.push_back (cur);
            cur.clear ();
        }
        else
            cur += ch;
    return out;
}

bool contains (const std::vector<std::string>& v, const std::string& s)
{
    for (const auto& x : v)
        if (x == s)
            return true;
    return false;
}

} // namespace

int main ()
{
    const fs::path root = fs::temp_directory_path () / ("pk_preset_tests_" + std::to_string ((long long)std::rand ()) + "_" +
                                                         std::to_string ((long long)fs::file_time_type::clock::now ().time_since_epoch ().count ()));
    fs::create_directories (root);
#if defined(_WIN32)
    _putenv_s ("PK_PRESETS_DIR", root.string ().c_str ());
#else
    setenv ("PK_PRESETS_DIR", root.string ().c_str (), 1);
#endif
    auto* hostApp = new HostApplication ();
    FUnknown* host = hostApp;
    const fs::path folder = root / kName;

    std::printf ("fileRoundTripWithTags\n");
    {
        fs::create_directories (folder / "Drums");
        const auto comp = stateOf ({0.1, 0.2, 1.0, 0.4});
        pk::presets::Meta m;
        m.name = "Kick";
        m.plugin = kName;
        m.category = "Drums";
        m.tags = {"punchy", "low end"};
        const std::string path = (folder / "Drums" / "Kick.vstpreset").string ();
        CHECK (pk::presets::write (path, kProcId, comp, {}, &m), "write");
        std::vector<char> c2, k2;
        CHECK (pk::presets::read (path, kProcId, c2, k2) && c2 == comp, "states back");
        CHECK (!pk::presets::read (path, kOtherId, c2, k2), "another plug-in's class refuses it");
        const auto back = pk::presets::readMeta (path);
        CHECK (back.tags == m.tags && back.category == "Drums" && back.name == "Kick" && back.plugin == kName, "%s",
               pk::presets::joinTags (back.tags).c_str ());
        // edit the tags later: the states stay
        pk::presets::Meta m2 = back;
        m2.tags = {"808"};
        CHECK (pk::presets::rewriteMeta (path, kProcId, m2), "rewrite");
        CHECK (pk::presets::readMeta (path).tags == std::vector<std::string> {"808"}, "new tags");
        CHECK (pk::presets::read (path, kProcId, c2, k2) && c2 == comp, "states unchanged");
        fs::remove (path);
    }

    std::printf ("oldFilesWithoutMetadata\n");
    {
        fs::create_directories (folder);
        const auto comp = stateOf ({0.3, 0.3, 0.0, 0.3});
        const std::string path = (folder / "Old.vstpreset").string ();
        CHECK (pk::presets::write (path, kProcId, comp, {}, nullptr), "write as 0.12 did");
        std::string xml;
        CHECK (!pk::presets::readMetaXml (path, xml), "no Info chunk");
        const auto m = pk::presets::readMeta (path);
        CHECK (m.tags.empty () && m.category.empty (), "empty metadata");
        const auto items = pk::presets::listUser (folder.string ());
        const auto* it = findItem (items, "Old");
        CHECK (it && it->tags.empty () && it->category.empty (), "listed");
        Instance a (host);
        CHECK (a.ctrl->loadPreset (path), "loads");
        CHECK (near (a.ctl (0), 0.3) && near (a.proc->v[3], 0.3), "values from it (controller %g, processor %g)", a.ctl (0), a.proc->v[3]);
        CHECK (a.ctrl->presetName () == "Old" && a.ctrl->presetKind () == pk::presets::Kind::User, "a user preset");
        // tags can be added to it
        CHECK (a.ctrl->setPresetTags (path, {"vintage"}), "tag it");
        CHECK (pk::presets::readMeta (path).tags == std::vector<std::string> {"vintage"}, "tagged");
        fs::remove (path);
    }

    std::printf ("saveUserPresetsAndFilterByTag\n");
    {
        Instance a (host);
        a.set (0, 0.7);
        a.set (1, 0.25);
        CHECK (a.ctrl->saveUserPreset ("Bright", "Vocals", {"vocal", "bright"}), "save");
        CHECK (fs::exists (folder / "Vocals" / "Bright.vstpreset"), "in its category's folder");
        CHECK (a.ctrl->presetName () == "Bright" && a.ctrl->presetKind () == pk::presets::Kind::User, "current");
        a.set (0, 0.2);
        CHECK (a.ctrl->saveUserPreset ("Dark", "", {"vocal"}), "save 2");
        CHECK (!a.ctrl->saveUserPreset ("Init", "", {}), "Init cannot be a user preset");
        CHECK (!a.ctrl->saveUserPreset ("a/b", "", {}), "bad name");
        const auto items = a.ctrl->userItems ();
        CHECK (items.size () == 2, "%zu user presets", items.size ());
        const auto* b = findItem (items, "Bright");
        CHECK (b && b->category == "Vocals" && pk::presets::hasTag (b->tags, "bright"), "tags listed");
        a.ctrl->tagFilter = "bright";
        auto titles = pk::presets::menuTitles (a.ctrl->presetMenu ());
        CHECK (contains (titles, "Vocals/Bright") && !contains (titles, "Dark"), "filtered");
        a.ctrl->tagFilter.clear ();
        titles = pk::presets::menuTitles (a.ctrl->presetMenu ());
        CHECK (contains (titles, "Vocals/Bright") && contains (titles, "Dark"), "all");
        // load it back in a new instance
        Instance c (host);
        CHECK (c.ctrl->loadPreset ((folder / "Vocals" / "Bright.vstpreset").string ()), "load");
        CHECK (near (c.ctl (0), 0.7) && near (c.ctl (1), 0.25) && near (c.proc->v[0], 0.7), "values");
        // Save keeps tags and category
        c.set (2, 1.0);
        CHECK (c.ctrl->savePreset (c.ctrl->presetPath ()), "save over");
        const auto m = pk::presets::readMeta ((folder / "Vocals" / "Bright.vstpreset").string ());
        CHECK (m.category == "Vocals" && m.tags.size () == 2, "kept its metadata");
        // rename and delete
        CHECK (c.ctrl->renamePreset (c.ctrl->presetPath (), "Shiny"), "rename");
        CHECK (fs::exists (folder / "Vocals" / "Shiny.vstpreset") && !fs::exists (folder / "Vocals" / "Bright.vstpreset"), "renamed");
        CHECK (c.ctrl->presetName () == "Shiny", "%s", c.ctrl->presetName ().c_str ());
        {
            Instance o (host);
            CHECK (o.ctrl->saveUserPreset ("Other", "Vocals", {}), "another in the category");
        }
        CHECK (!c.ctrl->renamePreset (c.ctrl->presetPath (), "Other"), "a rename never replaces another preset");
        CHECK (fs::exists (folder / "Vocals" / "Shiny.vstpreset") && fs::exists (folder / "Vocals" / "Other.vstpreset"), "both still there");
        fs::remove (folder / "Vocals" / "Other.vstpreset");
        CHECK (c.ctrl->deletePreset (c.ctrl->presetPath ()), "delete");
        CHECK (!fs::exists (folder / "Vocals" / "Shiny.vstpreset") && c.ctrl->presetKind () == pk::presets::Kind::None, "gone");
        fs::remove (folder / "Dark.vstpreset");
    }

    std::printf ("initIsTheDefaults\n");
    {
        Instance a (host);
        for (uint32_t i = 0; i < kN; ++i)
            a.set (i, 0.9);
        CHECK (a.ctrl->loadInit (), "Init");
        for (uint32_t i = 0; i < kN; ++i)
            CHECK (near (a.ctl (i), testTable ().defaultNormalized (i)), "param %u at its default (%g)", i, a.ctl (i));
        CHECK (a.ctrl->presetName () == "Init" && a.ctrl->presetKind () == pk::presets::Kind::Init, "named Init");
        CHECK (a.ctrl->extraResets == 1, "extra state reset");
        const auto menu = a.ctrl->presetMenu ();
        CHECK (!menu.empty () && menu[0].title == "Init" && menu[0].checked, "Init first and checked");
    }

    std::printf ("factoryPresets\n");
    {
        static const pk::presets::FactoryFile files[] = {
            {"Mixing/Tame.txt", "tags: mix, harsh\nFrequency = 3 kHz\nRange = 4 dB\n"},
            {"Broken.txt", "Nope = 1\n"}, // skipped: does not parse
        };
        pk::presets::registerFactory (files, 2);
        Instance a (host);
        const auto& f = a.ctrl->factoryPresets ();
        CHECK (f.size () == 1 && f[0].name == "Tame" && f[0].category == "Mixing", "%zu factory presets", f.size ());
        a.set (3, 0.1);
        CHECK (a.ctrl->loadFactory (0), "load");
        CHECK (std::fabs (testTable ().toPlain (0, a.ctl (0)) - 3000.0) < 0.5 && near (testTable ().toPlain (1, a.ctl (1)), 4.0), "its values");
        CHECK (near (a.ctl (3), testTable ().defaultNormalized (3)), "the rest at defaults");
        CHECK (a.ctrl->presetName () == "Tame" && a.ctrl->presetKind () == pk::presets::Kind::Factory, "current");
        a.ctrl->tagFilter = "harsh";
        const auto titles = pk::presets::menuTitles (a.ctrl->presetMenu ());
        CHECK (contains (titles, "Mixing/Tame") && contains (titles, "Tags: harsh/harsh"), "filtered by a factory tag");
        a.ctrl->tagFilter.clear ();
    }

    std::printf ("saveAsDefaultStartsNewInstances\n");
    {
        {
            Instance a (host);
            CHECK (!a.ctrl->hasDefault (), "none yet");
            a.set (0, 0.61);
            a.set (2, 1.0);
            a.ctrl->uiScale = 1.5;
            const std::string before = a.ctrl->presetName ();
            CHECK (a.ctrl->saveAsDefault (), "save as default");
            CHECK (a.ctrl->hasDefault () && fs::exists (folder / pk::presets::kDefaultFile), "the file");
            CHECK (a.ctrl->presetName () == before, "the current preset stays");
            CHECK (pk::presets::listUser (folder.string ()).empty (), "the default is not in the list");
        }
        {
            Instance b (host); // a new instance, no project
            CHECK (near (b.proc->v[0], 0.61) && near (b.proc->v[2], 1.0), "processor starts from it (%g)", b.proc->v[0]);
            CHECK (near (b.ctl (0), 0.61) && near (b.ctl (2), 1.0), "controller starts from it (%g)", b.ctl (0));
            CHECK (b.ctrl->presetName () == "Default" && b.ctrl->presetKind () == pk::presets::Kind::Default, "%s", b.ctrl->presetName ().c_str ());
            CHECK (near (b.ctrl->uiScale, 1.5), "its interface size");
            // Load Default after changes
            b.set (0, 0.1);
            CHECK (b.ctrl->loadDefault () && near (b.ctl (0), 0.61) && near (b.proc->v[0], 0.61), "Load Default");
            // Init is still the factory defaults
            CHECK (b.ctrl->loadInit () && near (b.ctl (0), testTable ().defaultNormalized (0)), "Init is not the saved default");
        }
        {
            // a project: the host restores its state after creating the instance, which wins
            Instance p (host);
            const auto project = stateOf ({0.05, 0.15, 0.0, 0.25});
            MemoryStream ps (const_cast<char*> (project.data ()), (TSize)project.size ());
            CHECK (p.proc->setState (&ps) == kResultOk, "processor project state");
            ps.seek (0, IBStream::kIBSeekSet, nullptr);
            CHECK (p.ctrl->setComponentState (&ps) == kResultOk, "controller project state");
            MemoryStream cs;
            {
                IBStreamer s (&cs, kLittleEndian);
                s.writeDouble (1.0);
                s.writeBool (true);
                s.writeStr8 ("From The Project"); // a controller state as 0.12 wrote it
            }
            cs.seek (0, IBStream::kIBSeekSet, nullptr);
            CHECK (p.ctrl->setState (&cs) == kResultOk, "controller state");
            CHECK (near (p.proc->v[0], 0.05) && near (p.ctl (0), 0.05) && near (p.ctl (3), 0.25), "the project's values, not the default's");
            CHECK (p.ctrl->presetName () == "From The Project" && near (p.ctrl->uiScale, 1.0), "%s", p.ctrl->presetName ().c_str ());
        }
        {
            // the host syncs a new instance's controller from the processor (getState -> setComponentState)
            Instance s (host);
            MemoryStream st;
            s.proc->getState (&st);
            st.seek (0, IBStream::kIBSeekSet, nullptr);
            s.ctrl->setComponentState (&st);
            CHECK (near (s.ctl (0), 0.61), "the same default either way");
        }
        {
            Instance r (host);
            CHECK (r.ctrl->resetDefault () && !r.ctrl->hasDefault (), "Reset Default");
            CHECK (r.ctrl->loadDefault () && r.ctrl->presetKind () == pk::presets::Kind::Init, "Load Default without one: Init");
        }
        {
            Instance n (host);
            for (uint32_t i = 0; i < kN; ++i)
                CHECK (near (n.proc->v[i], testTable ().defaultNormalized (i)) && near (n.ctl (i), testTable ().defaultNormalized (i)),
                       "factory defaults again (param %u)", i);
            CHECK (n.ctrl->presetName ().empty (), "no preset name");
        }
    }

    std::printf ("controllerStateKeepsTheCurrentPreset\n");
    {
        Instance a (host);
        a.ctrl->loadFactory (0);
        MemoryStream st;
        CHECK (a.ctrl->getState (&st) == kResultOk, "get");
        st.seek (0, IBStream::kIBSeekSet, nullptr);
        Instance b (host);
        CHECK (b.ctrl->setState (&st) == kResultOk, "set");
        CHECK (b.ctrl->presetName () == "Tame" && b.ctrl->presetKind () == pk::presets::Kind::Factory &&
                   b.ctrl->presetPath () == "Mixing/Tame.txt",
               "%s", b.ctrl->presetPath ().c_str ());
    }

    std::printf ("colorLayerInTheControllerState\n");
    {
        // a new instance: Gentlr's layer in front (Color up to 0.27)
        Instance n (host);
        CHECK (n.ctrl->uiColorLayer == 1, "a new instance: Gentlr in front (%d)", n.ctrl->uiColorLayer);
        // a state that saved its layer keeps it, Color or Gentlr
        for (int layer : {0, 1})
        {
            Instance a (host);
            a.ctrl->uiColorLayer = layer;
            MemoryStream st;
            CHECK (a.ctrl->getState (&st) == kResultOk, "get");
            st.seek (0, IBStream::kIBSeekSet, nullptr);
            Instance b (host);
            CHECK (b.ctrl->setState (&st) == kResultOk && b.ctrl->uiColorLayer == layer, "saved with %s: kept (%d)", layer ? "Gentlr" : "Color",
                   b.ctrl->uiColorLayer);
        }
        // a state from before the view state (it ends after the preset reference): Color, as it showed then
        MemoryStream old;
        {
            IBStreamer s (&old, kLittleEndian);
            s.writeDouble (1.0);
            s.writeBool (true);
            s.writeStr8 ("Old project");
            s.writeStr8 ("0:");
        }
        old.seek (0, IBStream::kIBSeekSet, nullptr);
        Instance c (host);
        CHECK (c.ctrl->setState (&old) == kResultOk && c.ctrl->uiColorLayer == 0, "no view state: Color (%d)", c.ctrl->uiColorLayer);
        // one with the view state's first value only (no layer): Color too
        MemoryStream one;
        {
            IBStreamer s (&one, kLittleEndian);
            s.writeDouble (1.0);
            s.writeBool (true);
            s.writeStr8 ("Old project");
            s.writeStr8 ("0:");
            s.writeInt32 (0x56575354); // 'VWST'
            s.writeInt32 (1);
            s.writeInt32 (-1);
        }
        one.seek (0, IBStream::kIBSeekSet, nullptr);
        Instance d (host);
        CHECK (d.ctrl->setState (&one) == kResultOk && d.ctrl->uiColorLayer == 0, "a view state without the layer: Color (%d)",
               d.ctrl->uiColorLayer);
    }

    std::printf ("layoutInTheControllerState\n");
    {
        // a new instance (no layouts file): Wide
        {
            Instance n (host);
            CHECK (n.ctrl->uiLayout == "wide" && n.ctrl->uiLayoutName == "Wide", "a new instance: Wide (%s / %s)", n.ctrl->uiLayout.c_str (),
                   n.ctrl->uiLayoutName.c_str ());
        }
        // the layout and its name after the view state, read back
        Instance a (host);
        a.ctrl->uiLayout = "display/sample,filter:400;rack";
        a.ctrl->uiLayoutName = "Mixing";
        a.ctrl->uiTailOpen = pk::ControllerBase::kTailOpenGentlr;
        MemoryStream st;
        CHECK (a.ctrl->getState (&st) == kResultOk, "get");
        st.seek (0, IBStream::kIBSeekSet, nullptr);
        Instance b (host);
        CHECK (b.ctrl->setState (&st) == kResultOk, "set");
        CHECK (b.ctrl->uiLayout == "display/sample,filter:400;rack" && b.ctrl->uiLayoutName == "Mixing", "%s / %s", b.ctrl->uiLayout.c_str (),
               b.ctrl->uiLayoutName.c_str ());
        CHECK (b.ctrl->uiTailOpen == pk::ControllerBase::kTailOpenGentlr, "the view state before it still read");

        // a state from before layouts (it ends after the view state): Wide, as a new instance
        MemoryStream old;
        {
            IBStreamer s (&old, kLittleEndian);
            s.writeDouble (1.25);
            s.writeBool (true);
            s.writeStr8 ("Old preset");
            s.writeStr8 ("0:");
            s.writeInt32 (0x56575354); // 'VWST'
            s.writeInt32 (2);
            s.writeInt32 (pk::ControllerBase::kTailOpenSaturator);
            s.writeInt32 (1);
        }
        old.seek (0, IBStream::kIBSeekSet, nullptr);
        CHECK (b.ctrl->setState (&old) == kResultOk, "old state");
        CHECK (b.ctrl->uiLayout == "wide" && b.ctrl->uiLayoutName == "Wide", "no layout field: Wide (%s)", b.ctrl->uiLayout.c_str ());
        CHECK (std::fabs (b.ctrl->uiScale - 1.25) < 1e-9 && b.ctrl->uiTailOpen == pk::ControllerBase::kTailOpenSaturator && b.ctrl->uiColorLayer == 1,
               "the rest of an old state");

        // what an older version reads of a new state: the fields it knows, in order, then it stops
        MemoryStream st2;
        a.ctrl->getState (&st2);
        st2.seek (0, IBStream::kIBSeekSet, nullptr);
        IBStreamer r (&st2, kLittleEndian);
        double scale = 0;
        bool tips = false;
        int32 tag = 0, count = 0, tailOpen = -1, layer = -1;
        char8* name = nullptr;
        char8* ref = nullptr;
        CHECK (r.readDouble (scale) && r.readBool (tips) && (name = r.readStr8 ()) && (ref = r.readStr8 ()) && r.readInt32 (tag) &&
                   tag == 0x56575354 && r.readInt32 (count) && count == 2 && r.readInt32 (tailOpen) && r.readInt32 (layer),
               "an older reader's fields");
        CHECK (tailOpen == pk::ControllerBase::kTailOpenGentlr, "its view state");
        delete[] name;
        delete[] ref;

        // an explicit Classic layout is kept: as this version writes it ("default", name "Classic"), and as
        // 0.14 wrote its Default ("", name "Default"), which is not the same as no field at all
        for (const auto& [text, label] : std::vector<std::pair<std::string, std::string>> {{"default", "Classic"}, {"", "Default"}})
        {
            a.ctrl->uiLayout = text;
            a.ctrl->uiLayoutName = label;
            MemoryStream cs;
            a.ctrl->getState (&cs);
            cs.seek (0, IBStream::kIBSeekSet, nullptr);
            Instance c (host);
            CHECK (c.ctrl->setState (&cs) == kResultOk, "set");
            CHECK (c.ctrl->uiLayout == text && pk::layout::isClassic (c.ctrl->uiLayout) && c.ctrl->uiLayoutName == label,
                   "explicit Classic kept (\"%s\" -> \"%s\")", text.c_str (), c.ctrl->uiLayout.c_str ());
        }
    }

    std::printf ("presetsKeepTheLayout\n");
    {
        Instance a (host);
        a.ctrl->uiLayout = "wide";
        a.ctrl->uiLayoutName = "Wide";
        CHECK (a.ctrl->saveUserPreset ("Laid out", "", {}), "save");
        a.ctrl->uiLayout = "default";
        a.ctrl->uiLayoutName = "Classic";
        CHECK (a.ctrl->loadPreset ((folder / "Laid out.vstpreset").string ()), "load");
        CHECK (a.ctrl->uiLayout == "default" && a.ctrl->uiLayoutName == "Classic", "the editor's layout stays (%s)", a.ctrl->uiLayout.c_str ());
        a.ctrl->deletePreset ((folder / "Laid out.vstpreset").string ());
    }

    std::printf ("defaultLayoutForNewInstances\n");
    {
        // Use as Default Layout: the layouts file beside the presets; a new instance starts with it, a
        // project's state still wins
        Instance a (host);
        pk::layout::Saved s = a.ctrl->savedLayouts ();
        s.put ("Tracking", "a,b;c");
        s.defaultLayout = "a,b;c";
        s.hasDefault = true;
        CHECK (a.ctrl->writeSavedLayouts (s), "write");
        CHECK (fs::exists (folder / ".layouts.txt"), "beside the presets");
        Instance n (host);
        CHECK (n.ctrl->uiLayout == "a,b;c" && n.ctrl->uiLayoutName == "Tracking", "%s / %s", n.ctrl->uiLayout.c_str (), n.ctrl->uiLayoutName.c_str ());
        CHECK (a.ctrl->uiLayout == "wide", "an instance made before the file: Wide (%s)", a.ctrl->uiLayout.c_str ());
        MemoryStream st;
        a.ctrl->getState (&st); // (a's layout: Wide)
        st.seek (0, IBStream::kIBSeekSet, nullptr);
        n.ctrl->setState (&st);
        CHECK (n.ctrl->uiLayout == "wide", "the project's layout wins (%s)", n.ctrl->uiLayout.c_str ());
        // the message the host tests use
        auto msg = owned (new HostMessage ());
        msg->setMessageID (pk::ControllerBase::kMsgSetLayout);
        msg->getAttributes ()->setBinary ("text", "wide", 4);
        msg->getAttributes ()->setBinary ("name", "Wide", 4);
        n.ctrl->notify (msg);
        CHECK (n.ctrl->uiLayout == "wide" && n.ctrl->uiLayoutName == "Wide", "set by message");
        // the user's default may be the Classic layout (written "default"; 0.14 wrote "" for its Default)
        for (const char* classic : {"default", ""})
        {
            s.defaultLayout = classic;
            CHECK (a.ctrl->writeSavedLayouts (s), "write");
            Instance c (host);
            CHECK (pk::layout::isClassic (c.ctrl->uiLayout) && c.ctrl->uiLayoutName == "Classic", "user default Classic (\"%s\": %s / %s)", classic,
                   c.ctrl->uiLayout.c_str (), c.ctrl->uiLayoutName.c_str ());
        }
        fs::remove (folder / ".layouts.txt");
        Instance m (host);
        CHECK (m.ctrl->uiLayout == "wide" && m.ctrl->uiLayoutName == "Wide", "no file: Wide (%s)", m.ctrl->uiLayout.c_str ());
    }

    std::printf ("gentlrDefaultsForNewInstances\n");
    {
        // Menu > Defaults: the file beside the presets; a new instance gets the switches on (both halves),
        // after the saved default preset; a project or a preset being loaded keeps its own values
        const fs::path file = folder / ".defaults.txt";
        auto on = [] (Instance& i, uint32_t id) { return near (i.ctl (id), 1.0) && near (i.proc->v[id], 1.0); };
        auto off = [] (Instance& i, uint32_t id) { return near (i.ctl (id), 0.0) && near (i.proc->v[id], 0.0); };
        Instance a (host);
        CHECK (!a.ctrl->gentlrDefaults ().gentlrOn.has_value () && !a.ctrl->gentlrDefaults ().advancedOn.has_value (), "no file: not set");
        CHECK (off (a, 4) && off (a, 5) && off (a, 6), "a new instance without them");
        pk::GentlrDefaults d;
        d.gentlrOn = true;
        CHECK (a.ctrl->writeGentlrDefaults (d) && fs::exists (file), "written beside the presets");
        CHECK (a.ctrl->gentlrDefaults ().gentlrOn == true && !a.ctrl->gentlrDefaults ().advancedOn.has_value (), "read back");
        CHECK (off (a, 5), "the instance it was set in keeps its values");
        {
            Instance b (host);
            CHECK (on (b, 4) && on (b, 5) && off (b, 6), "Gentlr On: the saturator and its Gentlr (%g %g %g)", b.ctl (4), b.ctl (5), b.ctl (6));
            CHECK (b.ctrl->presetName ().empty () && near (b.ctl (0), testTable ().defaultNormalized (0)), "the rest as a new instance has it");
            // the host syncs the controller from the processor: the same
            MemoryStream st;
            b.proc->getState (&st);
            st.seek (0, IBStream::kIBSeekSet, nullptr);
            b.ctrl->setComponentState (&st);
            CHECK (on (b, 4) && on (b, 5), "the same either way");
            // Init is still the factory defaults
            CHECK (b.ctrl->loadInit () && near (b.ctl (5), 0.0), "Init leaves them out");
        }
        d.advancedOn = true;
        CHECK (a.ctrl->writeGentlrDefaults (d), "write");
        {
            Instance c (host);
            CHECK (on (c, 4) && on (c, 5) && on (c, 6), "and Advanced");
        }
        d.gentlrOn = false;
        CHECK (a.ctrl->writeGentlrDefaults (d), "write");
        {
            Instance c (host);
            CHECK (off (c, 4) && off (c, 5) && on (c, 6), "Gentlr off, Advanced on");
        }
        d.gentlrOn = true;
        CHECK (a.ctrl->writeGentlrDefaults (d), "write");
        {
            // a project: the host restores its state after creating the instance, which wins
            Instance p (host);
            const auto project = stateOf ({0.05, 0.15, 0.0, 0.25, 0.0, 0.0, 0.0});
            MemoryStream ps (const_cast<char*> (project.data ()), (TSize)project.size ());
            CHECK (p.proc->setState (&ps) == kResultOk, "processor project state");
            ps.seek (0, IBStream::kIBSeekSet, nullptr);
            CHECK (p.ctrl->setComponentState (&ps) == kResultOk, "controller project state");
            CHECK (off (p, 4) && off (p, 5) && off (p, 6) && near (p.ctl (0), 0.05), "the project's values");
        }
        {
            // a preset being loaded: its values
            const std::string path = (folder / "Plain.vstpreset").string ();
            CHECK (pk::presets::write (path, kProcId, stateOf ({0.3, 0.3, 0.0, 0.3, 0.0, 0.0, 0.0}), {}, nullptr), "a preset");
            Instance l (host);
            CHECK (on (l, 5), "(a new instance)");
            CHECK (l.ctrl->loadPreset (path), "load");
            CHECK (near (l.ctl (5), 0.0) && near (l.ctl (6), 0.0) && near (l.ctl (0), 0.3), "the preset's values");
            fs::remove (path);
        }
        {
            // with a saved default preset: it first, then the switches over it
            {
                Instance s (host);
                s.set (0, 0.61);
                s.set (4, 0.0);
                s.set (5, 0.0);
                s.set (6, 0.0);
                CHECK (s.ctrl->saveAsDefault (), "save as default");
            }
            Instance n (host);
            CHECK (near (n.ctl (0), 0.61) && near (n.proc->v[0], 0.61), "the default preset's values");
            CHECK (on (n, 4) && on (n, 5) && on (n, 6), "the switches over them");
            CHECK (n.ctrl->presetKind () == pk::presets::Kind::Default, "started from the default");
            CHECK (n.ctrl->loadDefault () && near (n.ctl (5), 0.0) && near (n.proc->v[5], 0.0), "Load Default: the preset as saved");
            // not set: the default preset decides; set off: off over it (the saturator as the preset has it)
            {
                Instance s (host);
                s.set (4, 1.0);
                s.set (5, 1.0);
                s.set (6, 1.0);
                CHECK (s.ctrl->saveAsDefault (), "save as default 2");
            }
            CHECK (a.ctrl->writeGentlrDefaults ({}), "neither set");
            {
                Instance m (host);
                CHECK (on (m, 4) && on (m, 5) && on (m, 6) && near (m.ctl (0), 0.61), "the default preset's");
            }
            pk::GentlrDefaults offs;
            offs.gentlrOn = false;
            offs.advancedOn = false;
            CHECK (a.ctrl->writeGentlrDefaults (offs), "both off");
            Instance m (host);
            CHECK (on (m, 4) && off (m, 5) && off (m, 6) && near (m.ctl (0), 0.61), "off over the default preset (%g %g %g)", m.ctl (4),
                   m.ctl (5), m.ctl (6));
            CHECK (m.ctrl->resetDefault (), "reset default");
        }
        // a corrupt file: off
        {
            std::FILE* f = std::fopen (file.string ().c_str (), "wb");
            if (f)
            {
                std::fputs ("gentlr\n\x01 advanced == on? \n", f);
                std::fclose (f);
            }
            Instance c (host);
            CHECK (off (c, 4) && off (c, 5) && off (c, 6), "corrupt file: not set (the factory defaults)");
        }
        fs::remove (file);
    }

    std::printf ("menuMessage\n");
    {
        Instance a (host);
        const auto items = menuViaMessage (a.ctrl);
        CHECK (!items.empty () && items[0] == "Init", "Init first (%zu items)", items.size ());
        for (const char* t : {"Save as Default", "Load Default", "Reset Default", "Save As...", "Edit Tags...", "Tags/All", "Mixing/Tame"})
            CHECK (contains (items, t), "has %s", t);
    }

    hostApp->release ();
    std::error_code ec;
    fs::remove_all (root, ec);
    std::printf ("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
