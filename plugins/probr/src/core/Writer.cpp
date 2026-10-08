#include "Writer.h"

#include "Session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace probr {

namespace {

void put16 (uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
void put32 (uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}

// ---- JSON
void str (std::string& o, const std::string& s)
{
    o += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char b[8];
                    std::snprintf (b, sizeof (b), "\\u%04x", c);
                    o += b;
                }
                else
                    o += (char)c;
        }
    }
    o += '"';
}
void num (std::string& o, double v)
{
    if (!std::isfinite (v))
    {
        o += "null";
        return;
    }
    char b[32];
    std::snprintf (b, sizeof (b), "%.12g", v);
    o += b;
}
void inum (std::string& o, long long v) { o += std::to_string (v); }
void key (std::string& o, const char* k)
{
    o += "  \"";
    o += k;
    o += "\": ";
}

std::string localIso (int64_t now)
{
    const std::time_t t = (std::time_t)now;
    std::tm tm {};
#if defined(_WIN32)
    localtime_s (&tm, &t);
#else
    localtime_r (&t, &tm);
#endif
    char buf[32];
    std::strftime (buf, sizeof (buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

int64_t steadyMs ()
{
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
}

// takes are numbered under this lock, so two probes with one label in one process never take one number
std::mutex& namingLock ()
{
    static std::mutex m;
    return m;
}

} // namespace

void wavHeader (uint8_t* h, uint32_t sampleRate, uint64_t frames)
{
    const uint64_t data = frames * 8;
    std::memcpy (h, "RIFF", 4);
    put32 (h + 4, (uint32_t)std::min<uint64_t> (data + kWavHeaderBytes - 8, 0xFFFFFFFFu));
    std::memcpy (h + 8, "WAVE", 4);
    std::memcpy (h + 12, "fmt ", 4);
    put32 (h + 16, 18);
    put16 (h + 20, 3); // WAVE_FORMAT_IEEE_FLOAT
    put16 (h + 22, 2);
    put32 (h + 24, sampleRate);
    put32 (h + 28, sampleRate * 8);
    put16 (h + 32, 8);
    put16 (h + 34, 32);
    put16 (h + 36, 0); // cbSize
    std::memcpy (h + 38, "fact", 4);
    put32 (h + 42, 4);
    put32 (h + 46, (uint32_t)std::min<uint64_t> (frames, 0xFFFFFFFFu));
    std::memcpy (h + 50, "data", 4);
    put32 (h + 54, (uint32_t)std::min<uint64_t> (data, 0xFFFFFFFFu));
}

Writer::Writer (Ring& r, Status& s, Settings& set, FileSystem& f, std::string v) : ring (r), st (s), settings (set), fs (f), version (std::move (v)) {}

Writer::~Writer () { stop (); }

void Writer::start ()
{
    if (thread.joinable ())
        return;
    quit.store (false);
    thread = std::thread ([this] {
        while (!quit.load (std::memory_order_acquire))
        {
            if (pump () == 0)
            {
                idle ();
                std::this_thread::sleep_for (std::chrono::milliseconds (2));
            }
        }
    });
}

void Writer::stop ()
{
    if (thread.joinable ())
    {
        quit.store (true, std::memory_order_release);
        thread.join ();
    }
    pump ();
}

std::string Writer::folderNow ()
{
    std::string l, f;
    settings.get (l, f);
    return f.empty () ? defaultFolder () : f;
}

int Writer::pump ()
{
    int n = 0;
    Ring::Header h;
    while (ring.pop (h, payload))
    {
        handle (h.type, payload.data (), h.bytes);
        ++n;
    }
    return n;
}

void Writer::idle ()
{
    const int32_t state = st.state.load (std::memory_order_relaxed);
    const int64_t now = steadyMs ();
    if (state == kStateOff)
    {
        sessionMade = false;
        lastSpaceCheck = 0; // (checked at once when armed)
        return;
    }
    if (!sessionMade)
    {
        // the session is made when the first probe arms (every probe after that joins it)
        Session::global ().id (fs, folderNow (), unixNow ());
        sessionMade = true;
    }
    if (now - lastSpaceCheck >= 1000)
    {
        lastSpaceCheck = now;
        checkSpace ();
    }
    if (file >= 0 && now - lastTouch >= 10000)
    {
        lastTouch = now;
        Session::global ().touch (fs, folder, unixNow ());
    }
}

void Writer::checkSpace ()
{
    const int64_t free = fs.freeBytes (file >= 0 ? dir : folderNow ());
    st.freeBytes.store (free, std::memory_order_relaxed);
    if (file >= 0 && free >= 0 && free < kStopSpaceBytes)
        problem (kFaultDiskFull, kEndDiskFull);
}

void Writer::handle (uint32_t type, const uint8_t* p, uint32_t bytes)
{
    switch (type)
    {
        case kRecTakeStart:
            if (bytes >= sizeof (TakeStart))
            {
                TakeStart ts;
                std::memcpy (&ts, p, sizeof (ts));
                beginTake (ts);
            }
            break;
        case kRecAudio:
            if (file >= 0)
                audio (p, bytes);
            break;
        case kRecTimeMap:
            if (file >= 0 && bytes >= sizeof (TimePoint))
            {
                TimePoint tp;
                std::memcpy (&tp, p, sizeof (tp));
                points.push_back (tp);
            }
            break;
        case kRecMidi:
            if (file >= 0 && bytes >= sizeof (MidiRec))
            {
                MidiRec m;
                std::memcpy (&m, p, sizeof (m));
                midi.push_back (m);
            }
            break;
        case kRecTakeEnd:
        {
            TakeEnd te;
            if (bytes >= sizeof (TakeEnd))
                std::memcpy (&te, p, sizeof (te));
            if (file >= 0)
                endTake (te.reason);
            skipping = false;
            break;
        }
        default: break;
    }
}

void Writer::problem (int32_t fault, int32_t reason)
{
    if (file >= 0)
        endTake (reason);
    skipping = true;
    st.fault.store (fault, std::memory_order_release);
}

void Writer::beginTake (const TakeStart& ts)
{
    if (file >= 0)
        endTake (kEndDeactivated); // (a start without an end: cannot happen, but keep the files whole)
    skipping = false;
    start0 = ts;
    points.clear ();
    midi.clear ();
    framesWritten = sincePatch = sinceCheck = 0;
    std::string f;
    settings.get (label, f);
    folder = f.empty () ? defaultFolder () : f;
    const std::string sid = Session::global ().id (fs, folder, unixNow ());
    sessionMade = true;
    dir = joinPath (folder, sid);
    if (!fs.makeDirs (dir))
    {
        problem (kFaultWrite, kEndWriteError);
        return;
    }
    base = fileLabel (label);
    {
        std::lock_guard<std::mutex> g (namingLock ());
        take = nextTake (fs.list (dir), base);
        for (int tries = 0; tries < 1000 && file < 0; ++tries, ++take)
            if ((file = fs.create (joinPath (dir, base + "_" + takeText (take) + ".wav"), true)) >= 0)
                break;
    }
    if (file < 0)
    {
        problem (kFaultWrite, kEndWriteError);
        return;
    }
    uint8_t h[kWavHeaderBytes];
    wavHeader (h, (uint32_t)std::lround (ts.sampleRate), 0);
    if (!fs.write (file, h, sizeof (h)))
    {
        problem (kFaultDiskFull, kEndDiskFull);
        return;
    }
    if (patchEvery <= 0)
        patchEvery = (int64_t)std::max (1.0, ts.sampleRate);
    startedAt = unixNow ();
    st.take.store (take, std::memory_order_relaxed);
    settings.setWhere (dir, base + "_" + takeText (take) + ".wav");
    lastTouch = steadyMs ();
    checkSpace ();
}

void Writer::audio (const uint8_t* p, uint32_t bytes)
{
    uint32_t k = 0;
    if (bytes < 4)
        return;
    std::memcpy (&k, p, 4);
    if (bytes < 4 + (uint64_t)k * 8)
        return;
    if (kWavHeaderBytes + (uint64_t)(framesWritten + k) * 8 > 0xFFFFFFFFull)
    {
        problem (kFaultSizeLimit, kEndSizeLimit);
        return;
    }
    if (interleaved.size () < (size_t)k * 2)
        interleaved.resize ((size_t)k * 2);
    const uint8_t* l = p + 4;
    const uint8_t* r = l + (size_t)k * 4;
    for (uint32_t i = 0; i < k; ++i)
    {
        std::memcpy (&interleaved[2 * i], l + 4 * i, 4);
        std::memcpy (&interleaved[2 * i + 1], r + 4 * i, 4);
    }
    if (!fs.write (file, interleaved.data (), (size_t)k * 8))
    {
        problem (kFaultDiskFull, kEndDiskFull);
        return;
    }
    framesWritten += k;
    sincePatch += k;
    sinceCheck += k;
    if (sincePatch >= patchEvery)
    {
        sincePatch = 0;
        patchHeader (); // (a crash leaves a WAV that opens)
    }
    if (sinceCheck >= (int64_t)start0.sampleRate)
    {
        sinceCheck = 0;
        checkSpace ();
    }
}

bool Writer::patchHeader ()
{
    if (file < 0)
        return false;
    uint8_t h[kWavHeaderBytes];
    wavHeader (h, (uint32_t)std::lround (start0.sampleRate), (uint64_t)framesWritten);
    return fs.writeAt (file, 0, h, sizeof (h));
}

void Writer::endTake (int32_t reason)
{
    if (file < 0)
        return;
    patchHeader ();
    fs.close (file);
    file = -1;
    writeJson (reason);
    st.takesWritten.fetch_add (1, std::memory_order_relaxed);
    Session::global ().touch (fs, folder, unixNow ());
}

void Writer::writeJson (int32_t reason)
{
    const std::string name = base + "_" + takeText (take);
    const double sr = start0.sampleRate;
    const Transport& t = start0.t;
    std::stable_sort (midi.begin (), midi.end (), [] (const MidiRec& a, const MidiRec& b) { return a.sample < b.sample; });

    std::string o;
    o.reserve (4096 + points.size () * 64);
    o += "{\n";
    key (o, "probr");
    o += "1,\n";
    key (o, "version");
    str (o, version);
    o += ",\n";
    key (o, "label");
    str (o, label);
    o += ",\n";
    key (o, "session");
    str (o, Session::global ().current ());
    o += ",\n";
    key (o, "take");
    inum (o, take);
    o += ",\n";
    key (o, "wav");
    str (o, name + ".wav");
    o += ",\n";
    key (o, "midi");
    if (midi.empty ())
        o += "null";
    else
        str (o, name + ".midi.json");
    o += ",\n";
    key (o, "created");
    str (o, localIso (startedAt));
    o += ",\n";
    key (o, "sample_rate");
    num (o, sr);
    o += ",\n";
    key (o, "channels");
    o += "2,\n";
    key (o, "sample_format");
    o += "\"float32\",\n";
    key (o, "frames");
    inum (o, framesWritten);
    o += ",\n";
    key (o, "seconds");
    num (o, sr > 0 ? (double)framesWritten / sr : 0.0);
    o += ",\n";
    key (o, "mode");
    o += start0.mode == 1 ? "\"Always\",\n" : "\"While Playing\",\n";
    key (o, "ended");
    str (o, endReasonText (reason));
    o += ",\n";
    key (o, "complete");
    o += reason == kEndDisarmed || reason == kEndStopped || reason == kEndDeactivated ? "true,\n" : "false,\n";
    key (o, "host_transport");
    o += t.has (Transport::kValid) ? "true,\n" : "false,\n";
    key (o, "tempo");
    num (o, t.has (Transport::kTempoValid) ? t.tempo : NAN);
    o += ",\n";
    key (o, "time_signature");
    if (t.has (Transport::kSigValid))
        o += "[" + std::to_string (t.sigNum) + ", " + std::to_string (t.sigDen) + "],\n";
    else
        o += "null,\n";
    key (o, "start");
    o += "{\"ppq\": ";
    num (o, t.has (Transport::kPpqValid) ? t.ppq : NAN);
    o += ", \"bar_start\": ";
    num (o, t.has (Transport::kBarValid) ? t.barStart : NAN);
    o += ", \"project_sample\": ";
    if (t.has (Transport::kSamplesValid))
        inum (o, t.projectSample);
    else
        o += "null";
    o += ", \"playing\": ";
    o += t.playing () ? "true" : "false";
    o += "},\n";
    key (o, "midi_events");
    inum (o, (long long)midi.size ());
    o += ",\n";
    key (o, "time_map_rule");
    o += "\"an entry holds until the next: while playing, ppq(s) = ppq + (s - sample) / sample_rate * tempo / 60; stopped, ppq stays\",\n";
    key (o, "time_map_columns");
    o += "[\"sample\", \"ppq\", \"bar_start\", \"tempo\", \"playing\", \"ts_num\", \"ts_den\", \"project_sample\"],\n";
    key (o, "time_map");
    o += "[";
    for (size_t i = 0; i < points.size (); ++i)
    {
        const TimePoint& p = points[i];
        o += i ? ",\n    [" : "\n    [";
        inum (o, p.sample);
        o += ", ";
        num (o, p.t.has (Transport::kPpqValid) ? p.t.ppq : NAN);
        o += ", ";
        num (o, p.t.has (Transport::kBarValid) ? p.t.barStart : NAN);
        o += ", ";
        num (o, p.t.has (Transport::kTempoValid) ? p.t.tempo : NAN);
        o += p.t.playing () ? ", 1, " : ", 0, ";
        if (p.t.has (Transport::kSigValid))
            o += std::to_string (p.t.sigNum) + ", " + std::to_string (p.t.sigDen) + ", ";
        else
            o += "null, null, ";
        if (p.t.has (Transport::kSamplesValid))
            inum (o, p.t.projectSample);
        else
            o += "null";
        o += "]";
    }
    o += points.empty () ? "]\n" : "\n  ]\n";
    o += "}\n";
    fs.writeFile (joinPath (dir, name + ".json"), o);

    if (midi.empty ())
        return;
    std::string m;
    m.reserve (1024 + midi.size () * 96);
    m += "{\n";
    key (m, "probr_midi");
    m += "1,\n";
    key (m, "label");
    str (m, label);
    m += ",\n";
    key (m, "take");
    inum (m, take);
    m += ",\n";
    key (m, "wav");
    str (m, name + ".wav");
    m += ",\n";
    key (m, "sample_rate");
    num (m, sr);
    m += ",\n";
    key (m, "events");
    m += "[";
    for (size_t i = 0; i < midi.size (); ++i)
    {
        const MidiRec& e = midi[i];
        m += i ? ",\n    {\"sample\": " : "\n    {\"sample\": ";
        inum (m, e.sample);
        m += ", \"ppq\": ";
        num (m, e.ppq);
        m += ", \"channel\": ";
        inum (m, e.channel);
        if (e.kind == kMidiBend)
        {
            m += ", \"type\": \"pitch_bend\", \"value\": ";
            num (m, e.value);
        }
        else
        {
            m += e.kind == kMidiNoteOn ? ", \"type\": \"note_on\", \"note\": " : ", \"type\": \"note_off\", \"note\": ";
            inum (m, e.pitch);
            m += ", \"velocity\": ";
            num (m, e.value);
        }
        m += "}";
    }
    m += "\n  ]\n}\n";
    fs.writeFile (joinPath (dir, name + ".midi.json"), m);
}

} // namespace probr
