from PIL import Image, ImageDraw, ImageFont
from typing import Optional, List, Tuple

from . import c64_colors

class C64Screen:
    WIDTH = 320
    HEIGHT = 200
    COLS = 40
    ROWS = 25
    CHAR_WIDTH = 8
    CHAR_HEIGHT = 8

    @staticmethod
    def render_mcbm(bg_color_index: int,
                    screen_ram: bytes,
                    color_ram: bytes,
                    bitmap: bytes,
                    debug: bool=False,
                    debug_crosses: Optional[List[Tuple[int, int]]]=None,
                    debug_boxes: Optional[List[Tuple[int, int, int, int]]]=None) -> Image.Image:
        """
        Render a C64 screen in Multicolor Bitmap Mode (MCBM).

        Args:
            bg_color_index (int): The color index for bits 00.
            screen_ram (bytes): 1000 bytes, contains color information (high 4 bits: 01, low 4 bits: 10).
            color_ram (bytes): 1000 bytes, contains color information (low 4 bits: 11).
            bitmap (bytes): 8000 bytes, the bitmap itself.
            debug (bool, optional): If True, renders 8x larger with grid overlay. Defaults to False.
            debug_crosses (Optional[List[Tuple[int, int]]], optional): Optional list of (x, y) coordinates to draw crosses. First is red, second is blue, third is yellow. Defaults to None.

        Returns:
            Image.Image: A PIL Image object.
        """
        width_px = C64Screen.COLS * 8
        height_px = C64Screen.ROWS * 8
        img = Image.new('RGB', (width_px, height_px))
        pixels = img.load()
        if pixels is None:
            raise ValueError("pixels is None")

        for char_y in range(C64Screen.ROWS):
            for char_x in range(C64Screen.COLS):
                char_idx = char_y * C64Screen.COLS + char_x
                
                # Fetch colors for this character block
                color_byte_sr = screen_ram[char_idx]
                col_01 = (color_byte_sr >> 4) & 0x0F
                col_10 = color_byte_sr & 0x0F
                col_11 = color_ram[char_idx] & 0x0F
                col_00 = bg_color_index

                # Process 8 rows of the character
                for row_in_char in range(8):
                    bitmap_byte = bitmap[char_idx * 8 + row_in_char]
                    
                    # Process 4 pairs of pixels (multicolor mode doubles width)
                    for pixel_pair in range(4):
                        # Extract 2 bits. 
                        # Order is usually 76, 54, 32, 10
                        shift = 6 - (pixel_pair * 2)
                        bits = (bitmap_byte >> shift) & 0x03
                        
                        color_idx = 0
                        if bits == 0:
                            color_idx = col_00
                        elif bits == 1:
                            color_idx = col_01
                        elif bits == 2:
                            color_idx = col_10
                        elif bits == 3:
                            color_idx = col_11
                        
                        rgb = c64_colors.PALETTE_RGB[color_idx]
                        
                        # Set pixels in the output image
                        # Multicolor pixels are 2 high-res pixels wide
                        x_base = char_x * 8 + pixel_pair * 2
                        y = char_y * 8 + row_in_char
                        
                        pixels[x_base, y] = rgb
                        pixels[x_base + 1, y] = rgb

        if debug:
            return C64Screen._apply_debug_overlay(img,
                                                  screen_ram,
                                                  debug_crosses=debug_crosses,
                                                  debug_boxes=debug_boxes)

        return img

    @staticmethod
    def render_mccm(global_colors: List[int],
                    screen_ram: bytes,
                    color_ram: bytes,
                    charset: bytes,
                    debug: bool=False,
                    debug_indices: Optional[List[int]]=None,
                    debug_crosses: Optional[List[Tuple[int, int]]]=None,
                    debug_boxes: Optional[List[Tuple[int, int, int, int]]]=None) -> Image.Image:
        """
        Render a C64 screen in Multicolor Character Mode (MCCM).

        Args:
            global_colors (List[int]): 3 integers for colors 00, 01, 10.
            screen_ram (bytes): C64Screen.COLS*C64Screen.ROWS bytes, char indices.
            color_ram (bytes): C64Screen.COLS*C64Screen.ROWS bytes, color 11 (only low 3 bits used -> 0-7).
            charset (bytes): 2048 bytes, 256 characters * 8 bytes.
            debug (bool, optional): If True, renders 8x larger with grid overlay. Defaults to False.
            debug_indices (Optional[bytes], optional): Optional indices to display in debug overlay. Defaults to None.
            debug_crosses (Optional[List[Tuple[int, int]]], optional): Optional list of (x, y) coordinates to draw crosses. First is red, second is blue, third is yellow. Defaults to None.

        Returns:
            Image.Image: A PIL Image object.
        """
        width_px = C64Screen.COLS * 8
        height_px = C64Screen.ROWS * 8
        img = Image.new('RGB', (width_px, height_px))
        pixels = img.load()
        if pixels is None:
            raise ValueError("pixels is None")

        col_00 = global_colors[0]
        col_01 = global_colors[1]
        col_10 = global_colors[2]

        for char_y in range(C64Screen.ROWS):
            for char_x in range(C64Screen.COLS):
                char_idx = char_y * C64Screen.COLS + char_x
                
                # Get character code and specific color
                char_code = screen_ram[char_idx]
                col_11 = color_ram[char_idx] & 0x07 # Only lower 3 bits in MCCM for color RAM
                
                if color_ram[char_idx] & 0x08: # If bit 3 is set, it behaves as standard text mode, but here we assume MCCM enforced?
                    # The prompt says: "Multicolor Character Mode (MCCM)" implies the global flag is set.
                    # Usually bit 3 of color ram determines if a specific character is multicolor or not.
                    # However, the prompt description says:
                    # "For each character, the colors are represented by 8 bytes... 00, 01, 10 pick one of the 3 global colors, while 11 gives the per-position custom one."
                    # This implies ALL characters are treated as multicolor for the purpose of this visualizer stage,
                    # or at least we are simulating the mode where d800 bit 3 is set.
                    # Wait, usually for MCCM:
                    # $d016 bit 4 is set (Multicolor Mode)
                    # Color RAM (d800) bit 3:
                    #   0: Standard high-res character (foreground color = low 3 bits, background = $d021)
                    #   1: Multicolor character
                    
                    # The prompt description:
                    # "For each character position, the colors are represented by 8 bytes... 00, 01, 10 pick one of the 3 global colors, while 11 gives the per-position custom one."
                    # This suggests we are operating purely in the Multicolor context for the simulation.
                    # I will assume all characters are multicolor.
                    pass

                # Process 8 rows of the character definition
                for row_in_char in range(8):
                    # Look up the byte in the charset
                    # 256 chars, 8 bytes each
                    charset_byte = charset[char_code * 8 + row_in_char]
                    
                    for pixel_pair in range(4):
                        shift = 6 - (pixel_pair * 2)
                        bits = (charset_byte >> shift) & 0x03
                        
                        color_idx = 0
                        if bits == 0:
                            color_idx = col_00
                        elif bits == 1:
                            color_idx = col_01
                        elif bits == 2:
                            color_idx = col_10
                        elif bits == 3:
                            color_idx = col_11
                            
                        rgb = c64_colors.PALETTE_RGB[color_idx]
                        
                        x_base = char_x * 8 + pixel_pair * 2
                        y = char_y * 8 + row_in_char
                        
                        pixels[x_base, y] = rgb
                        pixels[x_base + 1, y] = rgb

        if debug:
            return C64Screen._apply_debug_overlay(img, screen_ram, debug_indices, debug_crosses, debug_boxes)

        return img

    @staticmethod
    def _apply_debug_overlay(img: Image.Image, 
                             screen_ram: Optional[bytes]=None, 
                             debug_indices: Optional[List[int]]=None, 
                             debug_crosses: Optional[List[Tuple[int, int]]]=None,
                             debug_boxes: Optional[List[Tuple[int, int, int, int]]]=None) -> Image.Image:
        """
        Applies 8x upscale, grid, and optional character index overlay.

        Args:
            img (Image.Image): The base image.
            screen_ram (Optional[bytes], optional): Screen RAM for indices if debug_indices is None. Defaults to None.
            debug_indices (Optional[bytes], optional): Explicit indices to display. Defaults to None.
            debug_crosses (Optional[List[Tuple[int, int]]], optional): Optional list of (x, y) coordinates to draw crosses. First is red, second is blue, third is yellow. Defaults to None.
            debug_boxes (Optional[List[Tuple[int, int, int, int]]], optional): Optional list of (x, y, w, h) character rectangles to draw. Defaults to None.

        Returns:
            Image.Image: The processed image with overlay.
        """
        # Upscale 8x
        new_width = img.width * 8
        new_height = img.height * 8
        # Use Image.Resampling.NEAREST for Pillow >= 10, fallback for older versions
        resample = Image.Resampling.NEAREST
        img = img.resize((new_width, new_height), resample)
        draw = ImageDraw.Draw(img)
        
        # Grid (red, 1px)
        # Vertical lines every 64px (8 * 8)
        for x in range(0, new_width, 64):
            draw.line([(x, 0), (x, new_height)], fill=(255, 0, 0), width=1)
        # Horizontal lines every 64px
        for y in range(0, new_height, 64):
            draw.line([(0, y), (new_width, y)], fill=(255, 0, 0), width=1)

        # Character indices if screen_ram provided (MCCM mode)
        source_indices = debug_indices if debug_indices is not None else screen_ram
        
        if source_indices:
            try:
                # Try to use a default font
                font = ImageFont.load_default()
            except IOError:
                font = None # Should generally exist

            for char_y in range(C64Screen.ROWS):
                for char_x in range(C64Screen.COLS):
                    char_idx = char_y * C64Screen.COLS + char_x
                    if char_idx < len(source_indices):
                        char_code = source_indices[char_idx]
                        
                        x = char_x * 64
                        y = char_y * 64
                        
                        if font:
                            # Draw text in white for visibility?
                            draw.text((x + 2, y + 2), str(char_code), fill=(255, 255, 255), font=font)
        
        # Draw boxes if requested
        if debug_boxes:
            for bx, by, bw, bh in debug_boxes:
                # bx, by, bw, bh are in characters
                left = bx * 64
                top = by * 64
                right = (bx + bw) * 64
                bottom = (by + bh) * 64
                
                # Draw rectangle (yellow-ish, semi-transparent if possible? PIL doesn't do it easily with Draw.line)
                # Let's use a cyan/green color for boxes to distinguish from crosses
                box_color = (0, 255, 255) # Cyan
                draw.rectangle([left, top, right, bottom], outline=box_color, width=2)

        # Draw crosses if requested
        if debug_crosses:
            # Colors: 0=Red, 1=Blue, 2=Yellow, 3+=Red
            cross_colors = [
                (255, 0, 0),   # Red
                (0, 0, 255),   # Blue
                (255, 255, 0), # Yellow
            ]
            
            scale = 8
            for i, cross in enumerate(debug_crosses):
                if cross is None:
                    continue
                cx, cy = cross
                color = cross_colors[i] if i < len(cross_colors) else (255, 0, 0)
                cross_size = 20 - (i * 2) # Slightly smaller for subsequent crosses
                if cross_size < 10:
                    cross_size = 10
                
                center_px_x = cx * scale
                center_px_y = cy * scale
                
                # Horizontal line
                draw.line(
                    [(center_px_x - cross_size, center_px_y), (center_px_x + cross_size, center_px_y)], 
                    fill=color, 
                    width=3 if i == 0 else 2
                )
                
                # Vertical line
                draw.line(
                    [(center_px_x, center_px_y - cross_size), (center_px_x, center_px_y + cross_size)], 
                    fill=color, 
                    width=3 if i == 0 else 2
                )
            
        return img
