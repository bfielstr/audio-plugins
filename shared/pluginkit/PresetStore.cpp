#include "pluginkit/PresetStore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace pk::presets {

namespace fs = std::filesystem;

namespace {

std::string lower (std::string s)
{
    for (char& c : s)
        c = (char)std::tolower ((unsigned char)c);
    return s;
}

bool iequal (const std::string& a, const std::string& b) { return lower (a) == lower (b); }

std::string envOr (const char* name, const char* fallback)
{
    const char* v = std::getenv (name);
    return v && *v ? v : fallback;
}

std::string xmlEscape (const std::string& s)
{
    std::string out;
    out.reserve (s.size ());
    for (char c : s)
        switch (c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:
                if ((unsigned char)c >= 0x20 || c == '\t')
                    out += c;
                else
                    out += ' ';
        }
    return out;
}

std::string xmlUnescape (const std::string& s)
{
    static const std::pair<const char*, char> ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    std::string out;
    out.reserve (s.size ());
    for (size_t i = 0; i < s.size ();)
    {
        bool done = false;
        if (s[i] == '&')
            for (const auto& [e, c] : ents)
            {
                const size_t n = std::strlen (e);
                if (s.compare (i, n, e) == 0)
                {
                    out += c;
                    i += n;
                    done = true;
                    break;
                }
            }
        if (!done)
            out += s[i++];
    }
    return out;
}

// the value of attribute `name` in one tag's text ("" when it has none)
bool attribute (const std::string& tag, const char* name, std::string& out)
{
    const std::string key = std::string (name) + "=";
    size_t pos = 0;
    while ((pos = tag.find (key, pos)) != std::string::npos)
    {
        // a whole attribute name: preceded by white space
        if (pos > 0 && !std::isspace ((unsigned char)tag[pos - 1]))
        {
            pos += key.size ();
            continue;
        }
        size_t q = pos + key.size ();
        if (q >= tag.size () || (tag[q] != '"' && tag[q] != '\''))
            return false;
        const char quote = tag[q];
        const size_t end = tag.find (quote, q + 1);
        if (end == std::string::npos)
            return false;
        out = xmlUnescape (tag.substr (q + 1, end - q - 1));
        return true;
    }
    return false;
}

void attr (std::string& xml, const char* id, const std::string& value, bool writeProtected = false)
{
    xml += "\t<Attr id=\"";
    xml += id;
    xml += "\" value=\"" + xmlEscape (value) + "\" type=\"string\"";
    if (writeProtected)
        xml += " flags=\"writeProtected\"";
    xml += "/>\n";
}

bool readLE (std::ifstream& f, void* p, size_t n)
{
    f.read ((char*)p, (std::streamsize)n);
    return (size_t)f.gcount () == n;
}

int64_t le64 (const unsigned char* b)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | b[i];
    return (int64_t)v;
}

int32_t le32 (const unsigned char* b) { return (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24)); }

bool hasExtension (const fs::path& p)
{
    return iequal (p.extension ().string (), kExtension);
}

void sortItems (std::vector<Item>& items)
{
    std::stable_sort (items.begin (), items.end (), [] (const Item& a, const Item& b) {
        const std::string ca = lower (a.category), cb = lower (b.category);
        if (ca != cb)
            return ca < cb;
        return lower (a.name) < lower (b.name);
    });
}

} // namespace

std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size ();
    while (a < b && std::isspace ((unsigned char)s[a]))
        ++a;
    while (b > a && std::isspace ((unsigned char)s[b - 1]))
        --b;
    return s.substr (a, b - a);
}

// ---- metadata -------------------------------------------------------------------------------------
std::vector<std::string> parseTags (const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        const std::string t = trim (cur);
        cur.clear ();
        if (!t.empty () && !hasTag (out, t))
            out.push_back (t);
    };
    for (char c : text)
    {
        if (c == ',' || c == ';' || c == '\n' || c == '\r')
            flush ();
        else
            cur += c;
    }
    flush ();
    return out;
}

std::string joinTags (const std::vector<std::string>& tags)
{
    std::string out;
    for (const auto& t : tags)
    {
        if (!out.empty ())
            out += ", ";
        out += t;
    }
    return out;
}

bool hasTag (const std::vector<std::string>& tags, const std::string& tag)
{
    return std::any_of (tags.begin (), tags.end (), [&] (const std::string& t) { return iequal (t, tag); });
}

