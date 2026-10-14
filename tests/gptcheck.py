#!/usr/bin/env python3
"""Independent GPT validator: protective MBR, primary+backup header CRCs, entry-array CRC, partition bounds."""
import struct, sys, zlib, os
f = open(sys.argv[1], 'rb'); size = os.fstat(f.fileno()).st_size; S = 512
def rd(lba, n=1): f.seek(lba*S); return f.read(n*S)
mbr = rd(0); assert mbr[510:512] == b'\x55\xaa', 'mbr signature'; assert mbr[450] == 0xEE, 'protective MBR type'
def hdr(lba):
    h = rd(lba)[:92]; assert h[:8] == b'EFI PART', f'signature @{lba}'
    sz, = struct.unpack_from('<I', h, 12); crc, = struct.unpack_from('<I', h, 16)
    z = bytearray(h[:sz]); z[16:20] = b'\0\0\0\0'; assert zlib.crc32(z) == crc, f'header crc @{lba}'
    cur, bak, first, last = struct.unpack_from('<QQQQ', h, 24); ent, n, esz, acrc = struct.unpack_from('<QIII', h, 72)
    arr = rd(ent, (n*esz+S-1)//S)[:n*esz]; assert zlib.crc32(arr) == acrc, f'array crc @{lba}'
    return dict(cur=cur, bak=bak, first=first, last=last, arr=arr, n=n, esz=esz)
a = hdr(1); b = hdr(a['bak']); assert a['bak'] == size//S-1, 'backup at last LBA'; assert b['bak'] == 1 and a['arr'] == b['arr'], 'backup mismatch'
for i in range(a['n']):
    e = a['arr'][i*a['esz']:(i+1)*a['esz']]
    if e[:16] == bytes(16): continue
    s, l = struct.unpack_from('<QQ', e, 32); assert a['first'] <= s <= l <= a['last'], 'bounds'
    print(i+1, 'ok', s, l, (l-s+1)*S//2**20, 'MiB')
print('GPT OK')
