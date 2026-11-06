// crt_line.h: the C runtime's own ends named in the mod's log (the crash line, round 4). The same file is in Gauges
// and Tire Inflation.
//
// A fast fail from the C runtime goes past the vectored handler and the filter: the kernel ends the process with
// 0xC0000409 at once (the game's end at 19:39:07 on 2026-11-05, which the crash line did not name). Two of the
// runtime's ways there can be heard first: the invalid-parameter handler (a CRT function given a bad argument, which
// ends the program when no handler is set) and SIGABRT (abort). Both are set in the game's ucrtbase.dll and in the
// mod's own static runtime, and each writes one line: the runtime, the thread, the expression and function where the
// debug runtime names them, and the callers as return addresses.
//
// A /GS failure (a stack buffer overrun, __report_gsfailure) also ends with 0xC0000409, but on purpose without
// running any code of the process: it cannot be caught here. Only a dump names it.
//
// A handler set before ours decides: ours writes its line, calls it and returns as it does, so a game that goes on
// after EINVAL goes on (those lines are capped at kLines; the line before an end is always written). Only when none
// was set does ours end the program with
// __fastfail(FAST_FAIL_INVALID_ARG), which is what the runtime would have done. SIGABRT's handler returns: abort then
// ends the program its own way (a fast fail, or _exit(3)). It never calls abort itself.
//
// The handlers write with no heap and no lock: out is the mod's crash line writer.
#pragma once
#include <windows.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace crtline
{
typedef void(__cdecl *InvalidParameterFn)(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t);
typedef InvalidParameterFn(__cdecl *SetInvalidParameterFn)(InvalidParameterFn);
typedef void(__cdecl *SignalHandlerFn)(int);
typedef SignalHandlerFn(__cdecl *SignalFn)(int, SignalHandlerFn);

static const int kFrames = 16; // the runtime's own frames come first (_invalid_parameter, the function, its helpers)
static const long kLines = 3;  // lines passed on to a handler that returns: a game that goes on after EINVAL may come here often

enum { kGame, kOwn, kRuntimes };
struct Runtime
{
    const char *name;
    InvalidParameterFn before;   // the invalid-parameter handler set before ours, or none
    SignalHandlerFn abortBefore; // SIGABRT's handler before ours (SIG_DFL when none)
    bool set;
};
static Runtime g_rt[kRuntimes] = { { "ucrtbase.dll", nullptr, SIG_DFL, false }, { "the mod's own runtime", nullptr, SIG_DFL, false } };
static void (*g_out)(const char *) = nullptr;
static volatile long g_lines = 0;

// at as module + RVA ("SnowRunner.exe+a58220") into out, with no heap.
inline int Place(const void *at, char *out, size_t size)
{
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)at, &module) && module)
        GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _snprintf_s(out, size, _TRUNCATE, "%ls+%llx", name, module ? (unsigned long long)((const uint8_t *)at - (const uint8_t *)module) : (unsigned long long)at);
}

// One line: what came, from which runtime, what the debug runtime names, the callers, and what happens next. capped:
// the program may go on after it, so it counts against kLines; a line before an end is always written.
inline void Write(int k, const char *what, bool invalid, const wchar_t *expr, const wchar_t *fn, const wchar_t *file, unsigned line, const char *then, bool capped)
{
    if (!g_out || (capped && InterlockedIncrement(&g_lines) > kLines)) return;
    char text[1300];
    int used = _snprintf_s(text, sizeof text, _TRUNCATE, "crash: %s in %s, thread %lu; ", what, g_rt[k].name, GetCurrentThreadId());
    if (used < 0) used = (int)strlen(text);
    int n;
    if (expr && *expr)
        n = _snprintf_s(text + used, sizeof text - used, _TRUNCATE, "the expression \"%.160ls\" in %.80ls (%.120ls:%u); ", expr, fn ? fn : L"?", file ? file : L"?", line);
    else if (invalid)
        n = _snprintf_s(text + used, sizeof text - used, _TRUNCATE, "no expression (only the debug runtime names it); ");
    else
        n = 0;
    used = n > 0 ? used + n : (int)strlen(text);
    n = _snprintf_s(text + used, sizeof text - used, _TRUNCATE, "called from ");
    used = n > 0 ? used + n : (int)strlen(text);
    void *frames[kFrames];
    const int count = CaptureStackBackTrace(1, kFrames, frames, nullptr);
    for (int i = 0; i < count && used + 48 < (int)sizeof text; i++)
    {
        if (i) used += _snprintf_s(text + used, sizeof text - used, _TRUNCATE, " < ");
        n = Place(frames[i], text + used, sizeof text - used);
        if (n > 0) used += n;
    }
    _snprintf_s(text + used, sizeof text - used, _TRUNCATE, "; %s", then);
    g_out(text);
}

