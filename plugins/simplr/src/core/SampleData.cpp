#include "SampleData.h"

#include "Fft.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

namespace simplr {

namespace {

std::string extensionOf (const std::string& path)
{
    auto dot = path.find_last_of ('.');
    if (dot == std::string::npos)
        return {};
    std::string e = path.substr (dot + 1);
    for (auto& c : e)
        c = (char)std::tolower ((unsigned char)c);
    return e;
}

std::string fileNameOf (const std::string& path)
{
    auto slash = path.find_last_of ("/\\");
    return slash == std::string::npos ? path : path.substr (slash + 1);
}

void deinterleave (const float* src, uint64_t frames, unsigned channels, std::vector<float>& l,
                   std::vector<float>& r)
{
    l.resize ((size_t)frames);
    if (channels >= 2)
        r.resize ((size_t)frames);
    for (uint64_t i = 0; i < frames; ++i)
    {
        l[(size_t)i] = src[i * channels];
        if (channels >= 2)
            r[(size_t)i] = src[i * channels + 1];
    }
}

} // namespace

std::filesystem::path pathFromUtf8 (const std::string& s)
{
    return std::filesystem::path (std::u8string (s.begin (), s.end ()));
}

std::string utf8FromPath (const std::filesystem::path& p)
{
    const auto u = p.u8string ();
    return std::string (u.begin (), u.end ());
}

bool isSupportedAudioFile (const std::string& path)
{
    auto e = extensionOf (path);
    return e == "wav" || e == "wave" || e == "aif" || e == "aiff" || e == "aifc" || e == "flac" || e == "mp3";
}

bool decodeAudioFile (const std::string& path, std::vector<float>& left, std::vector<float>& right,
                      int& numChannels, double& sampleRate, std::string& error)
{
    const auto ext = extensionOf (path);
    unsigned channels = 0, rate = 0;
    uint64_t frames = 0;
    float* pcm = nullptr;
    left.clear ();
    right.clear ();

    // Read the file ourselves: std::filesystem handles UTF-8 paths on every OS (the stdio paths
    // inside dr_libs don't on Windows), then decode from memory.
    std::vector<char> bytes;
    {
        std::ifstream in (pathFromUtf8 (path), std::ios::binary);
        if (in)
        {
            in.seekg (0, std::ios::end);
            const auto size = (std::streamoff)in.tellg ();
            if (size > 0)
            {
                bytes.resize ((size_t)size);
                in.seekg (0, std::ios::beg);
                in.read (bytes.data (), size);
                if (!in)
                    bytes.clear ();
            }
        }
    }
    if (!bytes.empty ())
    {
        if (ext == "flac")
        {
            drflac_uint64 f = 0;
            pcm = drflac_open_memory_and_read_pcm_frames_f32 (bytes.data (), bytes.size (), &channels, &rate, &f, nullptr);
            frames = f;
            if (pcm)
            {
                deinterleave (pcm, frames, channels, left, right);
                drflac_free (pcm, nullptr);
            }
        }
        else if (ext == "mp3")
        {
            drmp3_config cfg {};
            drmp3_uint64 f = 0;
            pcm = drmp3_open_memory_and_read_pcm_frames_f32 (bytes.data (), bytes.size (), &cfg, &f, nullptr);
            channels = cfg.channels;
            rate = cfg.sampleRate;
            frames = f;
            if (pcm)
            {
                deinterleave (pcm, frames, channels, left, right);
                drmp3_free (pcm, nullptr);
            }
        }
        else
        {
            // dr_wav handles RIFF/RF64/W64 and AIFF/AIFC.
            drwav_uint64 f = 0;
            pcm = drwav_open_memory_and_read_pcm_frames_f32 (bytes.data (), bytes.size (), &channels, &rate, &f, nullptr);
            frames = f;
            if (pcm)
            {
                deinterleave (pcm, frames, channels, left, right);
                drwav_free (pcm, nullptr);
            }
        }
    }

    if (left.empty () || channels == 0 || rate == 0)
    {
        error = "Could not decode \"" + fileNameOf (path) + "\"";
        left.clear ();
        right.clear ();
        return false;
    }
    numChannels = channels >= 2 ? 2 : 1;
    sampleRate = rate;
    return true;
}

std::shared_ptr<SampleData> SampleData::fromBuffers (std::vector<float> left, std::vector<float> right,
                                                     double sampleRate, const std::string& name)
{
    auto s = std::make_shared<SampleData> ();
    s->name = name;
    s->sampleRate = sampleRate;
    s->numChannels = right.empty () ? 1 : 2;
    s->length = (int)left.size ();
    s->ch[0] = std::move (left);
    if (s->numChannels == 2)
    {
        right.resize ((size_t)s->length);
        s->ch[1] = std::move (right);
    }
    s->analyse ();
    return s;
}

std::shared_ptr<SampleData> SampleData::load (const std::string& path, const SampleOps& opsIn, std::string& error)
{
    std::vector<float> l, r;
    int nch = 0;
    double sr = 0;
    if (!decodeAudioFile (path, l, r, nch, sr, error))
        return nullptr;

    SampleOps ops = opsIn;
    ops.cropStart = std::clamp (ops.cropStart, 0.0, 1.0);
    ops.cropEnd = std::clamp (ops.cropEnd, ops.cropStart, 1.0);
    const int total = (int)l.size ();
    int a = (int)std::floor (ops.cropStart * total);
    int b = (int)std::ceil (ops.cropEnd * total);
    a = std::clamp (a, 0, total);
    b = std::clamp (b, a, total);
    if (b - a < 16) // refuse a degenerate crop and fall back to the full file
    {
        a = 0;
        b = total;
        ops.cropStart = 0.0;
        ops.cropEnd = 1.0;
    }
    if (a > 0 || b < total)
    {
        l = std::vector<float> (l.begin () + a, l.begin () + b);
        if (nch == 2)
            r = std::vector<float> (r.begin () + a, r.begin () + b);
    }
    if (ops.reverse)
    {
        std::reverse (l.begin (), l.end ());
        if (nch == 2)
            std::reverse (r.begin (), r.end ());
    }
    if (ops.normalize)
    {
        float peak = 0.0f;
        for (float v : l)
            peak = std::max (peak, std::fabs (v));
        for (float v : r)
            peak = std::max (peak, std::fabs (v));
        if (peak > 1e-6f)
        {
            const float g = 1.0f / peak;
            for (auto& v : l)
                v *= g;
            for (auto& v : r)
                v *= g;
        }
    }

    auto s = fromBuffers (std::move (l), nch == 2 ? std::move (r) : std::vector<float> {}, sr, fileNameOf (path));
    s->path = path;
    s->ops = ops;
    return s;
}

int SampleData::snapToZero (int pos, int maxDistance) const
{
    if (length < 2)
        return pos;
    pos = std::clamp (pos, 0, length - 1);
    const float* d = ch[0].data ();
    auto isCrossing = [&] (int i) {
        if (i <= 0 || i >= length)
            return false;
        return (d[i - 1] <= 0.0f && d[i] > 0.0f) || (d[i - 1] >= 0.0f && d[i] < 0.0f) || d[i] == 0.0f;
    };
    for (int dist = 0; dist <= maxDistance; ++dist)
    {
        if (isCrossing (pos - dist))
            return pos - dist;
        if (isCrossing (pos + dist))
            return pos + dist;
    }
    return pos;
}

void SampleData::analyse ()
{
    peakAbs = 0.0f;
    for (int c = 0; c < numChannels; ++c)
        for (float v : ch[c])
            peakAbs = std::max (peakAbs, std::fabs (v));

    // Coarse peaks for drawing.
    peaks.blockSize = 256;
    const int blocks = (length + peaks.blockSize - 1) / peaks.blockSize;
    for (int c = 0; c < numChannels; ++c)
    {
        peaks.mn[c].assign ((size_t)blocks, 0.0f);
        peaks.mx[c].assign ((size_t)blocks, 0.0f);
        const float* d = ch[c].data ();
        for (int b = 0; b < blocks; ++b)
        {
            const int s = b * peaks.blockSize, e = std::min (length, s + peaks.blockSize);
            float lo = d[s], hi = d[s];
            for (int i = s + 1; i < e; ++i)
            {
                lo = std::min (lo, d[i]);
                hi = std::max (hi, d[i]);
            }
            peaks.mn[c][(size_t)b] = lo;
            peaks.mx[c][(size_t)b] = hi;
        }
    }

    // Onset detection: log-compressed spectral flux with an adaptive threshold.
    onsets.clear ();
    if (length < 64)
        return;
    int fftSize = 1024;
    if (sampleRate > 60000.0)
        fftSize = 2048;
    if (sampleRate > 120000.0)
        fftSize = 4096;
    const int hop = seconds () > 90.0 ? fftSize / 2 : fftSize / 4;
    const int frames = length / hop + 1;
    Fft fft (fftSize);
    std::vector<float> window ((size_t)fftSize), buf ((size_t)fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t)i] = 0.5f - 0.5f * std::cos (2.0f * (float)M_PI * i / fftSize);
    std::vector<Fft::cf> spec ((size_t)fft.bins ());
    std::vector<float> prevLog ((size_t)fft.bins (), 0.0f), flux ((size_t)frames, 0.0f);
    const float* L = ch[0].data ();
    const float* R = numChannels > 1 ? ch[1].data () : nullptr;
    const float magScale = 2.0f / (float)fftSize;

