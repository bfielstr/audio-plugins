// Samples that hosts hand over as temporary files (a rendered or consolidated clip, a recording)
// which the host deletes later. keepIfTemporary copies such a file into bfielstr/Samples in the
// user's documents, so a project that refers to the sample by its path keeps working.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace pk {

namespace filedetail {
inline std::filesystem::path fromUtf8 (const std::string& s) { return std::filesystem::path (std::u8string (s.begin (), s.end ())); }
inline std::string toUtf8 (const std::filesystem::path& p)
{
    const auto u = p.u8string ();
    return std::string (u.begin (), u.end ());
}
} // namespace filedetail

// Where copies of temporary samples are kept: <Documents>/bfielstr/Samples (Linux: ~/bfielstr/Samples).
inline std::filesystem::path samplesFolder ()
{
#if defined(_WIN32)
    const char* home = std::getenv ("USERPROFILE");
    return std::filesystem::path (home ? home : ".") / "Documents" / "bfielstr" / "Samples";
#elif defined(__APPLE__)
    const char* home = std::getenv ("HOME");
    return std::filesystem::path (home ? home : ".") / "Documents" / "bfielstr" / "Samples";
#else
    const char* home = std::getenv ("HOME");
    return std::filesystem::path (home ? home : ".") / "bfielstr" / "Samples";
#endif
}

// True for a file in the system's temporary folder or in any folder called temp / tmp (where
// REAPER, Live and the OS put rendered, consolidated and recorded clips they delete later).
inline bool isTemporaryFile (const std::string& path)
{
    std::error_code ec;
    const auto p = std::filesystem::weakly_canonical (filedetail::fromUtf8 (path), ec);
    const auto tmp = std::filesystem::weakly_canonical (std::filesystem::temp_directory_path (ec), ec);
    auto lower = [] (std::string s) {
        std::transform (s.begin (), s.end (), s.begin (), [] (unsigned char c) { return (char)std::tolower (c); });
        return s;
    };
    const std::string ps = lower (filedetail::toUtf8 (p)), ts = lower (filedetail::toUtf8 (tmp));
    if (!ts.empty () && ps.rfind (ts, 0) == 0)
        return true;
    for (const auto& part : p.parent_path ())
    {
        const std::string n = lower (filedetail::toUtf8 (part));
        if (n == "temp" || n == "tmp" || n == "temporary items" || n.find ("temp folder") != std::string::npos)
            return true;
    }
    return ps.find ("/var/folders/") != std::string::npos;
}

// A temporary file is copied into `dir` (a new name if one with a different size is already
// there) and the copy's path returned; any other path comes back as it is.
inline std::string keepIfTemporary (const std::string& path, const std::filesystem::path& dir = samplesFolder ())
{
    if (!isTemporaryFile (path))
        return path;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path src = filedetail::fromUtf8 (path);
    fs::create_directories (dir, ec);
    const auto size = fs::file_size (src, ec);
    if (ec)
        return path;
    const std::string stem = filedetail::toUtf8 (src.stem ()), ext = filedetail::toUtf8 (src.extension ());
    for (int n = 1; n < 1000; ++n)
    {
        const fs::path dst = dir / filedetail::fromUtf8 (n == 1 ? stem + ext : stem + " " + std::to_string (n) + ext);
        std::error_code e2;
        if (fs::exists (dst, e2))
        {
            if (fs::file_size (dst, e2) == size)
                return filedetail::toUtf8 (dst); // the same sample, kept before
            continue;
        }
        if (fs::copy_file (src, dst, e2))
            return filedetail::toUtf8 (dst);
        return path;
    }
    return path;
}

} // namespace pk
