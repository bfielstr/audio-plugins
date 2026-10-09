#include "GestureFile.h"

#include "Params.h"

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

// ---- the one gesture (0.30): many lanes

namespace {
// a lane's points: [beat, value] (or {"beat", "value"}), sorted by beat (keeping jumps), values clamped to 0 .. 1
bool lanePoints (const Json* pts, std::vector<std::pair<double, double>>& p, std::string& error)
{
    if (!pts || pts->kind != Json::Array || pts->items.empty ())
    {
        error = "no points";
        return false;
    }
    for (const Json& it : pts->items)
    {
        double b = 0.0, v = 0.0;
        if (it.kind == Json::Array && it.items.size () >= 2 && numberOf (&it.items[0], b) && numberOf (&it.items[1], v))
            p.emplace_back (b, std::clamp (v, 0.0, 1.0));
        else if (it.kind == Json::Object && numberOf (it.get ("beat"), b) && numberOf (it.get ("value"), v))
            p.emplace_back (b, std::clamp (v, 0.0, 1.0));
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
    return true;
}

void quoted (std::string& s, const std::string& text)
{
    s += '"';
    for (char c : text)
    {
        if (c == '"' || c == '\\')
            s += '\\';
        if (c == '\n')
        {
            s += "\\n";
            continue;
        }
        s += c;
    }
    s += '"';
}
} // namespace

bool parseSceneJson (const std::string& text, const std::string& fallbackName, SceneData& out, std::string& error)
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
    SceneData d;
    // (one lane at the top level: {"target", "points", "min", "max"})
    std::vector<const Json*> lanes;
    if (const Json* l = doc.get ("lanes"); l && l->kind == Json::Array)
        for (const Json& it : l->items)
            lanes.push_back (&it);
    else if (doc.get ("target") && doc.get ("points"))
        lanes.push_back (&doc);
    if (lanes.empty ())
    {
        error = doc.get ("points") ? "no lanes (a single curve needs a \"target\")" : "no lanes";
        return false;
    }
    int active = 0;
    double last = 0.0;
    for (const Json* l : lanes)
    {
        if (l->kind != Json::Object)
        {
            error = "a lane is not an object";
            return false;
        }
        SceneLaneData lane;
        const Json* t = l->get ("target");
        if (!t || t->kind != Json::String)
        {
            error = "a lane without a target";
            return false;
        }
        const int target = targetByName (t->text.c_str ());
        if (target < 0)
        {
            error = "unknown target \"" + t->text + "\"";
            return false;
        }
        lane.target = kTargetNames[target];
        if (const Json* s = l->get ("source"); s && s->kind == Json::String)
            lane.source = s->text;
        lane.hasMin = numberOf (l->get ("min"), lane.min);
        lane.hasMax = numberOf (l->get ("max"), lane.max);
        std::string why;
        if (!lanePoints (l->get ("points"), lane.points, why))
        {
            error = lane.target + ": " + why;
            return false;
        }
        last = std::max (last, lane.points.back ().first);
        if (target != kTargetOff && ++active > kMaxSceneLanes)
        {
            char buf[64];
            std::snprintf (buf, sizeof (buf), "more than %d lanes with a target", kMaxSceneLanes);
            error = buf;
            return false;
        }
        d.lanes.push_back (std::move (lane));
    }
    double length = 0.0;
    if (!(numberOf (doc.get ("length_beats"), length) && length > 0.0))
        length = last > 0.0 ? last : 1.0;
    d.length = length;
    for (SceneLaneData& lane : d.lanes)
        for (auto& [b, v] : lane.points)
            b = std::clamp (b, 0.0, length);
    const Json* name = doc.get ("name");
    d.name = name && name->kind == Json::String && !name->text.empty () ? name->text : fallbackName;
    if (d.name.size () > 60)
        d.name = d.name.substr (0, 57) + "...";
    out = std::move (d);
    return true;
}

bool toScene (const SceneData& d, Scene& s)
{
    if (!(d.length > 0.0))
        return false;
    s.length = d.length;
    s.count = 0;
    for (const SceneLaneData& l : d.lanes)
    {
        const int target = targetByName (l.target.c_str ());
        if (target <= kTargetOff)
            continue;
        if (s.count >= kMaxSceneLanes)
            return false;
        const int n = (int)l.points.size ();
        if (n <= 0 || n > kMaxGesturePoints)
            return false;
        auto b = std::make_unique<double[]> ((size_t)n);
        auto v = std::make_unique<double[]> ((size_t)n);
        for (int i = 0; i < n; ++i)
        {
            b[(size_t)i] = l.points[(size_t)i].first;
            v[(size_t)i] = l.points[(size_t)i].second;
        }
        SceneLane& lane = s.lane[s.count];
        if (!lane.curve.set (b.get (), v.get (), n, d.length))
            return false;
        double lo = 0.0, hi = 1.0;
        targetUnits (target, lo, hi);
        setLaneRange (lane, target, l.hasMin || l.hasMax, l.hasMin ? l.min : lo, l.hasMax ? l.max : hi);
        ++s.count;
    }
    return true;
}

std::string sceneJson (const SceneData& d)
{
    std::string s = "{\"name\": ";
    quoted (s, d.name);
    char buf[96];
    std::snprintf (buf, sizeof (buf), ", \"length_beats\": %.9g, \"lanes\": [", d.length);
    s += buf;
    for (size_t k = 0; k < d.lanes.size (); ++k)
    {
        const SceneLaneData& l = d.lanes[k];
        s += k ? ",\n  {\"target\": " : "\n  {\"target\": ";
        quoted (s, l.target);
        if (!l.source.empty ())
        {
            s += ", \"source\": ";
            quoted (s, l.source);
        }
        if (l.hasMin)
        {
            std::snprintf (buf, sizeof (buf), ", \"min\": %.9g", l.min);
            s += buf;
        }
        if (l.hasMax)
        {
            std::snprintf (buf, sizeof (buf), ", \"max\": %.9g", l.max);
            s += buf;
        }
        s += ", \"points\": [";
        for (size_t i = 0; i < l.points.size (); ++i)
        {
            std::snprintf (buf, sizeof (buf), "%s[%.9g, %.9g]", i ? ", " : "", l.points[i].first, l.points[i].second);
            s += buf;
        }
        s += "]}";
    }
    return s + "]}\n";
}

} // namespace moistr
