"""chargewatts: patched APK = stock up to its APK Signing Block, then replaced/added STORED entries, then the stock signing
block, a new central directory and EOCD. Old entries stay as dead bytes, so every stock offset is unchanged.
usage: mkapk.py stock.apk out.apk tail.bin name=file [name=file ...]"""
import struct, sys, zlib
stock = open(sys.argv[1], 'rb').read()
e = stock.rfind(b'PK\x05\x06'); assert e > 0
ndisk, cdisk, nent_d, nent, cd_size, cd_off, clen = struct.unpack_from('<HHHHIIH', stock, e + 4)
assert stock[cd_off - 16:cd_off] == b'APK Sig Block 42', 'no APK signing block before the central directory'
blk = struct.unpack_from('<Q', stock, cd_off - 24)[0]; sig = cd_off - 8 - blk
assert struct.unpack_from('<Q', stock, sig)[0] == blk
recs = []; p = cd_off
for _ in range(nent):
    assert stock[p:p+4] == b'PK\x01\x02'
    nl, xl, cl = struct.unpack_from('<HHH', stock, p + 28)
    name = stock[p+46:p+46+nl].decode(); recs.append([name, bytearray(stock[p:p+46+nl+xl+cl])]); p += 46 + nl + xl + cl
repl = dict(a.split('=', 1) for a in sys.argv[4:])
out = bytearray(stock[:sig]); tail_start = sig
names = [r[0] for r in recs]
for name, path in repl.items():
    data = open(path, 'rb').read(); crc = zlib.crc32(data) & 0xffffffff; nb = name.encode()
    off = len(out); pad = (-(off + 30 + len(nb))) % 4
    out += struct.pack('<IHHHHHIIIHH', 0x04034b50, 10, 0, 0, 0, 0x21, crc, len(data), len(data), len(nb), pad) + nb + b'\0' * pad + data
    assert (off + 30 + len(nb) + pad) % 4 == 0
    rec = bytearray(struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 10, 10, 0, 0, 0, 0x21, crc, len(data), len(data), len(nb), 0, 0, 0, 0, 0, off) + nb)
    if name in names: recs[names.index(name)][1] = rec
    else: recs.append([name, rec]); names.append(name)
out += stock[sig:cd_off]
new_cd = len(out)
for _, r in recs: out += r
cdsz = len(out) - new_cd
out += struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(recs), len(recs), cdsz, new_cd, 0)
open(sys.argv[2], 'wb').write(out); open(sys.argv[3], 'wb').write(out[tail_start:])
print('stock kept up to', tail_start, '; tail', len(out) - tail_start, 'bytes; entries', len(recs))
