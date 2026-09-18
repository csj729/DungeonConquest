"""스폰 좌표 규칙 검증.

좌표 규칙은 그림 문제가 아니라 **밸런스 문제**다. 스폰 거리가 접근 시간을 정하고,
접근 시간이 "지금 영웅을 때리고 있는 몹이 몇인가"를 정한다. 그 수가 곧 피격량이다.

규칙 (design.md §2):

1. **기준점은 영웅 위치**다. 카메라 기준으로 잡으면 영웅이 빠르게 이동할 때
   진행 방향 스폰이 코앞에 뜬다
2. **거리는 화면 밖**이어야 한다. 화면 안에서 튀어나오면 팝인이 보인다
3. **방향은 4섹터(90°) 균등**, 섹터 안의 각도만 RNG 스트림을 쓴다
4. 섹터가 맵 밖이면 **시계방향 다음 섹터**로 넘긴다 (고정 순서 — 결정론)
5. 같은 배치의 몹은 섹터 안에서 **고정 간격**으로 벌려 놓는다 (겹침 방지)
"""
import math
import sys

sys.path.insert(0, "tools")

from balance_baseline import (
    HERO, TRASH, TICK_HZ, concurrent_cap, SURROUND_COUNT, hero_dps,
    effective_targets, spawn_batch, SPAWN_DIRECTIONS, SPAWN_INTERVAL_TICKS,
    corruption_from_mass, CORRUPTION_THRESHOLD, TOTAL_SEGMENTS,
    TOTAL_LEVELUPS, hp_scale, power_mult,
)
from verify_segments import SEGMENTS, simulate, SEGMENT_TARGET_SEC

# ── 화면 · 거리 (타일 단위) ────────────────────────────────────
# PPU와 화면 픽셀 크기는 에셋 단계에서 정한다(§11 · §15). 여기서는 **타일 수**로
# 잡는다 — 타일이 게임플레이의 실제 단위이고, PPU는 그걸 픽셀로 옮기는 배율일 뿐이다.
SCREEN_TILES = (30, 17)        # 16:9 기준 화면에 보이는 타일 수
SPAWN_MARGIN = 1.15            # 화면 밖 여유 — 1.0이면 화면 경계에서 튀어나온다

# 스폰 후 영웅에게 닿기까지의 목표 시간. **이 값이 이동속도를 결정한다.**
APPROACH_SEC = 8.0

MIN_SEPARATION = 1.5           # 같은 배치 몹 사이 최소 간격(타일)
SURVIVE_TARGET_SEC = 15.0      # 목표 5와 같은 기준
LIFETIME_BAND = (10.0, 45.0)   # 몹 평균 생존 시간 — 넘으면 전장이 고인다


def spawn_radius():
    w, h = SCREEN_TILES
    return math.hypot(w / 2, h / 2) * SPAWN_MARGIN


def approach_speed():
    """스폰 거리를 목표 접근 시간에 맞추면 이동속도가 파생된다."""
    return spawn_radius() / APPROACH_SEC


def sector_arc_tiles():
    """한 섹터(90°)가 스폰 원호에서 차지하는 길이 — 배치를 벌려 놓을 공간."""
    return 2 * math.pi * spawn_radius() / SPAWN_DIRECTIONS


