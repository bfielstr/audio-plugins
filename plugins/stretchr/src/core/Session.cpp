#include "Session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace stretchr {

using Clock = std::chrono::steady_clock;

namespace {
long long nowMs ()
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (Clock::now ().time_since_epoch ()).count ();
}
uint64_t mix (uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}
uint64_t bits (double d)
{
    uint64_t v;
    std::memcpy (&v, &d, 8);
    return v;
}
} // namespace

Session::Session ()
{
    for (uint32_t id = 0; id < kNumParams; ++id)
        params[id].store (paramTable ().info (id).def);
    worker = std::thread ([this] { run (); });
}

Session::~Session ()
{
    quit.store (true);
    notifyWorker ();
    if (worker.joinable ())
        worker.join ();
    for (auto& c : chunks)
        delete c.exchange (nullptr);
}

//==============================================================================
// Clip

Clip Session::clip () const
{
    std::lock_guard<std::mutex> lock (clipMutex);
    Clip c = current;
    c.start = start.load ();
    return c;
}

void Session::snapshotLocked ()
{
    Clip c = current;
    c.start = start.load ();
    undoStack.push_back (std::move (c));
    if ((int)undoStack.size () > kMaxUndo)
        undoStack.erase (undoStack.begin ());
    redoStack.clear ();
}

void Session::setClip (Clip c, bool undoable)
{
    if (!c.empty ())
    {
        if (c.markers.empty ())
            c.markers = identityMarkers (c.srcLength ());
        sanitizeMarkers (c.markers, c.srcLength ());
        sanitizePitch (c.pitch, c.srcLength ());
    }
    {
        std::lock_guard<std::mutex> lock (clipMutex);
        if (undoable)
            snapshotLocked ();
        else
        {
            undoStack.clear ();
            redoStack.clear ();
        }
        start.store (c.start);
        current = std::move (c);
        hasAudio.store (!current.empty (), std::memory_order_release);
        version.fetch_add (1, std::memory_order_acq_rel);
    }
    if (undoable)
        changes.fetch_add (1);
    notifyWorker ();
}

void Session::edit (const std::function<void (Clip&)>& fn, bool snapshot)
{
    {
        std::lock_guard<std::mutex> lock (clipMutex);
        if (current.empty ())
            return;
        if (snapshot)
            snapshotLocked ();
        fn (current);
        sanitizeMarkers (current.markers, current.srcLength ());
        sanitizePitch (current.pitch, current.srcLength ());
        version.fetch_add (1, std::memory_order_acq_rel);
    }
    changes.fetch_add (1);
    notifyWorker ();
}

void Session::pushUndo ()
{
    std::lock_guard<std::mutex> lock (clipMutex);
    snapshotLocked ();
}

bool Session::undo ()
{
    {
        std::lock_guard<std::mutex> lock (clipMutex);
        if (undoStack.empty ())
            return false;
        Clip c = current;
        c.start = start.load ();
        redoStack.push_back (std::move (c));
        current = std::move (undoStack.back ());
        undoStack.pop_back ();
        start.store (current.start);
        hasAudio.store (!current.empty (), std::memory_order_release);
        version.fetch_add (1, std::memory_order_acq_rel);
    }
    changes.fetch_add (1);
    notifyWorker ();
    return true;
}

bool Session::redo ()
{
    {
        std::lock_guard<std::mutex> lock (clipMutex);
        if (redoStack.empty ())
            return false;
        Clip c = current;
        c.start = start.load ();
        undoStack.push_back (std::move (c));
        current = std::move (redoStack.back ());
        redoStack.pop_back ();
        start.store (current.start);
        hasAudio.store (!current.empty (), std::memory_order_release);
        version.fetch_add (1, std::memory_order_acq_rel);
    }
    changes.fetch_add (1);
    notifyWorker ();
    return true;
}

bool Session::canUndo () const
{
    std::lock_guard<std::mutex> lock (clipMutex);
    return !undoStack.empty ();
}

bool Session::canRedo () const
{
    std::lock_guard<std::mutex> lock (clipMutex);
    return !redoStack.empty ();
}

void Session::setClipStart (double seconds, bool snapshot)
{
    {
        std::lock_guard<std::mutex> lock (clipMutex);
        if (snapshot)
            snapshotLocked ();
        current.start = seconds;
        start.store (seconds);
    }
    changes.fetch_add (1);
}

