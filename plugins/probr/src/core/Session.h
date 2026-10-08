// Where the takes go: <folder>/<session id>/<label>_<take>.wav. Every probe in one host session writes
// into the same session folder: the session id (the local date and time, "2026-10-08_14-03-22") is made
// when the first probe arms and kept in a static the instances in one process share. Hosts that run
// plug-ins in processes of their own are coordinated by a small file, <folder>/.probr-session (the id and
// when a probe last used it): a probe in another process joins a session that was used in the last
// kJoinSeconds.
#pragma once

#include "FileSystem.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace probr {

// <Documents>/probr on macOS and Windows, ~/probr on Linux.
std::string defaultFolder ();
// A label as a file name: characters no file system takes become '_', no leading or trailing spaces
// or dots, at most 80 bytes (whole UTF-8 characters), "probr" when nothing is left.
std::string fileLabel (const std::string& label);
// The next free take number for `base` in a folder holding `names` (one more than the highest
// <base>_<n>.wav / .json / .midi.json there).
int nextTake (const std::vector<std::string>& names, const std::string& base);
// "001"
std::string takeText (int take);

class Session
{
public:
    static constexpr int64_t kJoinSeconds = 120;
    static Session& global ();

    // The session id for takes in `folder` (made now if the process has none yet). `now`: seconds since
    // 1970 (UTC). Writes the coordination file.
    std::string id (FileSystem& fs, const std::string& folder, int64_t now);
    // Tells other processes the session is still in use (the writer calls it while recording).
    void touch (FileSystem& fs, const std::string& folder, int64_t now);
    // The id of the session now ("" none yet).
    std::string current () const;
    // Forgets the session (tests; a new one is made at the next arming).
    void reset ();
    // The local date and time `now` as a session id.
    static std::string format (int64_t now);

private:
    mutable std::mutex m;
    std::string sid;
};

int64_t unixNow ();

} // namespace probr
