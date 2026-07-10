# CG2 Python AI sensor template
# 互換形式: 0 または 1
# 詳細形式: 1|label=Target|confidence=0.8|distance=3.2|text=...|command=...

from __future__ import annotations

import math
import sys


def main() -> None:
    # argv:
    # 1 ownerX, 2 ownerY, 3 ownerZ
    # 4 targetX, 5 targetY, 6 targetZ
    # 7 range, 8 componentType
    owner_x = float(sys.argv[1])
    owner_z = float(sys.argv[3])
    target_x = float(sys.argv[4])
    target_z = float(sys.argv[6])
    view_range = float(sys.argv[7])

    distance = math.sqrt((target_x - owner_x) ** 2 + (target_z - owner_z) ** 2)
    detected = 1 if distance <= view_range else 0
    confidence = max(0.0, min(1.0, 1.0 - distance / view_range)) if view_range > 0.0 else 0.0
    print(
        f"{detected}"
        f"|label=RangeTarget"
        f"|confidence={confidence:.3f}"
        f"|distance={distance:.3f}"
    )


if __name__ == "__main__":
    main()
