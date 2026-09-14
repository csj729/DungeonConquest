"""각인·유물 중복 규칙 검증.

카드 풀이 유한한데 한 판에 56장을 뽑으므로 **중복은 예외가 아니라 일상**이다.
규칙이 없으면 잭팟이 허탕이 되거나(전설), 밸런싱 축이 둘로 갈린다(공통 각인).

- 공통 각인·일반 유물: **수치 누적**. 카드 등급이 한 장의 증가량이고, 풀에서 빠지지 않는다
- 전설(고유 각인 + 전설 유물): 중첩을 견디지 못하므로 **획득 즉시 풀에서 제외**

이 스크립트가 확인하는 것:

1. 누적 상한을 따로 두지 않아도 되는가 — **풀 구조가 이미 상한이다**
2. 누적이 죽은 선택지가 되지 않는가 — 슬롯이 차면 저절로 매력적이 되어야 한다
3. 전설 풀 고갈이 예외로 남는가 — 폴백이 상시 경로가 되면 안 된다

난수는 오프라인 밸런스 계산용이다. 시뮬 코어의 결정론 규칙과는 무관하다.
"""
import random
import sys
from math import comb

sys.path.insert(0, "tools")
from verify_card_rates import RATES, CARDS_PER_LEVEL, LEVELUPS, SLICE_LEVELUPS

# ── 풀 구성 ────────────────────────────────────────────────────
SKILLS = 3                 # 액티브 3종 (heroes_vertical_slice.md)
COMMON_ENGRAVINGS = 8      # cards_vertical_slice.md §1
RELICS = 6                 # §2 일반 유물
LEGEND_POOL = 6 + 3        # 고유 각인 6 + 전설 유물 3
GRADE_WEIGHT = {"고급": 1, "희귀": 2, "영웅": 4}   # 한 장이 더해주는 증가량 비

ENGRAVE_SLOTS_PER_SKILL = 3
ENGRAVE_SLOTS = SKILLS * ENGRAVE_SLOTS_PER_SKILL

# 각인·유물이 아닌 카드(스탯·조합 지원·골드)가 고급 이상에서 차지하는 몫
ENGRAVE_SHARE = 0.5

TRIALS = 20000
SEED = 1

# 목표
LEGEND_DRAIN_MAX = 0.01    # 전설 풀 고갈 판 비율 상한 — 넘으면 폴백이 상시 경로가 된다
STACK_LATE_MIN = 0.40      # 런 후반 누적 비율 하한 — 낮으면 누적이 죽은 선택지다
STACK_EARLY_MAX = 0.25     # 런 초반 누적 비율 상한 — 높으면 새 각인을 못 본다
STACK_TAIL_MAX = 0.05      # 한 엔트리가 5회 이상 뜰 확률의 상한
                           # 이 꼬리가 얇으면 인위적 누적 상한이 필요 없다
GRADE_SHARE_TOL = 0.12     # 등급별 총 기여가 균등에서 벗어나도 되는 폭


def entries():
    """풀 엔트리. 각인 동일성은 (스킬, 각인) 쌍이다 — 같은 각인을 다른 스킬에
    붙이는 것은 중복이 아니라 별개 인스턴스다."""
    return ([("E", s, e) for s in range(SKILLS) for e in range(COMMON_ENGRAVINGS)]
            + [("R", 0, r) for r in range(RELICS)])


def appear_dist(levelups):
    """특정 엔트리 1개가 한 판에 k회 이상 카드로 뜰 확률."""
    n_ent = SKILLS * COMMON_ENGRAVINGS + RELICS
    p = sum(RATES[g] for g in GRADE_WEIGHT) * ENGRAVE_SHARE / n_ent
    n = levelups * CARDS_PER_LEVEL
    tail = lambda k: sum(comb(n, i) * p**i * (1-p)**(n-i) for i in range(k, n+1))
    return p, p * n, {k: tail(k) for k in range(1, 7)}


