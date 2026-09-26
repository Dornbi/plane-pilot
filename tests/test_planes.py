"""Tests for the traffic-sprite reference model (`lib/planes.py`).

These lock down the invariants `docs/planes.md` argues for: a pixel size that
depends on distance alone, a sprite layout nobody can see change, an apparent
size that does not breathe as the aircraft rotates, a silhouette that never
clips at any attitude, a fuselage that keeps its width, flat surfaces that
thin to a line edge-on rather than vanishing, and a bitmap that slides above
the sprite DMA cut instead of being dropped. TestTwin pins the JavaScript twin
in `docs/planes-prototype.html` to this file bit for bit.
"""

import math
import unittest

from lib import planes
from lib.planes import (LEVEL_1X, LEVEL_DOT, LEVEL_XEXP, LEVEL_XYEXP, Model,
                        SpriteBuffer, State, orient, place, render)


LEVEL = orient(0, 0, 0)
# The default model draws 1.5x oversize, so its apparent span is 16.5 m.
EFFECTIVE_SPAN_M = Model().span_m * Model().scale


def draw(distance_m, heading=180.0, pitch=0.0, bank=0.0, bearing=0.0,
         elevation=0.0, cam=LEVEL, state=None, model=None, **kw):
    state = state if state is not None else State()
    model = model if model is not None else Model()
    return render(state, cam, orient(heading, pitch, bank),
                  place(distance_m, bearing, elevation), model, **kw)


def attitudes(heading_step=15, banks=(0, 20, 45, 70, 89, -45, -90),
              pitches=(-40, 0, 40)):
    for heading in range(0, 360, heading_step):
        for bank in banks:
            for pitch in pitches:
                yield heading, pitch, bank


def screen_pixels(r):
    """The set of screen pixels the VIC would light for a result."""
    out = set()
    t = r.tier
    for y in range(r.buf.height):
        for x in range(r.buf.width):
            if r.buf.get(x, y):
                for dx in range(t.xs):
                    for dy in range(t.ys):
                        out.add((r.origin[0] + x * t.xs + dx, r.origin[1] + y * t.ys + dy))
    return out


class TestFixedPoint(unittest.TestCase):
    """`fmul` and `fdiv` must match vec_asm.cc, including the truncation."""

    def test_fmul_truncates_toward_zero(self):
        self.assertEqual(planes.fmul(7, 128), 3)
        self.assertEqual(planes.fmul(-7, 128), -3)     # not -4
        self.assertEqual(planes.fmul(256, 44), 44)
        self.assertEqual(planes.fmul(-256, 44), -44)

    def test_fmul_is_symmetric_about_zero(self):
        for a in range(-300, 301, 7):
            for b in range(-300, 301, 13):
                self.assertEqual(planes.fmul(-a, b), -planes.fmul(a, b))

    def test_fdiv(self):
        self.assertEqual(planes.fdiv(16, 50), 81)      # 4096 / 50
        self.assertEqual(planes.fdiv(-16, 50), -81)
        self.assertEqual(planes.fdiv(1, 0), 0)

    def test_smul_rounds_to_nearest_and_is_symmetric(self):
        for a in range(-200, 201, 3):
            for b in range(0, 300, 11):
                self.assertEqual(planes.smul(a, b), -planes.smul(-a, b))
                self.assertLessEqual(abs(planes.smul(a, b) - a * b / 256), 0.5 + 1e-9)

    def test_norm2_is_within_four_percent(self):
        for deg in range(0, 360, 3):
            x = round(100 * math.cos(math.radians(deg)))
            y = round(100 * math.sin(math.radians(deg)))
            exact = math.hypot(x, y)
            self.assertLess(abs(planes.norm2(x, y) - exact) / exact, 0.04)

    def test_isqrt_ceil(self):
        for n in range(0, 2000):
            r = planes.isqrt_ceil(n)
            self.assertGreaterEqual(r * r, n)
            self.assertTrue(r == 0 or (r - 1) * (r - 1) < n)


