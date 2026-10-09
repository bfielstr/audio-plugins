#include "pluginkit/GentlrDefaults.h"

#include "pluginkit/PresetStore.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pk {

namespace fs = std::filesystem;

namespace {
constexpr const char* kGentlrKey = "gentlr";
constexpr const char* kAdvancedKey = "advanced";
constexpr const char* kGlueOnTouchKey = "glue on touch";

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

// on, off, or not set for a value it does not understand
std::optional<bool> switchValue (const std::string& v)
{
    const std::string l = lower (v);
    if (l == "on" || l == "1" || l == "true" || l == "yes")
        return true;
    if (l == "off" || l == "0" || l == "false" || l == "no")
        return false;
    return std::nullopt;
}
} // namespace

GentlrDefaults parseGentlrDefaults (const std::string& text)
{
    GentlrDefaults d;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        line = trim (line);
        if (line.empty () || line[0] == '#')
            continue;
        const size_t eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = lower (trim (line.substr (0, eq))), value = trim (line.substr (eq + 1));
        if (key.empty ())
            continue;
        if (key == kGentlrKey)
            d.gentlrOn = switchValue (value);
        else if (key == kAdvancedKey)
            d.advancedOn = switchValue (value);
        else if (key == kGlueOnTouchKey)
            d.glueOnTouch = switchValue (value);
        else
            d.other.emplace_back (key, value);
    }
    return d;
}

std::string gentlrDefaultsText (const GentlrDefaults& d)
{
    std::string out = "# Defaults for new instances (Menu > Defaults). key = on / off\n";
    if (d.gentlrOn)
        out += std::string (kGentlrKey) + " = " + (*d.gentlrOn ? "on" : "off") + "\n";
    if (d.advancedOn)
        out += std::string (kAdvancedKey) + " = " + (*d.advancedOn ? "on" : "off") + "\n";
    if (d.glueOnTouch)
        out += std::string (kGlueOnTouchKey) + " = " + (*d.glueOnTouch ? "on" : "off") + "\n";
    for (const auto& [k, v] : d.other)
        out += k + " = " + v + "\n";
    return out;
}

std::string gentlrDefaultsPath (const std::string& presetFolder)
{
    return presetFolder.empty () ? std::string () : (fs::path (presetFolder) / ".defaults.txt").string ();
}

GentlrDefaults readGentlrDefaults (const std::string& presetFolder)
{
    const std::string path = gentlrDefaultsPath (presetFolder);
    if (path.empty ())
        return {};
    std::ifstream f (path, std::ios::binary);
    if (!f)
        return {};
    std::stringstream ss;
    ss << f.rdbuf ();
    return parseGentlrDefaults (ss.str ());
}

bool writeGentlrDefaults (const std::string& presetFolder, const GentlrDefaults& d)
{
    const std::string path = gentlrDefaultsPath (presetFolder);
    if (path.empty ())
        return false;
    std::error_code ec;
    fs::create_directories (fs::path (path).parent_path (), ec);
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f << gentlrDefaultsText (d);
    return (bool)f;
}

std::vector<std::pair<uint32_t, double>> gentlrDefaultValues (const GentlrIds& ids, const GentlrDefaults& d)
{
    std::vector<std::pair<uint32_t, double>> out;
    if (d.gentlrOn && ids.gentlr >= 0)
    {
        // (on: the saturator too, or Gentlr would not be heard; off: Gentlr only)
        if (*d.gentlrOn && ids.saturator >= 0)
            out.emplace_back ((uint32_t)ids.saturator, 1.0);
        out.emplace_back ((uint32_t)ids.gentlr, *d.gentlrOn ? 1.0 : 0.0);
    }
    if (d.advancedOn && ids.advanced >= 0)
        out.emplace_back ((uint32_t)ids.advanced, *d.advancedOn ? 1.0 : 0.0);
    return out;
}

bool glueOnTouch () { return readGentlrDefaults (presets::suiteFolder ()).glueOnTouch.value_or (false); }

bool writeGlueOnTouch (bool on)
{
    const std::string folder = presets::suiteFolder ();
    GentlrDefaults d = readGentlrDefaults (folder);
    d.glueOnTouch = on;
    return writeGentlrDefaults (folder, d);
}

} // namespace pk
