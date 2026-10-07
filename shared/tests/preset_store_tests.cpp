// The preset store without the SDK (pluginkit/PresetStore.h): tags, the metadata XML, factory preset
// parsing, the tag filter and the Presets menu's layout; the Gentlr defaults file beside the presets
// (pluginkit/GentlrDefaults.h).
#include "pluginkit/GentlrDefaults.h"
#include "pluginkit/PresetStore.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace pk;
using namespace pk::presets;

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

static const ParamTable& table ()
{
    static const ParamTable t ({
        make::real (0, "Frequency", "Freq", 20.0, 20000.0, 250.0, Curve::Log, Disp::Hz),
        make::real (1, "Range", "Range", 0.0, 24.0, 8.0, Curve::Linear, Disp::Db),
        make::choice (2, "Mode", "Mode", {"Soft", "Hard"}, 0),
        make::toggle (3, "Band On", "On", true),
        make::percent (4, "Mix", "Mix", 1.0),
    });
    return t;
}

static bool contains (const std::vector<std::string>& v, const std::string& s)
{
    for (const auto& x : v)
        if (x == s)
            return true;
    return false;
}

static void tagsParseTrimAndDedupe ()
{
    const auto t = parseTags (" vocal, Bright ;vocal,, warm  ,BRIGHT");
    CHECK (t.size () == 3, "%zu tags", t.size ());
    CHECK (t.size () == 3 && t[0] == "vocal" && t[1] == "Bright" && t[2] == "warm", "%s", joinTags (t).c_str ());
    CHECK (joinTags (t) == "vocal, Bright, warm", "%s", joinTags (t).c_str ());
    CHECK (hasTag (t, "BRIGHT") && !hasTag (t, "dark"), "case aside");
    CHECK (parseTags ("").empty () && parseTags (" , ;").empty (), "empty");
}

static void metaXmlRoundTrip ()
{
    Meta m;
    m.name = "Big & \"loud\" <one>";
    m.plugin = "Gentlr";
    m.category = "Mixing";
    m.comment = "it's fine";
    m.tags = {"mix", "low mids"};
    const std::string xml = metaToXml (m);
    CHECK (xml.find ("<MetaInfo>") != std::string::npos && xml.find ("MediaType") != std::string::npos, "standard XML");
    Meta r;
    CHECK (metaFromXml (xml, r), "parses");
    CHECK (r.name == m.name && r.plugin == m.plugin && r.category == m.category && r.comment == m.comment, "%s / %s", r.name.c_str (),
           r.category.c_str ());
    CHECK (r.tags == m.tags, "%s", joinTags (r.tags).c_str ());
    Meta none;
    CHECK (!metaFromXml ("not xml", none) && none.tags.empty (), "not MetaInfo");
    // a host's MetaInfo without our fields: nothing breaks
    Meta host;
    CHECK (metaFromXml ("<?xml version=\"1.0\"?><MetaInfo><Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/></MetaInfo>", host),
           "host meta");
    CHECK (host.tags.empty () && host.category.empty (), "no tags");
}

static void factoryPresetParses ()
{
    FactoryPreset fp;
    std::string err;
    const char* text = "# a comment\n"
                       "name: Tame It\n"
                       "tags: mix, harsh\n"
                       "comment: for tests\n"
                       "Frequency = 3 kHz\n"
                       "range = 4 dB\n"
                       "Mode = Hard\n"
                       "#3 = Off\n";
    CHECK (parseFactoryPreset (text, "Mixing/Tame.txt", table (), fp, err), "%s", err.c_str ());
    CHECK (fp.name == "Tame It" && fp.category == "Mixing" && fp.tags.size () == 2, "%s / %s", fp.name.c_str (), fp.category.c_str ());
    CHECK (fp.values.size () == 4, "%zu values", fp.values.size ());
    for (const auto& [id, n] : fp.values)
    {
        const double plain = table ().toPlain (id, n);
        if (id == 0)
            CHECK (std::fabs (plain - 3000.0) < 0.5, "freq %g", plain);
        if (id == 1)
            CHECK (std::fabs (plain - 4.0) < 1e-6, "range %g", plain);
        if (id == 2)
            CHECK (plain == 1.0, "mode %g", plain);
        if (id == 3)
            CHECK (plain == 0.0, "on %g", plain);
    }
    // the name and category default to the file's
    CHECK (parseFactoryPreset ("Mix = 50 %\n", "Lows/Half Wet.txt", table (), fp, err) && fp.name == "Half Wet" && fp.category == "Lows",
           "%s", err.c_str ());
    CHECK (parseFactoryPreset ("Mix = 50 %\n", "Root.txt", table (), fp, err) && fp.category.empty (), "no category");
}

