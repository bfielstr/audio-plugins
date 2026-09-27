#include "Wav.h"

#include "Render.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace stretchr {

namespace {
void put32 (std::vector<char>& b, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back ((char)(v >> (8 * i)));
}
void put16 (std::vector<char>& b, uint16_t v)
{
    b.push_back ((char)(v & 0xff));
    b.push_back ((char)(v >> 8));
}
} // namespace

bool writeWav (const std::string& path, const Rendered& r, std::string& error)
{
    const uint64_t frames = (uint64_t)r.l.size ();
    const uint64_t dataBytes = frames * 2 * 4;
    if (dataBytes > 0xffffff00ull - 64)
    {
        error = "Render is too long for a WAV file";
        return false;
    }
    std::vector<char> h;
    h.insert (h.end (), {'R', 'I', 'F', 'F'});
    put32 (h, (uint32_t)(4 + 26 + 12 + 8 + dataBytes));
    h.insert (h.end (), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32 (h, 18);
    put16 (h, 3); // IEEE float
    put16 (h, 2);
    put32 (h, (uint32_t)(r.sampleRate + 0.5));
    put32 (h, (uint32_t)(r.sampleRate + 0.5) * 8);
    put16 (h, 8);
    put16 (h, 32);
    put16 (h, 0);
    h.insert (h.end (), {'f', 'a', 'c', 't'});
    put32 (h, 4);
    put32 (h, (uint32_t)frames);
    h.insert (h.end (), {'d', 'a', 't', 'a'});
    put32 (h, (uint32_t)dataBytes);

    std::ofstream f (simplr::pathFromUtf8 (path), std::ios::binary | std::ios::trunc);
    if (!f)
    {
        error = "Could not create " + path;
        return false;
    }
    f.write (h.data (), (std::streamsize)h.size ());
    std::vector<float> buf;
    constexpr size_t kChunk = 16384;
    buf.reserve (kChunk * 2);
    for (size_t i = 0; i < frames; i += kChunk)
    {
        buf.clear ();
        const size_t n = std::min<size_t> (kChunk, frames - i);
        for (size_t j = 0; j < n; ++j)
        {
            buf.push_back (r.l[i + j]);
            buf.push_back (r.r.size () > i + j ? r.r[i + j] : r.l[i + j]);
        }
        static_assert (sizeof (float) == 4, "float");
        // WAV is little-endian, as are all supported platforms.
        f.write (reinterpret_cast<const char*> (buf.data ()), (std::streamsize)(buf.size () * 4));
    }
    if (!f)
    {
        error = "Could not write " + path;
        return false;
    }
    return true;
}

} // namespace stretchr
