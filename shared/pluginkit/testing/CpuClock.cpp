#include "CpuClock.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace pk::testing {

#if defined(_WIN32)
long long cpuClocksPerSec () { return 10000000; } // (FILETIME ticks: 100 ns)

CpuClock cpuClock ()
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes (GetCurrentProcess (), &created, &exited, &kernel, &user))
        return 0;
    auto ticks = [] (const FILETIME& f) { return ((long long)f.dwHighDateTime << 32) | (long long)f.dwLowDateTime; };
    return ticks (kernel) + ticks (user);
}
#else
long long cpuClocksPerSec () { return CLOCKS_PER_SEC; }
CpuClock cpuClock () { return (CpuClock)std::clock (); }
#endif

} // namespace pk::testing
