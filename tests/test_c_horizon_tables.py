"""The checked-in C64 horizon tables against the Python model they come from.

c64o/chardefs.cc stores each character once per vertical flip, and
c64o/boxdefs.cc refers to it by index, with a per-box run of local characters
that box_prepare() copies upside down, packed into one byte around
grad1_color_start (lib/find_boxes.py build_c_charset() and
compute_box_layout()). The generator checks its own output when it runs; this
checks the files as committed, without running it, and from the other end: it
decodes every box the way box.cc does and compares what each cell would show
with lib/boxdefs.py and lib/chardefs.py, which the generator writes alongside
and which keep the global character ids.

Per cell, not per local character, on purpose: the generator reorders each
box's local characters so the flipped ones form one run, and what has to hold
is that every cell still shows the same bytes in the same colour.
"""

import os
import re
import unittest

from lib import boxdefs, chardefs
from lib.verify_defs import parse_chardefs_c

C64O = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "c64o")


def _parse_boxdefs_c():
    with open(os.path.join(C64O, "boxdefs.cc")) as f:
        content = f.read()
    arrays = {m.group(1): [int(x) for x in m.group(2).split(",") if x.strip()]
              for m in re.finditer(
                  r"static const uint8_t (box_\w+)\[\] = \{([^}]*)\};", content)}
    boxes = {}
    for m in re.finditer(r"static const boxdef_t (box_\w+)_def = \{(.*?)\};",
                         content, re.DOTALL):
        fields = [f.strip() for f in re.sub(r"//.*", "", m.group(2)).split(",")
                  if f.strip()]
        name = m.group(1)
        grad1_start, flip_around = int(fields[7]), int(fields[9], 0)
        boxes[name] = {
            "w": int(fields[0]), "h": int(fields[1]),
            "total_size": int(fields[2]),
            "step_x": int(fields[3]), "step_y": int(fields[4]),
            "rel_x": int(fields[5]), "rel_y": int(fields[6]),
            "grad1_start": grad1_start, "char_count": int(fields[8]),
            # box_prepare()'s decoding of flip_around.
            "flip_start": grad1_start - (flip_around & 0x0F),
            "flip_end": grad1_start + (flip_around >> 4),
            "char_idx": arrays[fields[10]], "box_chars": arrays[fields[11]],
        }
    return boxes


def _c_cells(box, rows):
    """What each cell shows on the C64: a solid id, or (bytes, is_grad1)."""
    cells = []
    for v in box["box_chars"]:
        if v < 3:
            cells.append(("solid", v))
            continue
        i = v - 3
        char_bytes = rows[box["char_idx"][i]]
        if box["flip_start"] <= i < box["flip_end"]:
            char_bytes = bytes(reversed(char_bytes))
        cells.append((char_bytes, i >= box["grad1_start"]))
    return cells


def _py_cells(model):
    """The same, from a lib/boxdefs.py tuple and the global character set."""
    grad1_start, char_ids, grid = model[6], model[8], model[9]
    cells = []
    for v in grid:
        if v < 3:
            cells.append(("solid", v))
            continue
        i = v - 3
        cells.append((bytes(chardefs.ALL_CHARS[char_ids[i]]), i >= grad1_start))
    return cells


class TestCHorizonTables(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.rows = parse_chardefs_c(os.path.join(C64O, "chardefs.cc"))
        cls.boxes = _parse_boxdefs_c()
        cls.models = {name.lower(): getattr(boxdefs, name)
                      for name in dir(boxdefs) if name.startswith("BOX_")}

    def test_every_box_is_in_both(self):
        self.assertEqual(sorted(self.boxes), sorted(self.models))

    def test_a_byte_indexes_chardefs(self):
        # box_prepare() reads the index as a uint8_t.
        self.assertLessEqual(len(self.rows), 256)

    def test_stored_once_per_vertical_flip(self):
        # No row is another row, or another row upside down - otherwise the
        # dedup has missed something and the bytes are being paid for twice.
        seen = set()
        for row in self.rows:
            self.assertNotIn(row, seen)
            self.assertNotIn(bytes(reversed(row)), seen)
            seen.add(row)

    def test_fewer_rows_than_global_characters(self):
        self.assertLess(len(self.rows), len(chardefs.ALL_CHARS))

    def test_flip_range_is_inside_the_box(self):
        for name, box in self.boxes.items():
            with self.subTest(box=name):
                self.assertLessEqual(0, box["flip_start"])
                self.assertLessEqual(box["flip_start"], box["flip_end"])
                self.assertLessEqual(box["flip_end"], box["char_count"])

    def test_every_cell_shows_what_the_model_draws(self):
        for name, box in self.boxes.items():
            model = self.models[name]
            with self.subTest(box=name):
                self.assertEqual(
                    (box["w"], box["h"], box["step_x"], box["step_y"],
                     box["rel_x"], box["rel_y"]),
                    tuple(model[:6]))
                self.assertEqual(box["total_size"], box["w"] * box["h"])
                self.assertEqual(_c_cells(box, self.rows), _py_cells(model))


if __name__ == "__main__":
    unittest.main()
