// What the probe's three threads share: the Status (atomics: the audio thread and the writer publish,
// the editor reads) and the Settings (the label and the folder: strings behind a mutex, never touched
// by the audio thread).
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace probr {

enum ProbeState : int32_t
{
    kStateOff = 0,   // Record Off
    kStateArmed,     // armed, waiting for the transport (While Playing)
    kStateRecording, // a take is being written
    kStateStopped,   // armed, but the last take was stopped by a problem (fault says which): switch Record off and on
};

enum Fault : int32_t
{
    kFaultNone = 0,
    kFaultDiskFull,   // the disk is full or below the space probr keeps free
    kFaultOverrun,    // the writer could not keep up
    kFaultWrite,      // the folder could not be created or written
    kFaultSizeLimit,  // a take reached 4 GB
};

struct Status
{
    std::atomic<int32_t> state {kStateOff};
    std::atomic<int32_t> fault {kFaultNone}; // the writer or the audio thread sets it; disarming clears it
    std::atomic<int64_t> takeFrames {0};     // frames in the take being written (the last one once it ended)
    std::atomic<int32_t> take {0};           // the number of the take being written or written last (0: none yet)
    std::atomic<int32_t> takesWritten {0};   // takes this probe finished
    std::atomic<double> sampleRate {48000.0};
    std::atomic<int64_t> freeBytes {-1};     // free space where the takes go (-1: not known)
    std::atomic<bool> noTransport {false};   // While Playing, but the host gives no transport
    std::atomic<int32_t> midiEvents {0};     // MIDI events in the take
    std::atomic<float> peakL {0.0f}, peakR {0.0f}; // the largest sample since the editor last looked
};

// Free space below which a warning shows, and below which a take is stopped (so the files can still be
// closed and the time map written).
constexpr int64_t kLowSpaceBytes = 2LL << 30;
constexpr int64_t kStopSpaceBytes = 256LL << 20;

struct Settings
{
    // "" folder: the default one (defaultFolder ())
    void set (const std::string& l, const std::string& f)
    {
        std::lock_guard<std::mutex> g (m);
        label = l;
        folder = f;
    }
    void setLabel (const std::string& l)
    {
        std::lock_guard<std::mutex> g (m);
        label = l;
    }
    void setFolder (const std::string& f)
    {
        std::lock_guard<std::mutex> g (m);
        folder = f;
    }
    void get (std::string& l, std::string& f) const
    {
        std::lock_guard<std::mutex> g (m);
        l = label;
        f = folder;
    }
    // where the writer put the last take (for the editor): the session folder and the file's name
    void setWhere (const std::string& dir, const std::string& file)
    {
        std::lock_guard<std::mutex> g (m);
        sessionDir = dir;
        lastFile = file;
    }
    void where (std::string& dir, std::string& file) const
    {
        std::lock_guard<std::mutex> g (m);
        dir = sessionDir;
        file = lastFile;
    }

private:
    mutable std::mutex m;
    std::string label, folder, sessionDir, lastFile;
};

} // namespace probr
