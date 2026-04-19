import unittest
import sys
import os

# Ensure lib is in path
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from lib.find_boxes import find_box
from lib.roll_angle import RollAngle

class TestSuperbox(unittest.TestCase):
  def setUp(self):
    # Create a dummy screen_ram (40x25 characters)
    # 0 = Ground, 1 = Sky, 2 = Grad1
    self.screen_ram = [1] * 1000
    self.color_ram = [6] * 1000 # Default to Sky
    # Fill a 10x10 area with a identifiable repeating pattern
    # Range: Cols 20-29, Rows 10-19
    for y in range(10, 20):
        for x in range(20, 30):
            # Checkerboard pattern: 5 if (x+y) even, 6 if odd
            self.screen_ram[y * 40 + x] = 5 + ((x + y) % 2)
            
    self.center_x = 160 # char 20
    self.center_y = 96  # pixel 96 -> char 12
    
  def test_horizontal_major_expansion(self):
    # R8 is horizontal major, dx=8, dy=0, period=1
    roll = RollAngle.R8
    
    # Standard box (min_box_width=1)
    box1 = find_box(self.screen_ram, self.color_ram, roll, self.center_x, self.center_y, min_box_width=1)
    self.assertEqual(box1['w'], 1)
    # The pattern in col 20 spans rows 10..19
    self.assertEqual(box1['h'], 10)
    self.assertEqual(box1['step_x'], 1)
    self.assertEqual(box1['step_y'], 0)
    self.assertEqual(len(box1['chars']), 10)
    # Pattern in col 20 (x=20) is: 5+((20+10)%2), 5+((20+11)%2)...
    # 20+10=30 (even) -> 5
    # 20+11=31 (odd) -> 6
    expected = [(5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False)]
    self.assertEqual(box1['chars'], expected)
    
    # Expanded box (min_box_width=4)
    # k = (4 + 1 - 1) // 1 = 4
    box4 = find_box(self.screen_ram, self.color_ram, roll, self.center_x, self.center_y, min_box_width=4)
    self.assertEqual(box4['w'], 4)
    self.assertEqual(box4['step_x'], 4) # 1 * 4 = 4
    self.assertEqual(box4['step_y'], 0)
    # h remains 10. len = 4 * 10 = 40.
    self.assertEqual(box4['h'], 10)
    self.assertEqual(len(box4['chars']), 40)
    
    # Let's check first row of chars (Row 10 of scan)
    # x=20, y=10: 20+10=30 (even) -> 5
    # x=21, y=10: 21+10=31 (odd) -> 6
    # Row 10: [5, 6, 5, 6]
    self.assertEqual(box4['chars'][:4], [(5, False), (6, False), (5, False), (6, False)])
    # Row 11: [6, 5, 6, 5]
    self.assertEqual(box4['chars'][4:8], [(6, False), (5, False), (6, False), (5, False)])

  def test_vertical_major_expansion(self):
    # U8 is vertical major, dx=0, dy=-8, period=1
    roll = RollAngle.U8
    
    # Standard box (min_box_width=1)
    box1 = find_box(self.screen_ram, self.color_ram, roll, self.center_x, self.center_y, min_box_width=1)
    self.assertEqual(box1['h'], 1)
    # The pattern in row 12 spans cols 20..29
    self.assertEqual(box1['w'], 10)
    self.assertEqual(box1['step_x'], 0)
    self.assertEqual(box1['step_y'], -1)
    self.assertEqual(len(box1['chars']), 10)
    # Row 12 is 5, 6, 5, 6...
    expected = [(5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False)]
    self.assertEqual(box1['chars'], expected)
    
    # Expanded box (min_box_width=3)
    # k = 3
    box3 = find_box(self.screen_ram, self.color_ram, roll, self.center_x, self.center_y, min_box_width=3)
    self.assertEqual(box3['h'], 3)
    self.assertEqual(box3['step_x'], 0)
    self.assertEqual(box3['step_y'], -3)
    # w remains 10. len = 3 * 10 = 30.
    self.assertEqual(box3['w'], 10)
    self.assertEqual(len(box3['chars']), 30)
    
    # Row 12: [5, 6, 5, 6, 5, 6, 5, 6, 5, 6]  (20+12=32 even)
    # Row 11: [6, 5, 6, 5, 6, 5, 6, 5, 6, 5]  (20+11=31 odd)
    # Row 10: [5, 6, 5, 6, 5, 6, 5, 6, 5, 6]  (20+10=30 even)
    self.assertEqual(box3['chars'][:10], [(5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False)])
    self.assertEqual(box3['chars'][10:20], [(6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False)])
    self.assertEqual(box3['chars'][20:30], [(5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False), (5, False), (6, False)])

  def test_no_expansion_if_already_wide_enough(self):
    # R16U1 has period 16
    roll = RollAngle.R16U1
    box = find_box(self.screen_ram, self.color_ram, roll, self.center_x, self.center_y, min_box_width=4)
    self.assertEqual(box['w'], 16)
    self.assertEqual(box['step_x'], 16)
    self.assertEqual(box['step_y'], -1)

if __name__ == '__main__':
  unittest.main()
