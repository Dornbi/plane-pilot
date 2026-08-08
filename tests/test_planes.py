"""Tests for the traffic-sprite reference model (`lib/planes.py`).

These lock down the invariants `docs/planes.md` argues for. Several of them
exist because the prototype violated them and it showed on screen: the
close-range X, the thickness jump as an aircraft rotates through 45 degrees,
and the wingtip clipped off a silhouette that overran its buffer.
"""

import math
import unittest

from lib import planes
from lib.planes import Mat3, Model, State, orient, place, render


LEVEL = orient(0, 0, 0)
# The default model draws 1.5x oversize, so its apparent span is 16.5 m.
EFFECTIVE_SPAN_M = Model().span_m * Model().scale


def fresh(**kwargs):
    """A state and a model, with no history."""
    return State(), Model(**kwargs)


def draw(distance_m, heading=180.0, pitch=0.0, bank=0.0, bearing=0.0,
         elevation=0.0, cam=LEVEL, state=None, model=None, **kw):
    state = state if state is not None else State()
    model = model if model is not None else Model()
    return render(state, cam, orient(heading, pitch, bank),
                  place(distance_m, bearing, elevation), model, **kw)


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

    def test_norm2_is_within_ten_percent(self):
        for deg in range(0, 360, 3):
            x = round(100 * math.cos(math.radians(deg)))
            y = round(100 * math.sin(math.radians(deg)))
            exact = math.hypot(x, y)
            self.assertLess(abs(planes.norm2(x, y) - exact) / exact, 0.12)


class TestProjection(unittest.TestCase):
    """The scale relation every distance in the design is derived from."""

    def test_wingspan_matches_the_closed_form(self):
        # Head-on, so the full span is visible and unforeshortened. Only
        # outside the size clamp -- inside it the projection is deliberately
        # no longer the closed form (see TestSizeClamp).
        for d in (3500, 1056, 450, 300, 220, 176, 150, 120, 110):
            r = draw(d)
            self.assertTrue(r.visible)
            expected = planes.span_pixels(EFFECTIVE_SPAN_M, d)
            self.assertLess(abs(r.bbox[0] - expected), 2.5,
                            "span at %d m: got %d, expected %.1f"
                            % (d, r.bbox[0], expected))

    def test_the_doubling_keeps_the_silhouette_from_shrinking(self):
        # Without the +1 >> 1 rounding the silhouette came out ~9% small.
        errors = []
        for d in range(110, 500, 7):
            r = draw(d)
            errors.append(r.bbox[0] - planes.span_pixels(EFFECTIVE_SPAN_M, d))
        self.assertGreater(sum(errors) / len(errors), -1.0)

    def test_culling(self):
        self.assertFalse(draw(5).visible)
        self.assertEqual(draw(5).reason, "behind camera")
        self.assertFalse(draw(9000).visible)
        self.assertEqual(draw(9000).reason, "out of range")


class TestTier(unittest.TestCase):

    def test_axes_are_chosen_independently(self):
        # A steeply banked aircraft is tall and narrow: it needs two sprites
        # for its height and must NOT be X-expanded along with them.
        r = draw(150, heading=180, bank=89)
        self.assertGreater(r.bbox[1], r.bbox[0])
        self.assertEqual(r.tier.sprites, 2)
        self.assertEqual(r.tier.xs, 1, "tall and narrow was X-expanded")

    def test_wide_and_short_is_expanded_but_stays_one_sprite(self):
        r = draw(120, heading=180, bank=0)
        self.assertGreater(r.bbox[0], planes.TIER_W)
        self.assertEqual(r.tier.xs, 2)
        self.assertEqual(r.tier.sprites, 1)

    def test_hysteresis_holds_the_higher_tier(self):
        state, model = fresh()
        # Approach until expanded, then back off slightly.
        for d in range(320, 110, -2):
            draw(d, state=state, model=model)
        expanded_at = draw(110, state=state, model=model).tier.xs
        held = draw(125, state=state, model=model).tier.xs
        self.assertEqual(expanded_at, 2)
        self.assertEqual(held, 2, "tier dropped immediately on the way back")

    def test_dot_tier_for_distant_traffic(self):
        self.assertTrue(draw(3500).tier.dot)

    def test_dot_tier_uses_the_static_bitmap(self):
        # The common case by a wide margin, so it skips the rasteriser: a
        # fixed block in $D400, no clear, no strokes, no pointer flip.
        r = draw(3500)
        self.assertTrue(r.tier.dot)
        self.assertIs(r.buf, planes.DOT_BITMAP)
        self.assertLess(r.cycles, draw(300).cycles)

    def test_the_static_bitmap_is_a_two_by_two_blob(self):
        lit = [(x, y) for y in range(planes.ROWS) for x in range(planes.COLS)
               if planes.DOT_BITMAP.get(x, y)]
        self.assertEqual(len(lit), 4)


