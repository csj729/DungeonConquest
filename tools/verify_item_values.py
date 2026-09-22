"""아이템 51종 수치 검산 (design.md §4 · items_vertical_slice.md).

## 이 도구가 지키는 것

**위력의 주력은 아이템 조합이다** — 한 판 성장의 70%. 카드(30%)와 달리 아이템은
조합으로 상위 등급이 되므로, 수치가 지켜야 할 조건이 카드보다 하나 많다.

  1. 아이템이 판 성장의 70%를 담당한다
  2. **조합이 항상 이득이다** — 상위 1개 > 하위 2개. 아니면 조합할 이유가 없다
  3. 흔함 1개가 희귀 카드보다 좋고 영웅 카드보다 나쁘다 — 이 부등식이
     "3장 중 영웅 이상일 때만 카드를 고른다"를 만들고, 그 결과가 곧 70:30이다
"""
import json
from pathlib import Path

import gamedata as gd
from balance_baseline import CARD_POWER_SHARE, POWER_PER_LEVELUP

ITEMS = gd.load("items")
CARDS = gd.CARDS
TIERS = ITEMS["grades"]
TIER_POWER = [x / 1000 for x in ITEMS["tier_power_permille"]]
PREMIUM = ITEMS["craft_premium_permille"] / 1000

# 맵 1개(8구간)의 레벨업 수. **balance_baseline의 TOTAL_LEVELUPS(60) ÷ 3맵**이다 —
# 처음엔 10으로 잡았는데 설계값의 절반이었고, 그 상태로 사다리를 풀면 흔함 값이
# 어긋난다. 모델 가정끼리 어긋나면 두 도구가 서로 다른 게임을 검산하게 된다.
LEVELUPS_PER_RUN = 20
CARD_GRADE = {g["name"]: g["power_budget_permille"] / 1000 for g in CARDS["grades"]}
CARD_RATE = {g["name"]: g["rate_permille"] / 1000 for g in CARDS["grades"]}


def report():
    ok = True
    total = (1 + POWER_PER_LEVELUP) ** LEVELUPS_PER_RUN

    print("=== 목표 1: 조합이 항상 이득이다 ===")
    print("  조합은 재료를 **소모**한다. 상위 1개가 하위 2개보다 못하면 조합할 이유가 없다.")
    print(f"  {'등급':>6} {'위력':>8} {'하위 2개':>9} {'이득':>7}")
    for i, t in enumerate(TIERS):
        if i == 0:
            print(f"  {t:>6} {TIER_POWER[0]:>7.1%} {'—':>9} {'—':>7}")
            continue
        two = 2 * TIER_POWER[i - 1]
        gain = TIER_POWER[i] / two
        good = gain > 1.0
        ok &= good
        print(f"  {t:>6} {TIER_POWER[i]:>7.1%} {two:>8.1%} {gain:>6.2f}배"
              f"{'' if good else '  ← 조합이 손해다'}")
    print(f"  조합 프리미엄 {PREMIUM:.2f}배  {'PASS' if ok else 'FAIL'}")
    print("  ※ 1.0이면 조합할 이유가 없고, 너무 크면 하위 아이템이 쓰레기가 된다\n")

    print("=== 목표 2: 흔함 1개가 희귀 카드와 영웅 카드 사이에 있다 ===")
    common = TIER_POWER[0]
    lo, hi = CARD_GRADE["희귀"], CARD_GRADE["영웅"]
    good = lo < common < hi
    ok &= good
    print(f"  희귀 카드 {lo:.1%} < 흔함 아이템 {common:.1%} < 영웅 카드 {hi:.1%}  "
          f"{'PASS' if good else 'FAIL'}")
    # 3장 중 영웅 이상이 뜰 확률 = 카드를 고를 확률
    below = CARD_RATE["일반"] + CARD_RATE["고급"] + CARD_RATE["희귀"]
    p_card = 1 - below ** CARDS["cards_per_level"]
    print(f"  → 3장 중 영웅 이상 {p_card:.1%} = 카드를 고를 확률")
    print(f"  → 판당 아이템 {(1 - p_card) * LEVELUPS_PER_RUN:.1f}회 · "
          f"카드 {p_card * LEVELUPS_PER_RUN:.1f}회")
    print("  ※ 이 부등식 하나가 선택 빈도를 만들고, 선택 빈도가 곧 위력 배분이다\n")

    print("=== 목표 3: 아이템이 판 성장의 70%를 담당한다 ===")
    draws = (1 - p_card) * LEVELUPS_PER_RUN
    # 흔함 draws개를 **위에서부터** 채운다 — 사다리가 등급당 2.5배라 상위 1개가
    # 하위 여럿보다 항상 강하므로, 최적 조합은 가능한 한 높이 올리는 것이다.
    spec = int(draws // 4)
    rem = draws - spec * 4
    unc = int(rem // 2)
    spare = rem - unc * 2
    held = ((1 + TIER_POWER[2]) ** spec * (1 + TIER_POWER[1]) ** unc
            * (1 + TIER_POWER[0]) ** spare)
    want = total ** (1 - CARD_POWER_SHARE)
    err = abs(held - want) / want
    good = err < 0.10
    ok &= good
    print(f"  판당 {draws:.1f}회 뽑아 특별함 {spec} + 안흔함 {unc} + 흔함 {spare:.1f}개로 조합")
    print(f"  보유 위력 {held:.3f}배 vs 설계 목표 {want:.3f}배 (오차 {err:.1%})  "
          f"{'PASS' if good else 'FAIL'}")
    print(f"  전체 성장 {total:.3f}배 = 아이템 {want:.3f} × 카드 {total ** CARD_POWER_SHARE:.3f}\n")

    print("=== 목표 4: 3연성 스킵이 일반 경로를 죽이지 않는다 ===")
    print("  items_vertical_slice.md §8이 남겨둔 미결 항목이다.")
    # 희귀함: 일반 경로 = 특별함 2개 = 안흔함 4개 / 스킵 = 안흔함 3개
    normal, skip = 4, 3
    print(f"  희귀함 1개 비용 — 일반 경로 안흔함 {normal}개 · 3연성 스킵 안흔함 {skip}개")
    print(f"  재료만 보면 스킵이 {(1 - skip / normal):.0%} 싸다")
    print("  ※ 다만 스킵은 **같은 안흔함 3개**를 요구한다. 흔함 9종이 균등하게 뜨면")
    print("     같은 종류를 3쌍 모으는 것이 서로 다른 4개를 모으는 것보다 훨씬 어렵다 —")
    print("     이 두 효과의 크기 비교는 뽑기 분포가 정해져야 계산된다. **미결이다**\n")

    print("=== 축별 스탯 배분 ===")
    for axis, stats in ITEMS["axis_stats_permille"].items():
        s = sum(stats.values())
        mark = "" if abs(s - 1000) <= 1 else f"  ← 합이 {s}"
        if abs(s - 1000) > 1:
            ok = False
        print(f"  {axis:<4} " + " · ".join(f"{k} {v / 10:.0f}%" for k, v in stats.items()) + mark)
    print("  ※ 한 축을 스탯 하나에 몰지 않는다 — 몰면 그 축 아이템끼리 완전 대체재가")
    print("     되어 조합 경로 선택이 사라진다 (§5 다경로의 전제)\n")

    print("전체: " + ("PASS" if ok else "FAIL"))
    return ok


if __name__ == "__main__":
    report()
