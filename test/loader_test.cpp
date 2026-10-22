// Offline test of out\version.dll. Built three times:
//   loader_test.exe   imports version.dll (the proxy next to it), asks for kernel32.dll's file version through it,
//                     then waits for the marker the test .asi writes.
//   probe.asi         (LOADER_TEST_ASI) writes asi_marker.txt next to itself when loaded: the id of the process it
//                     was loaded into, so a marker left by another run does not count.
//   version_chain.dll (LOADER_TEST_CHAIN) a stand-in for another mod's version.dll that is an ASI loader itself: it
//                     loads probe.asi before the proxy looks for it, and it has none of version.dll's exports.
// build.bat also puts the three files into out\test\chain with a second copy of the proxy as version_chain.dll: run
// there, the same test checks that a chain of two of these loaders ends in System32 (it once ran in a circle). In
// out\test\twice the chain file is the stand-in: run there with the argument "already", the test also wants the
// proxy's log to say that it left the loaded probe.asi alone. In out\test\helper the test is named crash_reporter.exe,
// as the game's crash reporter that loads version.dll too: run there with the argument "helper", it wants the calls to
// go through, no .asi loaded, and the log test.bat put there (as the game's) kept, with one line added.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static std::wstring DirOf(HMODULE m)
{
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(m, p, MAX_PATH);
    std::wstring d = p;
    d.resize(d.find_last_of(L"\\/") + 1);
    return d;
}

#ifdef LOADER_TEST_ASI
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        FILE *f = nullptr;
        if (_wfopen_s(&f, (DirOf(inst) + L"asi_marker.txt").c_str(), L"w") == 0 && f) { fprintf(f, "%lu", GetCurrentProcessId()); fclose(f); }
    }
    return TRUE;
}
#elif defined(LOADER_TEST_CHAIN)
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) LoadLibraryW((DirOf(inst) + L"probe.asi").c_str());
    return TRUE;
}
#else
struct Version { bool wide, ansi; WORD major, minor, build; };

// The proxy's log of this very run (it is rewritten at each start; one left by an earlier run does not count), once
// its last line is in it. Empty when that does not happen in 5 s.
static std::string LoaderLog(const std::wstring &file)
{
    FILETIME made = {}, a, b, c;
    GetProcessTimes(GetCurrentProcess(), &made, &a, &b, &c);
    for (int i = 0; i < 50; i++, Sleep(100))
    {
        WIN32_FILE_ATTRIBUTE_DATA d = {};
        if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &d) || CompareFileTime(&d.ftLastWriteTime, &made) < 0) continue;
        std::string text;
        FILE *f = nullptr;
        if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || !f) continue;
        char part[512];
        for (size_t n; (n = fread(part, 1, sizeof part, f)) > 0;) text.append(part, n);
        fclose(f);
        if (text.find("done:") != std::string::npos) return text;
    }
    return std::string();
}

// The four exports the game itself imports, through the proxy.
static DWORD WINAPI AskVersion(void *out)
{
    Version &v = *(Version *)out;
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring k32 = std::wstring(sys) + L"\\kernel32.dll";
    DWORD dummy = 0;
    const DWORD size = GetFileVersionInfoSizeW(k32.c_str(), &dummy);
    std::string buf(size, 0);
    VS_FIXEDFILEINFO *ffi = nullptr, *ffiA = nullptr;
    UINT len = 0;
    v.wide = size && GetFileVersionInfoW(k32.c_str(), 0, size, &buf[0]) && VerQueryValueW(&buf[0], L"\\", (void **)&ffi, &len) && ffi &&
             ffi->dwSignature == VS_FFI_SIGNATURE;
    v.ansi = v.wide && VerQueryValueA(&buf[0], "\\", (void **)&ffiA, &len) && ffiA == ffi;
    if (ffi) { v.major = HIWORD(ffi->dwFileVersionMS); v.minor = LOWORD(ffi->dwFileVersionMS); v.build = HIWORD(ffi->dwFileVersionLS); }
    return 0;
}

