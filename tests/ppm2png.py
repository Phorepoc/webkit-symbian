# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause

import sys, zlib, struct
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
parts = data.split(b'\n', 3)
w, h = map(int, parts[1].split())
px = parts[3]
raw = b''.join(b'\x00' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
open(dst, 'wb').write(png)
print(w, h, len(px))
