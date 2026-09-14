"""각인·유물 중복 규칙 검증.

카드 풀이 유한한데 한 판에 56장을 뽑으므로 **중복은 예외가 아니라 일상**이다.
규칙이 없으면 잭팟이 허탕이 되거나(전설), 밸런싱 축이 둘로 갈린다(공통 각인).

규칙은 문장 하나로 통일한다 — **"더 올릴 곳이 없으면 카드 풀에서 뺀다."**

- 공통 각인·일반 유물: 중복 = 등급 승급. 영웅 도달 시 풀에서 제외
- 전설(고유 각인 + 전설 유물): 등급이 하나뿐 → 획득 즉시 풀에서 제외

이 스크립트는 그 규칙이 실제로 굴러가는지를 확인한다 — 풀이 마르는가,
승급이 죽은 선택지가 되지 않는가, 폴백이 필요한가.

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
TIERS = 3                  # 고급 · 희귀 · 영웅

ENGRAVE_SLOTS_PER_SKILL = 3
ENGRAVE_SLOTS = SKILLS * ENGRAVE_SLOTS_PER_SKILL

# 각인·유물이 아닌 카드(스탯·조합 지원·골드)가 고급 이상에서 차지하는 몫
ENGRAVE_SHARE = 0.5

TRIALS = 20000
SEED = 1

# 목표
LEGEND_DRAIN_MAX = 0.01    # 전설 풀 고갈 판 비율 상한 — 넘으면 폴백이 상시 경로가 된다
UPGRADE_LATE_MIN = 0.40    # 런 후반 승급 비율 하한 — 낮으면 승급이 죽은 선택지다
UPGRADE_EARLY_MAX = 0.25   # 런 초반 승급 비율 상한 — 높으면 새 각인을 못 본다


def entries():
    """풀 엔트리. 각인 동일성은 (스킬, 각인) 쌍이다 — 같은 각인을 다른 스킬에
    붙이는 것은 중복이 아니라 별개 인스턴스다."""
    return ([("E", s, e) for s in range(SKILLS) for e in range(COMMON_ENGRAVINGS)]
            + [("R", 0, r) for r in range(RELICS)])


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
    grade_idx = {"고급": 1, "희귀": 2, "영웅": 3}
    buckets = [(0, 10), (10, 20), (20, 30), (30, 44), (44, 56)]
    buckets = [(a, b) for a, b in buckets if a < levelups]
    new_n = [0] * len(buckets)
    up_n = [0] * len(buckets)
    slot_full, drained = [], 0

    for _ in range(TRIALS):
        tier = {k: 0 for k in ent}
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
                if g in grade_idx and rng.random() < ENGRAVE_SHARE and picked is None:
                    live = [k for k in ent if tier[k] < TIERS]
                    if live:
                        picked = (g, rng.choice(live))
            if picked is None:
                continue
            g, k = picked
            bi = next((i for i, (a, b) in enumerate(buckets) if a <= lv < b), None)
            if tier[k] == 0:
                if bi is not None:
                    new_n[bi] += 1
                if k[0] == "E":
                    slots[k[1]] += 1
            elif bi is not None:
                up_n[bi] += 1
            tier[k] = max(tier[k] + 1, grade_idx[g])
            if full_at is None and sum(min(v, ENGRAVE_SLOTS_PER_SKILL)
                                       for v in slots) >= ENGRAVE_SLOTS:
                full_at = lv + 1
        if all(tier[k] >= TIERS for k in ent):
            drained += 1
        slot_full.append(full_at or levelups + 1)

    return buckets, new_n, up_n, sorted(slot_full), drained


def report():
    ok = True

    print("=== 풀 크기 ===")
    steps = (SKILLS * COMMON_ENGRAVINGS + RELICS) * TIERS
    print(f"  공통 각인 {SKILLS}스킬 × {COMMON_ENGRAVINGS}종 = "
          f"{SKILLS*COMMON_ENGRAVINGS}쌍, 일반 유물 {RELICS}종")
    print(f"  → 엔트리 {SKILLS*COMMON_ENGRAVINGS + RELICS}개 × {TIERS}단계 = "
          f"**{steps} 승급단계**")
    print(f"  전설 풀 {LEGEND_POOL}종 (고유 각인 6 + 전설 유물 3)\n")

    print("=== 목표 1: 공통 각인·유물 풀은 구조적으로 마르지 않는다 ===")
    good = steps > LEVELUPS
    ok &= good
    print(f"  승급단계 {steps} vs 한 판 최대 획득 {LEVELUPS}장  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 레벨업당 1장만 고르므로 획득 상한이 레벨업 횟수다.")
    print("     이 부등식이 깨지면 후반에 고급~영웅 슬롯이 비어 폴백이 상시 경로가 된다\n")

    print("=== 목표 2: 전설 풀 고갈은 예외로 남는다 ===")
    p_screen, drain = legend_drain_prob(LEVELUPS)
    good = drain <= LEGEND_DRAIN_MAX
    ok &= good
    print(f"  레벨업 1회에 전설이 보일 확률 {p_screen:.2%} → 기대 "
          f"{p_screen*LEVELUPS:.2f}종 획득")
    print(f"  {LEGEND_POOL}종 전부 획득(풀 고갈) {drain:.4%} (상한 "
          f"{LEGEND_DRAIN_MAX:.0%})  {'PASS' if good else 'FAIL'}")
    print(f"  {'획득 종수':>10} {'확률':>7}")
    for k in range(0, 7):
        print(f"  {k:>10} {comb(LEVELUPS,k)*p_screen**k*(1-p_screen)**(LEVELUPS-k):>7.1%}")
    print("  ※ 거의 안 밟지만 **폴백 규칙은 있어야 한다.** 없으면 그 판에서 터진다\n")

    buckets, new_n, up_n, slot_full, drained = simulate(LEVELUPS)

    print("=== 목표 3: 승급이 죽은 선택지가 되지 않는다 ===")
    print(f"  {'구간':>16} {'신규':>7} {'승급':>7} {'승급 비율':>9}")
    ratios = []
    for (a, b), n, u in zip(buckets, new_n, up_n):
        r = u / max(n + u, 1)
        ratios.append(r)
        print(f"  레벨업 {a+1:>2}~{b:<3}    {n/TRIALS:>6.1f} {u/TRIALS:>7.1f} {r:>9.0%}")
    good = ratios[0] <= UPGRADE_EARLY_MAX and ratios[-1] >= UPGRADE_LATE_MIN
    ok &= good
    print(f"  초반 {ratios[0]:.0%} (상한 {UPGRADE_EARLY_MAX:.0%}) → "
          f"후반 {ratios[-1]:.0%} (하한 {UPGRADE_LATE_MIN:.0%})  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ **이 곡선은 자동으로 생긴다.** 풀이 유한하니 뽑을수록 승급이 늘고,")
    print("     슬롯 9칸이 차면 신규는 교체를 요구하지만 승급은 슬롯을 안 먹는다.")
    print("     별도 손잡이 없이 초반은 발견, 후반은 심화가 된다\n")

    print("=== 참고: 각인 슬롯 9칸이 차는 시점 ===")
    med = slot_full[TRIALS // 2]
    print(f"  중앙값 레벨업 {med}회 (한 판 {LEVELUPS}회 중)")
    print(f"  ※ 수직 슬라이스는 레벨업 {SLICE_LEVELUPS}회뿐이라 슬롯이 잘 차지 않는다.")
    print("     '꽉 찼을 때 무엇을 버릴까'(§4)는 슬라이스로 검증되지 않는 항목이다\n")

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