int main(int argc, char **argv)
{
    int fails = 0;
    const std::wstring dir = DirOf(nullptr);

    wchar_t mod[MAX_PATH] = {};
    GetModuleFileNameW(GetModuleHandleW(L"version.dll"), mod, MAX_PATH);
    const bool ours = _wcsnicmp(mod, dir.c_str(), dir.size()) == 0;
    printf("%s version.dll in use: %ls\n", ours ? "ok  " : "FAIL", mod);
    fails += !ours;

    // on a thread with a time limit: a proxy that forwards to itself never comes back
    Version v = {};
    const HANDLE t = CreateThread(nullptr, 0, AskVersion, &v, 0, nullptr);
    const bool back = t && WaitForSingleObject(t, 10000) == WAIT_OBJECT_0;
    printf("%s forwarded calls%s: kernel32 version %u.%u.%u (wide %d, ansi %d)\n", back && v.wide && v.ansi ? "ok  " : "FAIL",
           back ? "" : " DID NOT RETURN in 10 s", v.major, v.minor, v.build, v.wide, v.ansi);
    if (!back) { printf("1 FAILED\n"); fflush(stdout); ExitProcess(1); }
    fails += !(v.wide && v.ansi);

    if (argc > 1 && !strcmp(argv[1], "helper"))
    {
        std::string log;
        for (int i = 0; i < 50 && log.find("crash_reporter.exe started") == std::string::npos; i++, Sleep(100))
        {
            log.clear();
            FILE *f = nullptr;
            if (_wfopen_s(&f, (dir + L"AsiLoader.log").c_str(), L"rb") != 0 || !f) continue;
            char part[512];
            for (size_t n; (n = fread(part, 1, sizeof part, f)) > 0;) log.append(part, n);
            fclose(f);
        }
        Sleep(500); // time for a probe.asi that should not come
        const bool none = GetModuleHandleW(L"probe.asi") == nullptr;
        printf("%s no .asi loaded into the game's crash reporter\n", none ? "ok  " : "FAIL");
        fails += !none;
        const bool kept = log.find("the game's log") != std::string::npos && log.find("crash_reporter.exe started, one of the game's own programs") != std::string::npos &&
                          log.find("asi loader in") == std::string::npos;
        printf("%s the game's log kept, with the crash reporter's line added\n", kept ? "ok  " : "FAIL");
        fails += !kept;
        printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
        return fails;
    }

    // the marker of this very process
    bool marker = false;
    for (int i = 0; i < 50 && !marker; i++)
    {
        FILE *f = nullptr;
        char text[32] = {};
        if (_wfopen_s(&f, (dir + L"asi_marker.txt").c_str(), L"r") == 0 && f)
        {
            fgets(text, sizeof text, f);
            fclose(f);
            marker = strtoul(text, nullptr, 10) == GetCurrentProcessId();
        }
        if (!marker) Sleep(100);
    }
    printf("%s probe.asi loaded into this process\n", marker ? "ok  " : "FAIL");
    fails += !marker;

    const bool mapped = GetModuleHandleW(L"probe.asi") != nullptr;
    printf("%s probe.asi mapped in the process\n", mapped ? "ok  " : "FAIL");
    fails += !mapped;

    const bool chain = GetModuleHandleW(L"version_chain.dll") != nullptr;
    printf("     chain file: %s\n", chain ? "version_chain.dll is loaded, the calls went through it" : "none");

    if (argc > 1 && !strcmp(argv[1], "already"))
    {
        // the chain file is the stand-in loader here and had probe.asi first
        const std::string log = LoaderLog(dir + L"AsiLoader.log");
        const bool left = chain && log.find("already loaded by another loader: probe.asi") != std::string::npos &&
                          log.find("loaded probe.asi at") == std::string::npos;
        printf("%s the proxy left probe.asi alone, which the other loader had loaded%s\n", left ? "ok  " : "FAIL",
               log.empty() ? " (no log of this run)" : "");
        fails += !left;
    }

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails;
}
#endif