class TestProjection(unittest.TestCase):
    """The scale relation every distance in the design is derived from."""

    def test_wingspan_matches_the_closed_form(self):
        # Head-on, so the full span is visible and unforeshortened. Only
        # outside the size clamp -- inside it the projection is deliberately
        # no longer the closed form (see TestSizeClamp).
        for d in (1000, 600, 450, 300, 220, 176, 150, 120, 110, 80, 60):
            r = draw(d)
            self.assertTrue(r.visible)
            expected = planes.span_pixels(EFFECTIVE_SPAN_M, d)
            self.assertLess(abs(r.bbox[0] - expected), 2.5,
                            "span at %d m: got %d, expected %.1f"
                            % (d, r.bbox[0], expected))

    def test_the_doubling_keeps_the_silhouette_from_shrinking(self):
        # Without the half-rounded multiply the silhouette came out ~9% small.
        errors = []
        for d in range(60, 500, 7):
            r = draw(d)
            errors.append(r.bbox[0] - planes.span_pixels(EFFECTIVE_SPAN_M, d))
        self.assertGreater(sum(errors) / len(errors), -1.0)

    def test_culling(self):
        self.assertFalse(draw(5).visible)
        self.assertEqual(draw(5).reason, "behind camera")
        self.assertFalse(draw(9000).visible)
        self.assertEqual(draw(9000).reason, "out of range")


class TestPixelSize(unittest.TestCase):
    """The level -- 1:1, X-expanded, X+Y-expanded -- depends on distance alone."""

    def test_pixel_size_never_changes_as_the_aircraft_rotates(self):
        # The old ladder picked X-expansion from the bounding-box width, so a
        # plane at 120 m switched pixel size as it rolled through 45 degrees:
        # the same artefact the fuselage thickness rule was written against.
        for d in (40, 80, 105, 115, 150, 195, 205, 300, 1000, 1100):
            seen = set()
            for heading, pitch, bank in attitudes(heading_step=30):
                seen.add(draw(d, heading=heading, pitch=pitch, bank=bank).tier.level)
            self.assertEqual(len(seen), 1, "level varied with attitude at %d m: %s"
                             % (d, sorted(seen)))

    def test_the_ladder_climbs_as_it_closes(self):
        levels = [draw(d, heading=120, bank=20).tier.level for d in range(1500, 20, -5)]
        self.assertEqual(levels, sorted(levels))
        self.assertEqual(set(levels), {LEVEL_DOT, LEVEL_1X, LEVEL_XEXP, LEVEL_XYEXP})

    def test_where_the_steps_fall(self):
        # planes.md section 4 quotes these, for the default Cessna at 1.5x.
        self.assertEqual(draw(1100).tier.level, LEVEL_DOT)
        self.assertEqual(draw(1000).tier.level, LEVEL_1X)
        self.assertEqual(draw(205).tier.level, LEVEL_1X)
        self.assertEqual(draw(195).tier.level, LEVEL_XEXP)
        self.assertEqual(draw(115).tier.level, LEVEL_XEXP)
        self.assertEqual(draw(105).tier.level, LEVEL_XYEXP)

    def test_hysteresis_holds_the_higher_level(self):
        state = State()
        for d in range(320, 100, -2):
            draw(d, state=state)
        self.assertEqual(draw(100, state=state).tier.level, LEVEL_XYEXP)
        self.assertEqual(draw(118, state=state).tier.level, LEVEL_XYEXP,
                         "level dropped immediately on the way back")
        self.assertEqual(draw(160, state=state).tier.level, LEVEL_XEXP)

    def test_a_long_jump_lands_on_the_far_level(self):
        # The stroke design's thickness latch compared against the wrong
        # threshold and could be held two rungs up after a jump. Here the
        # demotion thresholds decide, however far it is.
        state = State()
        draw(40, state=state)
        self.assertEqual(draw(600, state=state).tier.level, LEVEL_1X)

    def test_dot_tier_for_distant_traffic(self):
        r = draw(1500)
        self.assertTrue(r.tier.dot)
        self.assertIs(r.buf, planes.DOT_BITMAP)

    def test_dot_tier_skips_the_body_axes(self):
        # Decided from d before the axes are transformed, so the common case
        # costs the position transform and one sprite, nothing else.
        self.assertEqual(draw(1500).cycles, planes.CYC_POSITION + planes.CYC_SPRITE)
        self.assertLess(draw(1500).cycles, draw(900).cycles)

    def test_the_static_bitmap_is_a_two_by_two_blob_on_the_last_rows(self):
        lit = [(x, y) for y in range(planes.ROWS) for x in range(planes.COLS)
               if planes.DOT_BITMAP.get(x, y)]
        self.assertEqual(len(lit), 4)
        self.assertEqual({y for _, y in lit}, {planes.ROWS - 2, planes.ROWS - 1})


