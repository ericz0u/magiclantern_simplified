#!/usr/bin/env python3
"""
Turn any image (ideally one with a transparent background) into a Magic Lantern
crop mark: a 720x480 8-bit BMP that ML draws on top of LiveView.

    python3 make_cropmark.py plush.webp PLUSH.BMP
    python3 make_cropmark.py plush.webp PLUSH.BMP --height 200 --no-dither

Then copy PLUSH.BMP to ML/CROPMKS/ on the SD card and pick it under
Overlay -> Crop marks.

How ML reads these files (see src/bmp.c and src/cropmarks.c):
  * The pixel values are indexes into the camera's fixed bitmap palette, so the
    image is snapped to the colors in ML_PALETTE below.
  * Index 0 is fully transparent.
  * Only indexes below 0x80 are usable: the crop mark cache uses bit 7 as a flag.
  * 0x03 and 0x14..0x25 are left out: they are transparent black/gray or get
    rewritten at runtime.
  * Uncompressed 8-bit files are fine; ML converts them to RLE8 when loading.
  * File names must end in .BMP and fit in 14 characters, and ML lists at most
    9 crop marks.
"""

import argparse

from PIL import Image

W, H = 720, 480

# RGB of the first 128 palette entries, taken from the BMP header of the crop
# marks that ship with ML (data/cropmks/PhiPhoto.bmp). It's the ML devs'
# approximation of what the DIGIC 4 LCD shows.
ML_PALETTE = [
    (254, 254, 255), (234, 235, 235), (0, 0, 0), (0, 0, 0), (163, 56, 0), (31, 187, 216),
    (0, 153, 0), (1, 172, 1), (234, 0, 1), (0, 66, 211), (185, 186, 139), (27, 34, 126),
    (200, 0, 0), (0, 0, 168), (201, 0, 154), (209, 191, 0), (232, 0, 231), (216, 94, 76),
    (0, 62, 75), (231, 109, 0), (232, 0, 232), (232, 0, 232), (232, 0, 232), (232, 0, 232),
    (232, 0, 232), (232, 0, 232), (232, 0, 232), (232, 0, 232), (232, 0, 232), (232, 0, 232),
    (232, 0, 232), (232, 0, 232), (232, 0, 232), (232, 0, 232), (232, 0, 232), (232, 0, 232),
    (232, 0, 232), (231, 0, 232), (9, 9, 9), (18, 18, 18), (27, 27, 27), (36, 36, 36),
    (41, 41, 41), (46, 46, 46), (50, 50, 50), (55, 55, 55), (59, 59, 59), (64, 64, 64),
    (69, 69, 69), (73, 73, 73), (82, 82, 82), (92, 92, 92), (101, 101, 101), (109, 110, 109),
    (117, 117, 117), (119, 119, 119), (124, 124, 124), (129, 129, 129), (133, 133, 133), (138, 138, 138),
    (142, 142, 142), (147, 147, 147), (152, 152, 152), (156, 156, 156), (161, 161, 161), (165, 165, 165),
    (170, 170, 170), (175, 175, 174), (179, 178, 178), (184, 184, 184), (188, 188, 188), (193, 192, 193),
    (198, 198, 198), (202, 202, 202), (207, 206, 207), (211, 210, 211), (216, 216, 216), (221, 221, 221),
    (225, 225, 224), (229, 230, 229), (220, 193, 195), (206, 148, 150), (193, 101, 102), (177, 53, 54),
    (162, 7, 10), (160, 0, 0), (156, 0, 1), (150, 0, 2), (140, 0, 1), (132, 0, 0),
    (125, 0, 1), (119, 0, 1), (106, 0, 0), (92, 0, 0), (76, 0, 2), (62, 0, 1),
    (49, 0, 1), (36, 0, 0), (22, 0, 2), (108, 35, 39), (101, 34, 34), (95, 31, 32),
    (89, 29, 29), (81, 26, 27), (72, 25, 25), (64, 20, 22), (55, 17, 18), (49, 17, 18),
    (42, 13, 15), (39, 12, 14), (233, 1, 1), (199, 0, 0), (192, 212, 227), (147, 186, 220),
    (102, 164, 212), (57, 139, 208), (16, 117, 202), (8, 111, 201), (6, 107, 189), (5, 102, 186),
    (6, 101, 181), (6, 94, 167), (3, 87, 155), (6, 81, 145), (3, 74, 132), (6, 66, 120),
    (4, 60, 107), (4, 52, 94),
]

