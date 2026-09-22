"""잠식 회복 예산 검산 (design.md §2).

## 왜 이 도구가 생겼나

`balance_baseline.py` 목표 5는 "잠식이 가득 차기까지 15초 이상"만 본다. 그건
**죽기까지의 시간에 대한 하한**이지 런이 완주 가능한가에 대한 검사가 아니다.
그래서 회복이 한 줄도 구현되지 않은 채로 12개 도구가 전부 PASS했고, 몬테카를로
하네스를 돌려서야 클리어율 0%가 드러났다.

이 도구는 그 빈자리를 메운다 — **유입과 정화를 같은 표에 놓고 수지를 본다.**

## 모델 가정 (데이터가 아니다 — C++ 코어가 읽지 않는다)

아래 수치는 `core/tools/dc_montecarlo` 실측에서 왔다. 감으로 적은 값이 아니라
헤드리스 12시드 × 600초 평균이고, 수치를 바꾸면 다시 재야 한다.
"""

import sys
sys.path.insert(0, "tools")

from balance_baseline import elite_pressure
from gamedata import PROGRESSION as _PROG, HERO as _HERO, SEGMENTS_DATA as _SEG

CORRUPTION_MAX = _HERO["corruption_max"]
ORB_TRASH_RATE = _PROG["orb_trash_drop_permille"] / 1000.0
ORB_TRASH_AMT = _PROG["orb_trash_amount"]
ORB_ELITE_AMT = _PROG["orb_elite_amount"]
# 습득률 — 구슬은 죽은 자리에 떨어지고 영웅은 주우러 가지 않는다(§3).
# 실측(12시드 × 맵 완주, 습득 반경 안에 들어온 구슬의 정화량 / 드랍 기댓값): 46%.
# 멀리 있는 엘리트를 쫓을수록 뒤에 흘린 구슬이 소멸하고, 그것이 드랍·습득을
# 2단계로 둔 이유 그 자체다.
ORB_PICKUP_RATE = 0.46

# **예산이 아니라 실효로 본다.** 잠식은 0에서 잘리고 구슬은 덩어리라, 잠식이
# 390보다 낮을 때 엘리트 구슬을 먹으면 초과분이 통째로 버려진다. 예산 기준으로는
# 97%를 덮는데 하네스 실측은 64.8%다 — 예산을 15% 올려도 실효는 2.8%p만 오른다
# (1.00배 59.4% → 1.15배 62.2% → 1.30배 64.8%).
#
# **닫힌 식으로는 이 값을 못 만든다.** 구간 평균으로 잘라도 76%가 나온다 —
# 낭비는 구슬 하나 단위로 일어나지 구간 단위로 일어나지 않기 때문이다.
# 그래서 다른 실측 상수들과 같이 여기에 적어 두고 수치를 바꾸면 다시 잰다.
REALIZED_COVERAGE = 0.648

