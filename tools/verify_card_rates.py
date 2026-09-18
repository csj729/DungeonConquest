"""레벨업 카드 등급별 등장 확률 검증.

전설에는 천장이 없으므로(design.md §4), 확률을 정하는 기준은 하나다 —
**전설 0회로 끝나는 판이 얼마나 자주 나오는가.** 너무 잦으면 캐릭터 정체성을
한 판 내내 못 보고, 너무 드물면 천장을 둔 것과 다를 게 없다.

레벨업 횟수는 `verify_exp_curve.py`에서 확정됐다(한 판 56회 = 카드 168장).
**장수가 늘면 전설이 흔해진다** — 곡선을 바꿀 때마다 이 파일도 다시 돌려야 한다.
"""
import sys
sys.path.insert(0, "tools")

# ── 등급별 확률 ────────────────────────────────────────────────
from gamedata import CARDS as _CARDS, pm

RATES = {g["name"]: pm(g["rate_permille"]) for g in _CARDS["grades"]}
CARDS_PER_LEVEL = _CARDS["cards_per_level"]   # 맨 왼쪽 고정 슬롯 제외
LEVELUPS = 60              # 풀 게임 한 판 (tools/verify_exp_curve.py)
SLICE_LEVELUPS = 20        # 맵 1개 = 수직 슬라이스
LEVELUP_RANGE = (45, 65)   # 곡선이 흔들릴 여지

# 목표
LEGEND_EXPECTED = (1.5, 3.5)   # 한 판 전설 기대 장수
LEGEND_ZERO_MAX = 0.15         # 전설 0회 판의 비율 상한
LEGEND_ZERO_MIN = 0.04         # 하한 — 이보다 낮으면 사실상 천장이 생긴 것이다
ENGRAVE_SLOTS = _CARDS["engrave_slots_per_skill"] * 3   # 스킬 3종 × 슬롯

# ── 수직 슬라이스 플레이테스트 전용 임시 확률 ──────────────────
# 슬라이스는 맵 1개(카드 75장)라 출하 확률 그대로면 전설 0회 판이 32%가 된다.
# 그렇다고 "자주 보이게" 올리면 플레이테스트에서 읽은 밸런스가 전부 무효다.
#
# 기준은 하나 — **전설 0회 판 비율을 풀 런과 같게 맞춘다.**
#   p_slice = 1 - (1 - p_full)^(풀 런 카드수 / 슬라이스 카드수)
# 잭팟이 얼마나 자주 비껴가는지가 보존되므로, 슬라이스에서 읽은 "전설 없이도
# 클리어되는가"(§4 불변조건)가 풀 런에서도 그대로 성립한다.
PLAYTEST_DONOR = _CARDS["playtest_donor_grade"]   # 늘어난 몫을 어디서 빼는가


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
    good = LEGEND_ZERO_MIN <= zero <= LEGEND_ZERO_MAX
    ok &= good
    print(f"  {zero:.1%} (목표 {LEGEND_ZERO_MIN:.0%}~{LEGEND_ZERO_MAX:.0%})  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 이 비율만큼의 판은 고유 각인을 한 번도 못 본다.")
    print("     '전설 0회 판도 클리어 가능해야 한다'는 불변조건이 여기에 걸린다 (§4)")
    print("  ※ **하한이 있는 이유**: 0회 판이 거의 없으면 천장을 둔 것과 같아진다.")
    print("     천장 없이 가기로 한 이상(§4), 안 나오는 판이 실제로 있어야 한다")
    zs = (1 - RATES["전설"]) ** (SLICE_LEVELUPS * CARDS_PER_LEVEL)
    print(f"  ※ 수직 슬라이스(맵 1개, 카드 {SLICE_LEVELUPS*CARDS_PER_LEVEL}장)만 보면 "
          f"0회 판 {zs:.0%} — 맵 1개는 런의 앞부분일 뿐이므로 정상이다.")
    print("     슬라이스 플레이테스트용 임시 확률은 맨 아래에서 역산한다\n")

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

    print("=== 수직 슬라이스 플레이테스트 임시 확률 ===")
    slice_cards = SLICE_LEVELUPS * CARDS_PER_LEVEL
    p_full = RATES["전설"]
    p_slice = 1 - (1 - p_full) ** (cards / slice_cards)
    p_slice = round(p_slice * 1000) / 1000          # 0.1%p 단위로 반올림
    donor = RATES[PLAYTEST_DONOR] - (p_slice - p_full)
    temp = dict(RATES)
    temp["전설"] = p_slice
    temp[PLAYTEST_DONOR] = donor

    print(f"  슬라이스 카드 {slice_cards}장 (레벨업 {SLICE_LEVELUPS}회)")
    print(f"  {'등급':>4} {'출하':>7} {'임시':>7}")
    for g in RATES:
        mark = "  ←" if abs(temp[g] - RATES[g]) > 1e-9 else ""
        print(f"  {g:>4} {RATES[g]:>7.1%} {temp[g]:>7.1%}{mark}")
    print(f"  합계 {sum(temp.values()):.3f}")

    zf = (1 - p_full) ** cards
    zt = (1 - p_slice) ** slice_cards
    good = abs(zt - zf) < 0.02
    ok &= good
    print(f"  전설 0회 판: 풀 런 {zf:.1%} vs 슬라이스 {zt:.1%}  "
          f"{'PASS' if good else 'FAIL'}")
    print(f"  전설 기대 장수: 풀 런 {p_full*cards:.2f}장 vs 슬라이스 "
          f"{p_slice*slice_cards:.2f}장")
    print(f"  ※ 늘어난 {p_slice - p_full:.1%}p는 **{PLAYTEST_DONOR} 등급에서만** 뺀다.")
    print("     고급·희귀·영웅을 건드리면 각인·유물 등급 티어 수치 검증이 같이 흔들린다\n")

    print("  이 값으로 검증되지 않는 것:")
    print(f"  - `RL_FORGE`의 획득 시점 민감도. 첫 전설이 카드 {1/p_slice:.0f}장째"
          f"(레벨 {1/p_slice/CARDS_PER_LEVEL:.0f})로 당겨져 '늦게 뜬 전설'을 볼 수 없다")
    dup2 = 1 - (9/9) * (8/9)
    print(f"  - 전설 중복 처리. 풀 9종에서 2장이면 중복 {dup2:.0%}라 슬라이스에서도"
          " 밟지만, 표본이 적어 규칙 검증에는 부족하다\n")

    print("  **연출 확인은 확률로 하지 않는다.** 전설 카드 등장 이펙트(§13)를 보려고")
    print("  확률을 올리면 그 판에서 읽은 밸런스가 전부 무효가 된다. 연출은 디버그")
    print("  커맨드로 강제 발급해서 확인하고, 확률은 이 표를 쓴다.\n")

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
