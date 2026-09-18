"""치명타 축 검증 — 수치가 아니라 **구조**를 먼저 본다.

치명타를 "뽑을 때마다 조금씩 오르는 스탯"으로 만들려면 증가량이 작아야 한다.
그런데 증가량의 크기는 취향이 아니라 **예산**이 정한다(cards_vertical_slice.md §0) —
카드 한 장은 등급별 위력 예산을 채워야 하고, 못 채우면 아무도 안 고른다.

그래서 순서를 뒤집는다. "얼마나 작게 만들 수 있는가"가 아니라
**"치명타 1%가 이 게임에서 얼마나 값어치가 있는가"** 를 먼저 재고, 거기서 역산한다.

두 가지가 치명타의 포인트당 가치를 누르고 있다:

1. **잡몹이 판 내내 기본 공격 1대로 죽는다** (balance_baseline 목표 1).
   치명타 배수가 전부 오버킬로 버려지므로, 영웅이 넣는 피해의 절반 이상에서
   치명타가 무의미하다
2. **기준선 치피가 1.5로 낮다.** 치확 1%가 올려주는 피해가 0.5%뿐이다

1번은 잡몹 HP를 10 → 30으로 올려 해소했다. 남은 것은 2번이다.
"""
import math
import sys

sys.path.insert(0, "tools")

from balance_baseline import (
    HERO, TRASH, hp_scale, effective_hp, ELITES, hero_dps, damage_mult,
)
from verify_segments import SEGMENTS as WAVES, SLICE_BOSS_HP, SLICE_BOSS_ARMOR, simulate
from verify_card_values import GRADE_BUDGET, ref_skill_dps

# 각인이었을 때의 값 — 비교 기준 (cards_vertical_slice.md §1)
ENGRAVE_CRIT = 0.20

# 목표: 카드 한 장의 치확 증가량이 이 이하면 "스탯이 조금씩 오르는" 느낌이 산다
INCREMENT_TARGET = 0.03


def trash_crit_util(rows):
    """잡몹 쪽에서 치명타가 버려지지 않는 비율.

    N대에 죽으면 마지막 한 대만 오버킬 위험이 있으므로 (N-1)/N로 잡는다.
    N=1이면 0 — 치명타 배수가 통째로 버려진다.
    """
    hs = []
    for i, _m, _r, _n, _e, _g, _s, _gain, cum, _et in rows:
        dmg = HERO["attack_power"] * damage_mult(cum)
        hs.append(TRASH["hp"] * hp_scale(i) / dmg)
    avg = sum(hs) / len(hs)
    return max(avg - 1, 0) / avg, avg


def crit_effective_share():
    """영웅이 넣는 총 피해 중 **치명타가 실제로 값을 갖는** 비중 (잡몹은 전량 낭비 가정)."""
    trash = sum((m + r) * TRASH["hp"] * hp_scale(i)
                for i, (m, r, _e, _n) in enumerate(WAVES, 1))
    elite = sum(effective_hp(ELITES[x]["hp"], ELITES[x]["armor"]) * hp_scale(i)
                for i, (_m, _r, e, _n) in enumerate(WAVES, 1) for x in e)
    boss = effective_hp(SLICE_BOSS_HP, SLICE_BOSS_ARMOR)
    return trash, elite, boss, (elite + boss) / (trash + elite + boss)


def solve_increment(pct, c0, m0, share, ratio=2.0):
    """예산 pct를 채우는 치확 증가량. 치피는 치확의 ratio배로 같이 오른다.

    share < 1이면 치명타가 값을 갖는 비중이 그만큼이라, 같은 예산을 채우는 데
    더 큰 증가량이 필요하다.
    """
    f0 = 1 + c0 * (m0 - 1)
    target = f0 * (1 + pct / share)
    # (c0+x)(m0-1+ratio*x) = target - 1
    a = ratio
    b = (m0 - 1) + ratio * c0
    c = c0 * (m0 - 1) - (target - 1)
    d = b * b - 4 * a * c
    if d < 0:
        return None
    x = (-b + math.sqrt(d)) / (2 * a)
    return x if c0 + x <= 1.0 else None


