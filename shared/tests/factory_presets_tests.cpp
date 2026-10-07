// Every plug-in's factory presets (plugins/<plugin>/presets/**/*.txt) parse against its parameter table:
// known parameter names, values the parameter accepts and inside its range, unique names. Also
// `factory_presets_tests --dump <plugin>` lists a plug-in's parameters (names, ranges, defaults,
// choices) for writing presets.
#include "pluginkit/PresetStore.h"

// Built with -DPK_ONLY_PLUGINS (CMakeLists.txt), PK_PRESET_SUBSET is defined and only the plug-ins
// with a PK_WITH_<name> are linked and checked.
#if !defined(PK_PRESET_SUBSET)
#define PK_WITH_ciphr
#define PK_WITH_deepr
#define PK_WITH_dropr
#define PK_WITH_gentlr
#define PK_WITH_levlr
#define PK_WITH_locus
#define PK_WITH_moistr
#define PK_WITH_multidyn
#define PK_WITH_orbitr
#define PK_WITH_para
#define PK_WITH_smacheratr
#define PK_WITH_smemplr
#define PK_WITH_smoothr
#define PK_WITH_stretchr
#define PK_WITH_widr
#define PK_WITH_wubr
#endif

#ifdef PK_WITH_ciphr
#include "ciphr/src/core/Params.h"
#endif
#ifdef PK_WITH_deepr
#include "deepr/src/core/Params.h"
#endif
#ifdef PK_WITH_dropr
#include "dropr/src/core/Params.h"
#endif
#ifdef PK_WITH_gentlr
#include "gentlr/src/core/Params.h"
#endif
#ifdef PK_WITH_levlr
#include "levlr/src/core/Params.h"
#endif
#ifdef PK_WITH_locus
#include "locus/src/core/Params.h"
#endif
#ifdef PK_WITH_moistr
#include "moistr/src/core/Params.h"
#endif
#ifdef PK_WITH_multidyn
#include "multidyn/src/core/Params.h"
#endif
#ifdef PK_WITH_orbitr
#include "orbitr/src/core/Params.h"
#endif
#ifdef PK_WITH_para
#include "para/src/core/Params.h"
#endif
#ifdef PK_WITH_smacheratr
#include "smacheratr/src/core/Params.h"
#endif
#ifdef PK_WITH_smemplr
#include "smemplr/src/core/Params.h"
#endif
#ifdef PK_WITH_smoothr
#include "smoothr/src/core/Params.h"
#endif
#ifdef PK_WITH_stretchr
#include "stretchr/src/core/Params.h"
#endif
#ifdef PK_WITH_widr
#include "widr/src/core/Params.h"
#endif
#ifdef PK_WITH_wubr
#include "wubr/src/core/Params.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#ifndef PK_SOURCE_DIR
#error PK_SOURCE_DIR must be defined
#endif

namespace fs = std::filesystem;

namespace {

struct Plugin
{
    const char* name;
    const pk::ParamTable& (*table) ();
};

const Plugin kPlugins[] = {
#ifdef PK_WITH_ciphr
    {"ciphr", ciphr::paramTable},
#endif
#ifdef PK_WITH_deepr
    {"deepr", deepr::paramTable},
#endif
#ifdef PK_WITH_dropr
    {"dropr", dropr::paramTable},
#endif
#ifdef PK_WITH_gentlr
    {"gentlr", gentlr::paramTable},
#endif
#ifdef PK_WITH_levlr
    {"levlr", levlr::paramTable},
#endif
#ifdef PK_WITH_locus
    {"locus", locus::paramTable},
#endif
#ifdef PK_WITH_multidyn
    {"multidyn", multidyn::paramTable},
#endif
#ifdef PK_WITH_moistr
    {"moistr", moistr::paramTable},
#endif
#ifdef PK_WITH_orbitr
    {"orbitr", orbitr::paramTable},
#endif
#ifdef PK_WITH_para
    {"para", para::paramTable},
#endif
#ifdef PK_WITH_smacheratr
    {"smacheratr", smacheratr::paramTable},
#endif
#ifdef PK_WITH_smemplr
    {"smemplr", smemplr::paramTable},
#endif
#ifdef PK_WITH_smoothr
    {"smoothr", smoothr::paramTable},
#endif
#ifdef PK_WITH_stretchr
    {"stretchr", stretchr::paramTable},
#endif
#ifdef PK_WITH_widr
    {"widr", widr::paramTable},
#endif
#ifdef PK_WITH_wubr
    {"wubr", wubr::paramTable},
#endif
};

int fails = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (!ok)
    {
        ++fails;
        std::printf ("FAIL: %s\n", what.c_str ());
    }
}

void dump (const Plugin& p)
{
    const auto& t = p.table ();
    for (uint32_t id = 0; id < t.size (); ++id)
    {
        const auto& i = t.info (id);
        std::printf ("%3u  %-32s  %s .. %s  default %s", id, i.name, t.toText (id, i.min).c_str (), t.toText (id, i.max).c_str (),
                     t.toText (id, i.def).c_str ());
        if (!i.choices.empty ())
        {
            std::printf ("  [");
            for (size_t c = 0; c < i.choices.size (); ++c)
                std::printf ("%s%s", c ? " | " : "", i.choices[c]);
            std::printf ("]");
        }
        std::printf ("\n");
    }
}

} // namespace

int main (int argc, char** argv)
{
    if (argc == 3 && std::strcmp (argv[1], "--dump") == 0)
    {
        for (const auto& p : kPlugins)
            if (std::strcmp (p.name, argv[2]) == 0)
                dump (p);
        return 0;
    }
    for (const auto& p : kPlugins)
    {
        const fs::path dir = fs::path (PK_SOURCE_DIR) / "plugins" / p.name / "presets";
        int count = 0;
        std::set<std::string> names;
        std::error_code ec;
        if (fs::is_directory (dir, ec))
            for (const auto& e : fs::recursive_directory_iterator (dir, ec))
            {
                if (!e.is_regular_file () || e.path ().extension () != ".txt")
                    continue;
                std::ifstream f (e.path ());
                std::stringstream ss;
                ss << f.rdbuf ();
                const std::string rel = fs::relative (e.path (), dir).generic_string ();
                pk::presets::FactoryPreset fp;
                std::string err;
                const bool ok = pk::presets::parseFactoryPreset (ss.str (), rel, p.table (), fp, err);
                check (ok, std::string (p.name) + ": " + err);
                if (!ok)
                    continue;
                ++count;
                std::string key = fp.category + "/" + fp.name;
                std::transform (key.begin (), key.end (), key.begin (), [] (unsigned char c) { return (char)std::tolower (c); });
                check (names.insert (key).second, std::string (p.name) + ": two factory presets called " + key);
                check (!fp.values.empty (), std::string (p.name) + ": " + rel + " sets no parameter");
            }
        check (count >= 1, std::string (p.name) + ": has a factory preset");
        std::printf ("%-11s %d factory presets\n", p.name, count);
    }
    std::printf ("%s: %d checks, %d failed\n", fails ? "FAILED" : "OK", checks, fails);
    return fails ? 1 : 0;
}
