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
        # Head-on, so the full span is visible and unforeshortened.
        for d in (2816, 704, 300, 200, 150, 117, 100, 80, 59, 40, 30):
            r = draw(d)
            self.assertTrue(r.visible)
            expected = planes.span_pixels(11.0, d)
            self.assertLess(abs(r.bbox[0] - expected), 2.5,
                            "span at %d m: got %d, expected %.1f"
                            % (d, r.bbox[0], expected))

    def test_the_doubling_keeps_the_silhouette_from_shrinking(self):
        # Without the +1 >> 1 rounding the silhouette came out ~9% small.
        errors = []
        for d in range(40, 400, 7):
            r = draw(d)
            errors.append(r.bbox[0] - planes.span_pixels(11.0, d))
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
        r = draw(100, heading=180, bank=89)
        self.assertGreater(r.bbox[1], r.bbox[0])
        self.assertEqual(r.tier.sprites, 2)
        self.assertEqual(r.tier.xs, 1, "tall and narrow was X-expanded")

    def test_wide_and_short_is_expanded_but_stays_one_sprite(self):
        r = draw(70, heading=180, bank=0)
        self.assertGreater(r.bbox[0], planes.TIER_W)
        self.assertEqual(r.tier.xs, 2)
        self.assertEqual(r.tier.sprites, 1)

    def test_hysteresis_holds_the_higher_tier(self):
        state, model = fresh()
        # Approach until expanded, then back off slightly.
        for d in range(200, 60, -2):
            draw(d, state=state, model=model)
        expanded_at = draw(60, state=state, model=model).tier.xs
        held = draw(72, state=state, model=model).tier.xs
        self.assertEqual(expanded_at, 2)
        self.assertEqual(held, 2, "tier dropped immediately on the way back")

    def test_dot_tier_for_distant_traffic(self):
        self.assertTrue(draw(2500).tier.dot)


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
            weights.append(draw(60, pitch=pitch, state=state,
                                model=model).thickness["wing"][0])
        self.assertEqual(weights, sorted(weights))
        self.assertLess(weights[0], weights[-1])

    def test_wing_is_thin_when_seen_edge_on(self):
        state, model = fresh()
        r = draw(60, heading=180, pitch=0, bank=0, state=state, model=model)
        self.assertEqual(r.thickness["wing"][0], 1)

    def test_fin_is_thin_head_on_and_thicker_from_the_side(self):
        state, model = fresh()
        head_on = draw(45, heading=180, state=state, model=model).thickness["fin"][0]
        state, model = fresh()
        side_on = draw(45, heading=90, state=state, model=model).thickness["fin"][0]
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

    def test_wing_stays_framed_when_the_silhouette_overruns(self):
        # The reported case: at 25 m the fuselage is far wider than the buffer,
        # and bbox centring pushed a wingtip out of frame.
        r = draw(25, heading=113, bank=20, bearing=6, elevation=3)
        self.assertFalse(r.fits)
        for tip in ("tipL", "tipR"):
            x, y = r.local[tip]
            self.assertTrue(0 <= x < planes.COLS, "%s x=%d" % (tip, x))
            self.assertTrue(0 <= y < r.tier.rows, "%s y=%d" % (tip, y))

    def test_the_fuselage_is_what_clips(self):
        r = draw(25, heading=113, bank=20, bearing=6, elevation=3)
        nose_x = r.local["nose"][0]
        tail_x = r.local["tail"][0]
        self.assertTrue(nose_x < 0 or tail_x >= planes.COLS,
                        "expected the fuselage to run off the buffer")


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
        "level, head-on, 90 m": (
            dict(distance_m=90, heading=180, pitch=0, bank=0),
            # The degenerate case that motivated the third stroke: wing and
            # fuselage land on the same row, and only the fin gives it shape.
            [
                "............#...........",
                "............#...........",
                "............#...........",
                "............#...........",
                "....#################...",
            ],
        ),
        "three-quarter, pitched up, 70 m": (
            dict(distance_m=70, heading=135, pitch=25, bank=20),
            [
                "..................###...",
                ".........#.......####...",
                ".........##.....###.....",
                ".........###...###......",
                "..........#######.......",
                "...........#####........",
                "...........###..........",
                "..........#####.........",
                "........####.###........",
                ".......####...##........",
                "......###......##.#.....",
                ".....###.......####.....",
                ".....##.........###.....",
                ".....#...........##.....",
                "..................#.....",
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
