#include "GestureFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

namespace moistr {

namespace {
// a small JSON reader: enough for the gesture files (objects, arrays, numbers, strings, true, false, null)
struct Json
{
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    double number = 0.0;
    bool boolean = false;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;
    const Json* get (const char* key) const
    {
        auto it = fields.find (key);
        return it == fields.end () ? nullptr : &it->second;
    }
};

class Reader
{
public:
    explicit Reader (const std::string& s) : s (s) {}
    bool document (Json& out)
    {
        if (!value (out, 0))
            return false;
        space ();
        return i == s.size ();
    }

private:
    const std::string& s;
    size_t i = 0;
    void space ()
    {
        while (i < s.size () && std::isspace ((unsigned char)s[i]))
            ++i;
    }
    bool literal (const char* w)
    {
        const size_t n = std::char_traits<char>::length (w);
        if (s.compare (i, n, w) != 0)
            return false;
        i += n;
        return true;
    }
    bool string (std::string& out)
    {
        if (i >= s.size () || s[i] != '"')
            return false;
        ++i;
        while (i < s.size () && s[i] != '"')
        {
            char c = s[i++];
            if (c == '\\')
            {
                if (i >= s.size ())
                    return false;
                const char e = s[i++];
                switch (e)
                {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u':
                    {
                        if (i + 4 > s.size ())
                            return false;
                        const unsigned code = (unsigned)std::strtoul (s.substr (i, 4).c_str (), nullptr, 16);
                        i += 4;
                        // (as UTF-8; only names have text, so a surrogate pair is simply kept as two halves)
                        if (code < 0x80)
                            out += (char)code;
                        else if (code < 0x800)
                        {
                            out += (char)(0xC0 | (code >> 6));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                        else
                        {
                            out += (char)(0xE0 | (code >> 12));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                        continue;
                    }
                    default: c = e; break;
                }
            }
            out += c;
        }
        if (i >= s.size ())
            return false;
        ++i;
        return true;
    }
    bool value (Json& out, int depth)
    {
        if (depth > 64)
            return false;
        space ();
        if (i >= s.size ())
            return false;
        const char c = s[i];
        if (c == '{')
        {
            out.kind = Json::Object;
            ++i;
            space ();
            if (i < s.size () && s[i] == '}')
            {
                ++i;
                return true;
            }
            for (;;)
            {
                space ();
                std::string key;
                if (!string (key))
                    return false;
                space ();
                if (i >= s.size () || s[i] != ':')
                    return false;
                ++i;
                Json v;
                if (!value (v, depth + 1))
                    return false;
                out.fields[key] = std::move (v);
                space ();
                if (i < s.size () && s[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < s.size () && s[i] == '}')
                {
                    ++i;
                    return true;
                }
                return false;
            }
        }
        if (c == '[')
        {
            out.kind = Json::Array;
            ++i;
            space ();
            if (i < s.size () && s[i] == ']')
            {
                ++i;
                return true;
            }
            for (;;)
            {
                Json v;
                if (!value (v, depth + 1))
                    return false;
                out.items.push_back (std::move (v));
                space ();
                if (i < s.size () && s[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < s.size () && s[i] == ']')
                {
                    ++i;
                    return true;
                }
                return false;
            }
        }
        if (c == '"')
        {
            out.kind = Json::String;
            return string (out.text);
        }
        if (literal ("true"))
        {
            out.kind = Json::Bool;
            out.boolean = true;
            return true;
        }
        if (literal ("false"))
        {
            out.kind = Json::Bool;
            return true;
        }
        if (literal ("null"))
            return true;
        // a number
        const char* begin = s.c_str () + i;
        char* end = nullptr;
        const double v = std::strtod (begin, &end);
        if (end == begin)
            return false;
        i += (size_t)(end - begin);
        out.kind = Json::Number;
        out.number = v;
        return true;
    }
};

bool numberOf (const Json* j, double& v)
{
    if (!j || j->kind != Json::Number || !std::isfinite (j->number))
        return false;
    v = j->number;
    return true;
}
} // namespace

bool parseGestureJson (const std::string& text, const std::string& fallbackName, GestureData& out, std::string& error)
{
    Json doc;
    if (!Reader (text).document (doc) || doc.kind != Json::Object)
    {
        error = "not a JSON object";
        return false;
    }
    if (const Json* unit = doc.get ("time_unit"); unit && unit->kind == Json::String && unit->text != "beats")
    {
        error = "time_unit is not beats";
        return false;
    }
    const Json* pts = doc.get ("points");
    if (!pts || pts->kind != Json::Array || pts->items.empty ())
    {
        error = "no points";
        return false;
    }
    std::vector<std::pair<double, double>> p;
    for (const Json& it : pts->items)
    {
        double b = 0.0, v = 0.0;
        if (it.kind == Json::Array && it.items.size () >= 2 && numberOf (&it.items[0], b) && numberOf (&it.items[1], v))
            p.emplace_back (b, v);
        else if (it.kind == Json::Object && numberOf (it.get ("beat"), b) && numberOf (it.get ("value"), v))
            p.emplace_back (b, v);
        else
        {
            error = "a point is not [beat, value]";
            return false;
        }
    }
    if ((int)p.size () > kMaxGesturePoints)
    {
        char buf[96];
        std::snprintf (buf, sizeof (buf), "%d points (at most %d)", (int)p.size (), kMaxGesturePoints);
        error = buf;
        return false;
    }
    std::stable_sort (p.begin (), p.end (), [] (const auto& a, const auto& b) { return a.first < b.first; });
    // the values: normalised by min and max when the file gives them (the extractor's raw values)
    double lo = 0.0, hi = 1.0;
    const bool hasMin = numberOf (doc.get ("min"), lo), hasMax = numberOf (doc.get ("max"), hi);
    if (hasMin || hasMax)
    {
        if (!hasMin)
            lo = 0.0;
        if (!hasMax)
            hi = 1.0;
        const double span = hi - lo;
        for (auto& [b, v] : p)
            v = std::fabs (span) > 1e-12 ? (v - lo) / span : 1.0;
    }
    for (auto& [b, v] : p)
        v = std::clamp (v, 0.0, 1.0);
    // the beats from the gesture's start
    double length = 0.0;
    const bool hasLength = numberOf (doc.get ("length_beats"), length) && length > 0.0;
    const double first = p.front ().first;
    if (first < 0.0 || (!hasLength && first > 0.0) || (hasLength && p.back ().first > length + 1e-9))
        for (auto& [b, v] : p)
            b -= first;
    if (!hasLength)
        length = p.back ().first;
    if (!(length > 0.0))
        length = 1.0;
    for (auto& [b, v] : p)
        b = std::clamp (b, 0.0, length);
    out.points = std::move (p);
    out.length = length;
    const Json* name = doc.get ("name");
    out.name = name && name->kind == Json::String && !name->text.empty () ? name->text : fallbackName;
    // (a long name, as the extractor's full parameter paths: its end, which names the parameter)
    if (out.name.size () > 60)
        out.name = "..." + out.name.substr (out.name.size () - 57);
    return true;
}

bool toGesture (const GestureData& d, Gesture& g)
{
    const int n = (int)d.points.size ();
    if (n <= 0 || n > kMaxGesturePoints)
        return false;
    auto b = std::make_unique<double[]> ((size_t)n);
    auto v = std::make_unique<double[]> ((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        b[(size_t)i] = d.points[(size_t)i].first;
        v[(size_t)i] = d.points[(size_t)i].second;
    }
    return g.set (b.get (), v.get (), n, d.length);
}

std::string gestureJson (const GestureData& d)
{
    std::string s = "{\"name\": \"";
    for (char c : d.name)
    {
        if (c == '"' || c == '\\')
            s += '\\';
        s += c;
    }
    char buf[64];
    std::snprintf (buf, sizeof (buf), "\", \"length_beats\": %.9g, \"points\": [", d.length);
    s += buf;
    for (size_t i = 0; i < d.points.size (); ++i)
    {
        std::snprintf (buf, sizeof (buf), "%s[%.9g, %.9g]", i ? ", " : "", d.points[i].first, d.points[i].second);
        s += buf;
    }
    return s + "]}\n";
}

} // namespace moistr
