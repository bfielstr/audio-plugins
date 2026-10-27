// sunder_render: a WAV through the engine at given settings (the plug-in's IDs and plain values: Drive, Feedback
// and Ratio in steps, Bass among them), for calibrating it against the reference renders (germcal).
// Usage: sunder_render in.wav out.wav [id=plain ...]   (the first two channels; a third and fourth are the
// sidechain input; all the latency taken out, so the output lines up with the input)
#include "Engine.h"
#include "Params.h"

#include "pluginkit/WavFile.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace sunder;

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: sunder_render in.wav out.wav [id=plain ...]\n");
        return 2;
    }
    std::ifstream f (argv[1], std::ios::binary);
    std::vector<char> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
    pk::wav::Info info;
    if (!pk::wav::parse (bytes, info) || info.channels < 1)
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    const size_t ch = (size_t)info.channels, frames = info.samples.size () / ch;
    auto e = std::make_unique<Engine> ();
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        const size_t eq = a.find ('=');
        if (eq != std::string::npos)
            e->setParam ((uint32_t)std::atoi (a.substr (0, eq).c_str ()), std::atof (a.substr (eq + 1).c_str ()));
    }
    e->prepare (info.sampleRate, 512);
    const int lat = e->latency (); // (the oversampler's)
    std::vector<float> l (frames + (size_t)lat, 0.0f), r (frames + (size_t)lat, 0.0f), sl (frames + (size_t)lat, 0.0f),
        sr (frames + (size_t)lat, 0.0f);
    for (size_t i = 0; i < frames; ++i)
    {
        l[i] = info.samples[i * ch];
        r[i] = info.samples[i * ch + (ch > 1 ? 1 : 0)];
        if (ch >= 4)
        {
            sl[i] = info.samples[i * ch + 2];
            sr[i] = info.samples[i * ch + 3];
        }
    }
    for (size_t a = 0; a < l.size (); a += 512)
    {
        const int n = (int)std::min<size_t> (512, l.size () - a);
        e->process (l.data () + a, r.data () + a, l.data () + a, r.data () + a, n, ch >= 4 ? sl.data () + a : nullptr,
                    ch >= 4 ? sr.data () + a : nullptr);
    }
    std::string err;
    const auto out = pk::wav::encode ({l.data () + lat, r.data () + lat}, frames, info.sampleRate);
    if (!pk::wav::write (argv[2], out, err))
    {
        std::fprintf (stderr, "%s\n", err.c_str ());
        return 1;
    }
    return 0;
}