class TestLayout(unittest.TestCase):
    """The sprite count follows the bounding box, and nobody can see it."""

    def test_two_sprites_at_most_until_x_plus_y(self):
        for d in (110, 150, 200, 300, 600):
            for heading, pitch, bank in attitudes(heading_step=30):
                r = draw(d, heading=heading, pitch=pitch, bank=bank)
                if r.tier.level < LEVEL_XYEXP:
                    self.assertLessEqual(r.tier.sprites, 2)
                    self.assertEqual(r.tier.cols, 1)

    def test_four_sprites_at_most_ever(self):
        for d in (20, 40, 60, 90):
            for heading, pitch, bank in attitudes(heading_step=30):
                self.assertLessEqual(draw(d, heading=heading, pitch=pitch,
                                          bank=bank).tier.sprites, 4)

    def test_tall_and_narrow_is_a_column(self):
        r = draw(60, heading=180, bank=89)
        self.assertGreater(r.bbox[1], r.bbox[0])
        self.assertEqual((r.tier.cols, r.tier.rows), (1, 2))

    def test_wide_and_flat_is_a_row(self):
        r = draw(60, heading=180, bank=0)
        self.assertEqual((r.tier.cols, r.tier.rows), (2, 1))

    def test_a_layout_change_moves_no_pixel(self):
        # Hold a bigger layout through the hysteresis and compare the lit
        # screen pixels with a fresh state's smaller layout.
        for heading in range(0, 360, 20):
            state = State(level=LEVEL_XYEXP, cols=2, rows=2)
            held = draw(62, heading=heading, bank=15, state=state)
            fresh = draw(62, heading=heading, bank=15)
            if held.tier.sprites == fresh.tier.sprites:
                continue
            self.assertEqual(screen_pixels(held), screen_pixels(fresh),
                             "layout change moved pixels at heading %d" % heading)


class TestSizeClamp(unittest.TestCase):
    """Closer than the clamp range the aircraft stops growing."""

    def test_apparent_size_does_not_change_as_it_rotates(self):
        for d in (17, 25, 40, 54):
            scales = {draw(d, heading=h, bank=b).k
                      for h in range(0, 360, 10) for b in (0, 30, 60, 89)}
            self.assertEqual(len(scales), 1,
                             "apparent size varied with attitude at %d m: %s"
                             % (d, sorted(scales)))

    def test_apparent_size_freezes_with_distance(self):
        sizes = {draw(d).k for d in (17, 25, 40, 50, 54)}
        self.assertEqual(sizes, {Model().k_max})

    def test_the_clamp_engages_close_in_and_not_far_out(self):
        for d in (17, 25, 40, 54):
            self.assertTrue(draw(d).clamped, "not clamped at %d m" % d)
        for d in (60, 110, 250, 900):
            self.assertFalse(draw(d).clamped, "clamped at %d m" % d)

    def test_nothing_clips_at_any_attitude(self):
        # The cap is taken from the shorter side of the largest layout and
        # each level's limit leaves two pixels for rounding, so -- unlike the
        # width-capped stroke design, whose tips clipped past 73 degrees of
        # bank -- every vertex of every attitude lands inside the buffer.
        for d in (17, 30, 54, 70, 90, 105, 115, 150, 195, 205, 400, 1000):
            for heading, pitch, bank in attitudes(heading_step=20,
                                                  pitches=(-60, -30, 0, 30, 60)):
                r = draw(d, heading=heading, bank=bank, pitch=pitch)
                self.assertTrue(r.fits, "overran at %d m hdg %d bank %d pitch %d"
                                % (d, heading, bank, pitch))
                self.assertEqual(r.slid, 0)
                for polys in r.local.values():
                    for poly in polys:
                        for x, y in poly:
                            self.assertTrue(0 <= x < r.buf.width and 0 <= y < r.buf.height,
                                            "vertex off the buffer at %d m hdg %d bank %d"
                                            % (d, heading, bank))

    def test_the_cap_is_a_constant_of_the_model(self):
        big = Model(span_m=20.0, length_m=18.0, scale=1.5)
        self.assertLess(big.k_max, Model().k_max)
        r = render(State(), LEVEL, orient(180, 0, 0), place(25), big)
        self.assertTrue(r.clamped)
        self.assertTrue(r.fits)


