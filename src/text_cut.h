// text_cut.h: a format into a fixed buffer that cuts a long text and says how much it wrote. The secure sprintf_s ends
// the game on a text too long for its buffer (the fast fail of 2026-11-06 21:41: a creep line of 167 characters in 160),
// and _snprintf_s with _TRUNCATE returns -1 when it cuts, which a line built piece by piece (used += ...) would take
// for a length. FmtCut returns the characters now in out, never more than size - 1 and never below 0.
#pragma once
#include <stdio.h>
#include <string.h>
#include <wchar.h>

template <typename... A> inline int FmtCut(char *out, size_t size, const char *fmt, A... args)
{
    if (!out || !size) return 0;
    const int n = _snprintf_s(out, size, _TRUNCATE, fmt, args...);
    return n >= 0 ? n : (int)strnlen(out, size);
}
template <typename... A> inline int FmtCut(wchar_t *out, size_t size, const wchar_t *fmt, A... args)
{
    if (!out || !size) return 0;
    const int n = _snwprintf_s(out, size, _TRUNCATE, fmt, args...);
    return n >= 0 ? n : (int)wcsnlen(out, size);
}