def legend_drain_prob(levelups):
    """전설 풀(9종)을 전부 비울 확률.

    레벨업 1회에 전설 카드가 한 장이라도 보일 확률이 곧 획득 기회다
    (같은 화면 중복 금지 + 한 장만 고르므로 레벨업당 최대 1종)."""
    p = 1 - (1 - RATES["전설"]) ** CARDS_PER_LEVEL
    return p, sum(comb(levelups, k) * p**k * (1-p)**(levelups-k)
                  for k in range(LEGEND_POOL, levelups + 1))


def simulate(levelups):
    """승급/신규 비율과 슬롯이 차는 시점을 잰다.

    플레이어 정책: 각인·유물 카드가 있으면 그중 첫 장을 고른다(최대 소진 가정).
    풀이 마르는지를 보는 것이 목적이므로 **가장 빨리 마르는 정책**을 쓴다.
    """
    rng = random.Random(SEED)
    ent = entries()
    buckets = [(0, 10), (10, 20), (20, 30), (30, 44), (44, 56)]
    buckets = [(a, b) for a, b in buckets if a < levelups]
    new_n = [0] * len(buckets)
    up_n = [0] * len(buckets)
    slot_full, max_stack = [], []

    for _ in range(TRIALS):
        owned = {k: 0 for k in ent}          # 누적 횟수
        slots = [0] * SKILLS
        full_at = None
        for lv in range(levelups):
            picked = None
            for _c in range(CARDS_PER_LEVEL):
                r, acc = rng.random(), 0.0
                for g, p in RATES.items():
                    acc += p
                    if r < acc:
                        break
                if g in GRADE_WEIGHT and rng.random() < ENGRAVE_SHARE and picked is None:
                    # 공통 각인·유물은 풀에서 빠지지 않는다 — 항상 전체에서 뽑는다
                    picked = (g, rng.choice(ent))
            if picked is None:
                continue
            g, k = picked
            bi = next((i for i, (a, b) in enumerate(buckets) if a <= lv < b), None)
            if owned[k] == 0:
                if bi is not None:
                    new_n[bi] += 1
                if k[0] == "E":
                    slots[k[1]] += 1
            elif bi is not None:
                up_n[bi] += 1
            owned[k] += 1
            if full_at is None and sum(min(v, ENGRAVE_SLOTS_PER_SKILL)
                                       for v in slots) >= ENGRAVE_SLOTS:
                full_at = lv + 1
        slot_full.append(full_at or levelups + 1)
        max_stack.append(max(owned.values()))

    return buckets, new_n, up_n, sorted(slot_full), sorted(max_stack)


