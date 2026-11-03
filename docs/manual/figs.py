# Inline SVG figures for the manuals: line charts and box diagrams, drawn from the models' own numbers.
from html import escape

PALETTE = ['#c6532a', '#3a7fb0', '#2b8a5a', '#8a5aa8', '#7a858f', '#d49a1f']
FONT = 'font-family="Segoe UI, Arial, sans-serif"'


def _ticks(lo, hi, step):
    v, out = lo, []
    while v <= hi + step * 1e-6:
        out.append(round(v, 6))
        v += step
    return out


def _fmt(v):
    return f'{v:g}'


def chart(series, xr, yr, xstep, ystep, xlabel, ylabel, cap='', w=640, h=250, marks=(), hlines=(), vlines=(), legend='tr',
          yfmt=_fmt, xfmt=_fmt):
    """series: (label, [(x, y), ...]) or (label, points, dict(color=, dash=, width=)). marks: (x, y, text).
    hlines/vlines: (value, text). legend: 'tr', 'tl', 'br', 'bl' or ''."""
    L, R, T, B = 52, 14, 12, 38
    pw, ph = w - L - R, h - T - B
    X = lambda v: L + (v - xr[0]) / (xr[1] - xr[0]) * pw
    Y = lambda v: T + ph - (v - yr[0]) / (yr[1] - yr[0]) * ph
    s = [f'<svg viewBox="0 0 {w} {h}" width="100%" xmlns="http://www.w3.org/2000/svg" {FONT} font-size="10">']
    for v in _ticks(yr[0], yr[1], ystep):
        s.append(f'<line x1="{L}" y1="{Y(v):.1f}" x2="{L + pw}" y2="{Y(v):.1f}" stroke="#e6e3de"/>')
        s.append(f'<text x="{L - 5}" y="{Y(v) + 3.5:.1f}" text-anchor="end" fill="#5a6570">{escape(yfmt(v))}</text>')
    for v in _ticks(xr[0], xr[1], xstep):
        s.append(f'<line x1="{X(v):.1f}" y1="{T}" x2="{X(v):.1f}" y2="{T + ph}" stroke="#f0eee9"/>')
        s.append(f'<text x="{X(v):.1f}" y="{T + ph + 13}" text-anchor="middle" fill="#5a6570">{escape(xfmt(v))}</text>')
    s.append(f'<rect x="{L}" y="{T}" width="{pw}" height="{ph}" fill="none" stroke="#9aa3ab"/>')
    for v, txt in hlines:
        s.append(f'<line x1="{L}" y1="{Y(v):.1f}" x2="{L + pw}" y2="{Y(v):.1f}" stroke="#5a6570" stroke-dasharray="3 3"/>')
        if txt:
            s.append(f'<text x="{L + pw - 4}" y="{Y(v) - 3:.1f}" text-anchor="end" fill="#5a6570" font-size="9">{escape(txt)}</text>')
    for v, txt in vlines:
        s.append(f'<line x1="{X(v):.1f}" y1="{T}" x2="{X(v):.1f}" y2="{T + ph}" stroke="#5a6570" stroke-dasharray="3 3"/>')
        if txt:
            s.append(f'<text x="{X(v) + 3:.1f}" y="{T + 10}" fill="#5a6570" font-size="9">{escape(txt)}</text>')
    for i, ser in enumerate(series):
        pts = ser[1]
        opt = ser[2] if len(ser) > 2 else {}
        col = opt.get('color', PALETTE[i % len(PALETTE)])
        dash = f' stroke-dasharray="{opt["dash"]}"' if opt.get('dash') else ''
        p = ' '.join(f'{X(a):.1f},{Y(min(max(b, yr[0]), yr[1])):.1f}' for a, b in pts)
        s.append(f'<polyline points="{p}" fill="none" stroke="{col}" stroke-width="{opt.get("width", 1.8)}"{dash}/>')
    for mx, my, txt in marks:
        s.append(f'<circle cx="{X(mx):.1f}" cy="{Y(my):.1f}" r="2.6" fill="#12171c"/>')
        if txt:
            s.append(f'<text x="{X(mx) + 5:.1f}" y="{Y(my) - 4:.1f}" fill="#12171c" font-size="9">{escape(txt)}</text>')
    rows = [(i, ser) for i, ser in enumerate(series) if ser[0]]
    if legend == 'out' and rows:
        # under the plot, in rows across its width
        lx, ly, line = L, h + 2, 0
        for i, ser in rows:
            opt = ser[2] if len(ser) > 2 else {}
            col = opt.get('color', PALETTE[i % len(PALETTE)])
            dash = f' stroke-dasharray="{opt["dash"]}"' if opt.get('dash') else ''
            iw = len(ser[0]) * 5.4 + 40
            if lx + iw > L + pw and lx > L:
                lx, line = L, line + 1
            yy = ly + 12 + 15 * line
            s.append(f'<line x1="{lx:.1f}" y1="{yy - 3.5}" x2="{lx + 17:.1f}" y2="{yy - 3.5}" stroke="{col}" stroke-width="2"{dash}/>')
            s.append(f'<text x="{lx + 22:.1f}" y="{yy}" fill="#1d2329">{escape(ser[0])}</text>')
            lx += iw
        s[0] = s[0].replace(f'viewBox="0 0 {w} {h}"', f'viewBox="0 0 {w} {h + 22 + 15 * line}"')
    elif legend and rows:
        lw = max(len(ser[0]) for _, ser in rows) * 5.4 + 32
        lx = L + pw - lw - 6 if legend[1] == 'r' else L + 6
        ly = T + 6 if legend[0] == 't' else T + (ph - 14 * len(rows)) / 2 if legend[0] == 'm' else T + ph - 10 - 14 * len(rows)
        s.append(f'<rect x="{lx:.1f}" y="{ly}" width="{lw:.1f}" height="{14 * len(rows) + 4}" fill="#fff" fill-opacity="0.92" stroke="#d8d4cd"/>')
        for j, (i, ser) in enumerate(rows):
            opt = ser[2] if len(ser) > 2 else {}
            col = opt.get('color', PALETTE[i % len(PALETTE)])
            dash = f' stroke-dasharray="{opt["dash"]}"' if opt.get('dash') else ''
            yy = ly + 13 + 14 * j
            s.append(f'<line x1="{lx + 5:.1f}" y1="{yy - 3.5}" x2="{lx + 22:.1f}" y2="{yy - 3.5}" stroke="{col}" stroke-width="2"{dash}/>')
            s.append(f'<text x="{lx + 27:.1f}" y="{yy}" fill="#1d2329">{escape(ser[0])}</text>')
    s.append(f'<text x="{L + pw / 2:.1f}" y="{h - 5}" text-anchor="middle" fill="#1d2329">{escape(xlabel)}</text>')
    s.append(f'<text transform="translate(13 {T + ph / 2:.1f}) rotate(-90)" text-anchor="middle" fill="#1d2329">{escape(ylabel)}</text>')
    s.append('</svg>')
    return '<figure>' + ''.join(s) + (f'<figcaption>{cap}</figcaption>' if cap else '') + '</figure>'


