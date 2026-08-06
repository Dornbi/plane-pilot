from typing import Dict, Tuple

# C64 Palette mappings

# Color indices
BLACK: int = 0
WHITE: int = 1
RED: int = 2
CYAN: int = 3
PURPLE: int = 4
GREEN: int = 5
BLUE: int = 6
YELLOW: int = 7
ORANGE: int = 8
BROWN: int = 9
LIGHT_RED: int = 10
DARK_GRAY: int = 11
MEDIUM_GRAY: int = 12
LIGHT_GREEN: int = 13
LIGHT_BLUE: int = 14
LIGHT_GRAY: int = 15

# RGB values
PALETTE_RGB: Dict[int, Tuple[int, int, int]] = {
    0: (0, 0, 0),          # Black
    1: (255, 255, 255),    # White
    2: (104, 55, 43),      # Red
    3: (112, 164, 178),    # Cyan
    4: (111, 61, 134),     # Purple
    5: (88, 141, 67),      # Green
    6: (53, 40, 121),      # Blue
    7: (184, 199, 111),    # Yellow
    8: (111, 79, 37),      # Orange
    9: (67, 57, 0),        # Brown
    10: (154, 103, 89),    # Light Red
    11: (68, 68, 68),      # Dark Gray
    12: (108, 108, 108),   # Medium Gray
    13: (154, 210, 132),   # Light Green
    14: (108, 94, 181),    # Light Blue
    15: (149, 149, 149),   # Light Gray
}

RGB_TO_INDEX: Dict[Tuple[int, int, int], int] = {
    rgb: i for i, rgb in PALETTE_RGB.items()
}


def to_indexed(im):
    """Returns a copy of an RGB image as a 16-color indexed image.

    The palette is the C64 palette above, in hardware order, so a color's
    index in the file *is* its C64 color number. That keeps the PNGs honest
    -- an off-palette pixel is a hard error here rather than something the
    generators have to notice later -- and lets GIMP show the art in indexed
    mode with the same palette the C64 uses.
    """
    from PIL import Image

    im = im.convert("RGB")
    out = Image.new("P", im.size)
    flat = []
    for i in range(16):
        flat.extend(PALETTE_RGB[i])
    out.putpalette(flat)

    src = im.load()
    dst = out.load()
    width, height = im.size
    for y in range(height):
        for x in range(width):
            rgb = src[x, y]
            if rgb not in RGB_TO_INDEX:
                raise ValueError(
                    f"to_indexed: pixel at ({x}, {y}) is {rgb}, "
                    "which is not a C64 palette color"
                )
            dst[x, y] = RGB_TO_INDEX[rgb]
    return out
