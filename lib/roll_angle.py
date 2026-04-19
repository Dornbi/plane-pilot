import enum
import re
from typing import Dict, Tuple, List

class RollAngle(enum.IntEnum):
    UNDEFINED = 255

    # Right
    R8 = 0
    R16U1 = 1
    R8U1 = 2
    R8U2 = 3
    R8U3 = 4
    R8U4 = 5
    R8U5 = 6
    R8U6 = 7
    R8U8 = 8
    R6U8 = 9
    R10U16 = 10
    R4U8 = 11
    R6U16 = 12
    R2U8 = 13
    R2U16 = 14
    
    # Up
    U8 = 15
    L2U16 = 16
    L2U8 = 17
    L6U16 = 18
    L4U8 = 19
    L10U16 = 20
    L6U8 = 21
    L8U8 = 22
    L8U6 = 23
    L8U5 = 24
    L8U4 = 25
    L8U3 = 26
    L8U2 = 27
    L8U1 = 28
    L16U1 = 29
    
    # Left
    L8 = 30
    L16D1 = 31
    L8D1 = 32
    L8D2 = 33
    L8D3 = 34
    L8D4 = 35
    L8D5 = 36
    L8D6 = 37
    L8D8 = 38
    L6D8 = 39
    L10D16 = 40
    L4D8 = 41
    L6D16 = 42
    L2D8 = 43
    L2D16 = 44
    
    # Down
    D8 = 45
    R2D16 = 46
    R2D8 = 47
    R6D16 = 48
    R4D8 = 49
    R10D16 = 50
    R6D8 = 51
    R8D8 = 52
    R8D6 = 53
    R8D5 = 54
    R8D4 = 55
    R8D3 = 56
    R8D2 = 57
    R8D1 = 58
    R16D1 = 59

    def _get_vector_data() -> Dict['RollAngle', Tuple[int, int, int]]:
        """
        Returns a dict mapping RollAngle -> (dx, dy, period).

        Returns:
            Dict[RollAngle, Tuple[int, int, int]]: The mapping.
        """
        return {
            # East
            RollAngle.R8: (8, 0, 1),
            # Northeast-East
            RollAngle.R16U1: (16, -1, 16),
            RollAngle.R8U1: (8, -1, 8),
            RollAngle.R8U2: (8, -2, 4),
            RollAngle.R8U3: (8, -3, 8),
            RollAngle.R8U4: (8, -4, 2),
            RollAngle.R8U5: (8, -5, 8),
            RollAngle.R8U6: (8, -6, 4),
            # Northeast
            RollAngle.R8U8: (8, -8, 1),
            # North-Northeast
            RollAngle.R6U8: (6, -8, 4),
            RollAngle.R10U16: (10, -16, 8),
            RollAngle.R4U8: (4, -8, 2),
            RollAngle.R6U16: (6, -16, 8),
            RollAngle.R2U8: (2, -8, 4),
            RollAngle.R2U16: (2, -16, 8),
            # North
            RollAngle.U8: (0, -8, 1),
            # Northwest-North
            RollAngle.L2U16: (-2, -16, 8),
            RollAngle.L2U8: (-2, -8, 4),
            RollAngle.L6U16: (-6, -16, 8),
            RollAngle.L4U8: (-4, -8, 2),
            RollAngle.L10U16: (-10, -16, 8),
            RollAngle.L6U8: (-6, -8, 4),
            # Northwest
            RollAngle.L8U8: (-8, -8, 1),
            # West-Northwest
            RollAngle.L8U6: (-8, -6, 4),
            RollAngle.L8U5: (-8, -5, 8),
            RollAngle.L8U4: (-8, -4, 2),
            RollAngle.L8U3: (-8, -3, 8),
            RollAngle.L8U2: (-8, -2, 4),
            RollAngle.L8U1: (-8, -1, 8),
            RollAngle.L16U1: (-16, -1, 16),
            # West
            RollAngle.L8: (-8, 0, 1),
            # Southwest-West
            RollAngle.L16D1: (-16, 1, 16),
            RollAngle.L8D1: (-8, 1, 8),
            RollAngle.L8D2: (-8, 2, 4),
            RollAngle.L8D3: (-8, 3, 8),
            RollAngle.L8D4: (-8, 4, 2),
            RollAngle.L8D5: (-8, 5, 8),
            RollAngle.L8D6: (-8, 6, 4),
            # Sourhwest
            RollAngle.L8D8: (-8, 8, 1),
            # South-Southwest
            RollAngle.L6D8: (-6, 8, 4),
            RollAngle.L10D16: (-10, 16, 8),
            RollAngle.L4D8: (-4, 8, 2),
            RollAngle.L6D16: (-6, 16, 8),
            RollAngle.L2D8: (-2, 8, 4),
            RollAngle.L2D16: (-2, 16, 8),
            # South
            RollAngle.D8: (0, 8, 1),
            # Southeast-South
            RollAngle.R2D16: (2, 16, 8),
            RollAngle.R2D8: (2, 8, 4),
            RollAngle.R6D16: (6, 16, 8),
            RollAngle.R4D8: (4, 8, 2),
            RollAngle.R10D16: (10, 16, 8),
            RollAngle.R6D8: (6, 8, 4),
            # Southeast
            RollAngle.R8D8: (8, 8, 1),
            # East-Southeast
            RollAngle.R8D6: (8, 6, 4),
            RollAngle.R8D5: (8, 5, 8),
            RollAngle.R8D4: (8, 4, 2),
            RollAngle.R8D3: (8, 3, 8),
            RollAngle.R8D2: (8, 2, 4),
            RollAngle.R8D1: (8, 1, 8),
            RollAngle.R16D1: (16, 1, 16),
        }

    @classmethod
    def from_vector(cls, x: int, y: int) -> 'RollAngle':
        """
        Returns the RollAngle enum member best matching the vector (x, y).

        Uses integer arithmetic to maximize cos(theta) = (v . t) / (|v| * |t|).

        Args:
            x (int): X component of vector.
            y (int): Y component of vector.

        Returns:
            RollAngle: The best matching RollAngle.

        Raises:
            ValueError: If the vector (0, 0) is passed or no match found.
        """
        if x == 0 and y == 0:
             raise ValueError("Vector (0, 0) is undefined")
             
        if not hasattr(cls, '_vector_data'):
             cls._vector_data = cls._get_vector_data()
        
        best_angle = RollAngle.UNDEFINED
        # We compare (dot^2) / len_sq
        # Store numerator and denominator
        best_n = -1 
        best_d = 1  
        
        for angle, data in cls._vector_data.items():
            tx, ty = data[:2]
            dot = x * tx + y * ty
            
            # We assume we are looking for the "nearest" angle, but definitely within +/- 90 degrees.
            # If dot product is <= 0, the angle difference is >= 90 degrees.
            if dot <= 0: continue 
            
            len_sq = tx * tx + ty * ty
            n = dot * dot
            d = len_sq
            
            # Compare n/d with best_n/best_d
            # n * best_d > best_n * d
            
            if best_angle == RollAngle.UNDEFINED or (n * best_d > best_n * d):
                best_angle = angle
                best_n = n
                best_d = d
                
        if best_angle == RollAngle.UNDEFINED:
             raise ValueError(f"No suitable RollAngle found for vector ({x}, {y})")
             
        return best_angle

    @classmethod
    def from_string(cls, roll_str: str) -> 'RollAngle':
        """
        Parses a roll string (e.g. 'r8u1', 'l16d2') into a RollAngle.

        Format: [l|r][number][u|d][number] (case insensitive).
        Delegates to from_vector() for fuzzy matching.

        Args:
            roll_str (str): The roll string to parse.

        Returns:
            RollAngle: The matched RollAngle.

        Raises:
            ValueError: If string format is invalid.
        """
        s = roll_str.lower().strip()
        
        # Parse tokens
        tokens = re.findall(r'([rlud])(\d+)', s)
        if not tokens:
             # Try simple direction with implicit magnitude? 
             # No, standard format is letter+number. 
             # Except maybe just "r8" -> [('r', '8')]
             pass
             
        dx = 0
        dy = 0
        
        valid_parse = False
        
        for direction, magnitude in tokens:
            val = int(magnitude)
            if direction == 'r':
                dx += val
            elif direction == 'l':
                dx -= val
            elif direction == 'u':
                dy -= val
            elif direction == 'd':
                dy += val
            valid_parse = True
            
        if not valid_parse:
            raise ValueError(f"Could not parse roll string: {roll_str}")
            
        return cls.from_vector(dx, dy)

    def get_vector(self) -> Tuple[int, int]:
        """
        Returns the (dx, dy) vector for this roll angle.

        Returns:
            Tuple[int, int]: The vector (dx, dy).
        """
        if not hasattr(self.__class__, '_vector_data'):
             self.__class__._vector_data = self.__class__._get_vector_data()
        return self.__class__._vector_data[self][:2]

    def get_normal(self) -> Tuple[int, int]:
        """
        Returns the (nx, ny) normal vector pointing to the Sky as integers.
        Based on logic: V=(dx, dy) -> N=(dy, -dx).

        Returns:
            Tuple[int, int]: The integer normal vector (nx, ny).
        """
        dx, dy = self.get_vector()
        return (dy, -dx)

    def period(self) -> int:
        """
        Returns the hardwired period for this roll angle.
        Period = max(|dx|, |dy|) / gcd(|dx|, |dy|).

        Returns:
            int: The period.
        """
        if not hasattr(self.__class__, '_vector_data'):
             self.__class__._vector_data = self.__class__._get_vector_data()
        return self.__class__._vector_data[self][2]

    def get_shift(self) -> int:
        """
        Returns the shift value (log2 of major axis magnitude).

        Returns:
            int: The shift.
        """
        dx, dy = self.get_vector()
        if (dx == 16 or dx == -16 or dy == 16 or dy == -16):
            return 4
        return 3

    def to_string(self) -> str:
        """
        Returns the string representation (e.g. 'r8u1').

        Returns:
            str: The string representation.
        """
        return self.name.lower()

    @classmethod
    def all_rolls(cls) -> List['RollAngle']:
        """
        Returns a list of all RollAngle members.

        Returns:
            List[RollAngle]: List of all enum members.
        """
        return [r for r in cls if r != cls.UNDEFINED]
