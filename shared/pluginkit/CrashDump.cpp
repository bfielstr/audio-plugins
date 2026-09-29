#include "CrashDump.h"

#if defined(_WIN32)

#include "SampleFiles.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h needs windows.h first
#include <dbghelp.h>

namespace pk {
namespace {

struct Crash
{
    HMODULE module = nullptr;
    uintptr_t begin = 0, end = 0; // this module's code and data
    std::wstring name;            // the module's file name without extension, e.g. "Smemplr"
    std::filesystem::path folder;
    PVOID handler = nullptr;
    std::atomic<bool> written {false};
    std::string lastPath;
    std::mutex installing;

    ~Crash () // the module is being unloaded: the handler must not outlive its code
    {
        if (handler)
            RemoveVectoredExceptionHandler (handler);
    }
};

Crash& crash ()
{
    static Crash c;
    return c;
}

bool inModule (uintptr_t a) { return a >= crash ().begin && a < crash ().end; }

bool isFatal (DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case 0xC0000374: // heap corruption
            return true;
        default: return false; // C++ exceptions, breakpoints, and the like are not crashes
    }
}

// Whether the crash is ours: the faulting instruction or one of the last calls on the way to it is in
// this module (a crash inside memcpy called from our code counts).
bool causedHere (const EXCEPTION_POINTERS* ep)
{
    if (inModule ((uintptr_t)ep->ExceptionRecord->ExceptionAddress))
        return true;
#if defined(_M_X64)
    CONTEXT ctx = *ep->ContextRecord;
    for (int i = 0; i < 32 && ctx.Rip; ++i)
    {
        if (inModule ((uintptr_t)ctx.Rip))
            return true;
        DWORD64 imageBase = 0;
        auto* fn = RtlLookupFunctionEntry (ctx.Rip, &imageBase, nullptr);
        if (!fn)
        {
            // a leaf function: the return address is on top of the stack
            MEMORY_BASIC_INFORMATION mbi {};
            if (!VirtualQuery ((LPCVOID)ctx.Rsp, &mbi, sizeof (mbi)) || mbi.State != MEM_COMMIT)
                return false;
            ctx.Rip = *(const DWORD64*)ctx.Rsp;
            ctx.Rsp += 8;
            continue;
        }
        PVOID handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
    }
#endif
    return false;
}

struct DumpJob
{
    EXCEPTION_POINTERS* ep;
    DWORD threadId;
};

// Runs on its own thread: the crashed thread may have next to no stack left.
DWORD WINAPI writeDump (LPVOID arg)
{
    auto* job = static_cast<DumpJob*> (arg);
    auto& c = crash ();
    std::error_code ec;
    std::filesystem::create_directories (c.folder, ec);

    std::time_t t = std::time (nullptr);
    std::tm tm {};
    localtime_s (&tm, &t);
    wchar_t stamp[32];
    wcsftime (stamp, 32, L"%Y-%m-%d %H-%M-%S", &tm);
    const auto path = c.folder / (c.name + L" " + stamp + L".dmp");

    HANDLE file = CreateFileW (path.c_str (), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 1;
    MINIDUMP_EXCEPTION_INFORMATION info {job->threadId, job->ep, FALSE};
    const auto type = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs |
                                      MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    const BOOL ok = MiniDumpWriteDump (GetCurrentProcess (), GetCurrentProcessId (), file, type, &info, nullptr, nullptr);
    CloseHandle (file);
    if (ok)
        c.lastPath = filedetail::toUtf8 (path);
    else
        DeleteFileW (path.c_str ());
    return ok ? 0 : 1;
}

LONG CALLBACK onException (EXCEPTION_POINTERS* ep)
{
    auto& c = crash ();
    if (!isFatal (ep->ExceptionRecord->ExceptionCode) || c.written.load ())
        return EXCEPTION_CONTINUE_SEARCH;
    // a crash while looking at the crash must not come back here
    static thread_local bool busy = false;
    if (busy)
        return EXCEPTION_CONTINUE_SEARCH;
    busy = true;
    if (causedHere (ep) && !c.written.exchange (true))
    {
        DumpJob job {ep, GetCurrentThreadId ()};
        if (HANDLE th = CreateThread (nullptr, 0, writeDump, &job, 0, nullptr))
        {
            WaitForSingleObject (th, 60000);
            CloseHandle (th);
        }
    }
    busy = false;
    return EXCEPTION_CONTINUE_SEARCH; // the host handles the crash as usual
}

} // namespace

void installCrashDump (const std::filesystem::path& folder)
{
    auto& c = crash ();
    std::lock_guard<std::mutex> lock (c.installing);
    if (!folder.empty ())
        c.folder = folder;
    if (c.handler)
        return;
    if (!GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR> (&installCrashDump), &c.module))
        return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*> (c.module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*> (reinterpret_cast<const char*> (c.module) + dos->e_lfanew);
    c.begin = (uintptr_t)c.module;
    c.end = c.begin + nt->OptionalHeader.SizeOfImage;
    wchar_t file[MAX_PATH] = {};
    GetModuleFileNameW (c.module, file, MAX_PATH);
    c.name = std::filesystem::path (file).stem ().wstring ();
    if (c.folder.empty ())
        c.folder = samplesFolder ().parent_path () / "CrashDumps";
    c.handler = AddVectoredExceptionHandler (0, onException);
}

std::string lastCrashDump () { return crash ().lastPath; }

} // namespace pk

#else

namespace pk {
void installCrashDump (const std::filesystem::path&) {}
std::string lastCrashDump () { return {}; }
} // namespace pk

#endif