def diagram(boxes, arrows, w=640, h=260, cap=''):
    """boxes: id -> (x, y, bw, bh, [lines], kind), kind 'game', 'mod', 'mem', 'file' or 'other'; the first line is the title.
    arrows: (from_id, to_id, label, opts) with opts dx1/dy1/dx2/dy2 (shift the ends), lx/ly (label), both, dash."""
    fill = {'game': '#eef0f2', 'mod': '#fbe9e1', 'mem': '#e6f0f7', 'file': '#f3f1ee', 'other': '#eef6ef'}
    edge = {'game': '#7a858f', 'mod': '#c6532a', 'mem': '#3a7fb0', 'file': '#9a8f7f', 'other': '#2b8a5a'}
    s = [f'<svg viewBox="0 0 {w} {h}" width="100%" xmlns="http://www.w3.org/2000/svg" {FONT} font-size="10">',
         '<defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
         '<path d="M0,0 L10,5 L0,10 z" fill="#3d454d"/></marker></defs>']
    for x, y, bw, bh, lines, kind in boxes.values():
        s.append(f'<rect x="{x}" y="{y}" width="{bw}" height="{bh}" rx="4" fill="{fill[kind]}" stroke="{edge[kind]}" stroke-width="1.3"/>')
        n = len(lines)
        for j, t in enumerate(lines):
            st = ' font-weight="600"' if j == 0 else ' fill="#3d454d" font-size="9"'
            yy = y + bh / 2 + (j - (n - 1) / 2) * 12 + 3.5
            s.append(f'<text x="{x + bw / 2}" y="{yy:.1f}" text-anchor="middle"{st}>{escape(t)}</text>')

    def edge_point(b, tx, ty):
        x, y, bw, bh = b[:4]
        cx, cy = x + bw / 2, y + bh / 2
        dx, dy = tx - cx, ty - cy
        if dx == 0 and dy == 0:
            return cx, cy
        k = min((bw / 2) / abs(dx) if dx else 1e9, (bh / 2) / abs(dy) if dy else 1e9)
        return cx + dx * k, cy + dy * k

    for a in arrows:
        A, Bb, label = boxes[a[0]], boxes[a[1]], a[2]
        o = a[3] if len(a) > 3 else {}
        ac = (A[0] + A[2] / 2 + o.get('dx1', 0), A[1] + A[3] / 2 + o.get('dy1', 0))
        bc = (Bb[0] + Bb[2] / 2 + o.get('dx2', 0), Bb[1] + Bb[3] / 2 + o.get('dy2', 0))
        x1, y1 = edge_point((A[0] + o.get('dx1', 0), A[1] + o.get('dy1', 0), A[2], A[3]), *bc)
        x2, y2 = edge_point((Bb[0] + o.get('dx2', 0), Bb[1] + o.get('dy2', 0), Bb[2], Bb[3]), *ac)
        both = ' marker-start="url(#ah)"' if o.get('both') else ''
        dash = ' stroke-dasharray="4 3"' if o.get('dash') else ''
        s.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#3d454d" stroke-width="1.2" marker-end="url(#ah)"{both}{dash}/>')
        if label:
            mx, my = (x1 + x2) / 2 + o.get('lx', 0), (y1 + y2) / 2 + o.get('ly', -4)
            for j, t in enumerate(label.split('\n')):
                s.append(f'<text x="{mx:.1f}" y="{my + j * 11:.1f}" text-anchor="middle" font-size="9" fill="#12171c" '
                         f'paint-order="stroke" stroke="#fff" stroke-width="3">{escape(t)}</text>')
    s.append('</svg>')
    return '<figure>' + ''.join(s) + (f'<figcaption>{cap}</figcaption>' if cap else '') + '</figure>'
