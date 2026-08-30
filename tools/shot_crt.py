#!/usr/bin/env python3
"""Turn a raw VICE frame into the CRT-looking PNG that goes in screens/.

    tools/shot_crt.py raw.png screens/screen01_crt.png

VICE writes its screenshots from the native frame buffer, before the video
chain runs - so -VICIIfilter and -VICIIdscan change what is on the emulator
window and nothing at all in the file.  The tube therefore has to be built
here, out of the three things that actually make a C64 picture look like one
on a monitor rather than a diagram of one:

  * horizontal bleed, in *source* pixels, because PAL carries colour at a
    lower bandwidth than luma and neighbouring pixels run into each other;
  * scanlines, because a 200-line picture on a 15 kHz monitor is 200 lit lines
    with gaps between them, which is most of why C64 dithering reads as shading
    rather than as a checkerboard;
  * bloom, because a phosphor that bright spills into its neighbours, and it
    is what stops the scanlines from just looking like a dark grid.

The defaults below are the ones screens/ is generated with.  The flags exist
so a scene can be checked against a different tube without editing this file.
"""

import argparse

from PIL import Image, ImageChops, ImageFilter

# Where the 320x200 screen sits inside the 384x272 frame VICE writes, and how
# much border to keep around it.  Some border is not decoration: a C64 picture
# with its border cropped off reads as a screenshot of a document.
SCREEN_X, SCREEN_Y = 32, 35
SCREEN_W, SCREEN_H = 320, 200
BORDER_X, BORDER_Y = 12, 8


def crt(src, scale, bleed, focus, scanline, bloom):
    im = src.convert("RGB").crop(
        (
            SCREEN_X - BORDER_X,
            SCREEN_Y - BORDER_Y,
            SCREEN_X + SCREEN_W + BORDER_X,
            SCREEN_Y + SCREEN_H + BORDER_Y,
        )
    )

    # Bleed, in source pixels: one C64 pixel leaks into the two beside it.
    if bleed > 0:
        side = bleed
        im = im.filter(ImageFilter.Kernel((3, 3), [0, 0, 0, side, 1.0 - 2 * side, side, 0, 0, 0], scale=1.0))

    w, h = im.size
    im = im.resize((w * scale, h * scale), Image.NEAREST)

    # Focus: the horizontal softness of the beam itself, in output pixels. Only
    # horizontal - a CRT is sharp along the scan line's own axis and soft across
    # it, which is why the scanlines below stay crisp.
    if focus > 0:
        k = [1.0, 2.0, 3.0, 2.0, 1.0]
        k = [v * focus for v in k]
        k[2] += 1.0 - focus * sum([1.0, 2.0, 3.0, 2.0, 1.0])
        row = [0.0] * 5
        im = im.filter(ImageFilter.Kernel((5, 5), row + row + k + row + row, scale=1.0))

    # Scanlines: one weight per output row within a source line, dark at the
    # edges and full in the middle.
    if scanline > 0:
        weights = []
        for i in range(scale):
            # 0 at the line's centre, 1 at its edge.
            d = abs((i + 0.5) / scale * 2.0 - 1.0)
            weights.append(1.0 - scanline * d * d)
        column = Image.new("L", (1, scale))
        column.putdata([int(round(255 * v)) for v in weights])
        mask = Image.new("L", (1, im.size[1]))
        for y in range(0, im.size[1], scale):
            mask.paste(column, (0, y))
        mask = mask.resize(im.size, Image.NEAREST)
        im = ImageChops.multiply(im, Image.merge("RGB", (mask, mask, mask)))

    # Bloom: the bright parts of the picture, blurred and added back, which is
    # the glow around a lit phosphor and the reason a white letter on a CRT is
    # fatter than the pixels that drew it.
    if bloom > 0:
        bright = im.point(lambda v: max(0, v - 96) * 2)
        bright = bright.filter(ImageFilter.GaussianBlur(radius=scale * 0.9))
        bright = bright.point(lambda v: int(v * bloom))
        im = ImageChops.add(im, bright)

    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--scale", type=int, default=5)
    ap.add_argument("--bleed", type=float, default=0.12)
    ap.add_argument("--focus", type=float, default=0.06)
    ap.add_argument("--scanline", type=float, default=0.50)
    ap.add_argument("--bloom", type=float, default=0.42)
    a = ap.parse_args()
    crt(Image.open(a.src), a.scale, a.bleed, a.focus, a.scanline, a.bloom).save(a.dst)


if __name__ == "__main__":
    main()
