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
