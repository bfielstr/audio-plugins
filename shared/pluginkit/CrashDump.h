// Crash dumps for the plug-ins (Windows). installCrashDump adds a handler that, when a crash happens
// inside this plug-in's code (the faulting instruction, or one of the calls that led to it, is in this
// module), writes a minidump to Documents/bfielstr/CrashDumps as "<plug-in> <date> <time>.dmp". It
// never handles the crash itself, so the host reacts as it always would. Crashes elsewhere in the host
// are ignored. At most one dump per plug-in per session. Does nothing on other systems.
#pragma once

#include <filesystem>
#include <string>

namespace pk {

// Safe to call any number of times; the handler goes away when the plug-in is unloaded.
// `folder` overrides the folder (for tests).
void installCrashDump (const std::filesystem::path& folder = {});

// The dump written this session, or "" (for tests).
std::string lastCrashDump ();

} // namespace pk
