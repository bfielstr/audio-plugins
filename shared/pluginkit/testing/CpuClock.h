// The process's CPU time for the CPU-budget tests, as std::clock () gives it on macOS and Linux. On Windows
// std::clock () is the wall-clock time since the process started, so a test paused by anything else on a
// shared machine counted as the plug-in being slow; there the process's user and kernel time are read instead
// (CpuClock.cpp, in pluginkit_core, so <windows.h> stays out of the tests).
#pragma once

namespace pk::testing {

using CpuClock = long long;
long long cpuClocksPerSec ();
CpuClock cpuClock ();
inline const long long kCpuClocksPerSec = cpuClocksPerSec ();

} // namespace pk::testing
