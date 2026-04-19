import unittest
from lib import roll_angle

class TestRollAnglePeriod(unittest.TestCase):
    def test_periods(self):
        # Case 1: Horizontal axis 0
        self.assertEqual(roll_angle.RollAngle.U8.period(), 1)
        self.assertEqual(roll_angle.RollAngle.D8.period(), 1)
        
        # Case 2: Vertical axis 0
        self.assertEqual(roll_angle.RollAngle.R8.period(), 1)
        self.assertEqual(roll_angle.RollAngle.L8.period(), 1)
        
        # Case 3: gcd = 1
        self.assertEqual(roll_angle.RollAngle.R8U1.period(), 8)
        self.assertEqual(roll_angle.RollAngle.R8U5.period(), 8)
        self.assertEqual(roll_angle.RollAngle.L16U1.period(), 16)
        
        # Case 4: gcd > 1
        # R8U2 -> dx=8, dy=-2, gcd=2, major=8. 8/2 = 4.
        self.assertEqual(roll_angle.RollAngle.R8U2.period(), 4)
        # R6U8 -> dx=6, dy=-8, gcd=2, major=8. 8/2 = 4.
        self.assertEqual(roll_angle.RollAngle.R6U8.period(), 4)
        # R8U4 -> dx=8, dy=-4, gcd=4, major=8. 8/4 = 2.
        self.assertEqual(roll_angle.RollAngle.R8U4.period(), 2)
        
        # Case 5: Diagonal
        self.assertEqual(roll_angle.RollAngle.R8U8.period(), 1)
        
    def test_get_vector_still_works(self):
        dx, dy = roll_angle.RollAngle.R8U1.get_vector()
        self.assertEqual(dx, 8)
        self.assertEqual(dy, -1)

if __name__ == '__main__':
    unittest.main()
