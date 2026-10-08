#include "FileSystem.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <system_error>

namespace probr {

namespace fs = std::filesystem;

namespace {

fs::path fromUtf8 (const std::string& s) { return fs::path (std::u8string (s.begin (), s.end ())); }
std::string toUtf8 (const fs::path& p)
{
    const auto u = p.u8string ();
    return std::string (u.begin (), u.end ());
}

std::FILE* openFile (const fs::path& p, bool forWrite)
{
#if defined(_WIN32)
    return _wfopen (p.c_str (), forWrite ? L"wb" : L"rb");
#else
    return std::fopen (p.c_str (), forWrite ? "wb" : "rb");
#endif
}

bool seekTo (std::FILE* f, uint64_t at)
{
#if defined(_WIN32)
    return _fseeki64 (f, (long long)at, SEEK_SET) == 0;
#else
    return fseeko (f, (off_t)at, SEEK_SET) == 0;
#endif
}

bool seekEnd (std::FILE* f)
{
#if defined(_WIN32)
    return _fseeki64 (f, 0, SEEK_END) == 0;
#else
    return fseeko (f, 0, SEEK_END) == 0;
#endif
}

class DiskFileSystem : public FileSystem
{
public:
    ~DiskFileSystem () override
    {
        for (auto& [id, f] : files)
            std::fclose (f);
    }
    bool makeDirs (const std::string& dir) override
    {
        std::error_code ec;
        fs::create_directories (fromUtf8 (dir), ec);
        return fs::is_directory (fromUtf8 (dir), ec);
    }
    std::vector<std::string> list (const std::string& dir) override
    {
        std::vector<std::string> out;
        std::error_code ec;
        for (fs::directory_iterator it (fromUtf8 (dir), ec), end; !ec && it != end; it.increment (ec))
            out.push_back (toUtf8 (it->path ().filename ()));
        return out;
    }
    int create (const std::string& path, bool exclusive) override
    {
        const fs::path p = fromUtf8 (path);
        std::error_code ec;
        if (exclusive && fs::exists (p, ec))
            return -1;
        std::FILE* f = openFile (p, true);
        if (!f)
            return -1;
        std::lock_guard<std::mutex> g (m);
        const int id = next++;
        files[id] = f;
        return id;
    }
    bool write (int file, const void* data, size_t bytes) override
    {
        std::FILE* f = get (file);
        return f && (bytes == 0 || std::fwrite (data, 1, bytes, f) == bytes);
    }
    bool writeAt (int file, uint64_t offset, const void* data, size_t bytes) override
    {
        std::FILE* f = get (file);
        if (!f || !seekTo (f, offset))
            return false;
        const bool ok = std::fwrite (data, 1, bytes, f) == bytes;
        return seekEnd (f) && ok;
    }
    bool close (int file) override
    {
        std::FILE* f = nullptr;
        {
            std::lock_guard<std::mutex> g (m);
            auto it = files.find (file);
            if (it == files.end ())
                return false;
            f = it->second;
            files.erase (it);
        }
        const bool flushed = std::fflush (f) == 0;
        return std::fclose (f) == 0 && flushed;
    }
    bool read (const std::string& path, std::string& out) override
    {
        out.clear ();
        std::FILE* f = openFile (fromUtf8 (path), false);
        if (!f)
            return false;
        char buf[4096];
        size_t n;
        while ((n = std::fread (buf, 1, sizeof (buf), f)) > 0)
            out.append (buf, n);
        std::fclose (f);
        return true;
    }
    int64_t freeBytes (const std::string& path) override
    {
        std::error_code ec;
        fs::path p = fromUtf8 (path);
        while (!p.empty () && !fs::exists (p, ec))
        {
            const fs::path up = p.parent_path ();
            if (up == p)
                break;
            p = up;
        }
        if (p.empty ())
            return -1;
        const auto s = fs::space (p, ec);
        return ec ? -1 : (int64_t)s.available;
    }

private:
    std::FILE* get (int file)
    {
        std::lock_guard<std::mutex> g (m);
        auto it = files.find (file);
        return it == files.end () ? nullptr : it->second;
    }
    std::mutex m;
    std::map<int, std::FILE*> files;
    int next = 1;
};

} // namespace

std::unique_ptr<FileSystem> makeDiskFileSystem () { return std::make_unique<DiskFileSystem> (); }

std::string joinPath (const std::string& dir, const std::string& name)
{
    if (dir.empty ())
        return name;
    const char last = dir.back ();
    if (last == '/' || last == '\\')
        return dir + name;
#if defined(_WIN32)
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

} // namespace probr
