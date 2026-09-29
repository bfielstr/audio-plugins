#include "Bridge.h"

#include "smempler/src/core/SampleData.h" // the suite's audio file decoder

#include <algorithm>
#include <filesystem>

namespace detonatr {

bool Bridge::loadCarrier (int slot, const std::string& path, std::string& error)
{
    if (slot < 0 || slot >= kCarrierSlots)
        return false;
    std::vector<float> l, r;
    int channels = 0;
    double rate = 48000.0;
    if (!smempler::decodeAudioFile (path, l, r, channels, rate, error))
        return false;
    if (l.empty () || rate <= 0.0)
    {
        error = "The file has no audio";
        return false;
    }
    auto c = std::make_shared<Carrier> ();
    const size_t frames = std::min (l.size (), (size_t)(kMaxCarrierSeconds * rate));
    c->ch[0].assign (l.begin (), l.begin () + (ptrdiff_t)frames);
    if (r.size () >= frames)
        c->ch[1].assign (r.begin (), r.begin () + (ptrdiff_t)frames);
    else
        c->ch[1] = c->ch[0];
    c->frames = (int)frames;
    c->sampleRate = rate;
    setCarrier (slot, c, smempler::utf8FromPath (smempler::pathFromUtf8 (path).filename ()));
    return true;
}

void Bridge::setCarrier (int slot, CarrierPtr c, const std::string& name)
{
    if (slot < 0 || slot >= kCarrierSlots)
        return;
    {
        std::lock_guard<std::mutex> lock (nameMutex);
        names[slot] = c ? name : std::string ();
    }
    carriers[slot].publish (std::move (c));
    changeCounter.fetch_add (1);
}

std::string Bridge::carrierName (int slot) const
{
    std::lock_guard<std::mutex> lock (nameMutex);
    return slot >= 0 && slot < kCarrierSlots ? names[slot] : std::string ();
}

} // namespace detonatr
