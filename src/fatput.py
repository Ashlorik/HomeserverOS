#!/usr/bin/env python3
"""fatput.py IMAGE OFFSET_BYTES SRC DEST  -- copy a file into a FAT32 filesystem inside IMAGE (no mount needed).
DEST uses 8.3 upper-case names, e.g. EFI/BOOT/BOOTX64.EFI. Missing directories are created."""
import struct, sys, re, os
img, off, src, dest = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
f = open(img, 'r+b')
def rd(p, n): f.seek(off+p); return f.read(n)
def wr(p, b): f.seek(off+p); f.write(b)
bs = rd(0, 512)
bps, spc, rsv, nfat = struct.unpack_from('<HBHB', bs, 11); fatsz, = struct.unpack_from('<I', bs, 36); root, = struct.unpack_from('<I', bs, 44)
tot, = struct.unpack_from('<I', bs, 32); assert bs[82:87] == b'FAT32', 'not FAT32'
fat0 = rsv*bps; data = (rsv + nfat*fatsz)*bps; csz = spc*bps; ncl = (tot - rsv - nfat*fatsz)//spc
fat = bytearray(rd(fat0, fatsz*bps)); used0 = sum(1 for c in range(2, ncl+2) if struct.unpack_from('<I', fat, 4*c)[0] & 0x0FFFFFFF)
def get(c): return struct.unpack_from('<I', fat, 4*c)[0] & 0x0FFFFFFF
def put(c, v): struct.pack_into('<I', fat, 4*c, v)
def cpos(c): return data + (c-2)*csz
def chain(c):
    r = []
    while 2 <= c < 0x0FFFFFF8: r.append(c); c = get(c)
    return r
def alloc(n):
    out = []; c = 2
    while len(out) < n:
        assert c < ncl+2, 'FAT image full'
        if get(c) == 0: out.append(c)
        c += 1
    for a, b in zip(out, out[1:]): put(a, b)
    put(out[-1], 0x0FFFFFFF)
    return out
def n83(n):
    assert re.fullmatch(r'[A-Z0-9_]{1,8}(\.[A-Z0-9_]{1,3})?', n), 'use 8.3 upper-case names: '+n
    b, _, e = n.partition('.'); return (b.ljust(8)+e.ljust(3)).encode()
def entries(c):
    buf = b''.join(rd(cpos(x), csz) for x in chain(c)); return buf
def find(c, name):
    buf = entries(c)
    for i in range(0, len(buf), 32):
        if buf[i] == 0: break
        if buf[i] != 0xE5 and buf[i+11] != 0x0F and buf[i:i+11] == name:
            return (struct.unpack_from('<H', buf, i+20)[0] << 16) | struct.unpack_from('<H', buf, i+26)[0]
    return None
def add_entry(dc, name, attr, cl, size):
    ent = bytearray(32); ent[:11] = name; ent[11] = attr
    struct.pack_into('<H', ent, 20, cl >> 16); struct.pack_into('<H', ent, 26, cl & 0xFFFF); struct.pack_into('<I', ent, 28, size)
    struct.pack_into('<HH', ent, 16, 0x5A21, 0x5A21)  # fixed date 2025-01-01
    for k, x in enumerate(chain(dc)):
        blk = bytearray(rd(cpos(x), csz))
        for i in range(0, csz, 32):
            if blk[i] in (0, 0xE5): blk[i:i+32] = ent; wr(cpos(x), bytes(blk)); return
    new = alloc(1)[0]; put(chain(dc)[-1], new); put(new, 0x0FFFFFFF)
    wr(cpos(new), bytes(ent) + bytes(csz-32))
def write_data(cl, data_):
    for i, x in enumerate(cl): wr(cpos(x), data_[i*csz:(i+1)*csz].ljust(csz, b'\0'))
d = root; parts = dest.split('/')
for p in parts[:-1]:
    nm = n83(p); c = find(d, nm)
    if c is None:
        c = alloc(1)[0]; e = bytearray(csz)
        e[0:32] = n83('.'.ljust(1)) if False else (b'.          ' + bytes([0x10]) + bytes(20)); struct.pack_into('<H', e, 20, c >> 16); struct.pack_into('<H', e, 26, c & 0xFFFF)
        e[32:64] = b'..         ' + bytes([0x10]) + bytes(20); pc = 0 if d == root else d
        struct.pack_into('<H', e, 52, pc >> 16); struct.pack_into('<H', e, 58, pc & 0xFFFF)
        wr(cpos(c), bytes(e)); add_entry(d, nm, 0x10, c, 0)
    d = c
blob = open(src, 'rb').read(); nm = n83(parts[-1]); assert find(d, nm) is None, 'file exists'
cl = alloc(max(1, -(-len(blob)//csz))); write_data(cl, blob); add_entry(d, nm, 0x20, cl[0], len(blob))
for k in range(nfat): wr(fat0 + k*fatsz*bps, bytes(fat))
used1 = sum(1 for c in range(2, ncl+2) if get(c)); fi, = struct.unpack_from('<H', bs, 48)   # keep the FSInfo free-cluster hint truthful
if fi:
    blk = bytearray(rd(fi*bps, bps))
    if blk[0:4] == b'RRaA': struct.pack_into('<II', blk, 488, ncl - used1, 0xFFFFFFFF); wr(fi*bps, bytes(blk))
f.close()
