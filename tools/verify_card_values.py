import math
"""각인·유물 등급별 기준 수치 검증.

수치를 각인마다 감으로 정하면 "어떤 각인이 센가"가 데이터 설계자의 취향이 된다.
그래서 **등급별 파워 예산을 먼저 고정하고, 각 각인의 고유 단위를 그 예산에서
역산**한다.

예산의 출처는 경험치 곡선이다 — `POWER_PER_LEVELUP`(레벨업 1회당 +7%)은 곧
**카드 한 장의 평균 가치**이고(레벨업당 정확히 한 장을 고르므로), 등급별 증가량
비 1 : 2 : 4(§4)를 확률로 가중평균하면 각 등급의 절대값이 나온다.

즉 §4 → §4 중복 규칙 → 여기가 한 줄로 이어진다. 곡선을 바꾸면 이 표가 움직인다.

기준선은 `balance_baseline.py`의 전사다. 각인은 스킬 하나에만 붙으므로
**회전 베기**(가중치 0.35, 광역)를 기준 스킬로 삼는다.
"""
import sys
sys.path.insert(0, "tools")

from balance_baseline import (
    HERO, SKILLS, PROC_RATE, TICK_HZ, TRASH, ARMOR_K, POWER_PER_LEVELUP,
    LEVEL_NEED_RATIO,
    hero_dps, effective_hp, ELITES,
)
from verify_card_rates import RATES

# ── 등급별 파워 예산 ───────────────────────────────────────────
from gamedata import CARDS as _CARDS, pm as _pm

GRADE_UNIT = {g["name"]: _pm(g["value_unit_permille"]) for g in _CARDS["grades"]}
GRADE_BUDGET = {g["name"]: _pm(g["power_budget_permille"]) for g in _CARDS["grades"]}
BUDGET_TOL = 0.30          # 각인별 편차 허용폭 (예산 대비 ±30%)

REF_SKILL = "회전 베기"

# ── 기준선 전투 가정 ───────────────────────────────────────────
# 예산 환산에 쓰는 가정. 전부 여기 모아둔다 — 흩어지면 검산이 안 된다.
PIERCE_TARGETS = 2.0       # 관통이 뒤로 추가로 맞히는 평균 적 수
SWARM_DENSITY = 5.0        # 주변 적 평균 수
AOE_TARGETS = 3.0          # 광역기 기본 타격 대상 수 (balance_baseline 목표 3과 동일)
DECAY_SEC = 4.0            # 부식 지속
RAGE_UPTIME = 0.5          # 분노 토템 최대 중첩 가동률
BOLT_PERIOD = 6.0          # 뇌전 주기(초)
ELITE_SHARE = 0.30         # 전투 시간 중 엘리트·보스를 때리는 비중

# ── 확정 수치 (고급 기준. 희귀 ×2, 영웅 ×4) ────────────────────
ENGRAVINGS = {e["id"]: (e["name"], e["desc"], _pm(e["uncommon_permille"]))
              for e in _CARDS["engravings"]}

RELICS = {r["id"]: (r["name"], r["desc"], _pm(r["uncommon_permille"]))
          for r in _CARDS["relics"]}

# 상한이 있는 수치는 초과분을 같은 축의 다른 수치로 전환한다.
# 상한에서 잘라버리면 그 카드가 죽은 선택지가 되고, 상한 없이 두면 표현이 깨진다.
OVERFLOW = _CARDS["overflow_rules"]

# 예산으로 환산할 수 없는 것 — 억지로 숫자를 붙이지 않고 따로 표시한다
UNPRICED = {
    "E_REND":   "기준선 몹 Armor가 대부분 0이라 상시 가치가 0이다. 방패병 전용 해답",
    "R_FROST":  "접근 지연(생존)이라 DPS 환산 기준이 없다",
    "R_GREED":  "전투력이 아니라 선택지 품질을 산다 (§6)",
}