# 엘리트 공격이 만든 잠식 (12시드 × 맵 완주). **엘리트 damage를 0으로 둔 런과의
# 차이로 잰다** — 엘리트가 살아 있는 것만으로 생기는 물량 잠식을 빼고 공격분만 남긴다.
# balance_baseline.elite_pressure()와 같은 것을 재는 값이라 직접 비교할 수 있다.
ELITE_MEASURED = 5.9
# 엘리트가 자기 사거리(3.5타일) 안에 있던 시간 비율. **이 도구가 실제로 지키는 값이다** —
# 사거리가 2.0타일이던 때 1.3%까지 떨어져 엘리트 축이 통째로 죽어 있었다.
ELITE_IN_RANGE = 0.514
ELITE_IN_RANGE_MIN = 0.40
# ── 보스전 (실측 · 하네스 24시드 중 도달 15판, 화력) ──────────────
# **맵 8구간만 보던 도구에 빠져 있던 자리다.** 게이지를 채운 런의 절반 이상이
# 여기서 죽는데 어느 도구도 보지 않았다. 보스전에는 구간 경계가 없어 숨 돌릴
# 자리가 하나도 없었고, 그게 boss_phase2_purge가 생긴 이유다.
BOSS_SEC        = 75.0    # 실측. 설계 밴드는 monsters.json:boss.target_sec 60~90
BOSS_INFLOW     = 39.3    # /초. 맵 구간(22.8)보다 높다 — 보스 피해가 얹힌다
BOSS_ORB        = 14.3    # /초. 보스전에도 잡몹 스폰이 상한을 유지해 구슬은 계속 나온다
BOSS_LEECH      = 6.9     # /초. 회복 카드 몫 (화력 정책 기준)
BOSS_ARRIVE     = 275.0   # 도달 시 잠식 (최대치 1250)
BOSS_PHASE2_PURGE = _PROG["boss_phase2_purge"]
# 보스전 적자가 남은 게이지의 몇 배여야 하는가. **1.0 근처가 목표다** —
# 훨씬 낮으면 보스전이 잠식과 무관해지고, 훨씬 높으면 회복 카드를 들어도
# 시간 안에 못 죽이면 지는 판이 되어 빌드가 사라진다.
BOSS_DECIDED_BAND = (0.7, 1.1)

SEGMENT_PURGE = _PROG["segment_clear_purge"]
SEGMENTS = _SEG["segment_start_points"]
CLEAR_TARGET = _SEG["clear_target_points"]

# ── 실측 (core/tools/dc_montecarlo · 12시드) ────────────────────
# 구간별 (잠식 유입/초, 체류 초). 잠식을 중간값에 고정해 전 구간을 관측한 값이다.
MEASURED = [
    #  유입/초  체류초   잠식을 중간값에 고정해 전 구간을 관측한 값이다
    (  1.2,  29.0),   # 1 — 전장이 아직 비어 있다 (평균 생존 7.5)
    ( 14.6,  31.7),   # 2 — 물량 임계 20 돌파
    ( 22.3,  35.3),   # 3 — 엘리트가 닿기 시작한다
    ( 22.2,  34.4),   # 4
    ( 27.1,  38.5),   # 5
    ( 27.7,  50.0),   # 6
    ( 26.8,  60.8),   # 7
    ( 27.2,  63.8),   # 8
]

# 목표. **밴드이지 점 추정이 아니다** — 회복이 유입을 정확히 상쇄하면 잠식이
# 게이지가 아니라 장식이 된다.
TARGET_BASELINE_COVERAGE = (0.55, 0.80)   # 기저 정화(처치+구간)가 덮는 유입 비율
TARGET_BASELINE_CLEARS = False            # 기저만으로는 완주하지 못해야 한다


def segment_kills(i):
    """구간 i(0 기반)에서 잡는 잡몹·엘리트 수."""
    seg = _SEG["segments"][i]
    return seg["melee"], len(seg["elites"])


def orb_value(i):
    """구간 i에서 드랍되는 구슬의 총 정화량 (습득 전 기댓값)."""
    trash, elite = segment_kills(i)
    return trash * ORB_TRASH_RATE * ORB_TRASH_AMT + elite * ORB_ELITE_AMT


