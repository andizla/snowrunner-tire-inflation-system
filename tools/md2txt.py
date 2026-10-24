# md2txt.py: a README.md as the plain text README.txt of a release zip. Headings become upper-case lines, tables aligned
# columns, bullets and paragraphs are wrapped at 92 columns, inline code marks and links are dropped, pictures are left
# out, and the sections named on the command line are skipped (the build notes, which a player does not need).
#   python tools\md2txt.py README.md release\1.3.0\README.txt --title "Next Gen Tire Inflation 1.3.0" --skip Build "How it works"
import io, re, sys, textwrap

WIDTH = 92

def inline(s):
    s = re.sub(r'!\[[^\]]*\]\([^)]*\)', '', s)       # a picture
    s = re.sub(r'\[([^\]]+)\]\((https?://[^)]*)\)', r'\1 (\2)', s)   # a web link: its text and its address
    s = re.sub(r'\[([^\]]+)\]\([^)]*\)', r'\1', s)    # a link into the repository: its text
    s = re.sub(r'\*\*([^*]+)\*\*', r'\1', s)
    return s.replace('`', '')

def table(rows):
    cells = [[inline(c).strip() for c in r.strip().strip('|').split('|')] for r in rows]
    cells = [r for r in cells if not all(c == '' or re.fullmatch(r':?-+:?', c) for c in r)]
    n = max(len(r) for r in cells)
    cells = [r + [''] * (n - len(r)) for r in cells]
    w = [max(len(r[i]) for r in cells) for i in range(n)]
    lines = ['  ' + '  '.join(c.ljust(w[i]) for i, c in enumerate(r)).rstrip() for r in cells]
    if n < 2 or max(len(l) for l in lines) <= WIDTH: return lines
    # too wide for aligned columns: one entry a row, the other cells after it with the header's names
    head, out = cells[0], []
    for r in cells[1:]:
        rest = '; '.join(('%s: %s' % (head[i], r[i])) if head[i] else r[i] for i in range(1, n) if r[i])
        out.extend(textwrap.wrap(r[0] + (' (' + rest + ')' if rest else ''), WIDTH, initial_indent='  - ', subsequent_indent='    '))
    return out

def convert(md, skip, title):
    out = [title, '=' * len(title), ''] if title else []
    para, tbl, code, skipping = [], [], False, False

    def flush():
        nonlocal para, tbl
        if (para or tbl) and out and out[-1] != '': out.append('')   # a blank line after a list or a code block
        if para:
            out.extend(textwrap.wrap(inline(' '.join(para)), WIDTH) or [''])
            out.append('')
            para = []
        if tbl:
            out.extend(table(tbl))
            out.append('')
            tbl = []

    for line in md.split('\n'):
        if line.startswith('```'):
            flush()
            code = not code
            continue
        if code:
            if not skipping: out.append('    ' + line)
            continue
        m = re.match(r'^(#+)\s+(.*)', line)
        if m:
            flush()
            name = m.group(2).strip()
            skipping = name in skip
            if skipping: continue
            if m.group(1) == '#':
                if not title: out.extend([name, '=' * len(name), ''])
            else:
                out.extend(['', name.upper(), ''])
            continue
        if skipping: continue
        if line.startswith('|'):
            if para: flush()
            tbl.append(line)
            continue
        if tbl: flush()
        if not line.strip():
            flush()
            continue
        mm = re.match(r'^\s*([-*]|\d+\.)\s+(.*)', line)
        if mm:
            flush()
            bullet = '- ' if mm.group(1) in '-*' else mm.group(1) + ' '
            out.extend(textwrap.wrap(inline(mm.group(2)), WIDTH, initial_indent='  ' + bullet, subsequent_indent='  ' + ' ' * len(bullet)))
            continue
        para.append(line.strip())
    flush()
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(out)).strip() + '\n'

if __name__ == '__main__':
    a = sys.argv[1:]
    src, dst, skip, title = a[0], a[1], [], ''
    if '--skip' in a:
        k = a.index('--skip') + 1
        while k < len(a) and not a[k].startswith('--'):
            skip.append(a[k])
            k += 1
    if '--title' in a: title = a[a.index('--title') + 1]
    text = convert(io.open(src, encoding='utf-8').read(), skip, title)
    io.open(dst, 'w', encoding='utf-8', newline='\r\n').write(text)
    print('%s: %d lines, %d section(s) skipped' % (dst, text.count('\n'), len(skip)))