template <int K> void __cdecl InvalidParameter(const wchar_t *expr, const wchar_t *fn, const wchar_t *file, unsigned line, uintptr_t reserved)
{
    const InvalidParameterFn before = g_rt[K].before;
    Write(K, "an invalid parameter", true, expr, fn, file, line,
          before ? "passed on to the handler set before ours" : "the program ends with a fast fail (0xC0000409), as with no handler", before != nullptr);
    if (before)
    {
        before(expr, fn, file, line, reserved);
        return;
    }
    __fastfail(FAST_FAIL_INVALID_ARG);
}

template <int K> void __cdecl Abort(int sig)
{
    const SignalHandlerFn before = g_rt[K].abortBefore;
    const bool chained = before && before != SIG_DFL && before != SIG_IGN && before != SIG_ERR;
    Write(K, "abort (SIGABRT)", false, nullptr, nullptr, nullptr, 0,
          chained ? "passed on to the handler set before ours, then abort ends the program its own way" : "abort ends the program its own way", false);
    if (chained) before(sig);
}

// Sets the handlers: in the game's ucrtbase.dll when game is true (one mod owns those: Gauges, or Tire Inflation
// without Gauges), and always in the mod's own runtime. Once; said says what was set, for the log.
inline void Set(void (*out)(const char *), bool game, wchar_t *said, size_t size)
{
    g_out = out;
    wchar_t gameSaid[160] = L"";
    if (game && !g_rt[kGame].set)
    {
        const HMODULE u = GetModuleHandleW(L"ucrtbase.dll");
        const SetInvalidParameterFn setIp = u ? (SetInvalidParameterFn)(void *)GetProcAddress(u, "_set_invalid_parameter_handler") : nullptr;
        const SignalFn sig = u ? (SignalFn)(void *)GetProcAddress(u, "signal") : nullptr;
        if (setIp && sig)
        {
            g_rt[kGame].before = setIp(InvalidParameter<kGame>);
            const SignalHandlerFn was = sig(SIGABRT, Abort<kGame>);
            g_rt[kGame].abortBefore = was == SIG_ERR ? SIG_DFL : was;
            g_rt[kGame].set = true;
            _snwprintf_s(gameSaid, _TRUNCATE, L"ucrtbase.dll's%s", g_rt[kGame].before ? L" (the game's own handler stays, called after the line)" : L"");
        }
        else
            _snwprintf_s(gameSaid, _TRUNCATE, L"not ucrtbase.dll's (%s)", u ? L"its two functions were not found" : L"it is not loaded");
    }
    else if (!game)
        _snwprintf_s(gameSaid, _TRUNCATE, L"not ucrtbase.dll's (Gauges.asi has them)");
    if (!g_rt[kOwn].set)
    {
        g_rt[kOwn].before = _set_invalid_parameter_handler(InvalidParameter<kOwn>);
        const SignalHandlerFn was = signal(SIGABRT, Abort<kOwn>);
        g_rt[kOwn].abortBefore = was == SIG_ERR ? SIG_DFL : was;
        g_rt[kOwn].set = true;
    }
    if (said && size)
        _snwprintf_s(said, size, _TRUNCATE, L"the C runtime's invalid parameters and abort are named in this log: the mod's own runtime's handlers set, %s", gameSaid);
}
} // namespace crtline
