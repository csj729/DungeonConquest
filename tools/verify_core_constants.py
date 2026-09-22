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
DEV_ITEMS_H = Path(__file__).resolve().parent.parent / "core" / "tools" / "dev_items.h"


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
    # **아이템 51종은 그동안 어느 도구도 보지 않았다.** dev_items.h가 대조 범위
    # 밖이라 51종 × 10스탯 + 둔화 + 축 + 등급이 통째로 무검사였다.
    ok &= _check_dev_items()

    ok &= _check_stats()
    return bool(ok)


def _check_dev_items():
    """`core/tools/dev_items.h`가 data/items.json과 어긋나지 않는지.

    dev_data.h와 같은 이유로 자동 대조를 건다 — 두 곳에 사는 값이기 때문이다.
    **그동안 이 파일만 대조 범위 밖이었다.** 아이템은 한 판 성장의 70%를
    담당하는 주력 축인데(design.md §4) 51종 × 10스탯이 통째로 무검사였다.

    값이 올바르게 **도출**됐는지는 tools/verify_derivations.py가 본다.
    여기서는 C++와 JSON이 같은지만 본다.
    """
    src = DEV_ITEMS_H.read_text(encoding="utf-8")
    items = gd.load("items")
    rows = items["commons"] + items["recipes"]
    stat_names = list(gd.load("stats")["stats"].keys())

    def flat(name, dims=""):
        m = re.search(rf"{name}\[[^\]]*\]{dims}\s*=\s*\{{(.*?)\}};", src, re.S)
        return [int(x) for x in re.findall(r"-?\d+", m.group(1))] if m else None

    def stat_rows():
        m = re.search(r"DEV_ITEM_STATS\[DEV_ITEM_COUNT\]\[10\] = \{(.*?)\n\};", src, re.S)
        if not m:
            return None
        return [[int(x) for x in re.findall(r"-?\d+", r)]
                for r in re.findall(r"\{([^}]*)\}", m.group(1))]

    checks = [
        ("DEV_ITEM_STATS", stat_rows(),
         [[it["stats_permille"].get(k, 0) for k in stat_names] for it in rows]),
        # slow_aura는 Stat enum에 없다 — CC 축의 둔화는 R_FROST가 쓰는
        # hero.slowAura 경로를 재사용하므로 별도 배열로 나간다.
        ("DEV_ITEM_SLOW", flat("DEV_ITEM_SLOW"),
         [it["stats_permille"].get("slow_aura", 0) for it in rows]),
        ("DEV_ITEM_AXIS", flat("DEV_ITEM_AXIS"),
         [items["axes"].index(it["axis"]) for it in rows]),
        ("DEV_ITEM_TIER", flat("DEV_ITEM_TIER"),
         [items["grades"].index(it["grade"]) for it in rows]),
        ("DEV_TIER_POWER", flat("DEV_TIER_POWER"), items["tier_power_permille"]),
        ("DEV_ITEM_COUNT", [int(re.search(r"DEV_ITEM_COUNT = (\d+)", src).group(1))],
         [len(rows)]),
    ]

    bad = [(n, g, w) for n, g, w in checks if g != w]
    for name, got, want in bad:
        if isinstance(got, list) and isinstance(want, list) and len(got) == len(want):
            idx = [i for i, (a, b) in enumerate(zip(got, want)) if a != b]
            print(f"  X   dev_items.h {name}: {len(idx)}칸 어긋남 — "
                  f"첫 자리 #{idx[0]} {got[idx[0]]} != {want[idx[0]]}")
        else:
            print(f"  X   dev_items.h {name}: 형태가 다르다")
    print(f"  {'OK ' if not bad else 'X  '} {'dev_items.h 대조':<18} "
          f"{len(checks) - len(bad)}/{len(checks)} 항목 일치 (아이템 {len(rows)}종)")
    return not bad


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