def report():
    ok = True
    base, _b, _a = hero_dps()

    print("=== 좌표 규칙 ===")
    r = spawn_radius()
    print(f"  화면 {SCREEN_TILES[0]}×{SCREEN_TILES[1]}타일 → 대각 절반 "
          f"{math.hypot(*[x/2 for x in SCREEN_TILES]):.1f}타일")
    print(f"  스폰 반경 = 대각 절반 × {SPAWN_MARGIN} = **{r:.1f}타일** (영웅 기준)")
    print(f"  섹터 4개 × 90°, 한 섹터의 원호 {sector_arc_tiles():.1f}타일")
    print(f"  → 최소 간격 {MIN_SEPARATION}타일이면 한 섹터에 "
          f"{int(sector_arc_tiles() / MIN_SEPARATION)}마리까지 겹치지 않는다")
    good = int(sector_arc_tiles() / MIN_SEPARATION) >= max(
        spawn_batch(i) for i in range(1, 9))
    ok &= good
    print(f"  최대 배치 {max(spawn_batch(i) for i in range(1,9))}마리  "
          f"{'PASS' if good else 'FAIL — 배치가 섹터에 안 들어간다'}\n")

    print("=== 이동속도는 좌표 규칙에서 파생된다 ===")
    v = approach_speed()
    print(f"  스폰 반경 {r:.1f}타일 / 접근 목표 {APPROACH_SEC:g}초 = "
          f"**{v:.2f}타일/초** ({v / TICK_HZ:.3f}타일/틱)")
    print("  ※ `approachSpeed`는 지금까지 미정이었다(monsters_vertical_slice.md §6).")
    print("     스폰 거리와 접근 시간을 정하면 이 값이 따라 나온다 — 거꾸로 잡을 이유가 없다\n")

    print("=== 접근 시간이 전장 구성을 만든다 ===")
    rows, _bs, _bg, _lv = simulate()
    print(f"  {'S':>2} {'상한':>5} {'처치율':>8} {'이동 중':>7} {'교전 중':>7} "
          f"{'평균 생존':>9}")
    lifetimes = []
    for i, _m, _r, n, _e, _g, sec, _gn, _c, _et in rows:
        k = n / sec
        moving = k * APPROACH_SEC
        engaged = concurrent_cap(i) - moving
        life = concurrent_cap(i) / k
        lifetimes.append(life)
        print(f"  {i:>2} {concurrent_cap(i):>5} {k:>6.2f}/초 {moving:>7.1f} "
              f"{engaged:>7.1f} {life:>8.0f}초")
    lo, hi = LIFETIME_BAND
    good = all(lo <= x <= hi for x in lifetimes)
    ok &= good
    print(f"  평균 생존 {min(lifetimes):.0f}~{max(lifetimes):.0f}초 "
          f"(목표 {lo:g}~{hi:g})  {'PASS' if good else 'FAIL'}")
    if not good:
        need = CONCURRENT_CAP[8] / hi
        thr = need * TRASH["hp"]
        cur = base * effective_targets(1)
        print(f"  ※ **전장이 고인다.** 상한만큼 쌓이는데 처치율이 못 따라간다.")
        print(f"     상한 {CONCURRENT_CAP[8]}에서 평균 생존 {hi:g}초가 되려면 처치율 "
              f"{need:.1f}/초 = 처리량 {thr:.0f} EHP/초가 필요한데 현재 {cur:.0f}이다")
        print(f"     → 공격 간격 {HERO['attack_interval_ticks']}틱 → "
              f"**{HERO['attack_interval_ticks'] * cur / thr:.0f}틱**")
    print()

    print("=== 잠식 게이지 — 피격 + 물량 ===")
    print(f"  {'S':>2} {'근접 포위':>8} {'원거리':>7} {'피격/초':>8} {'물량/초':>8} "
          f"{'합':>7} {'생존':>7}")
    worst = 1e9
    for i, (melee, ranged, _e, _n) in enumerate(SEGMENTS, 1):
        cap = concurrent_cap(i)
        ratio = ranged / (melee + ranged)
        mel = min(SURROUND_COUNT, round(cap * (1 - ratio)))
        rng_ = round(cap * ratio)
        hit = (mel * TRASH["damage"] + rng_ * TRASH["ranged_damage"]) \
            / (TRASH["cooldown_ticks"] / TICK_HZ)
        mass = corruption_from_mass(cap, cap)
        inc = hit + mass
        surv = HERO["corruption_max"] / inc if inc else 1e9
        worst = min(worst, surv)
        print(f"  {i:>2} {mel:>8} {rng_:>7} {hit:>8.1f} {mass:>8.1f} {inc:>7.1f} "
              f"{surv:>6.1f}초")
    good = worst >= SURVIVE_TARGET_SEC
    ok &= good
    print(f"  최악 {worst:.1f}초 (목표 {SURVIVE_TARGET_SEC:g}초)  "
          f"{'PASS' if good else 'FAIL'}")
    if not good:
        need = HERO["corruption_max"] * SURVIVE_TARGET_SEC / worst
        print(f"  ※ 잠식 최대치가 {need:.0f} 필요하다 "
              f"(현재 {HERO['corruption_max']})")
    print()

    print("=== 런 전체(24구간) 압박 곡선 — 맵마다 리셋되지 않는가 ===")
    print(f"  {'구간':>4} {'상한':>5} {'처치율':>8} {'평균생존':>8} {'잠식/초':>8} {'가득':>7}")
    prev_press, mono = 0.0, True
    for w in (1, 8, 16, TOTAL_SEGMENTS):
        cap = concurrent_cap(w)
        lv = round(TOTAL_LEVELUPS * (w - 1) / TOTAL_SEGMENTS)
        j = (w - 1) % len(SEGMENTS)
        n = round(SEGMENT_TARGET_SEC[j] * base * power_mult(lv)
                  * effective_targets(j + 1) / (TRASH["hp"] * hp_scale(w)))
        kill = n / SEGMENT_TARGET_SEC[j]
        melee, ranged, _e, _n = SEGMENTS[j]
        ratio = ranged / (melee + ranged)
        mel = min(SURROUND_COUNT, round(cap * (1 - ratio)))
        rng_ = round(cap * ratio)
        press = (mel * TRASH["damage"] + rng_ * TRASH["ranged_damage"]) \
            / (TRASH["cooldown_ticks"] / TICK_HZ) + corruption_from_mass(cap, cap)
        mono &= press >= prev_press
        prev_press = press
        print(f"  {w:>4} {cap:>5} {kill:>6.2f}/초 {cap/kill:>7.0f}초 {press:>8.1f} "
              f"{HERO['corruption_max']/press:>6.1f}초")
    ok &= mono
    print(f"  압박 단조 증가  {'PASS' if mono else 'FAIL'}")
    print("  ※ 동시 생존 상한을 **맵 안의 구간이 아니라 런 전체 구간 번호**로 램프한다.")
    print("     맵마다 리셋하면 영웅이 58배 강해지는 동안 위협이 그대로여서")
    print("     후반 맵이 무위험이 된다\n")

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