class TestFuselage(unittest.TestCase):
    """A body of revolution: the same width from every side."""

    def test_width_does_not_change_as_it_rotates(self):
        model = Model()
        for d in (20, 60, 120):
            r0 = draw(d)
            k = r0.k
            radius = planes.smul(model.geometry["body"][1][1], k)
            for heading, pitch, bank in attitudes(heading_step=10):
                t = orient(heading, pitch, bank)
                axes = [planes.q88(planes.to_cam(LEVEL, v)) for v in (t.front, t.left, t.up)]
                shapes, _ = planes.project_model(model, k, (160, 56), *axes)
                trapezoids = [p for p in shapes["body"] if len(p) == 4]
                if not trapezoids:
                    continue          # end-on: the disc, tested below
                p1, p2 = trapezoids[0][1], trapezoids[0][2]
                half = math.hypot(p1[0] - p2[0], p1[1] - p2[1]) / 2
                self.assertLessEqual(abs(half - radius), 1.0,
                                     "cabin half-width %.1f against %d at %d m hdg %d"
                                     % (half, radius, d, heading))

    def test_end_on_it_is_a_disc(self):
        r = draw(40, parts=frozenset({"body"}))
        self.assertTrue(any(len(p) == 8 for p in r.shapes["body"]))
        lit_rows = [row for row in r.buf.rows_as_text() if "#" in row]
        self.assertGreaterEqual(len(lit_rows), 3, "no cross-section seen end-on")

    def test_side_on_there_is_no_disc(self):
        r = draw(40, heading=90, parts=frozenset({"body"}))
        self.assertFalse(any(len(p) == 8 for p in r.shapes["body"]))


class TestSurfaces(unittest.TestCase):
    """Flat plates: the projected chord comes out of the fill, exactly."""

    def test_edge_on_is_a_line_not_nothing(self):
        r = draw(150, parts=frozenset({"wing"}))
        lit = [row for row in r.buf.rows_as_text() if "#" in row]
        self.assertEqual(len(lit), 1)
        span_cols = lit[0].count("#") * r.tier.xs
        self.assertLess(abs(span_cols - planes.span_pixels(EFFECTIVE_SPAN_M, 150)), 3)

    def test_the_wing_fills_in_as_its_chord_comes_into_view(self):
        ink = [draw(120, pitch=p, parts=frozenset({"wing"})).buf.ink() for p in (0, 20, 45)]
        self.assertEqual(ink, sorted(ink))
        self.assertGreater(ink[-1], 2 * ink[0])

    def test_the_tailplane_shows_from_above(self):
        with_stab = draw(80, heading=130, elevation=-25, cam=orient(0, -25, 0))
        without = draw(80, heading=130, elevation=-25, cam=orient(0, -25, 0),
                       parts=frozenset({"wing", "fin", "body"}))
        self.assertGreater(with_stab.buf.ink(), without.buf.ink())

    def test_the_fin_tells_upright_from_inverted(self):
        def fin_is_above_the_wing(bank):
            r = draw(80, heading=150, bank=bank)
            mean_y = lambda part: (sum(p[1] for p in r.shapes[part][0])
                                   / len(r.shapes[part][0]))
            return mean_y("fin") < mean_y("wing")
        self.assertTrue(fin_is_above_the_wing(0))
        self.assertFalse(fin_is_above_the_wing(180))


class TestSlide(unittest.TestCase):
    """Sprite DMA stops above the panel; the bitmap moves, the plane does not."""

    def test_a_close_plane_low_in_the_view_is_drawn(self):
        # 70 m and 10 degrees below the eye line: the stack would reject the
        # old 1 x 2 entry outright although its top half is on screen.
        r = draw(70, heading=120, bank=40, elevation=-10)
        self.assertTrue(r.visible)
        self.assertGreater(r.slid, 0)
        self.assertGreater(r.buf.ink(), 0)

    def test_the_last_sprite_row_always_starts_above_the_cut(self):
        for d in (30, 60, 90, 150, 250):
            for elev in range(-20, 21, 2):
                for bank in (0, 45, 89):
                    r = draw(d, heading=120, bank=bank, elevation=elev)
                    if not r.visible or r.tier.dot:
                        continue
                    t = r.tier
                    cut = (planes.SPRITE_START_MAX_EXP_Y if t.ys == 2
                           else planes.SPRITE_START_MAX)
                    self.assertLessEqual(r.origin[1] + (t.rows - 1) * planes.ROWS * t.ys, cut)

    def test_sliding_loses_only_the_last_two_lines(self):
        for d in (30, 60, 90, 150):
            for elev in range(-20, -4, 2):
                r = draw(d, heading=120, bank=30, elevation=elev)
                if r.visible and r.slid:
                    bottom = r.origin[1] + r.buf.height * r.tier.ys
                    self.assertEqual(bottom, planes.VIEW_H - 2)

    def test_the_dot_is_drawable_down_to_line_108(self):
        for elev_tenths in range(-60, -40):
            r = draw(1200, elevation=elev_tenths / 10)
            if r.visible:
                self.assertLessEqual(r.centre[1], 108)
            else:
                self.assertEqual(r.reason, "below the sprite cut")
                self.assertGreater(r.centre[1], 108)


