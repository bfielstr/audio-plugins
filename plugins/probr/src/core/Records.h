// What the audio thread hands the writer thread through the ring (Ring.h): plain structs copied byte for
// byte, so both sides agree on them in one process.
#pragma once

#include <cstdint>

namespace probr {

enum RecordType : uint32_t
{
    kRecTakeStart = 1, // TakeStart
    kRecAudio,         // uint32 frames, then the left channel's floats, then the right's
    kRecTimeMap,       // TimePoint
    kRecMidi,          // MidiRec
    kRecTakeEnd,       // TakeEnd
};

// What the host said about its transport at a point (VST3's ProcessContext, framework-free).
struct Transport
{
    enum : uint32_t
    {
        kValid = 1,         // the host gave a context at all
        kPlaying = 2,       // the transport runs
        kPpqValid = 4,      // ppq (project position in quarter notes)
        kBarValid = 8,      // barStart
        kTempoValid = 16,   // tempo
        kSigValid = 32,     // time signature
        kSamplesValid = 64, // projectSample
    };
    uint32_t flags = 0;
    double ppq = 0.0;      // project position in quarter notes
    double barStart = 0.0; // the last bar's start, in quarter notes
    double tempo = 120.0;  // BPM
    int32_t sigNum = 4, sigDen = 4;
    int64_t projectSample = 0; // the host's project position in samples
    bool playing () const { return (flags & kPlaying) != 0; }
    bool has (uint32_t f) const { return (flags & f) == f; }
};

// A time map entry: at `sample` (counted from the take's first sample) the host's transport was this.
struct TimePoint
{
    int64_t sample = 0;
    Transport t;
};

struct TakeStart
{
    double sampleRate = 48000.0;
    int32_t mode = 0;
    int32_t reserved = 0;
    Transport t; // at the take's first sample
};

enum MidiKind : int32_t { kMidiNoteOn = 0, kMidiNoteOff, kMidiBend };

struct MidiRec
{
    int64_t sample = 0; // counted from the take's first sample
    double ppq = 0.0;   // project position (NaN when the host gives none)
    int32_t kind = 0;
    int32_t channel = 0;
    int32_t pitch = 0;  // notes
    float value = 0.0f; // note velocity 0 .. 1, pitch bend -1 .. 1
};

// Why a take ended.
enum EndReason : int32_t
{
    kEndDisarmed = 0,     // Record switched Off
    kEndStopped,          // the transport stopped (While Playing)
    kEndDeactivated,      // the host switched processing off (or the plug-in was removed)
    kEndOverrun,          // the writer fell behind: the ring was full
    kEndDiskFull,         // the disk filled up (or fell below the space probr keeps free)
    kEndWriteError,       // the folder could not be written
    kEndSizeLimit,        // the WAV reached 4 GB
    kEndSampleRate,       // the sample rate changed
};
const char* endReasonText (int32_t reason);

struct TakeEnd
{
    int32_t reason = 0;
    int32_t reserved = 0;
};

} // namespace probr
