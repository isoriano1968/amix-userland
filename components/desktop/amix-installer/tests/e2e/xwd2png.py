import struct, sys
from PIL import Image
d = open(sys.argv[1], "rb").read()
h = struct.unpack(">25I", d[:100])
hs, w, ht, order, bpp, bpl, ncol = h[0], h[4], h[5], h[7], h[11], h[12], h[19]
off = hs + ncol * 12
raw = d[off:off + bpl * ht]
mode = "BGRX" if order == 0 else "XRGB"
Image.frombuffer("RGB", (w, ht), raw, "raw", mode, bpl, 1).save(sys.argv[2])
print(w, ht, bpp)