def report():
    ok = True
    print("=== 목표 1: 기저 정화가 유입의 절반 이상을 덮는다 ===")
    print(f"  {'구간':>4} {'체류초':>7} {'유입/초':>8} {'정화/초':>8} {'수지/초':>8} "
          f"{'덮는 비율':>9}")
    total_in = total_out = total_sec = 0.0
    for i, (inflow, sec) in enumerate(MEASURED):
        # 구슬 정화 — 드랍 기댓값 × 실측 습득률
        kill_purge = orb_value(i) * ORB_PICKUP_RATE / sec
        # 구간 진입 정화 — 첫 구간은 진입 이벤트가 없다
        entry_purge = (SEGMENT_PURGE / sec) if i > 0 else 0.0
        out = kill_purge + entry_purge
        total_in += inflow * sec
        total_out += out * sec
        total_sec += sec
        print(f"  {i+1:>4} {sec:>7.1f} {inflow:>8.2f} {out:>8.2f} {out - inflow:>8.2f} "
              f"{out / inflow if inflow else 9.99:>8.0%}")

    coverage = total_out / total_in
    lo, hi = TARGET_BASELINE_COVERAGE
    good = lo <= REALIZED_COVERAGE <= hi
    ok &= good
    print(f"  맵 전체 유입 {total_in:.0f} · 정화 {total_out:.0f} "
          f"→ 예산 {coverage:.0%} · **실효 {REALIZED_COVERAGE:.0%}** "
          f"(목표 {lo:.0%}~{hi:.0%}) {'PASS' if good else 'FAIL'}")
    print("  ※ **게이트는 예산이 아니라 실효다.** 잠식은 0에서 잘리고 구슬은 덩어리라,")
    print("     잠식이 390보다 낮을 때 엘리트 구슬을 먹으면 초과분이 통째로 버려진다.")
    print("     구간 1이 극단이다 — 유입 1.2/초에 정화가 넘쳐 대부분을 버린다.")
    print("     그래서 **정화를 후반에 몰아주면 오히려 나빠진다**: 예산을 고정한 채")
    print("     엘리트 쪽으로 옮기면 클리어가 5.0% → 1.0%(엘리트 85%)로 떨어진다")
    print("  ※ 100%를 넘기면 잠식이 장식이 된다. 50% 아래면 회복 카드가 없는 빌드가")
    print("     확정 실패가 되어 §7의 '실패를 죽음이 아니라 지연으로'가 깨진다\n")

    print("=== 목표 2: 기저만으로는 완주하지 못한다 ===")
    # 게이지 1250을 들고 시작해 맵 전체 적자를 견딜 수 있는가
    deficit = total_in * (1 - REALIZED_COVERAGE)
    survives = deficit < CORRUPTION_MAX
    good = survives == TARGET_BASELINE_CLEARS
    ok &= good
    print(f"  맵 전체 적자 {deficit:.0f} vs 잠식 최대치 {CORRUPTION_MAX} "
          f"→ 기저만으로 {'완주' if survives else '실패'} "
          f"{'PASS' if good else 'FAIL'}")
    print(f"  회복 각인이 메워야 할 몫 {deficit - CORRUPTION_MAX:.0f} "
          f"({(deficit - CORRUPTION_MAX) / total_sec:.1f}/초)")
    print("  ※ 카드 선택이 결과를 바꾸려면 기저가 완주를 보장하면 안 된다\n")

    print("=== 목표 3: 정화가 구간 램프를 따라 올라간다 ===")
    print("  구슬 정화가 구간이 진행되며 늘어나는가 — 잡몹 21 → 66마리, 엘리트 0 → 5마리")
    first = orb_value(0) * ORB_PICKUP_RATE / MEASURED[0][1]
    last = orb_value(7) * ORB_PICKUP_RATE / MEASURED[7][1]
    good = last > first
    ok &= good
    print(f"  구간 1 구슬 정화 {first:.2f}/초 → 구간 8 {last:.2f}/초 "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 구간이 길어질수록 초당 정화는 묽어진다 — 포인트 총량은 늘지만")
    print("     체류 시간이 더 빨리 늘기 때문이다. 이것이 후반 압박의 실체다\n")

    print("=== 목표 4: 구간 체류 시간이 설계 목표 안에 있다 ===")
    print("  **유입은 시간에 비례하므로 런이 길어진 만큼 적자가 커진다.**")
    print("  한때 2.0배(600초)였다. 원인 두 가지를 잡아 1.2배까지 왔다 —")
    print("  엘리트 등장 임계가 균등 분할 시절 값이라 19마리 전부 구간 8 이전에")
    print("  몰려 있었고(구간 8이 혼자 1.9배), 영웅 공속이 설계 처치율에 못 미쳤다.")
    print(f"  {'구간':>4} {'설계(초)':>9} {'실측(초)':>9} {'배':>6}")
    design_total = actual_total = 0.0
    for i, seg in enumerate(_SEG["segments"]):
        want = seg["target_ticks"] / _PROG["tick_hz"]
        got = MEASURED[i][1]
        design_total += want
        actual_total += got
        print(f"  {i+1:>4} {want:>9.1f} {got:>9.1f} {got / want:>5.1f}배")
    ratio = actual_total / design_total
    good = ratio <= 1.3
    ok &= good
    print(f"  맵 통과 설계 {design_total:.0f}초 · 실측 {actual_total:.0f}초 "
          f"→ {ratio:.1f}배 (목표 1.3배 이하) {'PASS' if good else 'FAIL'}")
    print("  ※ 남은 초과분은 구간 6~8에 몰려 있다(1.19~1.32배). 전장이 동시 생존")
    print("     상한까지 차 있어 처치율이 곧 통과 시간인 구간들이다. 반대로 구간 1은")
    print("     **스폰이 상한이다** — 21마리를 초당 1.0마리로 뿌리고 접근에 8초가 걸려")
    print("     29초 아래로는 내려가지 않는다. 영웅을 더 올려도 초반은 그대로고")
    print("     후반만 빨라져 난이도 곡선이 평평해진다. 남은 몫은 스폰 램프의 일이다.\n")

    print("=== 목표 5: 설계 모델의 엘리트 압박이 실측과 맞는가 ===")
    print("  **원거리 잡몹을 폐지하면서 맵 안의 난이도 곡선을 만드는 축은 엘리트뿐이다.**")
    print("  그 축이 살아 있는지 두 가지로 본다 — 엘리트가 영웅에게 **닿는가**,")
    print("  그리고 닿아서 만든 압박이 설계 모델과 같은 크기인가.")
    model = [elite_pressure(i) for i in range(1, 9)]
    model_avg = sum(model) / len(model)
    print(f"  {'항목':>10} {'설계 모델':>12} {'실측':>10}")
    print(f"  {'엘리트 압박':>9} {model_avg:>9.1f}/초 {ELITE_MEASURED:>7.1f}/초")
    print(f"  {'사거리 안':>10} {'(상시 가정)':>12} {ELITE_IN_RANGE:>9.1%}")
    good = ELITE_IN_RANGE >= ELITE_IN_RANGE_MIN
    ok &= good
    print(f"  엘리트가 사거리 안에 있던 시간 {ELITE_IN_RANGE:.1%} "
          f"(하한 {ELITE_IN_RANGE_MIN:.0%})  {'PASS' if good else 'FAIL'}")
    print("  ※ **게이트는 사거리 안 비율이다.** 사거리가 2.0타일이던 때 잡몹 링")
    print("     (영웅 이격 1.0 · 몹 이격 1.5)을 뚫지 못해 1.3%까지 떨어졌고,")
    print("     엘리트 축이 통째로 죽어 있었다. 3.5타일로 올려 링 밖에서 닿게 했다.")
    print(f"  ※ 압박 {ELITE_MEASURED:.1f} 대 모델 {model_avg:.1f}의 차는 모델이")
    print("     **영웅 Armor 감쇠와 사거리 안 비율을 둘 다 빼고** 계산하기 때문이다.")
    print(f"     {model_avg:.1f} × 사거리 {ELITE_IN_RANGE:.0%} × 설계/실측 시간 비 "
          f"≈ {model_avg * ELITE_IN_RANGE * 294 / sum(m[1] for m in MEASURED):.1f}, "
          "나머지가 감쇠분이다.")
    print("  ※ 방패병 체력을 실효 300 → 186으로 내릴 때 damage를 33 → 83으로 올린 것이")
    print("     이 값을 지키기 위해서다 — 압박은 `상주 시간 × 피해`이고, 체력만 깎으면")
    print("     상주 시간이 0.4배가 되어 압박 예산이 같이 무너진다.\n")

    print("=== 목표 6: 보스전이 빌드로 갈리는 자리인가 ===")
    print("  **8구간만 보던 도구에 빠져 있던 자리다.** 게이지를 채운 런의 절반 이상이")
    print("  여기서 죽는데 어느 도구도 보지 않았다. 보스전에는 구간 경계가 없어")
    print("  숨 돌릴 자리가 하나도 없었고, 그게 boss_phase2_purge가 생긴 이유다.")
    print("  맵 구간의 목표 1 + 2와 같은 모양으로 본다 — **기저만으로는 못 버티고,")
    print("  회복 카드를 들면 버틸 수 있는가.**")
    ph2 = BOSS_PHASE2_PURGE / BOSS_SEC
    base_net  = BOSS_INFLOW - (BOSS_ORB + ph2)
    total_net = base_net - BOSS_LEECH
    room = CORRUPTION_MAX - BOSS_ARRIVE
    print(f"  {BOSS_SEC:.0f}초 · 유입 {BOSS_INFLOW:.1f}/초 · 구슬 {BOSS_ORB:.1f}/초 "
          f"· 페이즈2 {BOSS_PHASE2_PURGE}(1회 = {ph2:.1f}/초) · 흡혈 {BOSS_LEECH:.1f}/초")
    print(f"  도달 시 잠식 {BOSS_ARRIVE:.0f}/{CORRUPTION_MAX} → 남은 게이지 {room:.0f}")
    print(f"  {'':>14} {'수지/초':>8} {'적자':>7} {'남은 게이지 대비':>14}")
    print(f"  {'기저만':>12} {-base_net:>8.1f} {base_net * BOSS_SEC:>7.0f} "
          f"{base_net * BOSS_SEC / room:>13.0%}")
    print(f"  {'회복 카드 포함':>11} {-total_net:>8.1f} {total_net * BOSS_SEC:>7.0f} "
          f"{total_net * BOSS_SEC / room:>13.0%}")
    lo, hi = BOSS_DECIDED_BAND
    ratio = total_net * BOSS_SEC / room
    good = base_net * BOSS_SEC > room and lo <= ratio <= hi
    ok &= good
    print(f"  기저만으로 {'실패' if base_net * BOSS_SEC > room else '완주'} · "
          f"카드 포함 {ratio:.0%} (목표 {lo:.0%}~{hi:.0%})  {'PASS' if good else 'FAIL'}")
    print("  ※ 1.0 근처라는 건 **보스전이 동전 던지기라는 뜻이 아니라 빌드로 갈린다는**")
    print("     뜻이다 — 평균이 경계에 있으므로 회복·화력에 얼마나 투자했는지가")
    print("     그대로 결과가 된다. 실측 도달 후 생존이 43%인 것이 그 모습이다")
    print("  ※ 페이즈 2 정화를 더 키워도 여기는 거의 안 움직인다 — 도달 시 잠식이")
    print(f"     {BOSS_ARRIVE:.0f}이라 {BOSS_PHASE2_PURGE}을 넘는 몫은 잘려 버려진다")
    print("     (스윕 실측: 300 → 650으로 올려도 도달 후 생존 29.8% → 32.6%)")
    print("  ※ 보스 damage로 이 자리를 풀 수 없다 — slice_boss_damage는 patterns[0]")
    print("     삼연격의 **한 대** 값(63)을 임시로 쓰는 자리표시자이고, 설계된 패턴은")
    print("     캐스팅당 평균 217이다. 패턴 시스템이 붙으면 보스는 더 세진다\n")

    print("전체: " + ("PASS" if ok else "FAIL"))
    return ok


if __name__ == "__main__":
    report()
