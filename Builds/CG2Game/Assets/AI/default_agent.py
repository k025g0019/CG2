# CG2 Python AI template
# 出力は必ず "x y z" の 3 つの数値にする。
# C++ 側はこの方向ベクトルを水平移動方向として受け取る。

from __future__ import annotations

import math
import sys


def normalize_xz(x: float, z: float) -> tuple[float, float]:
    length = math.sqrt(x * x + z * z)
    if length <= 0.0001:
        return 0.0, 0.0

    return x / length, z / length


def main() -> None:
    # argv:
    # 1 ownerX, 2 ownerY, 3 ownerZ
    # 4 targetX, 5 targetY, 6 targetZ
    # 7 deltaTime, 8 componentType
    owner_x = float(sys.argv[1])
    owner_z = float(sys.argv[3])
    target_x = float(sys.argv[4])
    target_z = float(sys.argv[6])

    direction_x, direction_z = normalize_xz(target_x - owner_x, target_z - owner_z)
    print(f"{direction_x} 0 {direction_z}")


if __name__ == "__main__":
    main()
