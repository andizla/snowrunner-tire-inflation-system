"""pe_same.py: are two builds of a mod the same but for the time of their link?

    python -I tools\\pe_same.py <one.asi> <other.asi>

The linker writes the time of the link into a file it makes (build.bat has no /Brepro), so two builds of one commit
differ in a few bytes. This compares two such files with those fields left out: the file header's TimeDateStamp, the
export directory's, and each debug directory entry's with the bytes of its data (they hold a time or a value made of
it). It names every field left out and every run of bytes that still differs. Exit code 0: the same but for those
fields; 1: they differ elsewhere; 2: not two files of this kind. The standard library only.
"""
import struct
import sys


def fields(d):
    """The byte ranges that hold the link's time, as (start, end, name)."""
    if d[:2] != b"MZ":
        raise ValueError("no MZ header")
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        raise ValueError("no PE header")
    out = [(pe + 8, pe + 12, "the file header's time")]
    sections = struct.unpack_from("<H", d, pe + 6)[0]
    optional = pe + 24
    size = struct.unpack_from("<H", d, pe + 20)[0]
    magic = struct.unpack_from("<H", d, optional)[0]
    if magic != 0x20B:
        raise ValueError("not a 64 bit image")
    # the checksum is of the whole file, so it follows the time
    out.append((optional + 64, optional + 68, "the header's checksum"))
    directories = optional + 112
    table = optional + size
    secs = []
    for i in range(sections):
        s = table + 40 * i
        vsize, vaddr, rsize, raw = struct.unpack_from("<IIII", d, s + 8)
        secs.append((vaddr, max(vsize, rsize), raw))

    def at(rva):
        for vaddr, length, raw in secs:
            if vaddr <= rva < vaddr + length:
                return raw + rva - vaddr
        return None

    export_rva, export_size = struct.unpack_from("<II", d, directories)
    if export_rva:
        e = at(export_rva)
        if e is not None:
            out.append((e + 4, e + 8, "the export directory's time"))
    debug_rva, debug_size = struct.unpack_from("<II", d, directories + 8 * 6)
    if debug_rva:
        g = at(debug_rva)
        for i in range(debug_size // 28 if g is not None else 0):
            entry = g + 28 * i
            kind = struct.unpack_from("<I", d, entry + 12)[0]
            data_size, _, data_raw = struct.unpack_from("<III", d, entry + 16)
            out.append((entry + 4, entry + 8, "debug entry %d's time (type %d)" % (i, kind)))
            if data_raw and data_size:
                out.append((data_raw, data_raw + data_size, "debug entry %d's data (type %d, %d bytes)" % (i, kind, data_size)))
    return out


def compare(a, b):
    """(same, the fields left out that differ, the runs that differ elsewhere)."""
    if len(a) != len(b):
        return False, [], [("the sizes", len(a), len(b))]
    fa, fb = fields(a), fields(b)
    if [(s, e) for s, e, _ in fa] != [(s, e) for s, e, _ in fb]:
        return False, [], [("the time fields stand at other places", 0, 0)]
    skip = bytearray(len(a))
    left = []
    for s, e, name in fa:
        if a[s:e] != b[s:e]:
            left.append("%s at 0x%x, %d bytes" % (name, s, e - s))
        for i in range(s, e):
            skip[i] = 1
    runs = []
    i = 0
    n = len(a)
    while i < n:
        if a[i] != b[i] and not skip[i]:
            j = i
            while j < n and (a[j] != b[j] or (j + 1 < n and a[j + 1] != b[j + 1])) and not skip[j]:
                j += 1
            runs.append(("bytes", i, j - i))
            i = j
        else:
            i += 1
    return not runs, left, runs


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    try:
        a, b = open(argv[1], "rb").read(), open(argv[2], "rb").read()
        same, left, runs = compare(a, b)
    except (OSError, ValueError, struct.error) as e:
        print("pe_same: %s" % e)
        return 2
    for line in left:
        print("left out and differing: " + line)
    for what, at, length in runs[:20]:
        print("DIFFERS: %s at 0x%x, %d" % (what, at, length))
    if len(runs) > 20:
        print("DIFFERS: and %d more runs" % (len(runs) - 20))
    print("THE SAME but for the link's time" if same else "NOT THE SAME")
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
