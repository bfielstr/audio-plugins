#pragma once

#include "Params.h"

namespace probr::help {

inline const char* forParam (uint32_t id)
{
    switch (id)
    {
        case kRecord:
            return "Arms the probe. Armed, it writes what passes through it (a WAV), the song position (a time map) and any MIDI it "
                   "gets into the session folder; the button turns red while a take is written. The sound passes untouched either "
                   "way. Not saved: a project always opens with the probe off.";
        case kMode:
            return "While Playing: records only while the song plays, a new take at every play start. Always: one take from arming "
                   "until Record goes off, playing or not.";
        default: return nullptr;
    }
}

constexpr const char* kLabel =
    "The name of this probe, used for its files (<label>_<take>.wav): say where it sits, such as \"after Trash MIDS\". Click to "
    "type, Return to keep. Saved with the project. Give every probe in a session its own label.";

constexpr const char* kFolder =
    "Where the sessions go: a folder per host session in it, named by the date and time the first probe armed. Choose... picks "
    "another folder, Default goes back to the default one. Everything stays on this computer: nothing is uploaded.";

constexpr const char* kTake =
    "The take being written (or the last one), how long it is, and how many takes this probe wrote. Each take is a WAV, a JSON "
    "with the tempo and the time map, and a MIDI JSON when MIDI came in.";

constexpr const char* kStatus =
    "What the probe is doing, the free disk space, and any problem: below 2 GB free a warning shows, below 256 MB the take "
    "stops; a full disk or a writer that cannot keep up also stops it cleanly (the sound never glitches). Switch Record off "
    "and on to record again.";

constexpr const char* kMeter =
    "The level passing through, left and right (peak, -60 to 0 dBFS). The probe does not change it.";

constexpr const char* kSession = "The session folder the takes are in now, and the last file written.";

} // namespace probr::help