static void factoryPresetErrors ()
{
    FactoryPreset fp;
    std::string err;
    CHECK (!parseFactoryPreset ("Nope = 3\n", "a.txt", table (), fp, err) && err.find ("unknown parameter") != std::string::npos, "%s",
           err.c_str ());
    CHECK (!parseFactoryPreset ("Range = 30 dB\n", "a.txt", table (), fp, err) && err.find ("outside") != std::string::npos, "%s", err.c_str ());
    CHECK (!parseFactoryPreset ("Frequency = 5 Hz\n", "a.txt", table (), fp, err), "below the range");
    CHECK (!parseFactoryPreset ("Mode = Medium\n", "a.txt", table (), fp, err), "not a choice");
    CHECK (!parseFactoryPreset ("Mix = 1\nMix = 2\n", "a.txt", table (), fp, err), "twice");
    CHECK (!parseFactoryPreset ("colour: red\n", "a.txt", table (), fp, err), "unknown key");
    CHECK (!parseFactoryPreset ("Mix = 50 %\n", "Init.txt", table (), fp, err), "Init is reserved");
    CHECK (!parseFactoryPreset ("#99 = 1\n", "a.txt", table (), fp, err), "unknown ID");
}

static void namesAreChecked ()
{
    CHECK (validName ("My Preset 2"), "plain");
    CHECK (!validName ("") && !validName ("a/b") && !validName ("a\\b") && !validName (".hidden") && !validName ("x:y"), "bad chars");
    CHECK (!validName ("Init") && !validName ("init") && validName ("Init", true), "Init reserved for presets");
    CHECK (!validName (" padded"), "untrimmed");
    CHECK (nameOf ("/a/b/My One.vstpreset") == "My One" && withExtension ("x") == "x.vstpreset" && withExtension ("x.VSTPRESET") == "x.VSTPRESET",
           "names");
}

static std::vector<Item> sampleFactory ()
{
    std::vector<Item> f (3);
    f[0] = {"De-mud", "Mixing", "Mixing/De-mud.txt", {"mix", "low mids"}, true, 0};
    f[1] = {"Tame Harshness", "Mixing", "Mixing/Tame Harshness.txt", {"mix", "harsh"}, true, 1};
    f[2] = {"Gentle", "", "Gentle.txt", {}, true, 2};
    return f;
}

static std::vector<Item> sampleUser ()
{
    std::vector<Item> u (2);
    u[0] = {"Mine", "", "/u/Mine.vstpreset", {"vocal"}, false, -1};
    u[1] = {"Old One", "Drums", "/u/Drums/Old One.vstpreset", {}, false, -1};
    return u;
}

static void tagFilter ()
{
    std::vector<Item> all = sampleFactory ();
    for (const auto& u : sampleUser ())
        all.push_back (u);
    const auto tags = allTags (all);
    CHECK (tags.size () == 4 && tags[0] == "harsh" && tags[3] == "vocal", "%zu tags", tags.size ());
    int n = 0;
    for (const auto& it : all)
        n += matches (it, "MIX") ? 1 : 0;
    CHECK (n == 2, "%d tagged mix", n);
    n = 0;
    for (const auto& it : all)
        n += matches (it, "") ? 1 : 0;
    CHECK (n == 5, "no filter: all");
}