//==============================================================================
// Parameters and render keys

void Session::setParam (uint32_t id, double plain, bool wake)
{
    if (id >= kNumParams)
        return;
    if (params[id].exchange (plain, std::memory_order_relaxed) != plain && wake)
        notifyWorker ();
}

RenderSettings Session::settings () const
{
    double p[kNumParams];
    for (uint32_t id = 0; id < kNumParams; ++id)
        p[id] = params[id].load (std::memory_order_relaxed);
    return settingsFromParams (p, hostBpm.load (std::memory_order_relaxed));
}

uint64_t Session::keyFor (uint64_t v, const RenderSettings& s, double rate)
{
    uint64_t h = mix (0x5354524355ull, v);
    h = mix (h, (uint64_t)s.algorithm);
    h = mix (h, bits (s.semis));
    h = mix (h, bits (s.formantSemis));
    h = mix (h, s.preserveFormants ? 1u : 0u);
    h = mix (h, bits (s.speed));
    h = mix (h, bits (s.windowMs));
    h = mix (h, (uint64_t)s.transients);
    h = mix (h, bits (s.smearMs));
    h = mix (h, bits (s.gainDb));
    h = mix (h, bits (rate));
    return h | 1u; // never 0 (0 = nothing published)
}

uint64_t Session::wantedKey () const
{
    return keyFor (version.load (std::memory_order_acquire), settings (), hostRate.load (std::memory_order_relaxed));
}

bool Session::upToDate () const
{
    if (!hasClip ())
        return true;
    return publishedKey.load (std::memory_order_acquire) == wantedKey ();
}