class TestRasteriser(unittest.TestCase):

    def test_a_span_crosses_bytes_and_blocks(self):
        buf = SpriteBuffer(2, 1)
        buf.fill_span(0, 20, 27)
        self.assertEqual(buf.blocks[0][2], 0x0F)
        self.assertEqual(buf.blocks[1][0], 0xF0)
        self.assertEqual(buf.ink(), 8)

    def test_a_span_lands_in_the_right_block_row(self):
        buf = SpriteBuffer(1, 2)
        buf.fill_span(21, 0, 7)
        self.assertEqual(buf.blocks[1][0], 0xFF)
        self.assertEqual(sum(buf.blocks[0]), 0)

    def test_clamping_a_span_is_an_exact_clip(self):
        # Overhangs every side, and no taller than the reciprocal table.
        buf = SpriteBuffer(1, 1)
        planes.fill_poly(buf, [(-30, -5), (60, -5), (60, 30), (-30, 30)])
        self.assertEqual(buf.ink(), planes.COLS * planes.ROWS)

    def test_a_degenerate_polygon_is_a_line(self):
        buf = SpriteBuffer(1, 1)
        planes.fill_poly(buf, [(2, 3), (20, 12), (20, 12), (2, 3)])
        self.assertGreater(buf.ink(), 10)
        for y, row in enumerate(buf.rows_as_text()):
            self.assertLessEqual(row.count("#"), 3, "row %d is not a line" % y)

    def test_no_edge_outgrows_the_reciprocal_table(self):
        for d in (17, 54, 70, 105, 115, 195):
            for heading, pitch, bank in attitudes(heading_step=30):
                r = draw(d, heading=heading, pitch=pitch, bank=bank)
                for polys in r.local.values():
                    for poly in polys:
                        ys = [p[1] for p in poly]
                        self.assertLess(max(ys) - min(ys), len(planes.RECIP))

    def test_every_attitude_draws_something(self):
        for d in (18, 30, 60, 110, 250, 600, 1000):
            for heading, pitch, bank in attitudes(heading_step=15, pitches=(0,)):
                r = draw(d, heading=heading, bank=bank)
                self.assertTrue(r.visible)
                self.assertGreater(r.buf.ink(), 0, "empty buffer at hdg %d bank %d %d m"
                                   % (heading, bank, d))

    def test_the_buffer_is_sized_to_the_layout(self):
        for d in (20, 45, 90, 180):
            for heading in range(0, 360, 11):
                r = draw(d, heading=heading, bank=45)
                self.assertEqual(len(r.buf.blocks), r.tier.cols * r.tier.rows)
                self.assertEqual(r.buf.height, planes.ROWS * r.tier.rows)


class TestCache(unittest.TestCase):

    def test_repeating_a_frame_hits(self):
        state = State()
        first = draw(150, state=state)
        second = draw(150, state=state)
        self.assertFalse(first.cached)
        self.assertTrue(second.cached)
        self.assertLess(second.cycles, first.cycles)

    def test_changing_orientation_misses(self):
        state = State()
        draw(150, heading=180, state=state)
        self.assertFalse(draw(150, heading=150, state=state).cached)

    def test_a_crossing_at_constant_range_hits(self):
        # The local vertices depend on k and the relative orientation, not on
        # where the target sits in the view.
        state = State()
        draw(150, heading=-90, state=state)
        cam = orient(0, 0, 0)
        r = render(state, cam, orient(-90, 0, 0), (150.0, -12.0, 0.0))
        self.assertTrue(r.cached)


