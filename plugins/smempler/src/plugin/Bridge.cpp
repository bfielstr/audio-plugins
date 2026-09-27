#include "Bridge.h"

namespace smempler {

bool Bridge::loadSample (const std::string& p, const SampleOps& o, std::string& err)
{
    auto s = SampleData::load (p, o, err);
    {
        std::lock_guard<std::mutex> lock (infoMutex);
        path = p;
        ops = s ? s->ops : o;
        missing = !s;
        error = s ? std::string () : err;
    }
    if (s)
        samples.publish (s);
    else
        samples.publish (nullptr);
    changeCounter.fetch_add (1);
    return s != nullptr;
}

void Bridge::clearSample ()
{
    {
        std::lock_guard<std::mutex> lock (infoMutex);
        path.clear ();
        ops = {};
        missing = false;
        error.clear ();
    }
    samples.publish (nullptr);
    setEdits ({});
    changeCounter.fetch_add (1);
}

std::string Bridge::samplePath () const
{
    std::lock_guard<std::mutex> lock (infoMutex);
    return path;
}

SampleOps Bridge::sampleOps () const
{
    std::lock_guard<std::mutex> lock (infoMutex);
    return ops;
}

bool Bridge::sampleMissing () const
{
    std::lock_guard<std::mutex> lock (infoMutex);
    return missing;
}

std::string Bridge::lastError () const
{
    std::lock_guard<std::mutex> lock (infoMutex);
    return error;
}

bool Bridge::pushPreview (int note, float velocity)
{
    const int w = previewWrite.load (std::memory_order_relaxed);
    const int next = (w + 1) % kPreviewSize;
    if (next == previewRead.load (std::memory_order_acquire))
        return false;
    previews[(size_t)w] = {note, velocity};
    previewWrite.store (next, std::memory_order_release);
    return true;
}

bool Bridge::popPreview (int& note, float& velocity)
{
    const int r = previewRead.load (std::memory_order_relaxed);
    if (r == previewWrite.load (std::memory_order_acquire))
        return false;
    note = previews[(size_t)r].note;
    velocity = previews[(size_t)r].velocity;
    previewRead.store ((r + 1) % kPreviewSize, std::memory_order_release);
    return true;
}

} // namespace smempler
