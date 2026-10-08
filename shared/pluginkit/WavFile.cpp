#include "pluginkit/WavFile.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace pk::wav {

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
uint32_t get32 (const std::vector<char>& b, size_t at)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= (uint32_t)(uint8_t)b[at + (size_t)i] << (8 * i);
    return v;
}
uint16_t get16 (const std::vector<char>& b, size_t at) { return (uint16_t)((uint8_t)b[at] | ((uint8_t)b[at + 1] << 8)); }
std::filesystem::path fromUtf8 (const std::string& s) { return std::filesystem::path (std::u8string (s.begin (), s.end ())); }
} // namespace

std::string clmText (int frameSize) { return "<!>" + std::to_string (frameSize) + " 01000000 wavetable (www.xferrecords.com)"; }

std::vector<char> encode (const std::vector<const float*>& data, size_t frames, double sampleRate, const std::string& clm)
{
    const auto channels = (uint16_t)data.size ();
    const auto sr = (uint32_t)std::lround (sampleRate);
    const uint64_t dataBytes = (uint64_t)frames * channels * 4;
    std::vector<char> clmChunk;
    if (!clm.empty ())
    {
        clmChunk.insert (clmChunk.end (), {'c', 'l', 'm', ' '});
        put32 (clmChunk, (uint32_t)clm.size ());
        clmChunk.insert (clmChunk.end (), clm.begin (), clm.end ());
        if (clm.size () & 1)
            clmChunk.push_back (0); // (chunks are padded to an even size)
    }
    std::vector<char> b;
    b.reserve ((size_t)(64 + clmChunk.size () + dataBytes));
    b.insert (b.end (), {'R', 'I', 'F', 'F'});
    put32 (b, (uint32_t)(4 + (8 + 18) + (8 + 4) + clmChunk.size () + 8 + dataBytes));
    b.insert (b.end (), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32 (b, 18);
    put16 (b, 3); // IEEE float
    put16 (b, channels);
    put32 (b, sr);
    put32 (b, sr * channels * 4);
    put16 (b, (uint16_t)(channels * 4));
    put16 (b, 32);
    put16 (b, 0);
    b.insert (b.end (), {'f', 'a', 'c', 't'});
    put32 (b, 4);
    put32 (b, (uint32_t)frames);
    b.insert (b.end (), clmChunk.begin (), clmChunk.end ());
    b.insert (b.end (), {'d', 'a', 't', 'a'});
    put32 (b, (uint32_t)dataBytes);
    static_assert (sizeof (float) == 4, "float");
    const size_t at = b.size ();
    b.resize (at + (size_t)dataBytes);
    char* out = b.data () + at;
    for (size_t f = 0; f < frames; ++f)
        for (size_t c = 0; c < channels; ++c, out += 4)
            std::memcpy (out, data[c] + f, 4);
    return b;
}

bool write (const std::string& path, const std::vector<char>& bytes, std::string& error)
{
    std::error_code ec;
    const auto p = fromUtf8 (path);
    if (p.has_parent_path ())
        std::filesystem::create_directories (p.parent_path (), ec);
    std::ofstream f (p, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        error = "Could not create " + path;
        return false;
    }
    f.write (bytes.data (), (std::streamsize)bytes.size ());
    if (!f)
    {
        error = "Could not write " + path;
        return false;
    }
    return true;
}

bool parse (const std::vector<char>& b, Info& out)
{
    if (b.size () < 12 || std::memcmp (b.data (), "RIFF", 4) != 0 || std::memcmp (b.data () + 8, "WAVE", 4) != 0)
        return false;
    size_t at = 12;
    bool fmt = false;
    while (at + 8 <= b.size ())
    {
        const std::string id (b.data () + at, 4);
        const uint32_t size = get32 (b, at + 4);
        const size_t body = at + 8;
        if (body + size > b.size ())
            return false;
        if (id == "fmt ")
        {
            out.format = get16 (b, body);
            out.channels = get16 (b, body + 2);
            out.sampleRate = get32 (b, body + 4);
            out.bits = get16 (b, body + 14);
            fmt = true;
        }
        else if (id == "clm ")
            out.clm.assign (b.data () + body, size);
        else if (id == "data" && fmt && out.format == 3 && out.bits == 32)
        {
            out.samples.resize (size / 4);
            std::memcpy (out.samples.data (), b.data () + body, out.samples.size () * 4);
        }
        at = body + size + (size & 1);
    }
    return fmt;
}

} // namespace pk::wav
