#include "Session.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace probr {

std::string defaultFolder ()
{
#if defined(_WIN32)
    const char* home = std::getenv ("USERPROFILE");
    return joinPath (joinPath (home ? home : ".", "Documents"), "probr");
#elif defined(__APPLE__)
    const char* home = std::getenv ("HOME");
    return joinPath (joinPath (home ? home : ".", "Documents"), "probr");
#else
    const char* home = std::getenv ("HOME");
    return joinPath (home ? home : ".", "probr");
#endif
}

std::string fileLabel (const std::string& label)
{
    std::string s;
    for (unsigned char c : label)
    {
        if (c < 0x20 || c == 0x7f || std::strchr ("<>:\"/\\|?*", (char)c))
            s += '_';
        else
            s += (char)c;
    }
    auto trimmable = [] (char c) { return c == ' ' || c == '.'; };
    while (!s.empty () && trimmable (s.back ()))
        s.pop_back ();
    size_t a = 0;
    while (a < s.size () && trimmable (s[a]))
        ++a;
    s = s.substr (a);
    if (s.size () > 80)
    {
        size_t n = 80;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) // (not inside a UTF-8 character)
            --n;
        s.resize (n);
        while (!s.empty () && trimmable (s.back ()))
            s.pop_back ();
    }
    if (s.empty ())
        return "probr";
    // names Windows keeps for devices
    std::string upper;
    for (char c : s.substr (0, s.find ('.')))
        upper += (char)std::toupper ((unsigned char)c);
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                     "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* r : reserved)
        if (upper == r)
            return "_" + s;
    return s;
}

int nextTake (const std::vector<std::string>& names, const std::string& base)
{
    int high = 0;
    const std::string prefix = base + "_";
    for (const auto& n : names)
    {
        if (n.size () <= prefix.size () || n.compare (0, prefix.size (), prefix) != 0)
            continue;
        size_t i = prefix.size ();
        int v = 0, digits = 0;
        while (i < n.size () && n[i] >= '0' && n[i] <= '9' && digits < 9)
        {
            v = v * 10 + (n[i] - '0');
            ++i;
            ++digits;
        }
        if (digits == 0)
            continue;
        const std::string rest = n.substr (i);
        if (rest == ".wav" || rest == ".json" || rest == ".midi.json")
            high = std::max (high, v);
    }
    return high + 1;
}

std::string takeText (int take)
{
    char buf[16];
    std::snprintf (buf, sizeof (buf), "%03d", take);
    return buf;
}

Session& Session::global ()
{
    static Session s;
    return s;
}

std::string Session::format (int64_t now)
{
    const std::time_t t = (std::time_t)now;
    std::tm tm {};
#if defined(_WIN32)
    localtime_s (&tm, &t);
#else
    localtime_r (&t, &tm);
#endif
    char buf[32];
    std::strftime (buf, sizeof (buf), "%Y-%m-%d_%H-%M-%S", &tm);
    return buf;
}

std::string Session::id (FileSystem& fs, const std::string& folder, int64_t now)
{
    std::lock_guard<std::mutex> g (m);
    if (sid.empty ())
    {
        // a session another process used a moment ago (its probes write into the same folder)
        std::string text;
        const std::string file = joinPath (folder, ".probr-session");
        if (fs.read (file, text))
        {
            char idBuf[64] = {};
            long long when = 0;
            if (std::sscanf (text.c_str (), "%63s %lld", idBuf, &when) == 2 && now - when >= 0 && now - when <= kJoinSeconds &&
                std::strlen (idBuf) == 19)
                sid = idBuf;
        }
        if (sid.empty ())
            sid = format (now);
    }
    fs.makeDirs (folder);
    fs.writeFile (joinPath (folder, ".probr-session"), sid + " " + std::to_string ((long long)now) + "\n");
    return sid;
}

void Session::touch (FileSystem& fs, const std::string& folder, int64_t now)
{
    std::lock_guard<std::mutex> g (m);
    if (!sid.empty ())
        fs.writeFile (joinPath (folder, ".probr-session"), sid + " " + std::to_string ((long long)now) + "\n");
}

std::string Session::current () const
{
    std::lock_guard<std::mutex> g (m);
    return sid;
}

void Session::reset ()
{
    std::lock_guard<std::mutex> g (m);
    sid.clear ();
}

int64_t unixNow ()
{
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}

} // namespace probr
