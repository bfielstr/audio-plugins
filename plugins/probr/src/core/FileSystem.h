// The files the writer thread touches, behind an interface: the disk (makeDiskFileSystem), or a fake
// in the tests (a disk that fills up, a folder that cannot be written). Paths are UTF-8.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace probr {

class FileSystem
{
public:
    virtual ~FileSystem () = default;
    // the folder and every folder above it
    virtual bool makeDirs (const std::string& dir) = 0;
    // the names (not paths) of the files in `dir`
    virtual std::vector<std::string> list (const std::string& dir) = 0;
    // a file opened for writing: `exclusive` fails when it exists, else it is emptied. -1: failed.
    virtual int create (const std::string& path, bool exclusive) = 0;
    // appends at the end
    virtual bool write (int file, const void* data, size_t bytes) = 0;
    // overwrites bytes already written (a WAV header's sizes); the next write still appends
    virtual bool writeAt (int file, uint64_t offset, const void* data, size_t bytes) = 0;
    virtual bool close (int file) = 0;
    // the whole file ("" when it is not there)
    virtual bool read (const std::string& path, std::string& out) = 0;
    // free bytes on the disk that holds `path` (or the nearest folder above it that exists); -1 unknown
    virtual int64_t freeBytes (const std::string& path) = 0;

    // A whole file written at once (replacing it).
    bool writeFile (const std::string& path, const std::string& text)
    {
        const int f = create (path, false);
        if (f < 0)
            return false;
        const bool ok = write (f, text.data (), text.size ());
        return close (f) && ok;
    }
};

std::unique_ptr<FileSystem> makeDiskFileSystem ();

// "a/b" + "c" with the platform's separator
std::string joinPath (const std::string& dir, const std::string& name);

} // namespace probr
