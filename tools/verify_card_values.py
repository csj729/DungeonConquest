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
    hero_dps, effective_hp, ELITES,
)
from verify_card_rates import RATES

# ── 등급별 파워 예산 ───────────────────────────────────────────
GRADE_UNIT = {"일반": 0.6, "고급": 1, "희귀": 2, "영웅": 4, "전설": 10}
GRADE_BUDGET = {"일반": 0.03, "고급": 0.05, "희귀": 0.10, "영웅": 0.20, "전설": 0.50}
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
ENGRAVINGS = {
    "E_PIERCE": ("관통",   "뒤의 적에게 {v:.0%} 피해",            0.25),
    "E_CHAIN":  ("연타",   "총 피해 +{v:.0%} (2회 분할)",          0.45),
    "E_DECAY":  ("부식",   "초당 공격력의 {v:.0%}, 4초",           0.30),
    "E_LEECH":  ("흡혈",   "그 스킬 피해의 {v:.0%} 회복",          0.50),
    "E_SWARM":  ("군집",   "주변 적 1명당 +{v:.0%} (최대 10명)",   0.09),
    "E_CRIT":   ("예리함", "치확 +{v:.0%}, 치피 +{v2:.0%}p",       0.15),
    "E_REND":   ("파쇄",   "대상 Armor {v:.0%} 무시",              0.25),
    "E_WIDE":   ("확장",   "광역 반경 +{v:.0%} / 단일 여파 {v:.0%}", 0.20),
}

RELICS = {
    "R_RAGE":   ("분노의 토템",     "피격 시 공격력 +{v:.0%} (최대 10중첩)", 0.01),
    "R_BOLT":   ("뇌전의 성물",     "6초마다 공격력의 {v:.0%} 피해",         0.40),
    "R_FROST":  ("서리 오라",       "반경 내 적 이동속도 −{v:.0%}",          0.10),
    "R_BEACON": ("추적의 신호탄",   "엘리트·보스 피해 +{v:.0%}",             0.15),
    "R_TIDE":   ("밀물의 인장",     "웨이브 중 초당 공격력 +{v:.1%}",        0.003),
    "R_GREED":  ("탐욕의 주머니",   "골드 획득 +{v:.0%}",                    0.20),
}

# 상한이 있는 수치는 초과분을 같은 축의 다른 수치로 전환한다.
# 상한에서 잘라버리면 그 카드가 죽은 선택지가 되고, 상한 없이 두면 표현이 깨진다.
OVERFLOW = {
    "E_CRIT":   "치확 100% 초과분 1%p → 치피 +2%p",
    "E_PIERCE": "뒤의 적 피해 100% 초과분 100%p → 관통 대상 +1",
}

# 예산으로 환산할 수 없는 것 — 억지로 숫자를 붙이지 않고 따로 표시한다
UNPRICED = {
    "E_REND":   "기준선 몹 Armor가 대부분 0이라 상시 가치가 0이다. 방패병 전용 해답",
    "R_FROST":  "접근 지연(생존)이라 DPS 환산 기준이 없다",
    "R_GREED":  "전투력이 아니라 선택지 품질을 산다 (§6)",
}


# 아직 확정하지 않은 것 — 수치가 아니라 구조를 먼저 정해야 한다
PENDING = {
    "E_CRIT": "기준선 치확 10% / 치피 1.5가 약해 치명타 축이 예산을 담지 못한다",
}


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
    if rid in ("R_FROST", "R_GREED"):
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
        elif eid in PENDING:
            flag = "  ← 미결"
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

    print("=== 상한 초과분 전환 ===")
    for k, rule in OVERFLOW.items():
        print(f"  {k}: {rule}")
    print("  ※ 상한에서 잘라버리면 그 카드가 죽은 선택지가 되고, 상한 없이 두면")
    print("     '뒤의 적에게 400% 피해' 같은 표현이 나온다. 같은 축 안에서 넘긴다\n")

    print("=== 미결 ===")
    for k, why in PENDING.items():
        name, _f, v = ENGRAVINGS[k]
        print(f"  {k} {name}: {why}")
        print(f"    현재값(치확 +{v:.0%}/치피 +{2*v:.0%}p)은 예산의 "
              f"{engraving_delta(k, v)/budget:.0%}뿐이다. 선택지:")
        c = HERO["crit_chance"]
        print(f"    ① 각인 유지 + 값 인상 → 고급 치확 +35% 필요. "
              f"영웅(×4)이면 치확 +140%로 상한을 넘는다")
        print(f"    ② 기준선 치명타 인상(치확 25%/치피 2.0) → 기준선 DPS가 "
              f"12.98 → 15.45. 몹·보스 HP 전부 재역산")
        print(f"    ③ **유물로 옮긴다(전역 작동)** → 치확 +7.5%/치피 +7.5%p로 "
              f"예산 96%. 영웅(×4)도 치확 40%로 여유")
        print("    → ③ 추천. 각인은 스킬 하나(총 DPS의 11%)에만 걸려 치명타 같은")
        print("       곱셈 축을 담기엔 파이가 작다. 대신 공통 각인 자리가 하나 빈다\n")

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