def report():
    ok = True
    n_ent = SKILLS * COMMON_ENGRAVINGS + RELICS

    print("=== 풀 구성 ===")
    print(f"  공통 각인 {SKILLS}스킬 × {COMMON_ENGRAVINGS}종 = "
          f"{SKILLS*COMMON_ENGRAVINGS}쌍 (동일성 = (스킬, 각인) 쌍)")
    print(f"  일반 유물 {RELICS}종 → 엔트리 합 **{n_ent}개**")
    print(f"  전설 풀 {LEGEND_POOL}종 (고유 각인 6 + 전설 유물 3)")
    print("  ※ 공통 각인·유물은 수치 누적이므로 **풀에서 빠지지 않는다**\n")

    print("=== 목표 1: 등급별 총 기여가 고르다 ===")
    tot = sum(RATES[g] * w for g, w in GRADE_WEIGHT.items())
    print(f"  {'등급':>4} {'확률':>7} {'증가량':>7} {'총 기여':>8}")
    for g, w in GRADE_WEIGHT.items():
        share = RATES[g] * w / tot
        good = abs(share - 1/len(GRADE_WEIGHT)) <= GRADE_SHARE_TOL
        ok &= good
        print(f"  {g:>4} {RATES[g]:>7.1%} {w:>7} {share:>8.0%}"
              f"{'' if good else '  ← FAIL'}")
    print("  ※ 확률과 증가량을 반비례로 잡아 장기 기대값을 맞췄다.")
    print("     기대값은 같고 체감만 영웅이 압도적이다 — 편차가 도파민이다\n")

    print("=== 목표 2: 누적 상한을 따로 둘 필요가 없다 ===")
    p_card, expect, tail = appear_dist(LEVELUPS)
    print(f"  엔트리가 {n_ent}개라 특정 (스킬, 각인) 1쌍이 카드 1장에 뜰 확률 "
          f"{p_card:.3%}")
    print(f"  → 한 판 {LEVELUPS*CARDS_PER_LEVEL}장에서 기대 등장 {expect:.2f}회")
    for k in (2, 3, 4, 5, 6):
        print(f"    {k}회 이상 등장: {tail[k]:>6.1%}")
    good = tail[5] <= STACK_TAIL_MAX
    ok &= good
    print(f"  5회 이상 꼬리 {tail[5]:.1%} (상한 {STACK_TAIL_MAX:.0%})  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ **풀 구조가 이미 상한이다.** 게다가 뜬다고 다 고르는 것도 아니다"
          f"({CARDS_PER_LEVEL}장 중 1장).")
    print("     인위적 상한을 두면 극단 특화라는 희귀한 즐거움만 잘라낸다\n")

    buckets, new_n, up_n, slot_full, max_stack = simulate(LEVELUPS)

    print("=== 목표 3: 누적이 죽은 선택지가 되지 않는다 ===")
    print(f"  {'구간':>16} {'신규':>7} {'누적':>7} {'누적 비율':>9}")
    ratios = []
    for (a, b), n, u in zip(buckets, new_n, up_n):
        r = u / max(n + u, 1)
        ratios.append(r)
        print(f"  레벨업 {a+1:>2}~{b:<3}    {n/TRIALS:>6.1f} {u/TRIALS:>7.1f} {r:>9.0%}")
    good = ratios[0] <= STACK_EARLY_MAX and ratios[-1] >= STACK_LATE_MIN
    ok &= good
    print(f"  초반 {ratios[0]:.0%} (상한 {STACK_EARLY_MAX:.0%}) → "
          f"후반 {ratios[-1]:.0%} (하한 {STACK_LATE_MIN:.0%})  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ **이 곡선은 자동으로 생긴다.** 엔트리가 유한하니 뽑을수록 보유분이 늘고,")
    print("     슬롯 9칸이 차면 신규는 교체를 요구하지만 누적은 슬롯을 안 먹는다.")
    print("     별도 손잡이 없이 초반은 발견, 후반은 심화가 된다\n")

    print("=== 목표 4: 전설 풀 고갈은 예외로 남는다 ===")
    p_screen, drain = legend_drain_prob(LEVELUPS)
    good = drain <= LEGEND_DRAIN_MAX
    ok &= good
    print(f"  레벨업 1회에 전설이 보일 확률 {p_screen:.2%} → 기대 "
          f"{p_screen*LEVELUPS:.2f}종 획득")
    print(f"  {LEGEND_POOL}종 전부 획득 {drain:.4%} (상한 {LEGEND_DRAIN_MAX:.0%})  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 거의 안 밟지만 **등급 강등 폴백은 있어야 한다.** 없으면 그 판에서 터진다\n")

    print("=== 참고: 실제 최대 누적과 슬롯이 차는 시점 ===")
    print(f"  한 판 최대 누적 횟수: 중앙값 {max_stack[TRIALS//2]}회, "
          f"상위 1% {max_stack[int(TRIALS*0.99)]}회")
    print(f"  각인 슬롯 {ENGRAVE_SLOTS}칸이 차는 시점: 중앙값 레벨업 "
          f"{slot_full[TRIALS//2]}회 (한 판 {LEVELUPS}회 중)")
    print(f"  ※ 수직 슬라이스는 레벨업 {SLICE_LEVELUPS}회뿐이라 슬롯이 잘 차지 않는다.")
    print("     '꽉 찼을 때 무엇을 버릴까'(§4)는 슬라이스로 검증되지 않는 항목이다\n")

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