    for (int f = 0; f < frames; ++f)
    {
        const int centre = f * hop;
        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = centre - fftSize / 2 + i;
            float v = 0.0f;
            if (idx >= 0 && idx < length)
                v = R ? 0.5f * (L[idx] + R[idx]) : L[idx];
            buf[(size_t)i] = v * window[(size_t)i];
        }
        fft.forward (buf.data (), spec.data ());
        float sum = 0.0f;
        for (int k = 1; k < fft.bins (); ++k)
        {
            const float lg = std::log1p (1000.0f * std::abs (spec[(size_t)k]) * magScale);
            const float d = lg - prevLog[(size_t)k];
            if (d > 0.0f)
                sum += d;
            prevLog[(size_t)k] = lg;
        }
        flux[(size_t)f] = f == 0 ? 0.0f : sum;
    }

    // Novelty = flux above its local mean.
    std::vector<float> novelty ((size_t)frames, 0.0f);
    const int meanRadius = std::max (4, (int)(0.1 * sampleRate / hop));
    std::vector<double> prefix ((size_t)frames + 1, 0.0);
    for (int f = 0; f < frames; ++f)
        prefix[(size_t)f + 1] = prefix[(size_t)f] + flux[(size_t)f];
    for (int f = 0; f < frames; ++f)
    {
        const int a = std::max (0, f - meanRadius), b = std::min (frames, f + meanRadius + 1);
        const double mean = (prefix[(size_t)b] - prefix[(size_t)a]) / (b - a);
        novelty[(size_t)f] = std::max (0.0f, flux[(size_t)f] - (float)(1.1 * mean));
    }

    const int peakRadius = std::max (2, (int)(0.03 * sampleRate / hop));
    const int minGap = (int)(0.045 * sampleRate);
    std::vector<Onset> found;
    for (int f = 1; f < frames; ++f)
    {
        const float v = novelty[(size_t)f];
        if (v <= 0.0f)
            continue;
        bool isMax = true;
        for (int j = std::max (0, f - peakRadius); j <= std::min (frames - 1, f + peakRadius) && isMax; ++j)
            if (novelty[(size_t)j] > v || (novelty[(size_t)j] == v && j < f))
                isMax = false;
        if (!isMax)
            continue;
        // Refine: the flux peaks somewhere while the onset is inside the analysis window, so
        // look through the window for the sharpest rise of a short-term peak envelope.
        const int chunk = 32;
        const int lo = std::max (0, f * hop - hop);
        const int hi = std::min (length - 1, f * hop + fftSize / 2 + hop);
        auto amp = [&] (int i) { return R ? 0.5f * std::fabs (L[i] + R[i]) : std::fabs (L[i]); };
        float prev1 = 0.0f, prev2 = 0.0f, bestRise = -1.0f, bestLevel = 0.0f;
        int bestChunk = f * hop;
        const int scanFrom = std::max (0, lo - 2 * chunk);
        for (int c = scanFrom; c + chunk <= hi; c += chunk)
        {
            float e = 0.0f;
            for (int i = c; i < c + chunk; ++i)
                e = std::max (e, amp (i));
            const float rise = e - std::max (prev1, prev2);
            if ((c >= lo || scanFrom == 0) && rise > bestRise)
            {
                bestRise = rise;
                bestChunk = c;
                bestLevel = e;
            }
            prev2 = prev1;
            prev1 = e;
        }
        int pos = bestChunk;
        for (int i = std::max (0, bestChunk - chunk); i < bestChunk + chunk && i < length; ++i)
            if (amp (i) >= 0.3f * bestLevel)
            {
                pos = i;
                break;
            }
        pos = std::max (0, pos - (int)(0.0005 * sampleRate));
        if (!found.empty () && pos - found.back ().pos < minGap)
        {
            if (v > found.back ().strength)
                found.back () = {pos, v};
            continue;
        }
        found.push_back ({pos, v});
    }
    float maxStrength = 0.0f;
    for (auto& o : found)
        maxStrength = std::max (maxStrength, o.strength);
    if (maxStrength > 0.0f)
        for (auto& o : found)
            o.strength /= maxStrength;
    onsets = std::move (found);
}

} // namespace simplr
