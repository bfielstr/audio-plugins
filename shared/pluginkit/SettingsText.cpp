#include "pluginkit/SettingsText.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace pk {

namespace {
const char* const kHeader = "bfielstr settings:";

std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size ();
    while (a < b && std::isspace ((unsigned char)s[a]))
        ++a;
    while (b > a && std::isspace ((unsigned char)s[b - 1]))
        --b;
    return s.substr (a, b - a);
}

std::string lower (std::string s)
{
    for (char& c : s)
        c = (char)std::tolower ((unsigned char)c);
    return s;
}
} // namespace

std::string settingsToText (const std::string& effect, const SettingValues& values, const ParamTable* names)
{
    std::string out = std::string (kHeader) + " " + effect + "\n";
    char buf[64];
    for (const auto& [id, v] : values)
    {
        std::snprintf (buf, sizeof buf, "%u %.17g", (unsigned)id, v);
        out += buf;
        if (names && id < names->size ())
        {
            out += ' ';
            out += names->info (id).name;
        }
        out += '\n';
    }
    return out;
}

std::string settingsEffect (const std::string& text)
{
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        line = trim (line);
        if (line.empty ())
            continue;
        if (line.compare (0, std::string (kHeader).size (), kHeader) != 0)
            return "";
        return trim (line.substr (std::string (kHeader).size ()));
    }
    return "";
}

bool settingsFromText (const std::string& text, const std::string& effect, SettingValues& out, const std::string& formerEffect)
{
    out.clear ();
    const std::string e = lower (settingsEffect (text));
    if (e.empty () || (e != lower (effect) && (formerEffect.empty () || e != lower (formerEffect))))
        return false;
    std::istringstream in (text);
    std::string line;
    bool header = true;
    while (std::getline (in, line))
    {
        line = trim (line);
        if (line.empty ())
            continue;
        if (header)
        {
            header = false;
            continue;
        }
        const char* p = line.c_str ();
        char* end = nullptr;
        const unsigned long id = std::strtoul (p, &end, 10);
        if (end == p || id > 0xffffffffUL)
            continue;
        const char* q = end;
        const double v = std::strtod (q, &end);
        if (end == q || v != v)
            continue;
        out.emplace_back ((uint32_t)id, std::clamp (v, 0.0, 1.0));
    }
    return true;
}

} // namespace pk