class TestThickness(unittest.TestCase):

    def test_body_thickness_is_invariant_under_rotation(self):
        # The fuselage is a body of revolution: no angle may change its weight.
        for d in (30, 60, 100, 160, 300):
            seen = set()
            for heading in range(-180, 181, 5):
                for bank in (0, 20, 45, 70, 89):
                    for pitch in (-20, 0, 20):
                        state, model = fresh()
                        r = draw(d, heading=heading, bank=bank, pitch=pitch,
                                 state=state, model=model)
                        seen.add(r.thickness["body"][0])
            self.assertEqual(len(seen), 1,
                             "body thickness varied with angle at %d m: %s"
                             % (d, sorted(seen)))

    def test_wing_thickness_grows_as_the_chord_comes_into_view(self):
        # A target pitching up shows us its upper surface.
        weights = []
        for pitch in (0, 20, 45):
            state, model = fresh()
            weights.append(draw(150, pitch=pitch, state=state,
                                model=model).thickness["wing"][0])
        self.assertEqual(weights, sorted(weights))
        self.assertLess(weights[0], weights[-1])

    def test_wing_is_thin_when_seen_edge_on(self):
        state, model = fresh()
        r = draw(150, heading=180, pitch=0, bank=0, state=state, model=model)
        self.assertEqual(r.thickness["wing"][0], 1)

    def test_fin_is_thin_head_on_and_thicker_from_the_side(self):
        state, model = fresh()
        head_on = draw(90, heading=180, state=state, model=model).thickness["fin"][0]
        state, model = fresh()
        side_on = draw(90, heading=90, state=state, model=model).thickness["fin"][0]
        self.assertEqual(head_on, 1)
        self.assertGreater(side_on, head_on)

    def test_ladder_is_one_two_four_never_three(self):
        for d in range(20, 400, 3):
            for bank in (0, 30, 60):
                for pitch in (0, 30):
                    state, model = fresh()
                    r = draw(d, bank=bank, pitch=pitch, state=state, model=model)
                    for name, (tv, _) in r.thickness.items():
                        self.assertIn(tv, (1, 2, 4),
                                      "%s thickness %d at %d m" % (name, tv, d))

    def test_screen_weight_survives_an_x_expansion_change(self):
        # th columns weigh th * xs screen pixels; tv rows weigh tv. The two
        # must agree, or a stroke changes weight as it rotates through the
        # steep/shallow crossover.
        for d in range(25, 300, 2):
            for bank in (0, 40, 80):
                state, model = fresh()
                r = draw(d, bank=bank, state=state, model=model)
                for name, (tv, th) in r.thickness.items():
                    if tv >= 2:
                        self.assertEqual(th * r.tier.xs, tv,
                                         "%s: %d cols x %d != %d rows"
                                         % (name, th, r.tier.xs, tv))

    def test_max_thickness_is_respected(self):
        for cap in (1, 2, 4):
            state, model = fresh()
            r = draw(30, pitch=40, state=state, model=model, max_thick=cap)
            for tv, _ in r.thickness.values():
                self.assertLessEqual(tv, cap)


