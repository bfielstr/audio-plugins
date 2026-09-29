// Audio dragged in from a DAW (REAPER, Ableton Live, a file browser). Hosts hand drops over in
// different ways: as file paths, or as text with one path or file:// URL per line. droppedPaths
// reads every form. What they hand over is often a temporary file: see pluginkit/SampleFiles.h.
#pragma once

#include "vstgui/lib/cdrawdefs.h"
#include "vstgui/lib/idatapackage.h"

#include "pluginkit/SampleFiles.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace pk {

namespace dropdetail {
inline int hexValue (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = (char)std::tolower ((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
// A line of dropped text as a path: trims it, takes file:// URLs apart (with %xx escapes).
inline std::string pathFromText (std::string s)
{
    while (!s.empty () && (std::isspace ((unsigned char)s.back ()) || s.back () == '"' || s.back () == '\0'))
        s.pop_back ();
    size_t a = 0;
    while (a < s.size () && (std::isspace ((unsigned char)s[a]) || s[a] == '"'))
        ++a;
    s = s.substr (a);
    if (s.rfind ("file://", 0) == 0)
    {
        s = s.substr (7);
        if (s.rfind ("localhost", 0) == 0)
            s = s.substr (9);
#if defined(_WIN32)
        if (s.size () > 2 && s[0] == '/' && s[2] == ':') // file:///C:/...
            s = s.substr (1);
#endif
        std::string out;
        for (size_t i = 0; i < s.size (); ++i)
            if (s[i] == '%' && i + 2 < s.size () && hexValue (s[i + 1]) >= 0 && hexValue (s[i + 2]) >= 0)
            {
                out += (char)(hexValue (s[i + 1]) * 16 + hexValue (s[i + 2]));
                i += 2;
            }
            else
                out += s[i];
        s = out;
    }
    return s;
}
} // namespace dropdetail

// Every path in a drop, in order (file paths, and paths or file:// URLs in text).
inline std::vector<std::string> droppedPaths (VSTGUI::IDataPackage* drag)
{
    std::vector<std::string> out;
    if (!drag)
        return out;
    for (uint32_t i = 0; i < drag->getCount (); ++i)
    {
        const void* buffer = nullptr;
        VSTGUI::IDataPackage::Type type;
        const uint32_t size = drag->getData (i, buffer, type);
        if (!buffer || size == 0)
            continue;
        const std::string raw (static_cast<const char*> (buffer), strnlen (static_cast<const char*> (buffer), size));
        if (type == VSTGUI::IDataPackage::kFilePath)
            out.push_back (raw);
        else if (type == VSTGUI::IDataPackage::kText)
        {
            size_t pos = 0;
            while (pos <= raw.size ())
            {
                const size_t nl = raw.find_first_of ("\r\n", pos);
                const std::string line = dropdetail::pathFromText (raw.substr (pos, nl == std::string::npos ? std::string::npos : nl - pos));
                std::error_code ec;
                if (!line.empty () && std::filesystem::is_regular_file (filedetail::fromUtf8 (line), ec))
                    out.push_back (line);
                if (nl == std::string::npos)
                    break;
                pos = nl + 1;
            }
        }
    }
    return out;
}

} // namespace pk
