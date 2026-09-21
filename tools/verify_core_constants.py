"""C++ 구조 상수가 데이터·설계와 어긋나지 않는지 대조.

`core/include/dc/config.h`에는 컴파일 타임에 필요한 값이 몇 개 있다. 이건
"밸런스 수치는 data/*.json이 단일 진실 원천"의 **예외**이므로, 예외인 만큼
자동 대조를 걸어둔다. 두 곳에 사는 값은 한쪽만 고치는 사고가 반드시 난다.
"""
import re
from pathlib import Path

import gamedata as gd

CORE_INC = Path(__file__).resolve().parent.parent / "core" / "include" / "dc"
CONFIG_H = CORE_INC / "config.h"
STAT_ID_H = CORE_INC / "stat_id.h"
DEV_DATA_H = Path(__file__).resolve().parent.parent / "core" / "tools" / "dev_data.h"


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

    # 임시 데이터 로더(core/tools/dev_data.h)가 data/*.json과 어긋나지 않는지.
    ok &= _check_dev_data()

    # 스탯 enum 순서와 stats.json 키 순서가 같아야 한다.
    # **enum 값이 바뀌면 기존 리플레이가 전부 깨진다** — 순서까지 대조한다.
    ok &= _check_stats()
    return bool(ok)


def _check_stats():
    src = STAT_ID_H.read_text(encoding="utf-8")
    # statName()의 case 순서 = enum 순서
    cpp = re.findall(r'case Stat::\w+:\s*return "([a-z_]+)";', src)
    data = list(gd.load("stats")["stats"].keys())

    ok = cpp == data
    print(f"  {'OK ' if ok else 'X  '} {'Stat enum 순서':<18} "
          f"C++ {len(cpp)}종 == stats.json {len(data)}종")
    if not ok:
        print(f"      C++  : {cpp}")
        print(f"      JSON : {data}")
        return False

    # 하한 값 자체도 대조한다. C++는 데이터 로더가 붙을 때까지 값을 갖지 않으므로
    # 여기서는 "JSON 쪽이 규약을 지키는가"만 본다.
    bad = []
    for name, b in gd.load("stats")["stats"].items():
        if b["min_pct_add_permille"] < -1000:
            bad.append(f"{name}: min_pct_add {b['min_pct_add_permille']} < -1000permille")
        if b["min_value_permille"] < 0:
            bad.append(f"{name}: min_value {b['min_value_permille']} < 0")
    for m in bad:
        print(f"  X   {m}")
    print(f"  {'OK ' if not bad else 'X  '} {'스탯 하한 규약':<18} "
          f"min_pct_add >= -1000permille, min_value >= 0")
    return not bad


if __name__ == "__main__":
    report()


def _check_dev_data():
    """`core/tools/dev_data.h`는 진짜 로더가 붙기 전까지의 임시 수치다.

    데이터와 두 곳에 사는 값이므로 자동 대조를 건다 — 한쪽만 고치면 여기서 걸린다.
    로더가 붙으면 이 함수와 dev_data.h가 함께 사라진다.
    """
    src = DEV_DATA_H.read_text(encoding="utf-8")

    def ints(name):
        m = re.search(rf"{name}\[\d*\]\s*=\s*\{{([^}}]*)\}}", src, re.S)
        return [int(x) for x in re.findall(r"-?\d+", m.group(1))] if m else None

    def scalar(name):
        m = re.search(rf"\b{name}\s*=\s*(-?\d+)\s*;", src)
        return int(m.group(1)) if m else None

    checks = [
        ("CAP_BY_SEGMENT", ints("CAP_BY_SEGMENT"), gd.SPAWN["concurrent_cap_by_segment"]),
        ("BATCH_BY_SEGMENT", ints("BATCH_BY_SEGMENT"), gd.SPAWN["batch_by_segment"]),
        ("HERO_ATTACK_POWER", scalar("HERO_ATTACK_POWER"), gd.HERO["attack_power"]),
        ("HERO_ATTACK_INTERVAL_TICKS", scalar("HERO_ATTACK_INTERVAL_TICKS"),
         gd.HERO["attack_interval_ticks"]),
        ("HERO_CRIT_CHANCE_PERMILLE", scalar("HERO_CRIT_CHANCE_PERMILLE"),
         gd.HERO["crit_chance_permille"]),
        ("HERO_CRIT_MULT_PERMILLE", scalar("HERO_CRIT_MULT_PERMILLE"),
         gd.HERO["crit_mult_permille"]),
        ("HERO_CORRUPTION_MAX", scalar("HERO_CORRUPTION_MAX"), gd.HERO["corruption_max"]),
        ("HERO_ARMOR", scalar("HERO_ARMOR"), gd.HERO["armor"]),
        ("HERO_PROC_PRD_C_Q16", scalar("HERO_PROC_PRD_C_Q16"), gd.HERO["proc_prd_c_q16"]),
        ("tickHz", scalar("c.tickHz"), gd.PROGRESSION["tick_hz"]),
        ("armorK", scalar("c.armorK"), gd.PROGRESSION["armor_k"]),
        ("totalSegments", scalar("c.totalSegments"), gd.PROGRESSION["total_segments"]),
        ("segmentsPerMap", scalar("c.segmentsPerMap"), gd.PROGRESSION["segments_per_map"]),
        ("spawnIntervalTicks", scalar("c.spawnIntervalTicks"), gd.SPAWN["interval_ticks"]),
        ("spawnRadiusMilli", scalar("c.spawnRadiusMilli"), gd.SPAWN["radius_millitile"]),
        ("minSeparationMilli", scalar("c.minSeparationMilli"), gd.SPAWN["min_separation_millitile"]),
        ("corruptionThreshold", scalar("c.corruptionThreshold"),
         gd.PROGRESSION["corruption_threshold"]),
        ("trashPoints", scalar("c.trashPoints"), gd.SEGMENTS_DATA["trash_points"]),
        ("elitePoints", scalar("c.elitePoints"), gd.SEGMENTS_DATA["elite_points"]),
    ]
    bad = [n for n, got, want in checks if got != want]
    for n, got, want in checks:
        if got != want:
            print(f"  X   dev_data.h {n}: {got} != 데이터 {want}")
    print(f"  {'OK ' if not bad else 'X  '} {'dev_data.h 대조':<18} "
          f"{len(checks) - len(bad)}/{len(checks)} 항목 일치")
    return not bad
