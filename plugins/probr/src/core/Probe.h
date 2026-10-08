// The audio thread's half of probr: passes the audio through untouched and, while armed, hands every
// sample, the host's transport (a time map) and the MIDI it gets to the writer thread through a
// lock-free ring (Ring.h). It never blocks, allocates or touches a file: a record that does not fit
// (the writer fell behind) ends the take cleanly instead.
//
// Takes. While Playing: a take starts when the transport starts and ends when it stops, so each play
// start makes a new take. Always: one take from arming until Record goes Off. Either way a take also
// ends when the host deactivates the plug-in or the writer reports a problem (Status::fault); after a
// problem no new take starts until Record is switched Off and on again.
//
// The time map: an entry at the take's first sample, then every kTimeMapStep samples (inside a block,
// extrapolated from the block's start), and at every block start where the transport does not continue
// the way the last entry predicts (a jump or a loop, a tempo or time signature change, play or stop). An
// entry holds until the next one: ppq (sample) = ppq + (sample - entry) / sampleRate * tempo / 60 while
// it plays, constant while it does not.
#pragma once

#include "Records.h"
#include "Ring.h"
#include "Status.h"

#include <cstdint>

namespace probr {

class Probe
{
public:
    static constexpr int kTimeMapStep = 512;
    static constexpr size_t kKeepFree = 1024;    // ring bytes kept for a take's end
    static constexpr int kChunkFrames = 4096;    // an audio record's largest block

    explicit Probe (Status* status = nullptr) : st (status) {}
    void setStatus (Status* s) { st = s; }

    // Not on the audio thread, not while process () runs: the ring for `bufferSeconds` of stereo audio at
    // `sampleRate` (with room for the time map and MIDI). Ends nothing: call endTake first.
    void prepare (double sampleRate, int maxBlock, double bufferSeconds = 3.0);
    Ring& ring () { return rb; }
    double sampleRate () const { return sr; }

    // Parameters (the audio thread, or before processing starts).
    void setRecord (bool armedNow) { armed = armedNow; }
    void setMode (int m) { mode = m; }
    bool isArmed () const { return armed; }
    int modeNow () const { return mode; }

    // The audio thread: out = in, sample for sample (out may be in); while armed, records. midi: the
    // block's MIDI, MidiRec::sample the offset in the block, sorted or not.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Transport& t, const MidiRec* midi = nullptr,
                  int numMidi = 0);

    // The producer's side, from another thread while process () does not run (deactivation): ends the take
    // being recorded, if any.
    void endTake (int32_t reason);
    bool takeOpen () const { return open; }
    int64_t takeFrames () const { return frames; }

private:
    bool startTake (const Transport& t);
    void finishTake (int32_t reason); // pushes the end (always fits: kKeepFree)
    bool pushPoint (int64_t sample, const Transport& t);
    bool timeMap (const Transport& t, int n);
    bool pushAudio (const float* l, const float* r, int n);
    void fail (int32_t fault, int32_t reason);
    void publishState ();

    Status* st = nullptr;
    Ring rb;
    double sr = 48000.0;
    bool armed = false;
    int mode = 0;
    bool open = false;
    bool stopped = false; // a problem ended the last take: wait for Record Off
    bool wasArmed = false;
    int64_t frames = 0;   // samples in the take so far
    TimePoint last;       // the time map's last entry
    int32_t midiCount = 0;
};

// The ppq at `samples` after `p`, by the time map's rule (NaN without a ppq).
double ppqAfter (const TimePoint& p, int64_t samples, double sampleRate);

} // namespace probr
