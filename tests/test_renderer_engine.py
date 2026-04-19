import glob
import json
import os
import sys
import unittest

from lib import chardefs
from lib import frame_generator
from lib import c64_converter
from lib import renderer_engine
from lib import roll_angle

sys.path.append(os.path.dirname(os.path.abspath(__file__)) + '/..')

class TestRendererEngine(unittest.TestCase):
    def setUp(self):
        self.test_output_dir = os.path.join(os.path.dirname(__file__), 'test_frames')
        if not os.path.exists(self.test_output_dir):
            os.makedirs(self.test_output_dir)

    def _render_to_image(self, screen_ram, color_ram, charset, colors, debug=False):
        # Helper to convert RAM to Image for visual inspection
        from lib import chardefs
        from lib import c64_converter
        from lib import c64_graphics
        
        # We rely on the populated charset
        # Fixed Mapping: 00=Grad2(c[2]), 01=Ground(c[0]), 10=Grad1(c[1])
        c_ground = colors[0]
        c_grad1 = colors[1]
        c_grad2 = colors[2]
        
        globals_list = [c_grad2, c_ground, c_grad1]
        return c64_graphics.C64Screen.render_mccm(globals_list, bytes(screen_ram), bytes(color_ram), bytes(charset), debug=debug)

    def test_basic_render(self):
        # Render r8u1 at standard center
        # We use standard colors
        colors = [5, 3, 14, 6]
        roll = roll_angle.RollAngle.from_string("r8u1")
        cx, cy = 160, 100
        
        screen_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        color_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        charset = bytearray(2048)
        
        try:
            # 1. Init Screen & Border
            renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
            # 2. Init Solid Chars
            renderer_engine.RendererEngine.init_solid_chars(charset)
            
            # 3. Render (Background fill is internal)
            local_map = renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll, cx, cy)
            
            # No manual fill needed
            
            # Verify RAM was written (check for non-zero/non-background)
            # Just ensure it didn't crash and we can generate image
            
            img = self._render_to_image(screen_ram, color_ram, charset, colors)
            self.assertIsNotNone(img)
            self.assertEqual(img.size, (320, 200))
            
            # Save for inspection
            img.save(os.path.join(self.test_output_dir, "test_stage4_r8u1.png"))
            
        except Exception as e:
            self.fail(f"Render failed: {e}")

    def test_alt_center_render(self):
        # Render r8u1 at alt center (164, 100 for vert? 160, 104 for horiz?)
        # r8u1 is horiz major (8 > 1).
        # Alt center is (160, 104).
        colors = [5, 3, 14, 6]
        roll = roll_angle.RollAngle.from_string("r8u1")
        cx, cy = 160, 104
        
        screen_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        color_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        charset = bytearray(2048)
        
        try:
            renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
            renderer_engine.RendererEngine.init_solid_chars(charset)
            local_map = renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll, cx, cy)
            # No manual fill needed
            img = self._render_to_image(screen_ram, color_ram, charset, colors)
            img.save(os.path.join(self.test_output_dir, "test_stage4_r8u1_alt.png"))
        except Exception as e:
            self.fail(f"Render alt failed: {e}")
            
    def test_clipping(self):
        # Center far away
        colors = [5, 3, 14, 6]
        roll = roll_angle.RollAngle.from_string("r8u1")
        cx, cy = 50, 50
        
        screen_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        color_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        charset = bytearray(2048)
        
        try:
            renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
            renderer_engine.RendererEngine.init_solid_chars(charset)
            local_map = renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll, cx, cy)
            # No manual fill needed
            img = self._render_to_image(screen_ram, color_ram, charset, colors)
            img.save(os.path.join(self.test_output_dir, "test_stage4_r8u1_clipped.png"))
        except Exception as e:
            self.fail(f"Render clipped failed: {e}")
            
    def test_center_outside_viewport(self):
        # Center far outside
        colors = [5, 3, 14, 6]
        roll = roll_angle.RollAngle.from_string("r8u1")
        cx, cy = 1000, 1000
    def test_center_just_outside_viewport(self):
        # Viewport params: Default is width=32 chars (256px), height=15 chars (120px)
        # Offset X=4 chars (32px), Y=0.
        # Pixel Bounds: X:[32, 288), Y:[0, 120).
        
        colors = [5, 3, 14, 6]
        roll = roll_angle.RollAngle.from_string("r8u1")
        
        # Test 8 directions just outside
        test_points = [
            (31, 60),   # Left
            (288, 60),  # Right
            (160, -1),  # Top
            (160, 120), # Bottom
            (31, -1),   # Top-Left
            (288, -1),  # Top-Right
            (31, 120),  # Bottom-Left
            (288, 120)  # Bottom-Right
        ]
        
        for cx, cy in test_points:
            screen_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
            color_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
            charset = bytearray(2048)
            try:
                renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
                renderer_engine.RendererEngine.init_solid_chars(charset)
                local_map = renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll, cx, cy)
                
                # No manual fill needed
                
                img = self._render_to_image(screen_ram, color_ram, charset, colors)
                self.assertIsNotNone(img)
            except Exception as e:
                self.fail(f"Render failed at ({cx}, {cy}): {e}")

        # Test Case 1: Close to Main
        roll = roll_angle.RollAngle.from_string("r8")
        # (160, 96) -> Phase (0, 0). Should be Main.
        cx_c1, cy_c1, alt1 = renderer_engine.RendererEngine._snap_center_chars(roll, 160, 96)
        self.assertEqual((cx_c1, cy_c1), (20, 12))
        self.assertFalse(alt1)
        box_main = renderer_engine.RendererEngine._get_box_def(roll, alt1)
        self.assertIsNotNone(box_main)
        
        # Test Case 2: Close to Alt
        # For r8, Alt shift is (0, 4)
        # (160, 100) -> px=160, py=100.
        # Dist to Main (160,96) is 4.
        # Dist to Alt (160, 100) is 0.
        cx_c2, cy_c2, alt2 = renderer_engine.RendererEngine._snap_center_chars(roll, 160, 100)
        self.assertTrue(alt2)
        box_alt = renderer_engine.RendererEngine._get_box_def(roll, alt2)
        
        # Test Case 3: Verify different boxes
        self.assertNotEqual(box_main, box_alt)


    def test_local_charset_isolation(self):
        """
        Verifies that:
        1. Charset buffer is ONLY updated in the [start, start + len] range.
        2. Screen RAM only contains indices in that range.
        """
        colors = [5, 3, 14, 6]
        roll = roll_angle.RollAngle.from_string("r8u1")
        cx, cy = 160, 100
        
        # 1. Initialize Dirty Buffers
        start_idx = 10
        dirty_byte = 0xFF
        
        screen_ram = bytearray([dirty_byte] * (renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS))
        color_ram = bytearray([dirty_byte] * (renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS))
        
        charset = bytearray([dirty_byte] * 2048)
        
        # 2. Render
        # 2. Render
        
        # Init with clean (0) or dirty?
        # init_solid_chars will overwrite first 24 bytes
        renderer_engine.RendererEngine.init_solid_chars(charset)
        # init_screen_and_border will overwrite RAM
        renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
        
        # 2. Render
        
        # Init with clean (0) or dirty?
        # init_solid_chars will overwrite first 24 bytes (indices 0,1,2)
        renderer_engine.RendererEngine.init_solid_chars(charset)
        # init_screen_and_border will overwrite BORDER with Solid 11 (Index 2)
        # Viewport should REMAIN dirty (0xFF) until render_frame touches it
        renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
        
        # If start_idx <= 2, render_frame will skip up to index 3.
        # But here start_idx=10.
        
        local_map = renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll, cx, cy, charset_start=start_idx)
        
        num_locals = len(local_map)
        
        # 3. Verify Charset Bounds
        # Bytes 0-23 (Indices 0,1,2) ARE modified by init_solid_chars
        # Bytes 24 to start_idx*8 should be DIRTY
        
        reserved_end = renderer_engine.RendererEngine.RESERVED_LOCAL_CHARS * 8
        pre_data = charset[reserved_end : start_idx * 8]
        
        self.assertTrue(all(b == dirty_byte for b in pre_data), "Charset modified between reserved region and start_idx!")
        
        # After usage
        # End of used region
        end_byte_idx = (start_idx + num_locals) * 8
        post_data = charset[end_byte_idx:]
        self.assertTrue(all(b == dirty_byte for b in post_data), "Charset modified after used region!")
        
        # Inside usage
        used_data = charset[start_idx * 8 : end_byte_idx]
        self.assertFalse(all(b == dirty_byte for b in used_data), "Charset data not written in used region!")
        
        # 4. Verify Screen RAM Indices
        # - Border area should be LOCAL_IDX_SOLID_11 (2)
        # - Viewport area should be valid local indices [start_idx, start_idx + num_locals) OR Fixed [0,1,2]
        # - Any untreated area (if any) should be dirty_byte?
        # render_frame fills the viewport completely.
        # init_screen_and_border fills border completely.
        # So there should be NO dirty bytes left in screen_ram!
        
        # However, render_frame might skip bytes if they are "transparent"?
        # No, sky/ground fill covers 100%.
        
        # So we just check validity.
        pass
        
        min_idx = start_idx
        max_idx = start_idx + num_locals - 1
        
        for idx, val in enumerate(screen_ram):
             # Determine if idx is in viewport or border
             y = idx // renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS
             x = idx % renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS
             
             vx = renderer_engine.RendererEngine.VIEWPORT_X_START_CHARS
             vy = renderer_engine.RendererEngine.VIEWPORT_Y_START_CHARS
             vw = renderer_engine.RendererEngine.VIEWPORT_WIDTH_CHARS
             vh = renderer_engine.RendererEngine.VIEWPORT_HEIGHT_CHARS
             
             in_viewport = (vx <= x < vx + vw) and (vy <= y < vy + vh)
             
             if in_viewport:
                 # Must be valid rendering index or fixed solid
                 is_valid = (min_idx <= val <= max_idx) or (val in [0, 1, 2])
                 # If render_frame skipped something, it would be dirty_byte. 
                 # We assert it IS NOT dirty_byte (implies full coverage).
                 self.assertNotEqual(val, dirty_byte, f"Viewport byte at {x},{y} was not written!")
                 self.assertTrue(is_valid, f"Viewport RAM value {val} at {x},{y} is outside valid range")
             else:
                 # Border -> Must be Solid 11 (2)
                 self.assertEqual(val, renderer_engine.RendererEngine.LOCAL_IDX_SOLID_11, f"Border byte at {x},{y} is not Solid 11 (2)!")
             


    def test_solid_chars_always_present(self):
        """
        Verifies that the 3 solid characters (Ground, Sky, 11) are always present in the returned map,
        even if the frame technically doesn't use them (e.g. fully sky).
        """
        colors = [5, 3, 14, 6]
        # Use a roll that might result in all Sky or all Ground to test edge cases, 
        # but the renderer implementation forces them anyway.
        roll = roll_angle.RollAngle.from_string("r8")
        cx, cy = 160, -100 # Way up in the sky? Or just standard.
        
        screen_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        color_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        charset = bytearray(2048)
        
        local_map = renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll, cx, cy)
        
        # Check for presence of global char codes
        g_char = chardefs.CHAR_SOLID_GROUND
        s_char = chardefs.CHAR_SOLID_SKY
        c11_char = chardefs.CHAR_SOLID_11
        
        # The map values are global indices
        global_indices = set(local_map.values())
        
        self.assertIn(g_char, global_indices, "Solid Ground char missing from local map")
        self.assertIn(s_char, global_indices, "Solid Sky char missing from local map")
        self.assertIn(c11_char, global_indices, "Solid 11 char missing from local map")

    def test_screen_ram_consistency_parameterized(self):
        """
        Verifies that renderer_engine.RendererEngine produces the same screen RAM indices as the 
        reference JSONs generated by generate_all.sh for multiple roll angles.
        """

        # Define test cases: (pattern, description, allowed_mismatches)
        # using the c160_96 center (Main) files
        patterns = [
            # Main angles
            ("ref_c160_96_*_r8.json", "r8", 0),
            ("ref_c160_96_*_r8u1.json", "r8u1", 0),
            ("ref_c160_96_*_r8u2.json", "r8u2", 0),
            ("ref_c160_96_*_r8u5.json", "r8u5", 2),
            ("ref_c160_96_*_r8u8.json", "r8u8", 0),
            ("ref_c160_96_*_r4u8.json", "r4u8", 0),
            ("ref_c160_96_*_l2u16.json", "l2u16", 0),
            ("ref_c160_96_*_l8u1.json", "l8u1", 0),
            ("ref_c160_96_*_l8d5.json", "l8d5", 0),
            ("ref_c160_96_*_l8d8.json", "l8d8", 0),
            ("ref_c160_96_*_l8d8.json", "l2d8", 0),
            ("ref_c160_96_*_l8d8.json", "d8", 0),
            ("ref_c160_96_*_l8d8.json", "r4d8", 0),
            ("ref_c160_96_*_l8d8.json", "r8d3", 0),
            # Alt angles
            ("ref_c160_100_*_r8.json", "r8", 0),
            ("ref_c160_100_*_r8u8.json", "r8u8", 0),
            ("ref_c160_100_*_r8d8.json", "r8d8", 0),
            ("ref_c160_100_*_r8.json", "l8", 0),
            ("ref_c160_100_*_l8u8.json", "l8u8", 0),
            ("ref_c160_100_*_l8d8.json", "l8d8", 0),
            ("ref_c164_96_*_u8.json", "u8", 0),
            ("ref_c164_96_*_d8.json", "d8", 0),
        ]
        
        ref_dir = os.path.join(os.path.dirname(__file__), "..", "reference_frames")
        ref_dir = os.path.abspath(ref_dir)
        
        colors = [5, 3, 14, 6] # Default colors
        
        # Use default viewport 32x15 at (4,0)
        vp_w, vp_h = 32, 15
        vp_x_off = (40 - vp_w) // 2
        vp_y_off = 0
        
        for pattern, label, tolerance in patterns:
            with self.subTest(roll=label):
                # Find file
                search_path = os.path.join(ref_dir, pattern)
                files = glob.glob(search_path)
                
                if not files:
                    self.fail(f"No reference file found for {label} (pattern: {pattern})")
                    continue
                    
                # Take the first match
                ref_path = files[0]
                
                with open(ref_path, 'r') as f:
                    ref_data = json.load(f)
                    
                ref_sram = ref_data['screen_ram']
                cx = ref_data['cx']
                cy = ref_data['cy']
                
                raw_roll = ref_data['roll']
                if isinstance(raw_roll, int):
                    roll = roll_angle.RollAngle(raw_roll)
                else:
                    roll = roll_angle.RollAngle.from_string(raw_roll)
                
                # 2. Render with Engine
                screen_ram_render = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
                color_ram_render = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
                charset = bytearray(2048)
                
                renderer_engine.RendererEngine.init_screen_and_border(screen_ram_render, color_ram_render)
                renderer_engine.RendererEngine.init_solid_chars(charset)
                
                local_to_global = renderer_engine.RendererEngine.render_frame(screen_ram_render, color_ram_render, charset, colors, roll, cx, cy)
                
                # No manual fill needed
                
                # Compare screen RAM (indices)
                
                errors = 0
                
                # The ref_sram is a flat list of 1000 indices (GLOBAL).
                # screen_ram_render contains LOCAL indices.
                # We must map local -> global to compare.
                
                for y in range(vp_y_off, vp_y_off + vp_h):
                    for x in range(vp_x_off, vp_x_off + vp_w):
                        s_idx = y * renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS + x
                        
                        ref_char_idx = ref_sram[s_idx]
                        rend_local_idx = screen_ram_render[s_idx]
                        
                        # Map back to global
                        if rend_local_idx not in local_to_global:
                             # This essentially means we rendered a char that wasn't accounted for in used_chars?
                             # Or buffer initialization issue.
                             print(f"[{label}] Error: Local char {rend_local_idx} at ({x},{y}) has no global mapping!")
                             errors += 1
                             continue
                             
                        rend_global_idx = local_to_global[rend_local_idx]
                        
                        if ref_char_idx != rend_global_idx:
                            errors += 1
                            if errors <= 5: # Print first few mismatches for debugging
                                print(f"[{label}] Mismatch at ({x},{y}): Renderer(Glo) {rend_global_idx} [Loc {rend_local_idx}] != Ref {ref_char_idx}")
                                try:
                                    r_bytes = chardefs.ALL_CHARS[rend_global_idx]
                                    ref_char_bytes_debug = chardefs.ALL_CHARS[ref_char_idx]
                                    print(f"  R: {r_bytes.hex()}")
                                    print(f"  L: {ref_char_bytes_debug.hex()}")
                                except:
                                    pass
                                
                self.assertLessEqual(errors, tolerance, f"[{label}] Found {errors} mismatches inside viewport (allowed {tolerance}).")

    def test_background_only_consistency_parameterized(self):
        """
        Verifies ONLY the _fill_viewport_background() function.
        We check this by generating a reference frame with NO GRADIENTS (grad_width_chars=0)
        and comparing it to the background fill result of renderer_engine.RendererEngine.
        """
        rolls = ['r8', 'r8u1', 'r8u2', 'r8u5', 'r8u8', 'r4u8', 'u8', 'l2u16', 'l8u1', 'l8d1', 'l8d6']
        
        colors = [5, 3, 14, 6] # Default colors
        cx, cy = 160, 96 # Standard Main Center
        
        # Viewport params 32x15
        vp_w, vp_h = 32, 15
        vp_x_off = (40 - vp_w) // 2
        vp_y_off = 0
        
        for roll_str in rolls:
            with self.subTest(roll=roll_str):
                roll = roll_angle.RollAngle.from_string(roll_str)
                # 1. Generate Reference (No Gradients)
                bg, sram, cram, bitmap = frame_generator.generate_frame_mcbm(
                    colors, roll, grad_width_chars=0, center_x=cx, center_y=cy
                )
                
                # Convert to Screen RAM using GLOBAL chardefs (Known Chars)
                # We want to map the 0-gradient result to our known Solid Chars.
                known_chars = [b for b in chardefs.ALL_CHARS]
                
                ref_globals, ref_sram, ref_cram, ref_charset = c64_converter.convert_mcbm_to_mccm(
                    bg, sram, cram, bitmap, 
                    ground_color_index=colors[0],
                    known_chars=known_chars,
                    tolerance=0,
                    colors=colors
                )
                
                # Verify length
                if len(ref_sram) != 1000:
                    self.fail("Reference SRAM length mismatch")
                    
                # Map local indices to global (if needed)
                # Since we provided known_chars, c64_converter.convert_mcbm_to_mccm prefers them.
                # But it might add new ones if not found?
                # Solid chars SHOULD be in chardefs.ALL_CHARS.
                # ref_sram contains INDICES.
                local_idx_to_bytes = {}
                for i in range(len(ref_charset) // 8):
                    start = i*8
                    end = i*8+8
                    local_idx_to_bytes[i] = ref_charset[start:end]
                    
                # 2. Run Renderer Background Fill (via _fill_sky_ground)
                renderer_sram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
                renderer_cram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
                
                # Init RAM
                renderer_engine.RendererEngine.init_screen_and_border(renderer_sram, renderer_cram)
                
                # Use fixed indices
                renderer_engine.RendererEngine._fill_sky_ground(
                    renderer_sram, renderer_cram, colors, roll, cx, cy
                )
                
                # Setup verification vars
                g_idx = renderer_engine.RendererEngine.LOCAL_IDX_GROUND
                s_idx = renderer_engine.RendererEngine.LOCAL_IDX_SKY
                c11_idx = renderer_engine.RendererEngine.LOCAL_IDX_SOLID_11
                
                # 3. Compare Viewport
                errors = 0
                
                # We need to know which chars in Reference are Solid Sky or Solid Ground.
                # solid_sky_char_bytes and solid_ground_char_bytes.
                # We can find them by looking at what generate_frame_mcbm produced.
                # Or just construct them.
                # Sky Col: colors[3]. Ground Col: colors[0].
                # But convert_mcbm mapped them to bits 00..11.
                # We have 'ref_globals'.
                # We need to know which byte pattern corresponds to "All Sky".
                
                # To be robust, let's just inspect the bytes directly.
                # Get Expected Solid Bytes
                def get_solid_bytes_for_color_idx(c_idx, global_colors, col11):
                    # We need to know how c_idx maps to 00, 01, 10, 11
                    # using the SAME mapping logic as convert_subsystem?
                    # Actually we have ref_globals = [c00, c01, c10] and col11.
                    # This tells us the mapping for THIS frame.
                    bits = -1
                    if c_idx == global_colors[0]: bits = 0
                    elif c_idx == global_colors[1]: bits = 1
                    elif c_idx == global_colors[2]: bits = 2
                    elif c_idx == col11: bits = 3
                    
                    if bits == -1: return None
                    
                    val = (bits << 6) | (bits << 4) | (bits << 2) | bits
                    return bytes([val] * 8)

                sky_bytes = get_solid_bytes_for_color_idx(colors[3], ref_globals, -1) # col11 not usually sky?
                ground_bytes = get_solid_bytes_for_color_idx(colors[0], ref_globals, -1)
                
                # Since we don't know col11 definitively without scanning CRAM, 
                # let's just assume simple case or rely on mismatches.
                # Better: Check if reference is solid.
                
                for y in range(vp_y_off, vp_y_off + vp_h):
                    for x in range(vp_x_off, vp_x_off + vp_w):
                        idx = y * renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS + x
                        
                        r_char_idx = renderer_sram[idx]
                        
                        # Map local index back to byte pattern
                        if r_char_idx == g_idx:
                            r_bytes = chardefs.ALL_CHARS[chardefs.CHAR_SOLID_GROUND]
                        elif r_char_idx == s_idx:
                            r_bytes = chardefs.ALL_CHARS[chardefs.CHAR_SOLID_SKY]
                        elif r_char_idx == c11_idx:
                            r_bytes = chardefs.ALL_CHARS[chardefs.CHAR_SOLID_11]
                        else:
                            r_bytes = b'\x00' * 8 # Should not happen
                        
                        ref_local_idx = ref_sram[idx]
                        ref_bytes = local_idx_to_bytes[ref_local_idx]
                        
                        # Check if reference is solid
                        is_solid = True
                        first_byte = ref_bytes[0]
                        # 1. Check vertical consistency
                        for b in ref_bytes:
                            if b != first_byte:
                                is_solid = False
                                break
                        
                        # 2. Check horizontal consistency (byte must be single-color pattern)
                        # patterns: 00000000 (0x00), 01010101 (0x55), 10101010 (0xaa), 11111111 (0xff)
                        if is_solid:
                            if first_byte not in [0x00, 0x55, 0xaa, 0xff]:
                                is_solid = False

                        if is_solid:
                            # It is a solid block (Sky, Ground, or Solid Grad if grad=0?)
                            # If grad=0, only Sky or Ground exists.
                            # So Ref is either Solid Sky or Solid Ground.
                            # Renderer MUST match.
                            if r_bytes != ref_bytes:
                                errors += 1
                                if errors <= 5:
                                    print(f"[{roll}] BG Mismatch at Solid Block ({x},{y}): Renderer {r_char_idx} != Ref {ref_local_idx}")
                                    print(f"  R: {r_bytes.hex()}")
                                    print(f"  L: {ref_bytes.hex()}")

                                    print(f"  L: {ref_bytes.hex()}")
 
                self.assertEqual(errors, 0, f"[{roll_str}] Found {errors} background mismatches on solid blocks.")

    def test_pull_to_center(self):
        """Verifies that _pull_to_center correctly moves points along the horizon line."""
        test_cases = [
            # (roll_angle.RollAngle, cx, cy, expected_px, expected_py)
            (roll_angle.RollAngle.R8, 1000, 64, 168, 64),
            (roll_angle.RollAngle.R16U1, 1000, 64, 104, 120),
            (roll_angle.RollAngle.L8, -1000, 64, 152, 64),
            (roll_angle.RollAngle.U8, 160, 1000, 160, 40),
            (roll_angle.RollAngle.D8, 160, -1000, 160, 88),
            (roll_angle.RollAngle.R8U1, 160, 64, 160, 64),
            (roll_angle.RollAngle.R8U1, 167, 65, 167, 65),
            (roll_angle.RollAngle.R10U16, 160, 64, 160, 64),
        ]
        
        for roll, cx, cy, exp_px, exp_py in test_cases:
            with self.subTest(roll=roll.name, cx=cx, cy=cy):
                px, py = renderer_engine.RendererEngine._pull_to_center(roll, cx, cy)
                dx, dy = roll.get_vector()
                
                # Check line consistency: (px-cx)*dy == (py-cy)*dx
                self.assertEqual((px - cx) * dy, (py - cy) * dx, f"Point ({px}, {py}) not on horizon line for roll {roll.name}")
                
                # Check expected value
                self.assertEqual((px, py), (exp_px, exp_py), f"Incorrect pull result for {roll.name} at ({cx}, {cy})")

    def test_snap_center_basic(self):
        roll = roll_angle.RollAngle.from_string("r8")
        
        # 160, 96 is perfectly on Main Lattice (20*8, 12*8)
        cx_c1, cy_c1, alt1 = renderer_engine.RendererEngine._snap_center_chars(roll, 160, 96)
        self.assertEqual((cx_c1, cy_c1), (20, 12))
        self.assertFalse(alt1)
        
        # 160, 100 is perfectly on Alt Lattice (20*8, 12*8 + 4) for r8
        cx_c2, cy_c2, alt2 = renderer_engine.RendererEngine._snap_center_chars(roll, 160, 100)
        self.assertEqual((cx_c2, cy_c2), (20, 12)) 
        self.assertTrue(alt2)

    def test_snap_center_rounding(self):
        roll = roll_angle.RollAngle.from_string("r8")
        
        # (161, 97) should snap to Main (160, 96) -> (20, 12)
        cx_c, cy_c, alt = renderer_engine.RendererEngine._snap_center_chars(roll, 161, 97)
        self.assertEqual((cx_c, cy_c), (20, 12))
        self.assertFalse(alt)
        
        # (161, 101) should snap to Alt (160, 100) -> (20, 12)
        cx_c, cy_c, alt = renderer_engine.RendererEngine._snap_center_chars(roll, 161, 101)
        self.assertEqual((cx_c, cy_c), (20, 12))
        self.assertTrue(alt)

    def test_no_tiles_render(self):
        """Verifies that no tiles are rendered when no_tiles=True."""
        colors = [5, 3, 14, 6]
        roll = roll_angle.RollAngle.from_string("r8u1")
        cx, cy = 160, 100
        
        screen_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        color_ram = bytearray(renderer_engine.RendererEngine.SCREEN_WIDTH_CHARS * renderer_engine.RendererEngine.SCREEN_HEIGHT_CHARS)
        charset = bytearray(2048)
        
        renderer_engine.RendererEngine.init_screen_and_border(screen_ram, color_ram)
        renderer_engine.RendererEngine.init_solid_chars(charset)
        
        # Render with no_tiles=True
        local_map = renderer_engine.RendererEngine.render_frame(screen_ram, color_ram, charset, colors, roll, cx, cy, no_tiles=True)
        
        # When no_tiles is True, only the 3 reserved characters should be in local_map
        # (Technically, it might return just those 3)
        self.assertEqual(len(local_map), 3)
        self.assertIn(chardefs.CHAR_SOLID_GROUND, local_map.values())
        self.assertIn(chardefs.CHAR_SOLID_SKY, local_map.values())
        self.assertIn(chardefs.CHAR_SOLID_11, local_map.values())
        
        # Verify Screen RAM only contains indices 0, 1, 2
        for idx, val in enumerate(screen_ram):
            self.assertIn(val, [0, 1, 2], f"Unexpected character index {val} at screen RAM index {idx}")