std::string metaToXml (const Meta& m)
{
    std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<MetaInfo>\n";
    attr (xml, "MediaType", "VstPreset", true);
    if (!m.plugin.empty ())
        attr (xml, "PlugInName", m.plugin, true);
    if (!m.name.empty ())
        attr (xml, "Name", m.name);
    attr (xml, "bfielstr.FormatVersion", "1");
    attr (xml, "bfielstr.Category", m.category);
    attr (xml, "bfielstr.Tags", joinTags (m.tags));
    if (!m.comment.empty ())
        attr (xml, "bfielstr.Comment", m.comment);
    xml += "</MetaInfo>\n";
    return xml;
}

bool metaFromXml (const std::string& xml, Meta& m)
{
    if (xml.find ("<MetaInfo") == std::string::npos)
        return false;
    size_t pos = 0;
    while ((pos = xml.find ("<Attr", pos)) != std::string::npos)
    {
        const size_t end = xml.find ('>', pos);
        if (end == std::string::npos)
            break;
        const std::string tag = xml.substr (pos, end - pos);
        pos = end;
        std::string id, value;
        if (!attribute (tag, "id", id) || !attribute (tag, "value", value))
            continue;
        if (id == "PlugInName")
            m.plugin = value;
        else if (id == "Name")
            m.name = value;
        else if (id == "bfielstr.Category")
            m.category = value;
        else if (id == "bfielstr.Tags")
            m.tags = parseTags (value);
        else if (id == "bfielstr.Comment")
            m.comment = value;
    }
    return true;
}

bool readMetaXml (const std::string& path, std::string& xml)
{
    // header: 'VST3', version (int32), class ID (32 chars), chunk list offset (int64); the list: 'List',
    // count (int32), then per chunk its ID (4 chars), offset and size (int64); all little-endian
    std::ifstream f (fs::path (path), std::ios::binary);
    if (!f)
        return false;
    unsigned char head[48];
    if (!readLE (f, head, sizeof head) || std::memcmp (head, "VST3", 4) != 0)
        return false;
    const int64_t listOffset = le64 (head + 40);
    if (listOffset <= 0)
        return false;
    f.seekg (listOffset);
    unsigned char list[8];
    if (!readLE (f, list, sizeof list) || std::memcmp (list, "List", 4) != 0)
        return false;
    const int32_t count = le32 (list + 4);
    for (int32_t i = 0; i < count && i < 64; ++i)
    {
        unsigned char e[20];
        if (!readLE (f, e, sizeof e))
            return false;
        if (std::memcmp (e, "Info", 4) != 0)
            continue;
        const int64_t off = le64 (e + 4), size = le64 (e + 12);
        if (off <= 0 || size <= 0 || size > (1 << 20))
            return false;
        xml.assign ((size_t)size, '\0');
        f.seekg (off);
        return readLE (f, xml.data (), (size_t)size);
    }
    return false;
}

// ---- folders and names ----------------------------------------------------------------------------
std::string suiteFolder ()
{
    const std::string over = envOr ("PK_PRESETS_DIR", "");
    if (!over.empty ())
        return over;
    fs::path base;
#if defined(_WIN32)
    std::string home = envOr ("USERPROFILE", "");
    if (home.empty ())
        home = envOr ("HOMEDRIVE", "") + envOr ("HOMEPATH", "");
    base = fs::path (home) / "Documents" / "VST3 Presets";
#elif defined(__APPLE__)
    base = fs::path (envOr ("HOME", "")) / "Library" / "Audio" / "Presets";
#else
    base = fs::path (envOr ("HOME", "")) / ".vst3" / "presets";
#endif
    return (base / "bfielstr").string ();
}

std::string userFolder (const char* pluginName)
{
    const fs::path folder = fs::path (suiteFolder ()) / pluginName;
    std::error_code ec;
    fs::create_directories (folder, ec);
    return fs::is_directory (folder, ec) ? folder.string () : std::string ();
}