static void menuLayout ()
{
    MenuState st;
    const auto m = buildMenu (sampleFactory (), sampleUser (), st);
    const auto titles = menuTitles (m);
    CHECK (!m.empty () && m[0].title == "Init" && m[0].action == Action::Init, "Init first");
    CHECK (!titles.empty () && titles[0] == "Init", "Init first title");
    for (const char* t : {"Save", "Save As...", "Rename...", "Edit Tags...", "Delete...", "Save as Default", "Load Default", "Reset Default",
                          "Save Preset File...", "Load Preset File...", "Mixing/De-mud", "Gentle", "Mine", "Drums/Old One", "Tags/All",
                          "Tags/mix", "Tags/vocal"})
        CHECK (contains (titles, t), "has %s", t);
    // Save, Rename, Edit Tags and Delete work on a user preset only; Reset Default needs a default
    auto find = [] (const std::vector<MenuEntry>& menu, Action a) -> const MenuEntry* {
        for (const auto& e : menu)
            if (e.action == a)
                return &e;
        return nullptr;
    };
    CHECK (find (m, Action::Save) && !find (m, Action::Save)->enabled, "Save off for no preset");
    CHECK (find (m, Action::Delete) && !find (m, Action::Delete)->enabled, "Delete off");
    CHECK (find (m, Action::ResetDefault) && !find (m, Action::ResetDefault)->enabled, "Reset Default off");
    st.kind = Kind::User;
    st.path = "/u/Mine.vstpreset";
    st.hasDefault = true;
    const auto m2 = buildMenu (sampleFactory (), sampleUser (), st);
    CHECK (find (m2, Action::Save)->enabled && find (m2, Action::Delete)->enabled && find (m2, Action::ResetDefault)->enabled, "on");
    const MenuEntry* mine = nullptr;
    for (const auto& e : m2)
        if (e.title == "Mine")
            mine = &e;
    CHECK (mine && mine->checked && mine->action == Action::User && mine->index == 0, "current user preset checked");
    // a factory preset is never overwritten: Save stays off while one is loaded
    st.kind = Kind::Factory;
    st.path = "Gentle.txt";
    CHECK (!find (buildMenu (sampleFactory (), sampleUser (), st), Action::Save)->enabled, "Save off for factory");
}

static void menuFilteredByTag ()
{
    MenuState st;
    st.tagFilter = "mix";
    const auto titles = menuTitles (buildMenu (sampleFactory (), sampleUser (), st));
    CHECK (titles[0] == "Init", "Init stays");
    CHECK (contains (titles, "Mixing/De-mud") && contains (titles, "Mixing/Tame Harshness"), "tagged ones");
    CHECK (!contains (titles, "Gentle") && !contains (titles, "Mine") && !contains (titles, "Drums/Old One"), "others hidden");
    CHECK (contains (titles, "Tags: mix") && contains (titles, "Tags: mix/All"), "the filter shows");
    st.tagFilter = "nothing has this";
    const auto m = buildMenu (sampleFactory (), sampleUser (), st);
    bool note = false;
    for (const auto& e : m)
        note = note || (e.heading && e.title.find ("No presets tagged") != std::string::npos);
    CHECK (note, "says nothing matches");
}

