// crt_test.cpp: the crash line's C runtime handlers (src\crt_line.h). Run with no argument it starts itself once for
// each case and checks the exit code and the line in crt_test.log beside it:
//   own      _invalid_parameter_noinfo of this exe's own runtime, no handler before: the line, then 0xC0000409
//   game     the same through ucrtbase.dll, loaded as the game has it: the line names ucrtbase.dll, then 0xC0000409
//   before   a handler set before ours that returns: the line, then strcpy_s gives EINVAL and the program goes on;
//            five such calls give three lines, and an invalid parameter with no handler after them still gets its line
//   abort    abort(): the line, and the exit code abort gives without our handler (plain)
//   again    ours set, then a "game" handler set over ours in ucrtbase.dll, then the second set (Again): the line
//            comes and passes on to the game's, which returns, and the program goes on
//   still    the second set with ours still in place: the one before stays (none), so one line, then 0xC0000409
//   loop     a "game" handler that calls the one it replaced (ours) set over ours, then Again (review M1): one line,
//            no recursion, the start's handler takes the second entry, and the program goes on as the game's decides
//   loopnone the same with no handler at the start: one line, the second entry returns, the program goes on
//   loopabort the same for SIGABRT: one line, no recursion, and abort ends the program its own way
//   guard    in this process: the guard per thread says "again" only on the thread already inside (the filter's
//            guard in Gauges is the same crtline::Guard)
#include "crt_line.h"
#include <errno.h>
#include <string>

static wchar_t g_logPath[MAX_PATH];
static void Out(const char *text)
{
    const HANDLE f = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(f, text, (DWORD)strlen(text), &wrote, nullptr);
    WriteFile(f, "\r\n", 2, &wrote, nullptr);
    CloseHandle(f);
}
static void Note(const char *text) { Out(text); }

static volatile long g_beforeCalls = 0;
static void __cdecl Before(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t) { InterlockedIncrement(&g_beforeCalls); }

// A handler that passes on to the one it replaced, as a crash reporter may: set over ours, it calls ours again.
static crtline::InvalidParameterFn g_replaced = nullptr;
static crtline::SignalHandlerFn g_abortReplaced = nullptr;
static volatile long g_passerCalls = 0;
static void __cdecl Passer(const wchar_t *expr, const wchar_t *fn, const wchar_t *file, unsigned line, uintptr_t reserved)
{
    if (InterlockedIncrement(&g_passerCalls) > 20)
    {
        Out("test: recursion");
        ExitProcess(9);
    }
    if (g_replaced) g_replaced(expr, fn, file, line, reserved);
}
static void __cdecl AbortPasser(int sig)
{
    if (InterlockedIncrement(&g_passerCalls) > 20)
    {
        Out("test: recursion");
        ExitProcess(9);
    }
    if (g_abortReplaced) g_abortReplaced(sig);
}

static crtline::Inside g_testInside = {};
static volatile long g_otherAgain = -1;
static DWORD WINAPI OtherThread(void *)
{
    crtline::Guard guard(g_testInside);
    g_otherAgain = guard.again ? 1 : 0;
    return 0;
}