std::string userFolder (const char* pluginName, const char* formerName)
{
    if (!formerName || !*formerName)
        return userFolder (pluginName);
    std::error_code ec;
    const fs::path base = suiteFolder ();
    const bool fresh = !fs::exists (base / pluginName, ec);
    const std::string folder = userFolder (pluginName);
    if (fresh && !folder.empty () && fs::is_directory (base / formerName, ec))
        fs::copy (base / formerName, folder, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    return folder;
}

std::string defaultPresetPath (const std::string& pluginName)
{
    if (pluginName.empty ())
        return {};
    return (fs::path (suiteFolder ()) / pluginName / kDefaultFile).string ();
}

std::string nameOf (const std::string& path)
{
    const fs::path p (path);
    return hasExtension (p) ? p.stem ().string () : p.filename ().string ();
}

std::string withExtension (const std::string& path) { return hasExtension (fs::path (path)) ? path : path + kExtension; }

bool validName (const std::string& name, bool category)
{
    if (name.empty () || name != trim (name) || name.size () > 120 || name[0] == '.')
        return false;
    for (unsigned char c : name)
        if (c < 0x20 || std::strchr ("/\\:*?\"<>|", (char)c))
            return false;
    if (!category && iequal (name, kInitName))
        return false;
    return true;
}

// ---- the presets a menu lists ---------------------------------------------------------------------
std::vector<Item> listUser (const std::string& folder)
{
    std::vector<Item> out;
    std::error_code ec;
    if (folder.empty () || !fs::is_directory (folder, ec))
        return out;
    auto add = [&] (const fs::path& file, const std::string& category) {
        Item it;
        it.name = file.stem ().string ();
        it.category = category;
        it.path = file.string ();
        std::string xml;
        Meta m;
        if (readMetaXml (it.path, xml) && metaFromXml (xml, m))
            it.tags = m.tags;
        out.push_back (std::move (it));
    };
    for (const auto& e : fs::directory_iterator (folder, ec))
    {
        const std::string fn = e.path ().filename ().string ();
        if (fn.empty () || fn[0] == '.')
            continue;
        if (e.is_regular_file (ec) && hasExtension (e.path ()))
            add (e.path (), "");
        else if (e.is_directory (ec))
        {
            std::error_code ec2;
            for (const auto& f : fs::directory_iterator (e.path (), ec2))
            {
                const std::string n = f.path ().filename ().string ();
                if (!n.empty () && n[0] != '.' && f.is_regular_file (ec2) && hasExtension (f.path ()))
                    add (f.path (), fn);
            }
        }
    }
    sortItems (out);
    return out;
}

std::vector<std::string> allTags (const std::vector<Item>& items)
{
    std::vector<std::string> out;
    for (const auto& it : items)
        for (const auto& t : it.tags)
            if (!hasTag (out, t))
                out.push_back (t);
    std::sort (out.begin (), out.end (), [] (const std::string& a, const std::string& b) { return lower (a) < lower (b); });
    return out;
}

bool matches (const Item& item, const std::string& tagFilter) { return tagFilter.empty () || hasTag (item.tags, tagFilter); }

// ---- factory presets ------------------------------------------------------------------------------
bool parseFactoryPreset (const std::string& text, const std::string& relPath, const ParamTable& table, FactoryPreset& out,
                         std::string& error)
{
    out = {};
    error.clear ();
    out.path = relPath;
    {
        const fs::path p (relPath);
        out.name = p.stem ().string ();
        if (p.has_parent_path ())
            out.category = p.parent_path ().filename ().string ();
    }
    // names, case aside; a name two parameters share is ambiguous (use #ID)
    std::map<std::string, int64_t> byName;
    for (uint32_t id = 0; id < table.size (); ++id)
    {
        const std::string n = lower (table.info (id).name);
        byName[n] = byName.count (n) ? -1 : (int64_t)id;
    }
    std::istringstream in (text);
    std::string line;
    int lineNo = 0;
    auto fail = [&] (const std::string& what) {
        error = relPath + ":" + std::to_string (lineNo) + ": " + what;
        return false;
    };
    std::vector<bool> seen (table.size (), false);
    while (std::getline (in, line))
    {
        ++lineNo;
        line = trim (line);
        if (line.empty () || (line[0] == '#' && (line.size () < 2 || !std::isdigit ((unsigned char)line[1]))))
            continue;
        const size_t eq = line.find ('=');
        if (eq == std::string::npos)
        {
            const size_t colon = line.find (':');
            if (colon == std::string::npos)
                return fail ("neither 'key: value' nor 'parameter = value'");
            const std::string key = lower (trim (line.substr (0, colon))), value = trim (line.substr (colon + 1));
            if (key == "name")
                out.name = value;
            else if (key == "category")
                out.category = value;
            else if (key == "tags")
                out.tags = parseTags (value);
            else if (key == "comment")
                out.comment = value;
            else if (key != "plugin" && key != "author")
                return fail ("unknown key '" + key + "'");
            continue;
        }
        const std::string key = trim (line.substr (0, eq)), value = trim (line.substr (eq + 1));
        int64_t id = -1;
        if (key.size () > 1 && key[0] == '#')
        {
            char* end = nullptr;
            const long v = std::strtol (key.c_str () + 1, &end, 10);
            if (*end == '\0' && v >= 0 && (uint32_t)v < table.size ())
                id = v;
        }
        else
        {
            const auto f = byName.find (lower (key));
            if (f != byName.end ())
            {
                if (f->second < 0)
                    return fail ("'" + key + "' names more than one parameter: use #<ID>");
                id = f->second;
            }
        }
        if (id < 0)
            return fail ("unknown parameter '" + key + "'");
        if (seen[(size_t)id])
            return fail ("'" + key + "' set twice");
        seen[(size_t)id] = true;
        const ParamInfo& info = table.info ((uint32_t)id);
        double plain = 0.0;
        if (!table.fromText ((uint32_t)id, value, plain))
            return fail ("'" + value + "' is not a value of '" + key + "'");
        // fromText keeps numbers inside the range: parse again without the limits to catch a typo
        if ((info.type == PType::Float || info.type == PType::Int) && info.disp != Disp::Choice &&
            lower (value).find ("inf") == std::string::npos)
        {
            ParamInfo wide = info;
            wide.id = 0;
            wide.min = -1e300;
            wide.max = 1e300;
            const ParamTable one ({wide});
            double raw = plain;
            if (one.fromText (0, value, raw))
            {
                const double tol = 1e-9 * std::max (1.0, std::abs (info.max - info.min));
                if (raw < info.min - tol || raw > info.max + tol)
                    return fail ("'" + value + "' is outside the range of '" + key + "'");
            }
        }
        out.values.emplace_back ((uint32_t)id, table.toNormalized ((uint32_t)id, plain));
    }
    if (!validName (out.name))
        return fail ("not a usable preset name: '" + out.name + "'");
    return true;
}

std::vector<FactoryPreset> parseFactoryFiles (const std::vector<FactoryFile>& files, const ParamTable& table)
{
    std::vector<FactoryPreset> out;
    for (const auto& f : files)
    {
        FactoryPreset fp;
        std::string err;
        if (f.path && f.text && parseFactoryPreset (f.text, f.path, table, fp, err))
            out.push_back (std::move (fp));
    }
    std::stable_sort (out.begin (), out.end (), [] (const FactoryPreset& a, const FactoryPreset& b) {
        return a.category != b.category ? a.category < b.category : a.name < b.name;
    });
    return out;
}

std::vector<Item> factoryItems (const std::vector<FactoryPreset>& presets)
{
    std::vector<Item> out;
    for (size_t i = 0; i < presets.size (); ++i)
    {
        Item it;
        it.name = presets[i].name;
        it.category = presets[i].category;
        it.path = presets[i].path;
        it.tags = presets[i].tags;
        it.factory = true;
        it.factoryIndex = (int)i;
        out.push_back (std::move (it));
    }
    return out;
}

// ---- another plug-in's presets --------------------------------------------------------------------
namespace {
std::map<std::string, std::vector<FactoryFile>>& hostedRegistry ()
{
    static std::map<std::string, std::vector<FactoryFile>> files;
    return files;
}
} // namespace

bool registerFactoryOf (const char* pluginName, const FactoryFile* files, int count)
{
    if (!pluginName || !*pluginName)
        return false;
    auto& reg = hostedRegistry ()[pluginName];
    for (int i = 0; i < count; ++i)
        reg.push_back (files[i]);
    return true;
}

const std::vector<FactoryFile>& factoryFilesOf (const std::string& pluginName)
{
    static const std::vector<FactoryFile> none;
    const auto& reg = hostedRegistry ();
    const auto f = reg.find (pluginName);
    return f == reg.end () ? none : f->second;
}

bool inFolder (const std::string& path, const std::string& folder)
{
    if (path.empty () || folder.empty ())
        return false;
    std::error_code ec;
    const fs::path f = fs::weakly_canonical (folder, ec);
    fs::path p = fs::weakly_canonical (path, ec).parent_path ();
    for (int i = 0; i < 2 && !p.empty (); ++i, p = p.parent_path ())
        if (p == f)
            return true;
    return false;
}

// ---- the menu -------------------------------------------------------------------------------------
namespace {
MenuEntry entry (const std::string& title, Action a, bool enabled = true)
{
    MenuEntry e;
    e.title = title;
    e.action = a;
    e.enabled = enabled;
    return e;
}
MenuEntry separator ()
{
    MenuEntry e;
    e.separator = true;
    return e;
}
MenuEntry heading (const std::string& title)
{
    MenuEntry e;
    e.title = title;
    e.heading = true;
    e.enabled = false;
    return e;
}

// presets without a category first, then one sub-menu per category
void addGroup (std::vector<MenuEntry>& menu, const std::vector<Item>& items, bool factory, const MenuState& s)
{
    std::vector<std::string> cats;
    for (size_t i = 0; i < items.size (); ++i)
    {
        const Item& it = items[i];
        if (!matches (it, s.tagFilter))
            continue;
        MenuEntry e = entry (it.name, factory ? Action::Factory : Action::User);
        e.index = factory ? it.factoryIndex : (int)i;
        e.checked = (factory ? s.kind == Kind::Factory : s.kind == Kind::User) && it.path == s.path;
        if (it.category.empty ())
            menu.push_back (e);
        else
        {
            auto f = std::find_if (cats.begin (), cats.end (), [&] (const std::string& c) { return iequal (c, it.category); });
            if (f == cats.end ())
                cats.push_back (it.category);
        }
    }
    std::sort (cats.begin (), cats.end (), [] (const std::string& a, const std::string& b) { return lower (a) < lower (b); });
    for (const auto& c : cats)
    {
        MenuEntry sub = entry (c, Action::None);
        for (size_t i = 0; i < items.size (); ++i)
        {
            const Item& it = items[i];
            if (!iequal (it.category, c) || !matches (it, s.tagFilter))
                continue;
            MenuEntry e = entry (it.name, factory ? Action::Factory : Action::User);
            e.index = factory ? it.factoryIndex : (int)i;
            e.checked = (factory ? s.kind == Kind::Factory : s.kind == Kind::User) && it.path == s.path;
            sub.checked = sub.checked || e.checked;
            sub.sub.push_back (e);
        }
        menu.push_back (sub);
    }
}

size_t countMatching (const std::vector<Item>& items, const std::string& tag)
{
    return (size_t)std::count_if (items.begin (), items.end (), [&] (const Item& it) { return matches (it, tag); });
}
} // namespace

std::vector<MenuEntry> buildMenu (const std::vector<Item>& factory, const std::vector<Item>& user, const MenuState& s)
{
    std::vector<MenuEntry> m;
    MenuEntry init = entry (kInitName, Action::Init);
    init.checked = s.kind == Kind::Init;
    m.push_back (init);

    const size_t nf = countMatching (factory, s.tagFilter), nu = countMatching (user, s.tagFilter);
    if (nf > 0)
    {
        m.push_back (separator ());
        m.push_back (heading ("Factory"));
        addGroup (m, factory, true, s);
    }
    if (nu > 0)
    {
        m.push_back (separator ());
        m.push_back (heading ("User"));
        addGroup (m, user, false, s);
    }
    if (!s.tagFilter.empty () && nf + nu == 0)
    {
        m.push_back (separator ());
        m.push_back (heading ("No presets tagged \"" + s.tagFilter + "\""));
    }

    // tags: the filter
    m.push_back (separator ());
    {
        std::vector<Item> both = factory;
        both.insert (both.end (), user.begin (), user.end ());
        const auto tags = allTags (both);
        MenuEntry t = entry (s.tagFilter.empty () ? std::string ("Tags") : "Tags: " + s.tagFilter, Action::None);
        MenuEntry all = entry ("All", Action::ClearFilter);
        all.checked = s.tagFilter.empty ();
        t.sub.push_back (all);
        t.sub.push_back (separator ());
        if (tags.empty ())
            t.sub.push_back (heading ("No tags yet"));
        for (const auto& tag : tags)
        {
            MenuEntry e = entry (tag, Action::FilterTag);
            e.tag = tag;
            e.checked = iequal (tag, s.tagFilter);
            t.sub.push_back (e);
        }
        m.push_back (t);
    }

    const bool userPreset = s.kind == Kind::User && !s.path.empty ();
    m.push_back (separator ());
    m.push_back (entry ("Save", Action::Save, userPreset));
    m.push_back (entry ("Save As...", Action::SaveAs));
    m.push_back (entry ("Rename...", Action::Rename, userPreset));
    m.push_back (entry ("Edit Tags...", Action::EditTags, userPreset));
    m.push_back (entry ("Delete...", Action::Delete, userPreset));
    m.push_back (separator ());
    m.push_back (entry ("Save as Default", Action::SaveDefault));
    m.push_back (entry ("Load Default", Action::LoadDefault));
    m.push_back (entry ("Reset Default", Action::ResetDefault, s.hasDefault));
    m.push_back (separator ());
    m.push_back (entry ("Save Preset File...", Action::SaveFile));
    m.push_back (entry ("Load Preset File...", Action::LoadFile));
    return m;
}

std::vector<std::string> menuTitles (const std::vector<MenuEntry>& menu)
{
    std::vector<std::string> out;
    for (const auto& e : menu)
    {
        if (e.separator || e.heading)
            continue;
        out.push_back (e.title);
        for (const auto& s : menuTitles (e.sub))
            out.push_back (e.title + "/" + s);
    }
    return out;
}

} // namespace pk::presets
