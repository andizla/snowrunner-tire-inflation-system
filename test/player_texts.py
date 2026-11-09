# The player texts carry no apostrophe (BRIEF_texts_no_apostrophes.md and the Reviewer's addendum): every string literal
# in the files that draw the tabs, the tips, the boxes, the panel and the cluster, outside comments and outside the
# calls that write the log, fails on an ASCII apostrophe or its look-alikes (U+2019, U+2018, U+02BC). Text the game
# hands over (truck, map and part names) is not a literal, so it is never seen here.
# Run: python -I test/player_texts.py <file> ...; it prints one line per apostrophe found and "ok" or "FAIL" last.
import sys

APOSTROPHES = ("'", "’", "‘", "ʼ")
# calls whose literals are the log's or the ini's, never drawn
LOG_CALLS = {"Log", "GaugeLog", "LogLine", "LogOnce", "NgLog", "NG_LOG", "OverlayLog", "TireLog", "PanelLog", "printf", "fprintf", "wprintf", "fwprintf",
             "OutputDebugStringA", "OutputDebugStringW", "WritePrivateProfileStringW", "WritePrivateProfileStringA",
             "GetPrivateProfileStringW", "GetPrivateProfileStringA", "GetPrivateProfileIntW", "GetPrivateProfileIntA", "Check"}
# literals that are never drawn though no log call holds them, by their first words: the ini guide NextGen.ini gets in
# its head, the byte pattern looked for in ReShade's file, and the route line the start's log gets (NgReShadeRoute)
NOT_DRAWN = ("; ", "crosire", "ReShade is installed: the overlay takes", "Sources\\\\Bin holds another program")


def literals(text):
    """Yield (line, literal text, wide, enclosing call) for each string literal outside comments."""
    i, n, line = 0, len(text), 1
    stack = []  # the names before each open parenthesis
    last_ident = ""
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
            continue
        if c == "'":  # a char literal
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == '"':
            wide = i > 0 and text[i - 1] in "Lu8" and (i < 2 or not (text[i - 2].isalnum() or text[i - 2] == "_"))
            raw = i > 0 and text[i - 1] == "R"
            if raw:
                k = text.find("(", i)
                delim = ")" + text[i + 1:k] + '"'
                j = text.find(delim, k)
                body = text[k + 1:j]
                end = j + len(delim)
            else:
                j = i + 1
                while j < n and text[j] != '"':
                    j += 2 if text[j] == "\\" else 1
                body = text[i + 1:j]
                end = j + 1
            yield line, body, wide, stack[-1] if stack else ""
            line += text.count("\n", i, end)
            i = end
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            last_ident = text[i:j]
            i = j
            continue
        if c == "(":
            stack.append(last_ident)
            last_ident = ""
        elif c == ")":
            if stack:
                stack.pop()
            last_ident = ""
        elif not c.isspace():
            last_ident = ""
        i += 1


def main(paths):
    found = 0
    for p in paths:
        text = open(p, encoding="utf-8", errors="replace").read()
        for line, body, wide, call in literals(text):
            if call in LOG_CALLS or body.startswith(NOT_DRAWN):
                continue
            if any(a in body for a in APOSTROPHES):
                found += 1
                print("%s:%d: %s" % (p, line, body[:160].encode("ascii", "backslashreplace").decode("ascii")))
    print(("FAIL player texts: %d with an apostrophe" % found) if found else "ok   player texts: no apostrophe in a string drawn for the player (%d files)" % len(paths))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
