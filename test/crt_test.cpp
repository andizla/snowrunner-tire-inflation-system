// crt_test.cpp: the crash line's C runtime handlers (src\crt_line.h). Run with no argument it starts itself once for
// each case and checks the exit code and the line in crt_test.log beside it:
//   own      _invalid_parameter_noinfo of this exe's own runtime, no handler before: the line, then 0xC0000409
//   game     the same through ucrtbase.dll, loaded as the game has it: the line names ucrtbase.dll, then 0xC0000409
//   before   a handler set before ours that returns: the line, then strcpy_s gives EINVAL and the program goes on;
//            five such calls give three lines, and an invalid parameter with no handler after them still gets its line
//   abort    abort(): the line, and the exit code abort gives without our handler (plain)
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
        if (mode == L"before") _set_invalid_parameter_handler(Before);
        if (mode != L"plain") crtline::Set(Out, true, nullptr, 0);
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
    if (g_failed) printf("CRT TEST: %d FAILED\n", g_failed);
    return g_failed ? 1 : 0;
}