# 빌드 종속 각인 — 기준선이 아니라 플레이어의 다른 투자 상태에 따라 값이 달라진다.
#
# **측정해보니 움직이는 것은 투자량이 아니라 배분이었다.** 치명타 아이템을
# 얼마나 껴도 예산비가 크게 오르지 않는다 — 기저가 커지면 분자(추가분)와
# 분모(현재 위력)가 같이 커지기 때문이다. 실제 손잡이는 **치피 대비 치확의 비**이고,
# 그다음이 각인 값 자체다 (고급 15% → 20%로 올린 것이 이 각인의 실제 해법이었다).
BUILD_SCALED = {
    "E_CRIT": ("치명타", [
        # (치확, 치피, 설명, 하한 검사 대상인가)
        # 기준선은 **약한 것이 설계**다(빌드 종속 각인). 투자 후 상태만 하한을 건다
        (0.10, 1.5, "기준선 — 투자 0. 약한 것이 설계다", False),
        (0.10, 2.0, "치피만 2.0 (아이템 없이)", True),
        (0.32, 2.0, "치명타 아이템 희귀함 1개 — 치확 편중", True),
        (0.21, 2.5, "치명타 아이템 희귀함 1개 — 치피 편중", True),
        (0.39, 2.5, "치명타 아이템 전설적인 1개", True),
    ]),
}
BUILD_SCALED_MIN = 0.55    # 투자 후 예산비 하한


def ref_skill_dps():
    """기준 스킬의 총 피해 DPS (기본 공격 대체분이 아니라 그 스킬이 내는 전량)."""
    aps = TICK_HZ / HERO["attack_interval_ticks"]
    crit = 1 + HERO["crit_chance"] * (HERO["crit_mult"] - 1)
    mult, weight, _aoe = SKILLS[REF_SKILL]
    proc_per_sec = aps * PROC_RATE * weight
    return proc_per_sec * HERO["attack_power"] * mult * crit, proc_per_sec


def engraving_delta(eid, v):
    """각인 1장이 기준선 총 DPS에 더하는 양."""
    total, _b, _a = hero_dps()
    skill_dps, proc_per_sec = ref_skill_dps()
    crit = 1 + HERO["crit_chance"] * (HERO["crit_mult"] - 1)
    if eid == "E_PIERCE":
        return skill_dps * PIERCE_TARGETS * v
    if eid == "E_CHAIN":
        return skill_dps * v
    if eid == "E_DECAY":
        return proc_per_sec * DECAY_SEC * HERO["attack_power"] * v
    if eid == "E_LEECH":
        return skill_dps * v          # 회복 1 = 피해 1로 환산
    if eid == "E_SWARM":
        return skill_dps * SWARM_DENSITY * v
    if eid == "E_CRIT":
        c, m = HERO["crit_chance"] + v, HERO["crit_mult"] + 2 * v
        return skill_dps * ((1 + c * (m - 1)) / crit - 1)
    if eid == "E_REND":
        # 방패병(Armor 200) 상대로만. 상시 가치가 아니므로 참고값이다
        before = ARMOR_K / (ARMOR_K + 200)
        after = ARMOR_K / (ARMOR_K + 200 * (1 - v))
        return skill_dps * (after / before - 1)
    if eid == "E_WIDE":
        # 반경 +v → 원 면적 (1+v)^2 배 → 타격 대상 수가 같은 비율로 는다
        return skill_dps * ((1 + v) ** 2 - 1)
    raise KeyError(eid)


def relic_delta(rid, v):
    """유물 1장이 기준선 총 DPS에 더하는 양. 유물은 스킬과 무관하게 전역 작동한다."""
    total, basic, _a = hero_dps()
    if rid == "R_RAGE":
        return total * (v * 10) * RAGE_UPTIME
    if rid == "R_BOLT":
        return HERO["attack_power"] * v / BOLT_PERIOD
    if rid == "R_BEACON":
        return total * v * ELITE_SHARE
    if rid == "R_TIDE":
        return total * v * 35.0 / 2      # 웨이브 평균 35초, 선형 증가 → 평균은 절반
    if rid == "R_GREED":
        # **경험치는 DPS로 환산된다.** 골드일 때는 기준이 없어 검산에서 빠져 있었는데,
        # 경험치로 바꾸면 경로가 닫힌다 — 경험치 +v → 레벨업 n회 추가 → 위력 1.07^n.
        #
        # 필요 경험치가 등비(ratio^n)라 누적 경험치 ×(1+v)는 레벨을
        # log(1+v)/log(ratio)회만큼 더 준다. 그 레벨이 각각 POWER_PER_LEVELUP만큼
        # 위력을 올린다.
        #
        # **런 평균으로 잡는다** — 효과가 0에서 시작해 종료 시점에 최대가 되므로
        # R_TIDE와 같은 이유로 절반을 쓴다.
        extra_levels = math.log(1 + v) / math.log(LEVEL_NEED_RATIO)
        end_gain = (1 + POWER_PER_LEVELUP) ** extra_levels - 1
        return total * end_gain / 2
    if rid == "R_FROST":
        return None
    raise KeyError(rid)


