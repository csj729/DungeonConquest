"""C++ 구조 상수가 데이터·설계와 어긋나지 않는지 대조.

`core/include/dc/config.h`에는 컴파일 타임에 필요한 값이 몇 개 있다. 이건
"밸런스 수치는 data/*.json이 단일 진실 원천"의 **예외**이므로, 예외인 만큼
자동 대조를 걸어둔다. 두 곳에 사는 값은 한쪽만 고치는 사고가 반드시 난다.
"""
import re
from pathlib import Path

import gamedata as gd

CONFIG_H = Path(__file__).resolve().parent.parent / "core" / "include" / "dc" / "config.h"


def read_constants():
    src = CONFIG_H.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r"constexpr\s+\w+\s+(\w+)\s*=\s*(\d+)\s*;", src):
        out[m.group(1)] = int(m.group(2))
    return out


def report():
    c = read_constants()
    prog = gd.load("progression")
    spawn = gd.load("spawn")

    checks = [
        ("TICK_HZ", c.get("TICK_HZ"), prog["tick_hz"], "progression.json:tick_hz"),
        ("SPAWN_DIRECTIONS", c.get("SPAWN_DIRECTIONS"), spawn["directions"],
         "spawn.json:directions"),
    ]

    ok = True
    for name, got, want, src in checks:
        good = got == want
        ok &= good
        print(f"  {'OK ' if good else 'X  '} {name:<18} config.h={got}  {src}={want}")

    # 용량은 데이터가 아니라 설계 방침(§11)이다. 피크 물량을 담을 수 있는지만 본다.
    cap = c.get("MAX_ENTITIES")
    peak = spawn["concurrent_peak"]
    good = cap is not None and cap >= peak * 4
    ok &= good
    print(f"  {'OK ' if good else 'X  '} {'MAX_ENTITIES':<18} {cap} "
          f">= 피크 {peak} × 4 (부하 여유)")

    # EntityId의 index 폭 안에 들어가야 한다 (entity_id.h의 INDEX_BITS = 12).
    good = cap is not None and cap <= 4096
    ok &= good
    print(f"  {'OK ' if good else 'X  '} {'index 폭':<18} {cap} <= 4096 (EntityId 12비트)")

    return bool(ok)


if __name__ == "__main__":
    report()
