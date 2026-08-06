
import os
import sys
import argparse
import pygame
from PIL import Image

# Ensure lib is in path
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

import lib.renderer_engine
import lib.roll_angle
import lib.c64_graphics

# Constants
C64_WIDTH = lib.c64_graphics.C64Screen.WIDTH
C64_HEIGHT = lib.c64_graphics.C64Screen.HEIGHT
SCALE = 4
WINDOW_WIDTH = C64_WIDTH * SCALE
WINDOW_HEIGHT = C64_HEIGHT * SCALE
FPS = 30
MIN_FRAME_TIME_MS = 1000 // FPS

def pil_to_surface(pil_img: Image.Image) -> pygame.Surface:
    """Converts a PIL image to a pygame surface."""
    # Ensure image is in a mode compatible with pygame.image.fromstring
    if pil_img.mode not in ("RGB", "RGBA", "RGBX", "ARGB", "BGRA", "P"):
        pil_img = pil_img.convert("RGB")
    
    mode = pil_img.mode
    size = pil_img.size
    data = pil_img.tobytes()
    # pygame.image.fromstring expects a Literal for format, 
    # but PIL.Image.mode is a str. We cast to any or ignore to satisfy static analysis.
    return pygame.image.fromstring(data, size, mode) # type: ignore

def parse_args():
    parser = argparse.ArgumentParser(description="C64 Flight Demo")
    parser.add_argument("--no-tiles", action="store_true", help="Disable tiled box rendering")
    parser.add_argument("--debug", action="store_true", help="Enable debug crosses")
    return parser.parse_args()

def main():
    args = parse_args()
    pygame.init()
    screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
    pygame.display.set_caption("C64 Flight Demo")
    clock = pygame.time.Clock()

    # Initial State
    center_x = C64_WIDTH // 2
    center_y = C64_HEIGHT // 2
    all_rolls = lib.roll_angle.RollAngle.all_rolls()
    roll_idx = 0
    # Try to find a good starting roll (like R8)
    for i, r in enumerate(all_rolls):
        if r.name == "R8":
            roll_idx = i
            break
            
    colors = [5, 3, 14, 6] # Ground, Grad1, Grad2, Sky
    
    # Pre-allocate RAMs
    screen_ram = bytearray(lib.renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * lib.renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
    color_ram = bytearray(lib.renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * lib.renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
    charset = bytearray(2048)

    running = True
    MOVE = 2
    while running:
        # 1. Handle Events (One shots: J, L, Q)
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_q:
                    running = False
                
        # 2. Continuous Keyboard Input (Movement: I, K, A, S)
        keys = pygame.key.get_pressed()
        if keys[pygame.K_i]:
            center_y -= MOVE
        if keys[pygame.K_k]:
            center_y += MOVE
        if keys[pygame.K_a]:
            center_x -= MOVE
        if keys[pygame.K_s]:
            center_x += MOVE
        if keys[pygame.K_j]:
            roll_idx = (roll_idx - 1) % len(all_rolls)
        if keys[pygame.K_l]:
            roll_idx = (roll_idx + 1) % len(all_rolls)
            
        # 3. Render
        roll = all_rolls[roll_idx]
        
        # Clear/Init RAMs
        lib.renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
        lib.renderer_engine.RendererEngine.init_solid_chars(charset)
        
        # Engine Render
        lib.renderer_engine.RendererEngine.render_frame(
            screen_ram, color_ram, charset, colors, roll,
            center_x, center_y, charset_start=0,
            no_tiles=args.no_tiles
        )
        
        # Visual Render (PIL)
        # Fixed: 00=Grad2, 01=Ground, 10=Grad1
        globals_list = [colors[2], colors[0], colors[1]] 
        
        if args.debug:
            (cx_pulled, cy_pulled), (cx_snap, cy_snap) = lib.renderer_engine.RendererEngine.get_pulled_snapped_centers(
                roll, center_x, center_y)
            debug_crosses = [(center_x, center_y), (cx_snap, cy_snap), (cx_pulled, cy_pulled)]
        else:
            debug_crosses = None
            
        pil_img = lib.c64_graphics.C64Screen.render_mccm(
            globals_list, bytes(screen_ram), bytes(color_ram), bytes(charset),
            debug=args.debug, debug_crosses=debug_crosses
        )
        
        # 4. Display
        # Convert to Pygame
        pg_surface = pil_to_surface(pil_img)
        # Scale Up
        scaled_surface = pygame.transform.scale(pg_surface, (WINDOW_WIDTH, WINDOW_HEIGHT))
        
        screen.blit(scaled_surface, (0, 0))
        
        # Draw status text
        if pygame.font.get_init():
            font = pygame.font.SysFont("monospace", 15)
            status = f"Pos: ({center_x}, {center_y}) Roll: {roll.name} (J/L: Next/Prev Roll, Q: Quit)"
            text = font.render(status, True, (255, 255, 255))
            screen.blit(text, (10, 10))

        pygame.display.flip()
        
        # Cap Frame Rate
        clock.tick(FPS)

    pygame.quit()

if __name__ == "__main__":
    main()