def report():
    ok = True
    total, basic, avg_mult = hero_dps()
    skill_dps, _p = ref_skill_dps()

    print("=== 등급별 파워 예산 (역산) ===")
    avg = sum(RATES[g] * GRADE_UNIT[g] for g in GRADE_UNIT)
    unit = POWER_PER_LEVELUP / avg
    print(f"  레벨업 1회당 위력 +{POWER_PER_LEVELUP:.0%} = 카드 한 장의 평균 가치")
    print(f"  등급 가중치 {GRADE_UNIT} → 평균 {avg:.3f}단위 → 1단위 {unit:.2%}")
    print(f"  {'등급':>4} {'역산':>7} {'확정':>7} {'기준선 DPS':>11}")
    for g in GRADE_BUDGET:
        print(f"  {g:>4} {GRADE_UNIT[g]*unit:>7.1%} {GRADE_BUDGET[g]:>7.1%} "
              f"{total*GRADE_BUDGET[g]:>10.2f}")
    got = sum(RATES[g] * GRADE_BUDGET[g] for g in GRADE_BUDGET)
    good = abs(got - POWER_PER_LEVELUP) / POWER_PER_LEVELUP < 0.05
    ok &= good
    print(f"  확정값 가중평균 {got:.2%} vs POWER_PER_LEVELUP {POWER_PER_LEVELUP:.0%}  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 이 한 줄이 경험치 곡선과 카드 수치를 잇는다. 곡선을 바꾸면 표 전체가 움직인다\n")

    budget = total * GRADE_BUDGET["고급"]
    print(f"=== 기준선 ===")
    print(f"  총 DPS {total:.2f} (기본 공격 {basic:.2f} = {basic/total:.0%})")
    print(f"  기준 스킬 {REF_SKILL}: 총 피해 DPS {skill_dps:.2f} "
          f"(= 총 DPS의 {skill_dps/total:.0%})")
    print(f"  고급 1장 예산 {budget:.2f} DPS = {REF_SKILL} 위력의 "
          f"**{budget/skill_dps:.0%}**")
    print("  ※ **각인은 스킬에만 붙는데 스킬이 총 DPS의 19%뿐이다.** 그래서 각인 한 장의")
    print("     수치가 스킬 기준으로는 크게 보인다 — 9칸을 채우면 스킬이 기본 공격을")
    print("     압도하도록 설계된 것이고, 그게 '스킬 빌드'가 성립하는 방식이다\n")

    print("=== 공통 각인 8종 (고급 / 희귀 / 영웅) ===")
    print(f"  {'ID':<10} {'이름':<5} {'고급':>7} {'희귀':>7} {'영웅':>7} "
          f"{'고급 ΔDPS':>10} {'예산비':>7}")
    print("  " + "-" * 74)
    for eid, (name, _fmt, v) in ENGRAVINGS.items():
        d = engraving_delta(eid, v)
        ratio = d / budget
        if eid in UNPRICED:
            flag = "  ※ 상황 가치"
        elif eid in BUILD_SCALED:
            flag = "  ※ 빌드 종속"
        elif abs(ratio - 1) > BUDGET_TOL:
            flag = "  ← 편차"
            ok = False
        else:
            flag = ""
        fv = (lambda x: f"{x:.0%}" if x >= 0.01 else f"{x:.1%}")
        print(f"  {eid:<10} {name:<5} {fv(v):>7} {fv(v*2):>7} {fv(v*4):>7} "
              f"{d:>10.2f} {ratio:>6.0%}{flag}")
    print()
    for eid, (name, fmt, v) in ENGRAVINGS.items():
        v2 = v * 2
        print(f"  {eid:<10} 고급: " + fmt.format(v=v, v2=v2))
    print()

    print("=== 일반 유물 6종 ===")
    print(f"  {'ID':<10} {'이름':<8} {'고급':>7} {'희귀':>7} {'영웅':>7} "
          f"{'고급 ΔDPS':>10} {'예산비':>7}")
    print("  " + "-" * 78)
    for rid, (name, _fmt, v) in RELICS.items():
        d = relic_delta(rid, v)
        fv = (lambda x: f"{x:.0%}" if x >= 0.01 else f"{x:.1%}")
        if d is None:
            print(f"  {rid:<10} {name:<8} {fv(v):>7} {fv(v*2):>7} {fv(v*4):>7} "
                  f"{'—':>10} {'—':>7}  ※")
            continue
        ratio = d / budget
        flag = "" if abs(ratio - 1) <= BUDGET_TOL else "  ← 편차"
        if abs(ratio - 1) > BUDGET_TOL:
            ok = False
        print(f"  {rid:<10} {name:<8} {fv(v):>7} {fv(v*2):>7} {fv(v*4):>7} "
              f"{d:>10.2f} {ratio:>6.0%}{flag}")
    print()
    for rid, (name, fmt, v) in RELICS.items():
        print(f"  {rid:<10} 고급: " + fmt.format(v=v))
    print()

    print("=== 빌드 종속 각인 — 무엇이 값을 움직이는가 ===")
    for eid, (axis, points) in BUILD_SCALED.items():
        name, _f, v = ENGRAVINGS[eid]
        print(f"  {eid} {name} — {axis} 축 (고급: 치확 +{v:.0%} / 치피 +{2*v:.0%}p)")
        print(f"    {'상태':<34} {'치확':>5} {'치피':>5} {'예산비':>7}")
        worst = 1.0
        for c0, m0, label, counts in points:
            f0 = 1 + c0 * (m0 - 1)
            c2, m2 = c0 + v, m0 + 2 * v
            if c2 > 1.0:
                m2 += 2 * (c2 - 1.0)
                c2 = 1.0
            r = skill_dps * ((1 + c2 * (m2 - 1)) / f0 - 1) / budget
            if counts:
                worst = min(worst, r)
            print(f"    {label:<34} {c0:>5.0%} {m0:>5.1f} {r:>7.0%}"
                  f"{'' if counts else '   (참고)'}")
        good = worst >= BUILD_SCALED_MIN
        ok &= good
        print(f"    투자 후 최저 {worst:.0%} (하한 {BUILD_SCALED_MIN:.0%})  "
              f"{'PASS' if good else 'FAIL'}")
    print("  ※ **아이템 투자량은 손잡이가 아니다.** 기저가 커지면 추가분과 현재 위력이")
    print("     같이 커지므로 예산비가 크게 오르지 않는다")
    print("  ※ 실제 손잡이는 **치피 대비 치확의 비**다. 치피가 높고 치확이 낮을수록")
    print("     E_CRIT이 채워줄 자리가 커진다 — 기준선이 낮은 것은 치피 1.5가 유독 낮은 탓이다")
    print("  ※ 그다음 손잡이가 각인 값 자체다. **고급 15% → 20%가 이 각인의 실제 해법**이었고,")
    print("     초과분 전환 규칙 덕에 영웅 상한에 걸리지 않는다:")
    for g, m in (("고급", 1), ("희귀", 2), ("영웅", 4)):
        name, _f, v = ENGRAVINGS["E_CRIT"]
        c2 = HERO["crit_chance"] + v * m
        d = engraving_delta("E_CRIT", v * m) / (total * GRADE_BUDGET[g])
        print(f"       {g} 1장 → 치확 {c2:.0%} / 치피 "
              f"{HERO['crit_mult'] + 2*v*m:.1f}, 예산비 {d:.0%}")
    print()

    print("=== 상한 초과분 전환 ===")
    for k, rule in OVERFLOW.items():
        print(f"  {k}: {rule}")
    print("  ※ 상한에서 잘라버리면 그 카드가 죽은 선택지가 되고, 상한 없이 두면")
    print("     '뒤의 적에게 400% 피해' 같은 표현이 나온다. 같은 축 안에서 넘긴다\n")

    print("=== 예산으로 환산하지 않는 것 ===")
    for k, why in UNPRICED.items():
        print(f"  {k}: {why}")
    print("  ※ 억지로 DPS를 붙이는 대신 **상황 가치**로 둔다. 몬테카를로 하네스(§14-10)에서")
    print("     '이 유물을 가진 판의 클리어율'로 사후 검증할 항목이다\n")

    print("=== 누적 4회 시점 (한 판 최대 누적 중앙값) ===")
    for eid in ("E_CHAIN", "E_PIERCE", "E_SWARM"):
        name, _f, v = ENGRAVINGS[eid]
        for g, m in (("고급", 1), ("영웅", 4)):
            d4 = engraving_delta(eid, v * m * 4)
            print(f"  {eid} {g} ×4 → 기준 스킬 위력 {1 + d4/skill_dps:.1f}배 "
                  f"(총 DPS +{d4/total:.0%})")
    print("  ※ 인위적 상한이 없어도 이 정도에서 멈춘다 (verify_duplicate_rules.py)\n")

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
