// version.dll for SnowRunner's Bin folder: forwards every export to the real version.dll and loads the .asi files
// that sit next to the game's exe. The real DLL is version_chain.dll from the same folder when another mod used the
// name first, otherwise System32\version.dll; an export the chain file lacks comes from System32.
//
// A copy of this loader may itself be the chain file (a player updating the mod renames the old version.dll as the
// instructions for another mod's file say): under that name it forwards straight to System32 and loads no .asi files,
// so the chain ends there. Without that rule it would find itself as its own chain file and every call would jump to
// its own stub forever.
//
// The .asi files are loaded from a thread, so they load after the loader lock of this DLL's own load is released.
// Log: AsiLoader.log next to the exe, rewritten each start.
//
// The game's other programs in Bin (its crash reporter, converters and encoders) load this DLL as well. They get no
// .asi files, and their one line goes at the end of the game's log instead of replacing it, so the crash reporter's
// line there says the game crashed (a tester's log of 2026-10-22 was the crash reporter's, the mods loaded into it).
#include <windows.h>
#include <stdio.h>
#include <string>
#include "version_names.h"
#include "marker.h"

extern "C" void *g_verReal[64] = {};

static std::wstring g_dir;      // the folder of this DLL, with the last backslash; empty = not known, nothing is loaded from it
static std::wstring g_forwards; // for the log: where the calls go
static bool g_logFresh = true;  // the first line rewrites the log; false = every line is added at its end

static const wchar_t *const kHelpers[] = { L"crash_reporter.exe", L"SnowrunnerResourceConverter.exe", L"TextureConverter.exe", L"lame.exe",
                                           L"opusenc.exe",        L"xma2encode.exe",                  L"xwmaencode.exe" };

static void Log(const wchar_t *fmt, ...)
{
    FILE *f = nullptr;
    if (_wfopen_s(&f, (g_dir + L"AsiLoader.log").c_str(), g_logFresh ? L"w, ccs=UTF-8" : L"a, ccs=UTF-8") != 0 || !f) return;
    g_logFresh = false;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fputwc(L'\n', f);
    fclose(f);
}

// A module's full path, however long; empty when Windows does not give it.
static std::wstring ModulePath(HMODULE m)
{
    std::wstring p(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD n = GetModuleFileNameW(m, &p[0], (DWORD)p.size());
        if (!n) return std::wstring();
        if (n < p.size()) { p.resize(n); return p; }
        if (p.size() >= 32768) return std::wstring();
        p.resize(p.size() * 2);
    }
}

// Fills g_verReal. chained: this DLL is itself somebody's chain file, so it goes to System32 only.
static bool LoadRealVersion(HINSTANCE self, bool chained)
{
    wchar_t sys[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    const HMODULE system = LoadLibraryW((std::wstring(sys) + L"\\version.dll").c_str());
    if (!system) return false;
    HMODULE chain = nullptr;
    if (!chained && !g_dir.empty())
    {
        chain = LoadLibraryW((g_dir + L"version_chain.dll").c_str());
        if (chain == self || chain == system) chain = nullptr;
    }
    int fromChain = 0;
    for (int i = 0; i < kVerCount; i++)
    {
        void *p = chain ? (void *)GetProcAddress(chain, kVerNames[i]) : nullptr;
        if (p) fromChain++;
        else p = (void *)GetProcAddress(system, kVerNames[i]);
        if (!p) return false;
        g_verReal[i] = p;
    }
    wchar_t note[160];
    if (chain) swprintf_s(note, L"version_chain.dll next to the exe (%d of %d exports; the rest from System32)", fromChain, kVerCount);
    else swprintf_s(note, L"System32\\version.dll%s", chained ? L" (this file is a chain file itself)" : L"");
    g_forwards = note;
    return true;
}

static DWORD WINAPI LoadAsiFiles(void *)
{
    const std::wstring exe = ModulePath(nullptr);
    const size_t cut = exe.find_last_of(L"\\/");
    const std::wstring name = cut == std::wstring::npos ? exe : exe.substr(cut + 1);
    for (const wchar_t *helper : kHelpers)
        if (!_wcsicmp(name.c_str(), helper))
        {
            SYSTEMTIME t;
            GetLocalTime(&t);
            g_logFresh = false;
            Log(L"%02u:%02u:%02u %s started, one of the game's own programs: no .asi loaded into it", t.wHour, t.wMinute, t.wSecond, name.c_str());
            return 0;
        }
    Log(L"asi loader in %s", exe.c_str());
    Log(L"version.dll calls go to %s", g_forwards.c_str());
    WIN32_FIND_DATAW fd = {};
    const HANDLE h = FindFirstFileW((g_dir + L"*.asi").c_str(), &fd);
    int loaded = 0, already = 0, failed = 0;
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            // the pattern also finds names that only start their extension with .asi (x.asi_off, x.asi~: a mod switched
            // off by renaming), through their short names: only what ends in .asi is loaded
            const size_t len = wcslen(fd.cFileName);
            if (len < 5 || _wcsicmp(fd.cFileName + len - 4, L".asi")) { Log(L"left alone: %s (does not end in .asi)", fd.cFileName); continue; }
            const std::wstring file = g_dir + fd.cFileName;
            // another ASI loader in the same game (the chain file can be one) may have been first. Windows would hand
            // back the same module and not start it a second time; left alone, the log says who loaded what
            if (GetModuleHandleW(file.c_str())) { already++; Log(L"already loaded by another loader: %s", fd.cFileName); continue; }
            const HMODULE m = LoadLibraryW(file.c_str());
            if (m) { loaded++; Log(L"loaded %s at %p", fd.cFileName, (void *)m); }
            else { failed++; Log(L"FAILED %s, error %lu", fd.cFileName, GetLastError()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (already) Log(L"done: %d loaded, %d loaded by another loader, %d failed", loaded, already, failed);
    else Log(L"done: %d loaded, %d failed", loaded, failed);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    // without this DLL's own full path nothing is loaded by a name alone (that would search the current directory
    // and the path): the calls then go to System32 and no .asi is loaded
    const std::wstring path = ModulePath(inst);
    const size_t slash = path.find_last_of(L"\\/");
    bool chained = false;
    if (slash != std::wstring::npos)
    {
        g_dir = path.substr(0, slash + 1);
        chained = !_wcsicmp(path.c_str() + slash + 1, L"version_chain.dll");
    }
    if (!LoadRealVersion(inst, chained)) return FALSE;
    if (g_dir.empty() || chained) return TRUE;
    const HANDLE t = CreateThread(nullptr, 0, LoadAsiFiles, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    return TRUE;
}
