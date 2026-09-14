"""레벨업 카드 등급별 등장 확률 검증.

전설에는 천장이 없으므로(design.md §4), 확률을 정하는 기준은 하나다 —
**전설 0회로 끝나는 판이 얼마나 자주 나오는가.** 너무 잦으면 캐릭터 정체성을
한 판 내내 못 보고, 너무 드물면 천장을 둔 것과 다를 게 없다.

레벨업 횟수는 아직 미정(§15)이므로 범위로 감도를 본다.
"""

# ── 등급별 확률 ────────────────────────────────────────────────
RATES = {
    "일반": 0.42,
    "고급": 0.31,
    "희귀": 0.17,
    "영웅": 0.06,
    "전설": 0.04,
}

CARDS_PER_LEVEL = 3        # 레벨업당 성장 카드 3장 (맨 왼쪽 고정 슬롯 제외)
LEVELUPS = 20              # 한 판 기준 가정
LEVELUP_RANGE = (15, 25)   # §15 미결정이므로 범위로 확인

# 목표
LEGEND_EXPECTED = (1.2, 2.6)   # 한 판 전설 기대 장수
LEGEND_ZERO_MAX = 0.20         # 전설 0회 판의 비율 상한
ENGRAVE_SLOTS = 9              # 스킬 3종 × 3칸


def report():
    ok = True

    print("=== 등급별 확률 ===")
    total = sum(RATES.values())
    for g, p in RATES.items():
        print(f"  {g:>3}: {p:>6.1%}")
    print(f"  합계: {total:.3f}")
    ok &= abs(total - 1.0) < 1e-9
    print(f"  {'PASS' if abs(total - 1.0) < 1e-9 else 'FAIL — 합이 1이 아니다'}\n")

    cards = LEVELUPS * CARDS_PER_LEVEL
    print(f"=== 한 판 기준 (레벨업 {LEVELUPS}회 × {CARDS_PER_LEVEL}장 = {cards}장) ===")
    for g, p in RATES.items():
        print(f"  {g:>3}: 기대 {p*cards:>5.1f}장")
    print()

    print("=== 목표 1: 전설 기대 장수 ===")
    exp = RATES["전설"] * cards
    lo, hi = LEGEND_EXPECTED
    good = lo <= exp <= hi
    ok &= good
    print(f"  {exp:.2f}장 (목표 {lo}~{hi})  {'PASS' if good else 'FAIL'}\n")

    print("=== 목표 2: 전설 0회 판의 비율 ===")
    zero = (1 - RATES["전설"]) ** cards
    good = zero <= LEGEND_ZERO_MAX
    ok &= good
    print(f"  {zero:.1%} (상한 {LEGEND_ZERO_MAX:.0%})  {'PASS' if good else 'FAIL'}")
    print("  ※ 이 비율만큼의 판은 고유 각인을 한 번도 못 본다.")
    print("     '전설 0회 판도 클리어 가능해야 한다'는 불변조건이 여기에 걸린다 (§4)\n")

    print("=== 목표 3: 각인·유물 카드가 슬롯보다 많이 나온다 ===")
    # 각인·유물이 나오는 등급: 고급 이상. 그중 절반가량이 각인·유물이라 본다
    # (나머지는 스탯·조합 지원·골드)
    engrave_share = 0.5
    engrave_cards = sum(RATES[g] for g in ("고급", "희귀", "영웅", "전설")) * cards * engrave_share
    good = engrave_cards > ENGRAVE_SLOTS
    ok &= good
    print(f"  각인·유물 카드 기대 {engrave_cards:.1f}장 vs 슬롯 {ENGRAVE_SLOTS}칸  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 슬롯보다 많이 나와야 '무엇을 버릴까' 판단이 생긴다 (§4)\n")

    print("=== 목표 4: 레벨업 횟수가 흔들려도 버틴다 ===")
    lo_l, hi_l = LEVELUP_RANGE
    print(f"{'레벨업':>5} {'카드':>5} {'전설 기대':>9} {'전설 0회':>9}")
    print("  " + "-" * 34)
    for lv in range(lo_l, hi_l + 1, 2):
        c = lv * CARDS_PER_LEVEL
        e = RATES["전설"] * c
        z = (1 - RATES["전설"]) ** c
        flag = "" if z <= LEGEND_ZERO_MAX else "  ← 0회 판이 많다"
        if z > LEGEND_ZERO_MAX:
            ok = False
        print(f"{lv:>5} {c:>5} {e:>8.2f}장 {z:>8.1%}{flag}")
    print()

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