static void gentlrDefaultsFile ()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path () /
                         ("pk_gentlr_defaults_" + std::to_string ((long long)fs::file_time_type::clock::now ().time_since_epoch ().count ()));
    const auto unset = [] (const GentlrDefaults& x) { return !x.gentlrOn.has_value () && !x.advancedOn.has_value (); };
    // no folder, no file: neither switch set
    CHECK (gentlrDefaultsPath ("").empty () && !writeGentlrDefaults ("", {}), "no folder");
    GentlrDefaults d = readGentlrDefaults (dir.string ());
    CHECK (unset (d), "no file yet: not set");
    // round trip (the folder is made); a switch not set writes no line
    d.gentlrOn = true;
    CHECK (writeGentlrDefaults (dir.string (), d), "write");
    CHECK (gentlrDefaultsPath (dir.string ()).find (".defaults.txt") != std::string::npos && fs::exists (dir / ".defaults.txt"),
           "beside the presets");
    CHECK (gentlrDefaultsText (d).find ("advanced") == std::string::npos, "no line for Advanced");
    d = readGentlrDefaults (dir.string ());
    CHECK (d.gentlrOn == true && !d.advancedOn.has_value (), "gentlr on, advanced not set");
    d.advancedOn = true;
    d.gentlrOn = false;
    CHECK (writeGentlrDefaults (dir.string (), d), "write 2");
    d = readGentlrDefaults (dir.string ());
    CHECK (d.gentlrOn == false && d.advancedOn == true, "gentlr off, advanced on");
    // a corrupt file: what it does not understand is not set
    {
        std::ofstream f (dir / ".defaults.txt", std::ios::binary | std::ios::trunc);
        f << "\x01\xff garbage\n=on\ngentlr\nadvanced = maybe\n";
    }
    CHECK (unset (readGentlrDefaults (dir.string ())), "corrupt: not set");
    // case, spaces and other spellings; comments; keys a later version may add are kept
    d = parseGentlrDefaults ("# hi\r\n  Gentlr = ON \r\nADVANCED=0\nfuture = 3\n");
    CHECK (d.gentlrOn == true && d.advancedOn == false && d.other.size () == 1, "parsed (%zu other)", d.other.size ());
    CHECK (parseGentlrDefaults ("gentlr = yes\nadvanced = no").gentlrOn == true && parseGentlrDefaults ("advanced = false").advancedOn == false,
           "yes / no, true / false");
    CHECK (writeGentlrDefaults (dir.string (), d), "write 3");
    const GentlrDefaults back = readGentlrDefaults (dir.string ());
    CHECK (back.gentlrOn == true && back.advancedOn == false && back.other.size () == 1 && back.other[0].first == "future" &&
               back.other[0].second == "3",
           "kept");
    // a folder in the file's place: reads as not set, a write fails
    fs::remove (dir / ".defaults.txt");
    fs::create_directories (dir / ".defaults.txt");
    CHECK (unset (readGentlrDefaults (dir.string ())) && !writeGentlrDefaults (dir.string (), back), "unwritable");
    std::error_code ec;
    fs::remove_all (dir, ec);
}

static void gentlrDefaultValuesPerPlugin ()
{
    using V = std::vector<std::pair<uint32_t, double>>;
    const GentlrIds tail = tailGentlrIds (10, 20, 40);
    CHECK (tail.saturator == 10 + (int)kTailOn && tail.gentlr == 20 + (int)kTailExtClarity && tail.advanced == 40 + (int)kTailExt2Advanced,
           "tail IDs");
    GentlrDefaults d;
    CHECK (gentlrDefaultValues (tail, d).empty (), "not set: nothing");
    d.gentlrOn = true;
    CHECK ((gentlrDefaultValues (tail, d) == V {{10u, 1.0}, {29u, 1.0}}), "Gentlr On: the saturator and its Gentlr");
    d.advancedOn = true;
    CHECK ((gentlrDefaultValues (tail, d) == V {{10u, 1.0}, {29u, 1.0}, {40u, 1.0}}), "and Advanced");
    d.gentlrOn = false;
    d.advancedOn = false;
    CHECK ((gentlrDefaultValues (tail, d) == V {{29u, 0.0}, {40u, 0.0}}), "off: Gentlr and Advanced off, the saturator as it is");
    // gentlr itself: only its Advanced
    GentlrIds own;
    own.advanced = 0;
    d.gentlrOn = true;
    CHECK ((gentlrDefaultValues (own, d) == V {{0u, 0.0}}), "gentlr: Advanced only");
    CHECK (gentlrDefaultValues (GentlrIds {}, d).empty (), "no IDs (smemplr): nothing");
}

int main ()
{
    struct T
    {
        const char* name;
        void (*fn) ();
    } tests[] = {
        {"tagsParseTrimAndDedupe", tagsParseTrimAndDedupe}, {"metaXmlRoundTrip", metaXmlRoundTrip},
        {"factoryPresetParses", factoryPresetParses},       {"factoryPresetErrors", factoryPresetErrors},
        {"namesAreChecked", namesAreChecked},               {"tagFilter", tagFilter},
        {"menuLayout", menuLayout},                         {"menuFilteredByTag", menuFilteredByTag},
        {"gentlrDefaultsFile", gentlrDefaultsFile},         {"gentlrDefaultValuesPerPlugin", gentlrDefaultValuesPerPlugin},
    };
    for (const auto& t : tests)
    {
        const int before = gFailures;
        std::printf ("%s\n", t.name);
        t.fn ();
        std::printf ("  %s\n", gFailures == before ? "ok" : "FAILED");
    }
    std::printf ("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
