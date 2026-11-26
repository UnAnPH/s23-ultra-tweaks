#!/usr/bin/env python3
"""Make device/files/SecSettings-nh-<build>.patch for a firmware's SecSettings.apk (highlights).
SecNotificationIntelligenceController.getAvailabilityStatus() is compiled to return 3 when
com.android.settings:bool/config_na_enable is false, which hides the Notification highlights page on the
S23. The method starts with 'if (!getBoolean(config_na_enable)) { const/4 p0, 3; return p0; }', so changing
that literal to 0 makes the page available. Fixes the dex SHA-1 and adler32 and the zip CRCs too.
Usage: nhpatch.py SecSettings.apk <build> [out.patch]     (prints the stock and patched MD5)"""
import hashlib, struct, sys, zlib

def uleb(d, p):
    r = s = 0
    while True:
        b = d[p]; p += 1; r |= (b & 0x7f) << s; s += 7
        if b < 0x80: return r, p

def find_insns(d):
    """Offset inside the dex of the 'const/4 p0, 3' in getAvailabilityStatus(), or None if not in it."""
    u32 = lambda o: struct.unpack_from('<I', d, o)[0]
    nstr, strs, ntyp, typs = u32(56), u32(60), u32(64), u32(68)
    meths, ncls, clss = u32(92), u32(96), u32(100)
    def string(i):
        n, p = uleb(d, u32(strs + 4 * i)); return d[p:d.index(b'\0', p)].decode('utf-8', 'replace')
    for c in range(ncls):
        o = clss + 32 * c
        if not string(u32(typs + 4 * u32(o))).endswith('/SecNotificationIntelligenceController;'): continue
        p = u32(o + 24); assert p, 'class has no data'
        ns, p = uleb(d, p); ni, p = uleb(d, p); nd, p = uleb(d, p); nv, p = uleb(d, p)
        for _ in range(2 * (ns + ni)): _, p = uleb(d, p)
        for n in (nd, nv):
            m = 0
            for _ in range(n):
                diff, p = uleb(d, p); _, p = uleb(d, p); code, p = uleb(d, p); m += diff
                if string(u32(meths + 8 * m + 4)) == 'getAvailabilityStatus':
                    assert code, 'method has no code'; n = 2 * u32(code + 12)   # instruction bytes
                    for i in range(code + 16, code + 16 + n - 4):
                        if (d[i] == 0x12 and d[i + 1] >> 4 == 3 and d[i + 2] == 0x0f
                                and d[i + 3] == d[i + 1] & 0x0f):   # const/4 vA, #3; return vA
                            return i
                    raise SystemExit('getAvailabilityStatus has no "const/4 p0, 3; return p0"')
        raise SystemExit('SecNotificationIntelligenceController has no getAvailabilityStatus()')
    return None

apk = bytearray(open(sys.argv[1], 'rb').read()); build = sys.argv[2]
stock = hashlib.md5(apk).hexdigest(); orig = bytes(apk)
eocd = apk.rindex(b'PK\x05\x06'); ncd, _, cd = struct.unpack_from('<HII', apk, eocd + 10)
done = False
for _ in range(ncd):
    assert apk[cd:cd + 4] == b'PK\x01\x02'
    method, = struct.unpack_from('<H', apk, cd + 10)
    size, = struct.unpack_from('<I', apk, cd + 24)
    nl, el, cl = struct.unpack_from('<HHH', apk, cd + 28); lh, = struct.unpack_from('<I', apk, cd + 42)
    name = apk[cd + 46:cd + 46 + nl].decode()
    if name.endswith('.dex') and '/' not in name:
        assert method == 0, name + ' is compressed; this patch format needs stored dex files'
        lnl, lel = struct.unpack_from('<HH', apk, lh + 26); start = lh + 30 + lnl + lel
        dex = apk[start:start + size]; at = find_insns(dex)
        if at is not None:
            dex[at + 1] &= 0x0f                                     # const/4 p0, 3 -> const/4 p0, 0
            dex[12:32] = hashlib.sha1(dex[32:]).digest()
            dex[8:12] = struct.pack('<I', zlib.adler32(bytes(dex[12:])))
            apk[start:start + size] = dex
            crc = struct.pack('<I', zlib.crc32(bytes(dex)))
            apk[lh + 14:lh + 18] = crc; apk[cd + 16:cd + 20] = crc
            print('%s: const/4 p0, 3 at file offset %d' % (name, start + at)); done = True
    cd += 46 + nl + el + cl
assert done, 'SecNotificationIntelligenceController not found in any dex'
new = hashlib.md5(apk).hexdigest()
out = ['# Notification highlights: SecSettings.apk (%s, md5 %s) -> md5 %s' % (build, stock, new),
       "# SecNotificationIntelligenceController.getAvailabilityStatus() returns 3 while",
       # (const/4 p0, 3 -> 0) makes the page available. The other bytes are the dex's SHA-1 and adler32 and
       '# com.android.settings:bool/config_na_enable is false; changing that literal to 0 makes it available.',
       "# The other bytes are that dex's SHA-1 and adler32 and the zip CRCs. Made by tools/nhpatch.py.",
       '# Format: <zero-based file offset> <new byte in octal>']
out += ['%d %o' % (i, b) for i, (a, b) in enumerate(zip(orig, apk)) if a != b]
open(sys.argv[3] if len(sys.argv) > 3 else 'SecSettings-nh-%s.patch' % build, 'w').write('\n'.join(out) + '\n')
print('stock md5  ', stock); print('patched md5', new); print(len(out) - 5, 'bytes changed')