def _check_dev_data():
    """`core/tools/dev_data.h`는 진짜 로더가 붙기 전까지의 임시 수치다.

    데이터와 두 곳에 사는 값이므로 자동 대조를 건다 — 한쪽만 고치면 여기서 걸린다.
    로더가 붙으면 이 함수와 dev_data.h가 함께 사라진다.
    """
    src = DEV_DATA_H.read_text(encoding="utf-8")

    def ints(name):
        m = re.search(rf"\b{name}\[\d*\]\s*=\s*\{{([^}}]*)\}}", src, re.S)
        return [int(x) for x in re.findall(r"-?\d+", m.group(1))] if m else None

    def scalar(name):
        m = re.search(rf"\b{name}\s*=\s*(-?\d+)\s*;", src)
        return int(m.group(1)) if m else None

    def elite_rows():
        """kElites 초기화 블록을 행 단위로 뜯는다 — {hp, armor, dmg, prio, windup, typeId}."""
        m = re.search(r"kElites\[\d*\]\s*=\s*\{(.*?)\n\s*\};", src, re.S)
        if not m:
            return None
        rows = re.findall(r"\{([^}]*)\}", m.group(1))
        # typeId(마지막 열)는 JSON에 없으므로 앞 5개만 본다
        return [[int(x) for x in re.findall(r"-?\d+", r)][:5] for r in rows]

    def elite_spawns():
        """EliteSpawnPoint sp[] 초기화 블록을 [포인트, 엘리트 인덱스] 행으로 뜯는다."""
        m = re.search(r"EliteSpawnPoint sp\[\d*\]\s*=\s*\{(.*?)\n\s*\};", src, re.S)
        if not m:
            return None
        return [[int(x) for x in re.findall(r"-?\d+", r)]
                for r in re.findall(r"\{([^}]*)\}", m.group(1))]

    def slice_boss_hp():
        m = re.search(r"c\.boss\.hp\s*=\s*Fixed\((\d+)\)", src)
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
        ("MOB_SEPARATION_MILLITILE", scalar("MOB_SEPARATION_MILLITILE"),
         gd.SPAWN["mob_separation_millitile"]),
        ("TRASH_ATTACK_RANGE_MILLITILE", scalar("TRASH_ATTACK_RANGE_MILLITILE"),
         gd.MONSTERS["trash"]["attack_range_millitile"]),
        # **엘리트 표 전체를 대조한다.** damage가 4.2배 어긋난 채로(40/8/5/3 대
        # 167/33/21/13) 파이썬 검증과 C++ 시뮬이 서로 다른 게임을 재고 있었다.
        # 한 필드만 보면 또 놓치므로 kElites 행을 통째로 본다.
        ("kElites", elite_rows(),
         [[e["hp"], e["armor"], e["damage"], e["target_priority"], e["windup_ticks"]]
          for e in gd.MONSTERS["elites"]]),
        # **엘리트 등장 임계는 구간 구성에서 파생되는 값이다.** 손으로 적은 표가
        # 균등 분할 시절 값으로 남아 19마리 전부 구간 8 이전에 몰려 있었다.
        ("eliteSpawns", elite_spawns(),
         [list(r) for r in gd.SEGMENTS_DATA["elite_spawn_points"]]),
        ("ELITE_COOLDOWN_TICKS", scalar("ELITE_COOLDOWN_TICKS"),
         gd.MONSTERS["elites"][0]["cooldown_ticks"]),
        ("ELITE_ATTACK_RANGE_MILLITILE", scalar("ELITE_ATTACK_RANGE_MILLITILE"),
         gd.MONSTERS["elite_attack_range_millitile"]),
        ("ELITE_CC_GAUGE_MAX", scalar("ELITE_CC_GAUGE_MAX"),
         gd.MONSTERS["elite_cc_gauge_max"]),
        ("TARGET_PRIORITY_FALLOFF_PER_TILE", scalar("TARGET_PRIORITY_FALLOFF_PER_TILE"),
         gd.MONSTERS["target_priority_falloff_per_tile"]),
        ("TRASH_COOLDOWN_TICKS", scalar("c.trash.cooldownTicks"),
         gd.MONSTERS["trash"]["cooldown_ticks"]),
        ("SLICE_BOSS_HP", scalar("c.boss.hp = Fixed") or slice_boss_hp(),
         gd.MONSTERS["slice_boss_hp"]),
        ("bossPhase2AtPermille", scalar("c.bossPhase2AtPermille"),
         gd.MONSTERS["boss"]["phase2_at_permille"]),
        ("bossPhase2Purge", scalar("c.bossPhase2Purge"), gd.PROGRESSION["boss_phase2_purge"]),
        ("SLICE_BOSS_DAMAGE", scalar("SLICE_BOSS_DAMAGE"), gd.MONSTERS["slice_boss_damage"]),
        ("SLICE_BOSS_COOLDOWN_TICKS", scalar("SLICE_BOSS_COOLDOWN_TICKS"),
         gd.MONSTERS["slice_boss_cooldown_ticks"]),
        ("SLICE_BOSS_ATTACK_RANGE_MILLITILE", scalar("SLICE_BOSS_ATTACK_RANGE_MILLITILE"),
         gd.MONSTERS["slice_boss_attack_range_millitile"]),
        ("BOSS_TARGET_PRIORITY", scalar("c.boss.targetPriority"),
         gd.MONSTERS["boss"]["target_priority"]),
        ("trashHpScale", scalar("c.trashHpScalePerSegmentPermille"),
         gd.PROGRESSION["trash_hp_scale_per_segment_permille"]),
        ("HERO_SEPARATION_MILLITILE", scalar("HERO_SEPARATION_MILLITILE"),
         gd.SPAWN["hero_separation_millitile"]),
        ("APPROACH_MARGIN_MILLITILE", scalar("APPROACH_MARGIN_MILLITILE"),
         gd.SPAWN["approach_margin_millitile"]),
        ("HERO_MOVE_SPEED_PERMILLE", scalar("HERO_MOVE_SPEED_PERMILLE"),
         gd.HERO["move_speed_permille"]),
        ("HERO_AOE_RADIUS_MILLITILE", scalar("HERO_AOE_RADIUS_MILLITILE"),
         gd.HERO["aoe_radius_millitile"]),
        ("HERO_QTE_COOLDOWN_TICKS", scalar("HERO_QTE_COOLDOWN_TICKS"),
         gd.HERO["qte_cooldown_ticks"]),
        ("HERO_QTE_SUCCESS_PERMILLE", scalar("HERO_QTE_SUCCESS_PERMILLE"),
         gd.HERO["qte_success_mult_permille"]),
        ("HERO_QTE_PERFECT_PERMILLE", scalar("HERO_QTE_PERFECT_PERMILLE"),
         gd.HERO["qte_perfect_mult_permille"]),
        ("HERO_QTE_PERFECT_WINDOW", scalar("HERO_QTE_PERFECT_WINDOW"),
         gd.HERO["qte_perfect_window_ticks"]),
        ("HERO_GROGGY_TICKS", scalar("HERO_GROGGY_TICKS"), gd.HERO["groggy_ticks"]),
        ("LEVEL_NEED", ints("LEVEL_NEED"), gd.PROGRESSION["level_need_table"][:80]),
        ("CARD_GRADE_RATE", ints("CARD_GRADE_RATE"),
         [g["rate_permille"] for g in gd.CARDS["grades"]]),
        ("CARD_GRADE_BUDGET", ints("CARD_GRADE_BUDGET"),
         [g["power_budget_permille"] for g in gd.CARDS["grades"]]),
        # grade_step은 배수(1·2·4)이고 C++는 permille로 담는다. 일반 0.6 · 전설 10을 덧붙인다.
        ("CARD_GRADE_STEP", ints("CARD_GRADE_STEP"),
         [600] + [x * 1000 for x in gd.CARDS["grade_step"]] + [10000]),
        ("ENGRAVE_BASE", ints("ENGRAVE_BASE"),
         [e["uncommon_permille"] for e in gd.CARDS["engravings"]]),
        ("RELIC_BASE", ints("RELIC_BASE"),
         [r["uncommon_permille"] for r in gd.CARDS["relics"]]),
        ("LEGEND_POOL_SIZE", scalar("LEGEND_POOL_SIZE"), gd.CARDS["legend_pool_size"]),
        ("SWARM_RADIUS_MILLITILE", scalar("SWARM_RADIUS_MILLITILE"),
         gd.CARDS["swarm_radius_millitile"]),
        ("SWARM_MAX_STACKS", scalar("SWARM_MAX_STACKS"), gd.CARDS["swarm_max_stacks"]),
        ("DECAY_TICKS", scalar("DECAY_TICKS"), gd.CARDS["decay_ticks"]),
        ("RAGE_DURATION_TICKS", scalar("RAGE_DURATION_TICKS"),
         gd.CARDS["rage_duration_ticks"]),
        ("RAGE_MAX_STACKS", scalar("RAGE_MAX_STACKS"), gd.CARDS["rage_max_stacks"]),
        ("BOLT_INTERVAL_TICKS", scalar("BOLT_INTERVAL_TICKS"),
         gd.CARDS["bolt_interval_ticks"]),
        ("FROST_RADIUS_MILLITILE", scalar("FROST_RADIUS_MILLITILE"),
         gd.CARDS["frost_radius_millitile"]),
        ("STORM_RADIUS_MILLITILE", scalar("STORM_RADIUS_MILLITILE"),
         gd.CARDS["storm_radius_millitile"]),
        ("STORM_DPS_PERMILLE", scalar("STORM_DPS_PERMILLE"),
         gd.CARDS["storm_dps_permille"]),
        ("PIERCE_WIDTH_MILLITILE", scalar("PIERCE_WIDTH_MILLITILE"),
         gd.CARDS["pierce_width_millitile"]),
        ("PIERCE_LENGTH_MILLITILE", scalar("PIERCE_LENGTH_MILLITILE"),
         gd.CARDS["pierce_length_millitile"]),
        ("CARDS_PER_LEVEL", scalar("CARDS_PER_LEVEL"), gd.CARDS["cards_per_level"]),
        ("corruptionThreshold", scalar("c.corruptionThreshold"),
         gd.PROGRESSION["corruption_threshold"]),
        ("trashPoints", scalar("c.trashPoints"), gd.SEGMENTS_DATA["trash_points"]),
        ("elitePoints", scalar("c.elitePoints"), gd.SEGMENTS_DATA["elite_points"]),
        ("CLEAR_TARGET_POINTS", scalar("CLEAR_TARGET_POINTS"),
         gd.SEGMENTS_DATA["clear_target_points"]),
        ("SEGMENT_START_POINTS", ints("SEGMENT_START_POINTS"),
         gd.SEGMENTS_DATA["segment_start_points"]),
        ("segmentClearPurge", scalar("c.segmentClearPurge"),
         gd.PROGRESSION["segment_clear_purge"]),
        ("orbTrashDropPermille", scalar("c.orbTrashDropPermille"),
         gd.PROGRESSION["orb_trash_drop_permille"]),
        ("orbTrashAmount", scalar("c.orbTrashAmount"), gd.PROGRESSION["orb_trash_amount"]),
        ("orbEliteAmount", scalar("c.orbEliteAmount"), gd.PROGRESSION["orb_elite_amount"]),
        ("orbPickupRadiusMilli", scalar("c.orbPickupRadiusMilli"),
         gd.PROGRESSION["orb_pickup_radius_millitile"]),
        ("orbLifetimeTicks", scalar("c.orbLifetimeTicks"),
         gd.PROGRESSION["orb_lifetime_ticks"]),
        ("corruptionPerMobPermille", scalar("c.corruptionPerMobPermille"),
         gd.PROGRESSION["corruption_per_mob_permille"]),
        ("corruptionOverflowMultPermille", scalar("c.corruptionOverflowMultPermille"),
         gd.PROGRESSION["corruption_overflow_mult_permille"]),
    ]
    bad = [n for n, got, want in checks if got != want]
    for n, got, want in checks:
        if got != want:
            print(f"  X   dev_data.h {n}: {got} != 데이터 {want}")
    print(f"  {'OK ' if not bad else 'X  '} {'dev_data.h 대조':<18} "
          f"{len(checks) - len(bad)}/{len(checks)} 항목 일치")
    return not bad


if __name__ == "__main__":
    report()