def report():
    ok = True
    total, basic, _a = hero_dps()
    skill_dps, _p = ref_skill_dps()

    print("=== 구조 1: 잡몹은 판 내내 기본 공격 1대로 죽는다 ===")
    rows, _bsec, _bg, _lv = simulate()
    print(f"  {'W':>2} {'몹 HP':>7} {'영웅 성장':>9} {'실효 공격력':>11} {'필요 타수':>9}")
    worst = 0.0
    for i, _m, _r, _n, _els, g, _sec, _gain, _cum, _et in rows:
        hp = TRASH["hp"] * hp_scale(i)
        ap = HERO["attack_power"] * g
        worst = max(worst, hp / ap)
        print(f"  {i:>2} {hp:>7.1f} {g:>8.2f}배 {ap:>11.1f} {hp / ap:>9.2f}")
    util, avg_hits = trash_crit_util(rows)
    good = avg_hits >= 2.0
    ok &= good
    print(f"  평균 필요 타수 {avg_hits:.2f}  {'PASS' if good else 'FAIL — 원샷이면 치명타가 버려진다'}")
    print(f"  → 잡몹 쪽 치명타 활용률 {util:.0%} (N대에 죽으면 (N-1)/N)")
    print("  ※ 잡몹 HP를 10 → 30으로 올린 것이 이 수치를 0%에서 끌어올린 변경이다\n")

    trash, elite, boss, _old = crit_effective_share()
    tot = trash + elite + boss
    share = (trash * util + elite + boss) / tot
    print("=== 구조 2: 그래서 치명타가 값을 갖는 피해 비중 ===")
    for lbl, v, u in (("잡몹", trash, util), ("엘리트", elite, 1.0), ("보스", boss, 1.0)):
        print(f"  {lbl:<8} {v:>8.0f} ({v / tot:>5.1%})  치명타 활용 {u:>4.0%}")
    print(f"  → **실효 비중 {share:.0%}**  (잡몹이 원샷이던 시절에는 46%였다)")
    print("  ※ 잡몹을 여러 대 때려야 죽게 만든 것이 치명타를 '정예 특효'에서")
    print("     '상시 유용한 스탯'으로 바꾼다 — 유물 `R_BEACON`과 역할이 갈린다\n")

    print("=== 각인 → 전역 스탯으로 옮기면 수치가 얼마나 줄어드는가 ===")
    print(f"  각인은 스킬 하나에만 걸린다 → 총 DPS의 {skill_dps / total:.0%}")
    print(f"  전역 스탯은 100%에 걸린다 → 같은 가치를 {total / skill_dps:.1f}배 작은 수치로 낸다")
    print(f"  (다만 치명타는 실효 {share:.0%}라 그만큼 도로 커진다)\n")

    print("=== 예산을 채우는 전역 치확 증가량 (치피는 2배로 동반 상승) ===")
    # 오버킬 이월은 검토 후 폐기했다 (새 메커니즘 비용 대비 이득이 작고,
    # 잡몹 HP 인상이 같은 문제를 더 싸게 푼다). 남은 손잡이는 기준선 치피뿐이다.
    cases = [("현행 (치피 1.5)", HERO["crit_mult"], share)] + \
           [(f"기준선 치피 {m:.1f}로 인상", m, share) for m in (2.0, 2.5, 3.0, 4.0)]
    print(f"  {'구조':<34} " + " ".join(f"{g:>7}" for g in GRADE_BUDGET if g != "전설"))
    print("  " + "-" * 70)
    best = None
    for label, m0, sh in cases:
        cells = []
        for g, p in GRADE_BUDGET.items():
            if g == "전설":
                continue
            x = solve_increment(p, HERO["crit_chance"], m0, sh)
            cells.append("상한초과" if x is None else f"{x:>6.1%}")
        common = solve_increment(GRADE_BUDGET["일반"], HERO["crit_chance"], m0, sh)
        if best is None or (common is not None and common < best[1]):
            best = (label, common)
        print(f"  {label:<34} " + " ".join(f"{c:>7}" for c in cells))
    print()
    print(f"  각인이었을 때의 고급값: 치확 +{ENGRAVE_CRIT:.0%} — 위 표와 비교할 것")
    print(f"  '1/10로 줄인다' = 고급 치확 +{ENGRAVE_CRIT / 10:.0%} 수준\n")

    print(f"=== 목표: 일반 등급 증가량이 {INCREMENT_TARGET:.0%} 이하 ===")
    for label, m0, sh in cases:
        x = solve_increment(GRADE_BUDGET["일반"], HERO["crit_chance"], m0, sh)
        good = x is not None and x <= INCREMENT_TARGET
        print(f"  {label:<34} {x:>6.1%}  {'PASS' if good else 'FAIL'}")
    print("  ※ **구조를 바꾸지 않으면 작은 수치가 나오지 않는다.** 증가량의 크기는")
    print("     취향이 아니라 예산이 정하고, 예산은 치명타의 포인트당 가치가 정한다\n")

    print("=== 참고: 반복 획득 시 누적 (치피 3.0 + 오버킬 이월, 일반 등급) ===")
    x = solve_increment(GRADE_BUDGET["일반"], HERO["crit_chance"], 3.0, 1.0)
    if x:
        c0, m0 = HERO["crit_chance"], 3.0
        f_prev = 1 + c0 * (m0 - 1)
        print(f"  {'획득':>4} {'치확':>6} {'치피':>6} {'계수':>6} {'직전 1장 가치':>12}")
        for n in range(0, 31, 5):
            c, m = c0 + x * n, m0 + 2 * x * n
            if c > 1.0:
                m += 2 * (c - 1.0)
                c = 1.0
            f = 1 + c * (m - 1)
            cp, mp = c0 + x * max(n - 1, 0), m0 + 2 * x * max(n - 1, 0)
            if cp > 1.0:
                mp += 2 * (cp - 1.0)
                cp = 1.0
            fp = 1 + cp * (mp - 1)
            print(f"  {n:>4} {c:>6.0%} {m:>6.2f} {f:>6.2f} "
                  f"{(f / fp - 1) if n else 0:>11.1%}")
        print("  ※ 한 장의 가치가 3.0% → 2.5%로 **거의 평평하다.** 30장을 쌓아도 예산에서")
        print("     크게 벗어나지 않는다 — 반복 획득형 스탯으로 정확히 원하는 성질이다")
        print("     (치피 기저가 높으면 치확 × 치피의 곱 효과와 분모 증가가 상쇄된다)\n")

    # 구조 진단(잡몹 타수)은 위 목표에서 이미 검사했다
    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
