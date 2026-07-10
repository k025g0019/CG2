# CG2 Python AI template
# 対象から離れる方向を返すサンプル。

from __future__ import annotations

import math
import sys


def normalize_xz(x: float, z: float) -> tuple[float, float]:
    length = math.sqrt(x * x + z * z)
    if length <= 0.0001:
        return 0.0, 0.0

    return x / length, z / length


def main() -> None:
    owner_x = float(sys.argv[1])
    owner_z = float(sys.argv[3])
    target_x = float(sys.argv[4])
    target_z = float(sys.argv[6])

    direction_x, direction_z = normalize_xz(owner_x - target_x, owner_z - target_z)
    print(f"{direction_x} 0 {direction_z}")


if __name__ == "__main__":
    main()
