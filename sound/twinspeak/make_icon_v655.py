#!/usr/bin/env python3
"""Render the TwinSpeak icon and emit twinspeak.ico + preview PNGs.

Concept "ONE VOICE, TWIN SPEAKERS": a white waveform travels in from the
left edge of a dark rounded tile and hits a fork node; two IDENTICAL legs
(mirrored twins -- the split rig's two device legs) carry it to two speaker
drivers, one cyan (mains leg), one amber (voice leg). Flat vector look,
drawn at 1024 px and downscaled with LANCZOS to the ICO ladder
(256/128/64/48/32/24/16).
"""
import math
from PIL import Image, ImageDraw

S = 1024
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

TILE = (23, 30, 42, 255)        # dark navy tile
BORD = (60, 72, 94, 255)        # subtle tile border
WHITE = (243, 246, 250, 255)    # the single input stream
CYAN = (72, 214, 224, 255)      # leg 1 accent (mains)
AMBER = (255, 178, 84, 255)     # leg 2 accent (voice/center)
CAB = (228, 233, 241, 255)      # speaker cabinet

# ---- tile ------------------------------------------------------------------
d.rounded_rectangle([56, 56, 968, 968], radius=216, fill=TILE,
                    outline=BORD, width=10)

# ---- input waveform: white sine -> fork node --------------------------------
x0, x1 = 92, 392
pts = []
for x in range(x0, x1 + 1, 3):
    y = 512 + 84 * math.sin((x - x0) / 40.0)
    pts.append((x, y))
d.line(pts, fill=WHITE, width=36, joint='curve')
d.ellipse([x1 - 34, 512 - 34, x1 + 34, 512 + 34], fill=WHITE)   # fork node


# ---- twin legs: identical mirrored bezier curves -----------------------------
def bez(p0, p1, p2, n=72):
    out = []
    for i in range(n + 1):
        t = i / n
        xa = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
        ya = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
        out.append((xa, ya))
    return out


d.line(bez((400, 512), (520, 512), (648, 344)), fill=CYAN, width=30, joint='curve')
d.line(bez((400, 512), (520, 512), (648, 680)), fill=AMBER, width=30, joint='curve')


# ---- twin speakers -----------------------------------------------------------
def speaker(cx, cy, accent):
    w, h = 236, 288
    d.rounded_rectangle([cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2],
                        radius=40, fill=CAB)
    r = 86
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=accent, width=30)
    r2 = 24
    d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=TILE)


speaker(768, 344, CYAN)
speaker(768, 680, AMBER)

# ---- outputs ------------------------------------------------------------------
ico_path = '/home/z/my-project/work/ts652/twinspeak.ico'
img.save(ico_path, format='ICO',
         sizes=[(256, 256), (128, 128), (64, 64), (48, 48),
                (32, 32), (24, 24), (16, 16)])
img.resize((512, 512), Image.LANCZOS).save(
    '/home/z/my-project/download/twinspeak-icon-v6.5.5.png')
img.resize((256, 256), Image.LANCZOS).save(
    '/home/z/my-project/download/twinspeak-icon-v6.5.5-256.png')
print('wrote', ico_path, '+ preview PNGs (512, 256)')

# sanity: read back the ICO directory
import struct
ico = open(ico_path, 'rb').read()
cnt = ico[4]
print(f'ico entries: {cnt}')
for k in range(cnt):
    w, h, cc, _r, _p, bpp, sz = struct.unpack_from('<BBBBHHI', ico, 6 + 16 * k)
    print(f'  {256 if w == 0 else w}x{256 if h == 0 else h} {bpp}bpp {sz}B')