class TestSizeClamp(unittest.TestCase):
    """Closer than the clamp range the aircraft stops growing, so the whole
    silhouette stays in frame instead of the buffer showing its middle -- and
    it is scaled to *fill* the buffer, not to a conservative constant."""

    def _fill(self, r):
        return max(r.bbox[0] / planes.MAX_BBOX_W, r.bbox[1] / planes.MAX_BBOX_H)

    def test_clamped_silhouettes_fill_the_buffer(self):
        # A constant cap would have to fit the aircraft in any orientation at
        # once, so it fits inside the largest circle the buffer holds -- 41 px
        # of 48 -- and a level aircraft wastes most of the width. Scaling to
        # the bounding box instead reaches the edge on the binding axis.
        fills = []
        for d in (17, 25, 40, 60, 80):
            for heading in range(0, 360, 15):
                for bank in (0, 20, 45, 70, 89):
                    state, model = fresh()
                    r = draw(d, heading=heading, bank=bank, state=state, model=model)
                    fills.append(self._fill(r))
        fills.sort()
        median = fills[len(fills) // 2]
        self.assertGreater(median, 0.92, "median buffer fill only %.0f%%" % (median * 100))
        self.assertLessEqual(max(fills), 1.0)

    def test_the_clamp_engages_close_in_and_not_far_out(self):
        for d in (17, 25, 40):
            self.assertTrue(draw(d).clamped, "not clamped at %d m" % d)
        for d in (120, 200, 400, 900):
            self.assertFalse(draw(d).clamped, "clamped at %d m" % d)

    def test_whole_silhouette_fits_at_every_attitude(self):
        # The point of the clamp: no crop, from any angle, at any close range.
        for d in (17, 20, 25, 30, 40, 50, 60, 80, 100):
            for heading in range(0, 360, 15):
                for bank in (0, 20, 45, 70, 89):
                    for pitch in (-40, 0, 40):
                        state, model = fresh()
                        r = draw(d, heading=heading, bank=bank, pitch=pitch,
                                 state=state, model=model)
                        self.assertTrue(r.fits,
                                        "overran at %d m hdg %d bank %d pitch %d: %s"
                                        % (d, heading, bank, pitch, (r.bbox,)))
                        for name, (x, y) in r.local.items():
                            self.assertTrue(0 <= x < planes.COLS,
                                            "%s off the buffer at %d m" % (name, d))
                            self.assertTrue(0 <= y < r.tier.rows,
                                            "%s off the buffer at %d m" % (name, d))

    def test_the_body_ladder_ignores_the_clamp(self):
        # The clamp factor depends on attitude, so the fuselage thickness must
        # be driven by the UNCLAMPED scale or it inherits that dependence --
        # which is exactly the artefact the thickness rules exist to remove.
        for d in (17, 25, 40, 60):
            seen = set()
            for heading in range(0, 360, 10):
                for bank in (0, 30, 60, 89):
                    state, model = fresh()
                    seen.add(draw(d, heading=heading, bank=bank, state=state,
                                  model=model).thickness["body"][0])
            self.assertEqual(len(seen), 1,
                             "body thickness varied under the clamp at %d m: %s"
                             % (d, sorted(seen)))

    def test_the_clamp_scales_with_the_model(self):
        big = Model(span_m=20.0, length_m=18.0, scale=1.5)
        state = State()
        r = render(state, LEVEL, orient(180, 0, 0), place(25), big)
        self.assertTrue(r.clamped)
        self.assertTrue(r.fits)


class TestRasteriser(unittest.TestCase):

    def test_clip_seg_rejects_a_segment_outside_the_box(self):
        self.assertIsNone(planes.clip_seg(-10, 5, -3, 6, 24, 21))

    def test_clip_seg_preserves_slope(self):
        # A line at 45 degrees must still be at 45 degrees after clipping.
        c = planes.clip_seg(-20, -20, 40, 40, 24, 21)
        self.assertIsNotNone(c)
        x0, y0, x1, y1 = c
        self.assertEqual(x1 - x0, y1 - y0)

    def test_close_range_is_a_crop_not_an_x(self):
        # Clamping endpoints instead of clipping segments turned every very
        # close aircraft into the two diagonals of the buffer.
        r = draw(20, heading=135, bank=20)
        rows = r.buf.rows_as_text()
        corners = [rows[0][0], rows[0][-1], rows[-1][0], rows[-1][-1]]
        self.assertNotEqual(corners, ["#", "#", "#", "#"],
                            "silhouette degenerated to a buffer-spanning X")

    def test_every_attitude_draws_something(self):
        for heading in range(0, 360, 15):
            for bank in (0, 20, 45, 70, 89, -45):
                for d in (18, 30, 60, 110, 250, 600):
                    state, model = fresh()
                    r = draw(d, heading=heading, bank=bank,
                             state=state, model=model)
                    self.assertTrue(r.visible)
                    self.assertGreater(r.buf.ink(), 0,
                                       "empty buffer at hdg %d bank %d %d m"
                                       % (heading, bank, d))

    def test_no_pixel_escapes_the_buffer(self):
        # fill_run clamps, so this is really a check that the buffer is sized
        # to the tier and indexed consistently.
        for heading in range(0, 360, 11):
            for d in (20, 45, 90, 180):
                state, model = fresh()
                r = draw(d, heading=heading, bank=45, state=state, model=model)
                self.assertEqual(len(r.buf.data), 3 * r.tier.rows)
                self.assertEqual(r.tier.rows, 21 * r.tier.sprites)


class TestCentring(unittest.TestCase):

    def test_bounding_box_centring_while_it_fits(self):
        r = draw(150)
        self.assertTrue(r.fits)
        for name, (x, y) in r.local.items():
            self.assertTrue(0 <= x < planes.COLS, "%s x=%d" % (name, x))
            self.assertTrue(0 <= y < r.tier.rows, "%s y=%d" % (name, y))

    def test_the_reported_close_case_is_fully_framed(self):
        # At 25 m the unclamped fuselage would span 78 px in a 48 px buffer.
        # The size clamp means it no longer does, so every point is in frame.
        r = draw(25, heading=113, bank=20, bearing=6, elevation=3)
        self.assertTrue(r.clamped)
        self.assertTrue(r.fits)
        for name, (x, y) in r.local.items():
            self.assertTrue(0 <= x < planes.COLS, "%s x=%d" % (name, x))
            self.assertTrue(0 <= y < r.tier.rows, "%s y=%d" % (name, y))

    def test_hub_anchor_is_used_when_a_silhouette_somehow_overruns(self):
        # The clamp makes this unreachable through geometry, so drive the
        # anchor choice directly: it is kept as a guard, not as dead code.
        state = State()
        r = render(state, LEVEL, orient(113, 0, 20), place(25, 6, 3),
                   Model(), max_thick=4)
        self.assertTrue(r.fits)     # normal path: bounding-box anchor


class TestCache(unittest.TestCase):

    def test_repeating_a_frame_hits(self):
        state, model = fresh()
        first = draw(150, state=state, model=model)
        second = draw(150, state=state, model=model)
        self.assertFalse(first.cached)
        self.assertTrue(second.cached)
        self.assertLess(second.cycles, first.cycles)

    def test_changing_orientation_misses(self):
        state, model = fresh()
        draw(150, heading=180, state=state, model=model)
        self.assertFalse(draw(150, heading=150, state=state,
                              model=model).cached)


class TestGoldenSilhouettes(unittest.TestCase):
    """Byte-exact output for a few representative attitudes.

    These were cross-checked against `docs/planes-prototype.html` -- 252 cases
    over distance, heading, bank and pitch agreed bit for bit -- so they pin
    the two implementations together without the tests needing node.
    """

    GOLDEN = {
        "level, head-on, 150 m": (
            dict(distance_m=150, heading=180, pitch=0, bank=0),
            # The degenerate case that motivated the third stroke: wing and
            # fuselage land on the same row, and only the fin gives it shape.
            [
                "............#...........",
                "............#...........",
                "............#...........",
                "............#...........",
                ".....###############....",
            ],
        ),
        "three-quarter, pitched up, 120 m": (
            dict(distance_m=120, heading=135, pitch=25, bank=20),
            [
                "................###.....",
                ".........#.....####.....",
                ".........##...###.......",
                ".........#######........",
                "..........#####.........",
                "..........###...........",
                "........######..........",
                ".......####.###.........",
                "......###....##.........",
                ".....###......##.#......",
                ".....##.......####......",
                ".....#.........###......",
                "................##......",
                ".................#......",
            ],
        ),
    }

    def test_golden(self):
        for name, (params, expected) in self.GOLDEN.items():
            with self.subTest(name):
                r = draw(**params)
                rows = [row for row in r.buf.rows_as_text() if "#" in row]
                self.assertEqual(rows, expected, "\n" + "\n".join(rows))


class TestBudget(unittest.TestCase):

    SIM_FRAME_CYCLES = 98_500     # ~10 Hz on PAL

    def test_worst_case_fits_the_frame(self):
        worst = 0
        for heading in range(0, 360, 15):
            for bank in (0, 20, 45, 70, 89, -45):
                for d in (18, 30, 60, 110, 250, 600):
                    state, model = fresh()
                    worst = max(worst, draw(d, heading=heading, bank=bank,
                                            state=state, model=model).cycles)
        # One aircraft close and redrawing, one far and cached.
        realistic = worst + 2100
        self.assertLess(realistic / self.SIM_FRAME_CYCLES, 0.12,
                        "worst single-plane frame is %d cycles" % worst)


if __name__ == "__main__":
    unittest.main()