TRANSPARENT = 0
SKIP = {0x00, 0x03} | set(range(0x14, 0x26))
USABLE = [i for i in range(len(ML_PALETTE)) if i not in SKIP]


def to_ml_indexes(img, dither):
    """Snap an RGB image to the usable ML colors; returns a list of ML indexes."""
    # Pillow quantizes against a 256-entry palette, so pad the usable colors
    # with copies of the first one; ties resolve to the earlier entry.
    compact = [ML_PALETTE[i] for i in USABLE]
    compact += [compact[0]] * (256 - len(compact))
    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette([c for rgb in compact for c in rgb])
    mode = Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE
    q = img.quantize(palette=pal_img, dither=mode)
    lut = [USABLE[i] if i < len(USABLE) else USABLE[0] for i in range(256)]
    return [lut[p] for p in q.getdata()]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output", help="e.g. PLUSH.BMP")
    ap.add_argument("--height", type=int, default=150, help="sticker height in screen pixels (150 ~ half an inch on a 550D)")
    ap.add_argument("--x", type=int, help="left edge (default: centered)")
    ap.add_argument("--y", type=int, help="top edge (default: centered)")
    ap.add_argument("--sticker", action="store_true",
                    help="output just the image (no 720x480 canvas), for the plush module; "
                         "use it with --height 288, the module's largest size")
    ap.add_argument("--no-dither", action="store_true", help="flat colors instead of dithering")
    ap.add_argument("--preview", help="also write a PNG of how it should look on the camera")
    args = ap.parse_args()

    src = Image.open(args.input).convert("RGBA")
    src = src.crop(src.getbbox())  # trim transparent margins
    h = args.height
    w = round(src.width * h / src.height)
    sticker = src.resize((w, h), Image.LANCZOS)

    if args.sticker:
        # Rows padded to 4 bytes, so BMP readers that ignore row padding still work.
        cw, ch = (w + 3) & ~3, h
        x0 = y0 = 0
    else:
        cw, ch = W, H
        x0 = (W - w) // 2 if args.x is None else args.x
        y0 = (H - h) // 2 if args.y is None else args.y

    indexes = to_ml_indexes(sticker.convert("RGB"), dither=not args.no_dither)
    alpha = sticker.getchannel("A").getdata()

    canvas = Image.new("P", (cw, ch), TRANSPARENT)
    canvas.putpalette([c for rgb in ML_PALETTE for c in rgb] + [0] * 3 * (256 - len(ML_PALETTE)))
    for i, (idx, a) in enumerate(zip(indexes, alpha)):
        if a >= 128:
            x, y = x0 + i % w, y0 + i // w
            if 0 <= x < cw and 0 <= y < ch:
                canvas.putpixel((x, y), idx)
    canvas.save(args.output, format="BMP")  # 8-bit, uncompressed, bottom-up
    print(f"wrote {args.output}: {w}x{h} sticker at ({x0}, {y0})")

    if args.preview:
        # Fake LiveView background so the transparent area is visible.
        bg = Image.linear_gradient("L").resize((cw, ch)).convert("RGB")
        mask = canvas.point(lambda p: 255 if p != TRANSPARENT else 0, "L")
        bg.paste(canvas.convert("RGB"), mask=mask)
        bg.save(args.preview)
        print(f"wrote {args.preview}")


if __name__ == "__main__":
    main()