bool Session::waitUntilRendered (double timeoutSeconds)
{
    const auto deadline = Clock::now () + std::chrono::duration<double> (timeoutSeconds);
    offlineWaiting.store (true);
    notifyWorker ();
    while (!upToDate ())
    {
        if (Clock::now () > deadline)
        {
            offlineWaiting.store (false);
            return false;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    offlineWaiting.store (false);
    return true;
}

void Session::processBegin ()
{
    inProcess.store (true, std::memory_order_release);
    lastProcessMs.store (nowMs (), std::memory_order_relaxed);
}

//==============================================================================
// Capture

void Session::setArmed (bool on)
{
    if (on)
    {
        int expected = kIdle;
        state.compare_exchange_strong (expected, kArmed);
    }
    else
    {
        int expected = kArmed;
        if (!state.compare_exchange_strong (expected, kIdle) && expected == kRecording)
            stopRequest.store (true);
    }
    notifyWorker ();
}

double Session::capturedSeconds () const
{
    return (double)capFrames.load (std::memory_order_relaxed) / std::max (1.0, capRate.load ());
}

void Session::captureBlock (const float* l, const float* r, int n, long long pos, bool isPlaying, double sr)
{
    int st = state.load (std::memory_order_acquire);
    if (st == kArmed && isPlaying && n > 0 && chunksReady.load (std::memory_order_acquire) > 0)
    {
        int expected = kArmed;
        if (state.compare_exchange_strong (expected, kRecording))
        {
            capStartSample.store (pos);
            capFrames.store (0);
            capRate.store (sr);
            capNext = pos;
            stopRequest.store (false);
            st = kRecording;
        }
    }
    if (st != kRecording)
        return;
    if (!isPlaying || pos != capNext || sr != capRate.load () || stopRequest.load ())
    {
        state.store (kFinishing, std::memory_order_release);
        return;
    }
    long long f = capFrames.load (std::memory_order_relaxed);
    const int ready = chunksReady.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i, ++f)
    {
        const long long c = f / kChunkFrames;
        if (c >= ready)
        {
            capFrames.store (f, std::memory_order_release);
            state.store (kFinishing, std::memory_order_release);
            return;
        }
        Chunk* ch = chunks[(size_t)c].load (std::memory_order_relaxed);
        ch->l[f % kChunkFrames] = l[i];
        ch->r[f % kChunkFrames] = r ? r[i] : l[i];
    }
    capFrames.store (f, std::memory_order_release);
    capNext += n;
}

void Session::manageCapture ()
{
    const int st = state.load (std::memory_order_acquire);
    if (st == kArmed || st == kRecording)
    {
        const long long need = capFrames.load () / kChunkFrames + 8;
        int ready = chunksReady.load ();
        while (ready < std::min<long long> (need, kMaxChunks))
        {
            chunks[(size_t)ready].store (new Chunk (), std::memory_order_relaxed);
            chunksReady.store (++ready, std::memory_order_release);
        }
        // The host stopped calling process (e.g. transport stopped with processing suspended):
        // finish a recording the user asked to stop.
        if (st == kRecording && stopRequest.load () && !inProcess.load () &&
            nowMs () - lastProcessMs.load () > 250)
        {
            int expected = kRecording;
            state.compare_exchange_strong (expected, kFinishing);
        }
    }
    if (state.load (std::memory_order_acquire) == kFinishing)
        finishCapture ();
    else if (st == kIdle && chunksReady.load () > 0)
    {
        for (int i = 0; i < chunksReady.load (); ++i)
            delete chunks[(size_t)i].exchange (nullptr);
        chunksReady.store (0);
    }
}

void Session::finishCapture ()
{
    const long long frames = capFrames.load (std::memory_order_acquire);
    const double rate = capRate.load ();
    if (frames >= (long long)(0.1 * rate))
    {
        std::vector<float> l ((size_t)frames), r ((size_t)frames);
        for (long long f = 0; f < frames; ++f)
        {
            const Chunk* ch = chunks[(size_t)(f / kChunkFrames)].load ();
            l[(size_t)f] = ch->l[f % kChunkFrames];
            r[(size_t)f] = ch->r[f % kChunkFrames];
        }
        bool isStereo = false;
        for (long long f = 0; f < frames && !isStereo; ++f)
            isStereo = l[(size_t)f] != r[(size_t)f];
        char name[64];
        std::snprintf (name, sizeof (name), "Capture %d", ++captureCount);
        Clip c;
        c.name = name;
        c.audio = SampleData::fromBuffers (std::move (l), isStereo ? std::move (r) : std::vector<float> {}, rate, name);
        c.start = (double)capStartSample.load () / rate;
        setClip (std::move (c), true);
    }
    for (int i = 0; i < chunksReady.load (); ++i)
        delete chunks[(size_t)i].exchange (nullptr);
    chunksReady.store (0);
    capFrames.store (0);
    stopRequest.store (false);
    state.store (kIdle, std::memory_order_release);
}

//==============================================================================
// Worker

void Session::renderIfNeeded (Clock::time_point now)
{
    const double rate = hostRate.load ();
    if (rate <= 0.0)
        return;
    const uint64_t key = wantedKey ();
    if (key == publishedKey.load ())
    {
        pendingKey = 0;
        return;
    }
    if (pendingKey == 0)
        firstPending = now;
    if (key != pendingKey)
    {
        pendingKey = key;
        pendingSince = now;
    }
    // Wait for edits to settle (knob drags, marker drags), but never for too long.
    const bool offline = offlineWaiting.load ();
    if (!offline && now - pendingSince < std::chrono::milliseconds (40) &&
        now - firstPending < std::chrono::milliseconds (300))
        return;

    Clip c;
    uint64_t v;
    {
        std::lock_guard<std::mutex> lock (clipMutex);
        c = current;
        v = version.load ();
    }
    const RenderSettings s = settings ();
    const uint64_t used = keyFor (v, s, rate);
    if (c.empty ())
    {
        renders.publish (nullptr);
        publishedKey.store (used, std::memory_order_release);
        return;
    }
    rendering.store (true);
    progress.store (0.0f);
    auto out = std::make_shared<Rendered> ();
    const bool ok = renderClip (c, s, rate, *out, cache, [this] (float p) {
        progress.store (p, std::memory_order_relaxed);
        return !quit.load (std::memory_order_relaxed);
    });
    if (ok)
    {
        out->request = used;
        renders.publish (std::move (out));
        publishedKey.store (used, std::memory_order_release);
    }
    progress.store (1.0f);
    rendering.store (false);
}

void Session::run ()
{
    while (!quit.load ())
    {
        {
            std::unique_lock<std::mutex> lock (wakeMutex);
            wake.wait_for (lock, std::chrono::milliseconds (10));
        }
        if (quit.load ())
            break;
        manageCapture ();
        renderIfNeeded (Clock::now ());
        renders.collectGarbage ();
    }
}

} // namespace stretchr