class TestGoldenSilhouettes(unittest.TestCase):
    """Byte-exact output for a few representative attitudes, pinned to the
    JavaScript twin through TestTwin."""

    GOLDEN = {
        "level, head-on, 150 m": (
            dict(distance_m=150, heading=180, pitch=0, bank=0),
            # Edge-on wing and tailplane on one row each, the fin above them,
            # the fuselage seen end-on as its cross-section.
            [
                "............#...........",
                "............#...........",
                "............#...........",
                ".....###############....",
                "..........#####.........",
                "............#...........",
            ],
        ),
        "three-quarter, pitched up, 120 m": (
            dict(distance_m=120, heading=135, pitch=25, bank=20),
            [
                "...............####.....",
                "..............######....",
                "........###..#######....",
                "........##########......",
                "........#########.......",
                "........########........",
                ".......#######..........",
                "......#########.........",
                ".....##########.........",
                "....######.#######......",
                "....#####...######......",
                ".....###.....#####......",
                "......#.......#####.....",
                "..............#####.....",
                "..............####......",
                "...............#........",
            ],
        ),
        "crossing, 30 degree bank, 60 m": (
            dict(distance_m=60, heading=110, pitch=0, bank=30),
            # X+Y-expanded across two sprites: the planform, the tailplane and
            # the fin all read at once.
            [
                ".....................#######....................",
                "....................#######.....................",
                "....................######......................",
                "...................#######......................",
                "...................######.......................",
                "..................#######.......................",
                ".................#######........####............",
                ".................######........#######..........",
                "...........##########################...........",
                "...........#########################............",
                "...........#########################............",
                "...........##################.#####.............",
                "..............######.#........#####.............",
                ".............#######............................",
                ".............######.............................",
                "............#######.............................",
                "............######..............................",
                "............######..............................",
            ],
        ),
    }

    def test_golden(self):
        for name, (params, expected) in self.GOLDEN.items():
            with self.subTest(name):
                r = draw(**params)
                rows = [row for row in r.buf.rows_as_text() if "#" in row]
                self.assertEqual(rows, expected, "\n" + "\n".join(rows))


class TestTwin(unittest.TestCase):
    """`docs/planes-prototype.html` agrees with this file bit for bit.

    These are `twinHashes()` as the prototype printed them: 11,880 stateless
    frames over distance, heading, bank, pitch and position for two models,
    and two 212-frame approaches through the hysteresis and the cache. Each
    hash covers every bitmap byte, the level, layout, origin, slide, clamp and
    cycle estimate of its block. If a change here is deliberate, run
    `twinHashes()` in the prototype's console after making the same change
    there, and paste its output below.
    """

    JAVASCRIPT = {
        "default 18 m": "ade5f5bd", "default 30 m": "9d7de17c",
        "default 55 m": "1adf6a0d", "default 80 m": "8814edab",
        "default 110 m": "c07ccd66", "default 150 m": "19fd59ff",
        "default 210 m": "6c124be1", "default 300 m": "86b33286",
        "default 600 m": "0b7b38dc", "default 1100 m": "977c7c61",
        "default 1500 m": "977c7c61",
        "big 18 m": "b1b294e0", "big 30 m": "c6a2f2e3", "big 55 m": "0c83d824",
        "big 80 m": "284cc261", "big 110 m": "0bc834e2", "big 150 m": "c829f1e5",
        "big 210 m": "6f6bb05f", "big 300 m": "8e68477b", "big 600 m": "f94bb953",
        "big 1100 m": "3e8a7b16", "big 1500 m": "b4e5327f",
        "approach, level": "d82fea18", "approach, banked": "49278d71",
    }

    def test_python_matches_the_prototype(self):
        self.assertEqual(planes.twin_hashes(), self.JAVASCRIPT)


class TestBudget(unittest.TestCase):

    SIM_FRAME_CYCLES = 98_500     # ~10 Hz on PAL

    def test_worst_case_fits_the_frame(self):
        worst = 0
        for d in (18, 30, 54, 70, 105, 115, 195, 205, 600):
            for heading, pitch, bank in attitudes(heading_step=15):
                worst = max(worst, draw(d, heading=heading, bank=bank, pitch=pitch).cycles)
        # One aircraft close and redrawing, one far and cached.
        state = State()
        draw(400, state=state)
        cached = draw(400, state=state).cycles
        self.assertLess((worst + cached) / self.SIM_FRAME_CYCLES, 0.20,
                        "worst single-plane frame is %d cycles" % worst)


if __name__ == "__main__":
    unittest.main()