static std::string LogText()
{
    std::string s;
    FILE *f = nullptr;
    if (_wfopen_s(&f, g_logPath, L"rb") || !f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static DWORD Child(const wchar_t *self, const wchar_t *mode)
{
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" " + mode;
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 0xFFFFFFFF;
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0xFFFFFFFE;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static int g_failed = 0;
static void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wcscpy_s(g_logPath, self);
    wchar_t *slash = wcsrchr(g_logPath, L'\\');
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - g_logPath), L"crt_test.log");
    if (argc > 1)
    {
        // a child: no error box, nothing on the console from abort
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG);
        const std::wstring mode = argv[1];
        const HMODULE u = LoadLibraryW(L"ucrtbase.dll");
        typedef void(__cdecl * NoInfo)();
        const NoInfo noInfo = u ? (NoInfo)(void *)GetProcAddress(u, "_invalid_parameter_noinfo") : nullptr;
        const crtline::SetInvalidParameterFn ucrtSet = u ? (crtline::SetInvalidParameterFn)(void *)GetProcAddress(u, "_set_invalid_parameter_handler") : nullptr;
        if (mode == L"before") _set_invalid_parameter_handler(Before);
        if (mode == L"loop" && ucrtSet) ucrtSet(Before); // set before the mod's start: the start's handler
        if (mode != L"plain") crtline::Set(Out, true, nullptr, 0);
        if (mode == L"loop" || mode == L"loopnone")
        {
            if (ucrtSet) g_replaced = ucrtSet(Passer); // set after the mod's start, passing on to ours
            wchar_t said[400];
            crtline::Again(Out, true, said, sizeof said / sizeof said[0]);
            char saidLine[600];
            _snprintf_s(saidLine, _TRUNCATE, "test: %ls", said);
            Note(saidLine);
            if (noInfo) noInfo();
            const long wantBefore = mode == L"loop" ? 1 : 0;
            Note(g_passerCalls == 1 && g_beforeCalls == wantBefore ? "test: the game's handler was called once, and the program goes on" : "test: not as wanted");
            return 7;
        }
        if (mode == L"loopabort")
        {
            // ours in the mod's own runtime is what abort() here reaches: a handler over it that passes on to it
            g_abortReplaced = signal(SIGABRT, AbortPasser);
            signal(SIGABRT, crtline::Abort<crtline::kOwn>); // ours back in front, as the second set does
            crtline::g_rt[crtline::kOwn].abortBefore = AbortPasser;
            abort();
        }
        if (mode == L"own") _invalid_parameter_noinfo();
        else if (mode == L"game")
        {
            typedef void(__cdecl * NoInfo)();
            const NoInfo f = u ? (NoInfo)(void *)GetProcAddress(u, "_invalid_parameter_noinfo") : nullptr;
            if (f) f();
        }
        else if (mode == L"before")
        {
            char room[4];
            bool einval = true;
            for (int i = 0; i < 5; i++) einval = einval && strcpy_s(room, sizeof room, nullptr) == EINVAL;
            Note(einval && g_beforeCalls == 5 ? "test: EINVAL, the handler before was called, and the program goes on" : "test: not as wanted");
            crtline::g_rt[crtline::kOwn].before = nullptr; // as if none had been set: the next one ends the program
            _invalid_parameter_noinfo();
            Note("test: the program went on");
            return 7;
        }
        else if (mode == L"again" || mode == L"still")
        {
            const NoInfo f = noInfo;
            if (mode == L"again" && ucrtSet) ucrtSet(Before); // the game sets its own after the mod's start
            wchar_t said[400];
            crtline::Again(Out, true, said, sizeof said / sizeof said[0]);
            char line[600];
            _snprintf_s(line, _TRUNCATE, "test: %ls", said);
            Note(line);
            if (f) f();
            Note(g_beforeCalls == 1 ? "test: the game's handler was called, and the program goes on" : "test: not as wanted");
            return 7;
        }
        else if (mode == L"abort" || mode == L"plain") abort();
        Note("test: the program went on");
        return 7;
    }
    DeleteFileW(g_logPath);
    const DWORD own = Child(self, L"own");
    const std::string ownLog = LogText();
    Check(own == 0xC0000409 && ownLog.find("crash: an invalid parameter in the mod's own runtime, thread ") == 0 &&
              ownLog.find("no expression (only the debug runtime names it); called from crt_test.exe+") != std::string::npos &&
              ownLog.find("; the program ends with a fast fail (0xC0000409), as with no handler\r\n") != std::string::npos && ownLog.find("went on") == std::string::npos,
          "own runtime: _invalid_parameter_noinfo with no handler before gives the line, then ends with 0xC0000409");
    if (own != 0xC0000409) printf("     exit code %08lX\n%s", own, ownLog.c_str());
    DeleteFileW(g_logPath);
    const DWORD game = Child(self, L"game");
    const std::string gameLog = LogText();
    Check(game == 0xC0000409 && gameLog.find("crash: an invalid parameter in ucrtbase.dll, thread ") == 0 && gameLog.find("ucrtbase.dll+") != std::string::npos,
          "ucrtbase.dll: its _invalid_parameter_noinfo gives the line naming ucrtbase.dll, then ends with 0xC0000409");
    if (game != 0xC0000409) printf("     exit code %08lX\n%s", game, gameLog.c_str());
    DeleteFileW(g_logPath);
    const DWORD before = Child(self, L"before");
    const std::string beforeLog = LogText();
    size_t passed = 0;
    for (size_t at = beforeLog.find("; passed on to the handler set before ours\r\n"); at != std::string::npos; at = beforeLog.find("; passed on to the handler set before ours\r\n", at + 1)) passed++;
    Check(before == 0xC0000409 && beforeLog.find("crash: an invalid parameter in the mod's own runtime") == 0 && passed == 3 &&
              beforeLog.find("; passed on to the handler set before ours\r\ntest: EINVAL, the handler before was called, and the program goes on\r\ncrash: ") != std::string::npos &&
              beforeLog.find("; the program ends with a fast fail (0xC0000409), as with no handler\r\n") != std::string::npos && beforeLog.find("went on") == std::string::npos,
          "a handler set before ours: the line, then it is called, the call gives EINVAL and the program goes on; five such calls give three lines, and the fatal "
          "line after them is still written");
    if (before != 0xC0000409 || passed != 3) printf("     exit code %08lX\n%s", before, beforeLog.c_str());
    DeleteFileW(g_logPath);
    const DWORD plain = Child(self, L"plain");
    const bool plainSilent = LogText().empty();
    const DWORD aborted = Child(self, L"abort");
    const std::string abortLog = LogText();
    Check(plainSilent && aborted == plain && aborted != 7 && abortLog.find("crash: abort (SIGABRT) in the mod's own runtime, thread ") == 0 &&
              abortLog.find("; abort ends the program its own way\r\n") != std::string::npos,
          "abort: the line, and the exit code abort gives without our handler");
    printf("     abort's exit code %08lX (without our handler %08lX)\n", aborted, plain);
    DeleteFileW(g_logPath);
    auto count = [](const std::string &s, const char *what) {
        size_t n = 0;
        for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) n++;
        return n;
    };
    const DWORD again = Child(self, L"again");
    const std::string againLog = LogText();
    Check(again == 7 && againLog.find("had been replaced by the game's own, ours is back in front and passes on to it") != std::string::npos &&
              count(againLog, "crash: an invalid parameter in ucrtbase.dll, thread ") == 1 && againLog.find("; passed on to the handler set before ours\r\n") != std::string::npos &&
              againLog.find("test: the game's handler was called, and the program goes on") != std::string::npos,
          "the second set over a game handler: ours is back in front, the line comes once and passes on to the game's, and the program goes on");
    if (again != 7) printf("     exit code %08lX\n%s", again, againLog.c_str());
    DeleteFileW(g_logPath);
    const DWORD still = Child(self, L"still");
    const std::string stillLog = LogText();
    Check(still == 0xC0000409 && stillLog.find("the invalid-parameter handler set at the start was still ours; abort's was still ours") != std::string::npos &&
              count(stillLog, "crash: ") == 1 && stillLog.find("; the program ends with a fast fail (0xC0000409), as with no handler\r\n") != std::string::npos &&
              stillLog.find("went on") == std::string::npos,
          "the second set with ours still there: the one before stays (none), one line, then 0xC0000409");
    if (still != 0xC0000409) printf("     exit code %08lX\n%s", still, stillLog.c_str());
    DeleteFileW(g_logPath);
    const wchar_t *loops[2] = { L"loop", L"loopnone" };
    for (const wchar_t *mode : loops)
    {
        const DWORD looped = Child(self, mode);
        const std::string loopLog = LogText();
        const bool withStart = !wcscmp(mode, L"loop");
        Check(looped == 7 && count(loopLog, "crash: ") == 1 && loopLog.find("recursion") == std::string::npos &&
                  loopLog.find("had been replaced by the game's own, ours is back in front and passes on to it") != std::string::npos &&
                  loopLog.find("test: the game's handler was called once, and the program goes on") != std::string::npos,
              withStart ? "a game handler over ours that calls ours: one line, no recursion, the start's handler takes the second entry, the program goes on"
                        : "the same with no handler at the start: one line, no recursion, the second entry returns, the program goes on");
        if (looped != 7) printf("     exit code %08lX\n%s", looped, loopLog.c_str());
        DeleteFileW(g_logPath);
    }
    const DWORD loopAbort = Child(self, L"loopabort");
    const std::string loopAbortLog = LogText();
    Check(loopAbort == plain && count(loopAbortLog, "crash: ") == 1 && loopAbortLog.find("recursion") == std::string::npos &&
              loopAbortLog.find("; passed on to the handler set before ours, then abort ends the program its own way\r\n") != std::string::npos,
          "abort with a handler over ours that calls ours: one line, no recursion, and abort ends the program its own way");
    if (loopAbort != plain) printf("     exit code %08lX\n%s", loopAbort, loopAbortLog.c_str());
    DeleteFileW(g_logPath);
    {
        // the guard per thread: again only on the thread already inside; out again, the next entry is the first
        bool first = false, second = false, after = false;
        {
            crtline::Guard outer(g_testInside);
            first = !outer.again;
            {
                crtline::Guard inner(g_testInside);
                second = inner.again;
            }
            const HANDLE t = CreateThread(nullptr, 0, OtherThread, nullptr, 0, nullptr);
            if (t)
            {
                WaitForSingleObject(t, 10000);
                CloseHandle(t);
            }
        }
        {
            crtline::Guard later(g_testInside);
            after = !later.again;
        }
        Check(first && second && g_otherAgain == 0 && after, "the guard per thread: a second entry on the same thread is seen, another thread's is not, and leaving clears it");
    }
    if (g_failed) printf("CRT TEST: %d FAILED\n", g_failed);
    return g_failed ? 1 : 0;
}
