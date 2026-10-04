// Every plug-in's factory presets (plugins/<plugin>/presets/**/*.txt) parse against its parameter table:
// known parameter names, values the parameter accepts and inside its range, unique names. Also
// `factory_presets_tests --dump <plugin>` lists a plug-in's parameters (names, ranges, defaults,
// choices) for writing presets.
#include "pluginkit/PresetStore.h"

#include "deepr/src/core/Params.h"
#include "dropr/src/core/Params.h"
#include "gentlr/src/core/Params.h"
#include "levlr/src/core/Params.h"
#include "locus/src/core/Params.h"
#include "multidyn/src/core/Params.h"
#include "orbitr/src/core/Params.h"
#include "para/src/core/Params.h"
#include "smacheratr/src/core/Params.h"
#include "smemplr/src/core/Params.h"
#include "smoothr/src/core/Params.h"
#include "stretchr/src/core/Params.h"
#include "widr/src/core/Params.h"
#include "wubr/src/core/Params.h"

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
    {"deepr", deepr::paramTable},       {"dropr", dropr::paramTable},   {"gentlr", gentlr::paramTable},
    {"levlr", levlr::paramTable},       {"locus", locus::paramTable},   {"multidyn", multidyn::paramTable},
    {"orbitr", orbitr::paramTable},     {"para", para::paramTable},     {"smacheratr", smacheratr::paramTable},
    {"smemplr", smemplr::paramTable},   {"smoothr", smoothr::paramTable}, {"stretchr", stretchr::paramTable},
    {"widr", widr::paramTable},         {"wubr", wubr::paramTable},
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
