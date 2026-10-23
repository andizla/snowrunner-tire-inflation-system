# Makes the dxgi.dll the release brings: ReShade 6.8.0 with full add-on support (reshade.me) with its start banner left
# out. ReShade has no setting for the banner; one conditional jump decides whether its "Splash Window" is drawn, and here
# it jumps past it always (RVA 0xD14E4: je +0x9E7 becomes jmp to the same place). The file is checked before and after:
# any other build of ReShade is refused. ReShade is under the BSD 3-Clause License, which allows a changed build with its
# notice (THIRD_PARTY_NOTICES.md). Usage: python tools/reshade_nobanner.py <ReShade 6.8.0's dxgi.dll> <the new dxgi.dll>
import hashlib, struct, sys

SOURCE = '0CEE63F9C9F13F3AC909C5B4903F4DBB4B719A7AB3B4F13B0DEAF83C814B94F7'
RESULT = 'EA63183980A78D2C13A921F76E76A9B5CEA319E3E4457AC0B8D858E83AF35CAD'
RVA = 0xD14E4
OLD = bytes.fromhex('0f84e7090000')  # je  0xD1ED1, past the banner when it is not to be shown
NEW = bytes.fromhex('e9e809000090')  # jmp 0xD1ED1, nop

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
if hashlib.sha256(d).hexdigest().upper() != SOURCE: sys.exit('not ReShade 6.8.0 with full add-on support (SHA-256 %s...)' % SOURCE[:8])
e = struct.unpack_from('<I', d, 0x3c)[0]
nsec, optsz = struct.unpack_from('<H', d, e + 6)[0], struct.unpack_from('<H', d, e + 20)[0]
fo = None
for i in range(nsec):
    o = e + 24 + optsz + 40 * i
    vs, va, raw = struct.unpack_from('<III', d, o + 8)[0], struct.unpack_from('<I', d, o + 12)[0], struct.unpack_from('<I', d, o + 20)[0]
    if va <= RVA < va + vs: fo = RVA - va + raw
if fo is None or bytes(d[fo:fo + 6]) != OLD: sys.exit('the banner jump is not where it should be')
d[fo:fo + 6] = NEW
if hashlib.sha256(d).hexdigest().upper() != RESULT: sys.exit('the result is not the expected file')
open(dst, 'wb').write(d)
print('written', dst, 'SHA-256', RESULT)
