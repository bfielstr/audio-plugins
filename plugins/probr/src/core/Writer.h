// The writer thread's half of probr: takes the records the audio thread pushes (Probe.h) out of the ring
// and writes, per take, into <folder>/<session id>/:
//   <label>_<take>.wav        32-bit float stereo at the host's rate, the samples exactly as they passed
//   <label>_<take>.json       the host context and the time map (written when the take ends)
//   <label>_<take>.midi.json  the MIDI that came in (only when some did)
// A problem (the disk full or below kStopSpaceBytes free, the folder not writable, 4 GB reached) closes
// the take's files at once (the WAV holds what was written, the JSON says why it ended), and sets
// Status::fault, which ends the take in the audio thread.
#pragma once

#include "FileSystem.h"
#include "Records.h"
#include "Ring.h"
#include "Status.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace probr {

class Writer
{
public:
    // version: written into the JSON ("probr 0.27.1")
    Writer (Ring& ring, Status& status, Settings& settings, FileSystem& fs, std::string version = {});
    ~Writer ();

    // The thread: start () after the ring is allocated; stop () writes what is in the ring, then joins.
    void start ();
    void stop ();
    bool running () const { return thread.joinable (); }

    // One pass without the thread (the tests, and stop ()): every record in the ring. Returns how many.
    int pump ();
    // Free space where the takes go into Status::freeBytes; a take being written is stopped below
    // kStopSpaceBytes.
    void checkSpace ();
    // The thread's idle work: the session made at arming, the free space every second.
    void idle ();
    bool takeActive () const { return file >= 0; }

    // For the tests: write the WAV header's sizes every `frames` frames (default: a second of audio).
    void setPatchInterval (int64_t frames) { patchEvery = frames; }

private:
    void handle (uint32_t type, const uint8_t* p, uint32_t bytes);
    void beginTake (const TakeStart& ts);
    void audio (const uint8_t* p, uint32_t bytes);
    void endTake (int32_t reason);
    void problem (int32_t fault, int32_t reason);
    bool patchHeader ();
    void writeJson (int32_t reason);
    std::string folderNow ();

    Ring& ring;
    Status& st;
    Settings& settings;
    FileSystem& fs;
    std::string version;
    std::thread thread;
    std::atomic<bool> quit {false};
    std::vector<uint8_t> payload;
    std::vector<float> interleaved;

    // the take being written
    int file = -1;
    bool skipping = false; // a problem closed this take: its records are dropped until its end
    std::string dir, base, folder, label;
    int take = 0;
    TakeStart start0;
    int64_t framesWritten = 0, sincePatch = 0, sinceCheck = 0, patchEvery = 0;
    int64_t startedAt = 0;
    std::vector<TimePoint> points;
    std::vector<MidiRec> midi;
    bool sessionMade = false;
    int64_t lastSpaceCheck = 0, lastTouch = 0;
};

// The WAV header for 32-bit float stereo (WAVE_FORMAT_IEEE_FLOAT, with a fact chunk): kWavHeaderBytes.
constexpr size_t kWavHeaderBytes = 58;
void wavHeader (uint8_t* out, uint32_t sampleRate, uint64_t frames);

} // namespace probr
