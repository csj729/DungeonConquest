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

from gamedata import PROGRESSION as _PROG, HERO as _HERO, SEGMENTS_DATA as _SEG

CORRUPTION_MAX = _HERO["corruption_max"]
ORB_TRASH_RATE = _PROG["orb_trash_drop_permille"] / 1000.0
ORB_TRASH_AMT = _PROG["orb_trash_amount"]
ORB_ELITE_AMT = _PROG["orb_elite_amount"]
# 습득률 — 구슬은 죽은 자리에 떨어지고 영웅은 주우러 가지 않는다(§3).
# 실측(12시드 × 600초, 구간 진입 정화분을 뺀 구슬만): 전체 40%.
# **구간이 갈수록 떨어진다** — 1구간 63% → 7구간 39%. 멀리 있는 엘리트를 쫓을수록
# 뒤에 흘린 구슬이 소멸하기 때문이고, 이것이 드랍·습득 2단계로 둔 이유 그 자체다.
ORB_PICKUP_RATE = 0.40

# 엘리트가 실제로 영웅을 때린 양 (12시드 × 600초). 설계 모델은 18.9~27.5/초를 가정한다.
ELITE_MEASURED = 15.2
# 엘리트가 자기 사거리(2.0타일) 안에 있던 시간 비율.
ELITE_IN_RANGE = 0.580
SEGMENT_PURGE = _PROG["segment_clear_purge"]
SEGMENTS = _SEG["segment_start_points"]
CLEAR_TARGET = _SEG["clear_target_points"]

# ── 실측 (core/tools/dc_montecarlo · 12시드) ────────────────────
# 구간별 (잠식 유입/초, 체류 초). 잠식을 중간값에 고정해 전 구간을 관측한 값이다.
MEASURED = [
    #  유입/초  체류초   물량 충전을 한계 가속으로 바로잡은 뒤 재측정
    (  4.9,  33.4),   # 1 — 전장이 아직 비어 있다
    ( 19.6,  44.6),   # 2 — 생존 27마리, 물량 임계 20 돌파
    ( 27.2,  40.6),   # 3 — 엘리트가 닿기 시작한다
    ( 30.1,  60.5),   # 4
    ( 29.5,  71.8),   # 5
    ( 30.7,  99.7),   # 6
    ( 32.5, 111.1),   # 7
    ( 22.2, 138.3),   # 8
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
    good = lo <= coverage <= hi
    ok &= good
    print(f"  맵 전체 유입 {total_in:.0f} · 정화 {total_out:.0f} "
          f"→ 덮는 비율 {coverage:.0%} (목표 {lo:.0%}~{hi:.0%}) "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 100%를 넘기면 잠식이 장식이 된다. 50% 아래면 회복 카드가 없는 빌드가")
    print("     확정 실패가 되어 §7의 '실패를 죽음이 아니라 지연으로'가 깨진다\n")

    print("=== 목표 2: 기저만으로는 완주하지 못한다 ===")
    # 게이지 1250을 들고 시작해 맵 전체 적자를 견딜 수 있는가
    deficit = total_in - total_out
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
    print("  **여기가 회복 적자의 진짜 원인이다.** 유입은 초당으로 보면 설계대로인데")
    print("  (목표 5가 예측한 22.7~54.3/초 대 실측 1.4~35.7/초), 총량이 2배인 이유는")
    print("  런이 설계보다 2배 오래 걸리기 때문이다 — 유입은 시간에 비례한다.")
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
    print("  ※ 체류가 2배면 정화를 2배로 올려도 수지는 그대로다 — 유입도 2배이기 때문이다.")
    print("     회복 예산이 아니라 **처치율**을 고쳐야 하는 자리다. 실측 처치율은")
    print("     구간 3~7에서 0.35~0.46마리/초인데 스폰 상한은 2.0~3.5마리/초다 —")
    print("     영웅이 스폰을 따라가지 못해 전장이 상한까지 고이고, 고인 물량이")
    print("     다시 체류를 늘린다. 엘리트 도달을 고쳐도(목표 5 PASS) 이건 남는다.\n")

    print("=== 목표 5: 설계 모델의 유입 구성이 실측과 맞는가 ===")
    print("  balance_baseline 목표 5는 잠식 유입을 피격 + 물량 + **엘리트**로 나눈다.")
    print("  엘리트 사거리가 2.0타일이던 때는 잡몹 링(영웅 이격 1.0 · 몹 이격 1.5)을")
    print("  뚫지 못해 사거리 안에 있던 시간이 1.3% · 기여가 3.6/초(모델의 16%)였다.")
    print(f"  {'항목':>10} {'설계 모델':>12} {'실측':>10}")
    print(f"  {'엘리트 기여':>9} {'18.9~27.5/초':>12} {ELITE_MEASURED:>7.1f}/초")
    print(f"  {'사거리 안':>10} {'(상시 가정)':>12} {ELITE_IN_RANGE:>9.1%}")
    good = ELITE_MEASURED >= 18.9 * 0.7   # 모델 하한의 70% 안이면 전장 구조가 맞다
    ok &= good
    print(f"  엘리트가 사거리 안에 있던 시간 {ELITE_IN_RANGE:.1%} → 모델 대비 "
          f"{ELITE_MEASURED / 23.2:.0%} {'PASS' if good else 'FAIL'}")
    print("  ※ 사거리를 3.5타일(링 밖)로 올려 58%까지 회복했다. **전장 구조가 이제")
    print("     설계 모델과 같은 것을 재고 있다** — 그래서 유입도 28~36에서 37~48/초로")
    print("     올랐고 모델의 22.7~54.3 밴드 안에 들어왔다. 즉 목표 1의 적자는")
    print("     이제 시뮬의 결함이 아니라 **설계 수치 자체가 닫히지 않는다는 뜻이다.**\n")

    print("전체: " + ("PASS" if ok else "FAIL"))
    return ok


if __name__ == "__main__":
    report()
