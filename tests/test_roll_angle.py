import unittest
import sys
import os

from lib import roll_angle

# Add project root to path
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

class TestRollAngles(unittest.TestCase):
    def test_values_are_integers(self):
        # Verify it's an IntEnum
        self.assertIsInstance(roll_angle.RollAngle.R8, int)
        self.assertEqual(roll_angle.RollAngle.R8, 0) # First one
        self.assertEqual(roll_angle.RollAngle.R16D1, 59) # Last one

    def test_from_vector_exact(self):
        # Test exact vector conversions (should still work)
        self.assertEqual(roll_angle.RollAngle.from_vector(8, 0), roll_angle.RollAngle.R8)
        self.assertEqual(roll_angle.RollAngle.from_vector(8, -1), roll_angle.RollAngle.R8U1)
        self.assertEqual(roll_angle.RollAngle.from_vector(0, -8), roll_angle.RollAngle.U8)
        self.assertEqual(roll_angle.RollAngle.from_vector(-16, 1), roll_angle.RollAngle.L16D1)
        self.assertEqual(roll_angle.RollAngle.from_vector(0, 8), roll_angle.RollAngle.D8)

    def test_from_vector_fuzzy(self):
        # Test fuzzy vector conversions
        
        # (100, -1) -> Angle approx -0.57 deg.
        # R8 (0 deg), R16U1 (-3.58 deg).
        # Should match R8.
        self.assertEqual(roll_angle.RollAngle.from_vector(100, -1), roll_angle.RollAngle.R8)
        
        # (10, -1) -> Angle approx -5.7 deg.
        # R8 (0 deg), R16U1 (-3.58 deg), R8U1 (-7.1 deg).
        # Dist to R16U1: 2.12 deg.
        # Dist to R8U1: 1.4 deg.
        # Should match R8U1.
        self.assertEqual(roll_angle.RollAngle.from_vector(10, -1), roll_angle.RollAngle.R8U1)
        
        # Scaled vectors should map to same angle
        # R8 is (8, 0). (100, 0) should be R8.
        self.assertEqual(roll_angle.RollAngle.from_vector(100, 0), roll_angle.RollAngle.R8)
        
        # (16, -2) is exactly (8, -1) scaled by 2. Should be R8U1.
        self.assertEqual(roll_angle.RollAngle.from_vector(16, -2), roll_angle.RollAngle.R8U1)

    def test_from_vector_invalid_direction(self):
        # Vectors strictly >= 90 degrees away should fail
        # R8 is (8, 0). (-1, 0) is 180 degrees away.
        # But wait, looking at the code, it iterates ALL angles.
        # It skips if dot product <= 0.
        # So (-1, 0) will have dot <= 0 with ALL R* vectors (which have +x).
        # But it might map to L8 (-8, 0)!
        self.assertEqual(roll_angle.RollAngle.from_vector(-1, 0), roll_angle.RollAngle.L8)
        
        # What doesn't match anything? 
        # (0, 0) is undefined.
        with self.assertRaises(ValueError):
            roll_angle.RollAngle.from_vector(0, 0)
            
    def test_all_members_covered(self):
        # Sanity check that the data map covers all members
        mapping = roll_angle.RollAngle._get_vector_data()
        # Exclude UNDEFINED which is not in the map
        self.assertEqual(len(mapping), len(roll_angle.RollAngle) - 1)

    def test_all_rolls_excludes_undefined(self):
        # Verify all_rolls() returns everything EXCEPT UNDEFINED
        rolls = roll_angle.RollAngle.all_rolls()
        self.assertNotIn(roll_angle.RollAngle.UNDEFINED, rolls)
        self.assertEqual(len(rolls), len(roll_angle.RollAngle) - 1)

if __name__ == '__main__':
    unittest.main()
