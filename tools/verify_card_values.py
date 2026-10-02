import math
"""각인·유물 등급별 기준 수치 검증.

수치를 각인마다 감으로 정하면 "어떤 각인이 센가"가 데이터 설계자의 취향이 된다.
그래서 **등급별 파워 예산을 먼저 고정하고, 각 각인의 고유 단위를 그 예산에서
역산**한다.

예산의 출처는 경험치 곡선이다 — `CARD_POWER_PER_LEVELUP`(레벨업 1회당 카드 몫)은 곧
**카드 한 장의 평균 가치**이고(레벨업당 정확히 한 장을 고르므로), 등급별 증가량
비 1 : 2 : 4(§4)를 확률로 가중평균하면 각 등급의 절대값이 나온다.

즉 §4 → §4 중복 규칙 → 여기가 한 줄로 이어진다. 곡선을 바꾸면 이 표가 움직인다.

기준선은 `balance_baseline.py`의 전사다. 각인은 스킬 하나에만 붙으므로
**회전 베기**(가중치 0.35, 광역)를 기준 스킬로 삼는다.

## 이 도구가 재지 않는 축이 있다

**여기 통과가 "밸런스가 맞다"를 뜻하지 않는다.** 이 도구는 기준선 DPS 대비 증분만
재고, 잠식 유입 ↔ 살아 있는 잡몹 수 ↔ 클리어 게이지의 되먹임은 모델에 없다.

실측(`core/tools/dc_legend` 1000시드)에서 고유 각인 6종은 **예산비가 95~100%로
같은데 클리어율 기여가 +0.5 ~ +13.4%p로 27배 벌어진다.** 잡몹을 빨리 치우는 효과가
게이지와 잠식 양쪽에 동시에 얹히기 때문이다. 어느 축을 예산의 정의로 삼을지는
`heroes_vertical_slice.md` §4에 세 선택지로 적어 두었고 **미결이다.**

## 전설 등급도 여기서 본다

한동안 이 도구는 **공통 각인 8종과 일반 유물 6종만** 검산했다. 그래서 전설 유물
3종이 예산의 몇 배인지 아무도 보지 않았고, `RL_ECHO`가 예산의 5.4배인 것이
빨간불로 뜨지 않았다 — 검산 밖에 있는 수치는 틀려도 조용하다.

지금은 전설 유물 3종과 고유 각인 6종을 함께 본다. 유물의 초과는 **의도된
미결 사항**(전설은 "판의 규칙을 바꾸는 급"이라는 설계 문구)이므로 FAIL로 두지
않고 `LEGEND_RELIC_OVER` 에 못 박는다 — 값이 움직이면 그때 걸린다.
"""
import sys
sys.path.insert(0, "tools")

from balance_baseline import (
    HERO, SKILLS, PROC_RATE, TICK_HZ, TRASH, ARMOR_K, CARD_POWER_PER_LEVELUP,
    LEVEL_NEED_RATIO,
    hero_dps, effective_hp, ELITES,
)
from verify_card_rates import RATES

# ── 등급별 파워 예산 ───────────────────────────────────────────
import gamedata as gd
from gamedata import CARDS as _CARDS, pm as _pm

GRADE_UNIT = {g["name"]: _pm(g["value_unit_permille"]) for g in _CARDS["grades"]}
GRADE_BUDGET = {g["name"]: _pm(g["power_budget_permille"]) for g in _CARDS["grades"]}
BUDGET_TOL = 0.30          # 각인별 편차 허용폭 (예산 대비 ±30%)

REF_SKILL = "회전 베기"

# 전설 유물의 예산 초과 배수. **PASS/FAIL이 아니라 못 박은 현상이다** —
# 전설 등급 예산(+15%)과 전설 유물 설계("판의 규칙을 바꾸는 급")가 어긋나 있고,
# 그 격차를 숫자로 고정해 둔다. 유물 수치를 건드리면 여기가 걸린다.
LEGEND_RELIC_OVER = {"RL_ECHO": 5.4, "RL_STORM": 3.4, "RL_FORGE": 0.5}
LEGEND_OVER_TOL = 0.15      # ±15% 안에서만 움직일 수 있다

# 고유 각인 6종의 예산 환산 가정. 전부 여기 모아둔다.
EXEC_TTK = {"잡몹": None, "엘리트": 9.5, "보스": 70.0}   # 잡몹은 기준선 DPS에서 계산
# 전투 시간 배분 — 엘리트·보스 안에서 보스 몫. 보스 70초 vs 엘리트 19마리×9.5초=180초
BOSS_SHARE_IN_ELITE = 70.0 / (70.0 + 19 * 9.5)
PIERCE_EXTRA_PER_WIDTH = 2.0 / 700      # E_PIERCE: 반폭 700에서 추가 2.0명 (선형 가정)

# 보스 처형 면역은 **몬스터 데이터가 정한다** (각인이 "보스면 제외"를 알지 않는다)
BOSS_EXECUTE_IMMUNE = bool(gd.load("monsters")["boss_execute_immune"])

# RL_STORM 반경 안 평균 대상 수 — dc_field 실측 (cards.json `_storm` 참조)
STORM_TARGETS = 5.4
# RL_FORGE 1회 발동의 위력 증가. 흔함 환산 1.64개분 × 흔함 1개의 위력
_ITEMS = gd.load("items")
FORGE_FIRST_GAIN = 1.64 * _pm(_ITEMS["tier_power_permille"][0])

# ── 기준선 전투 가정 ───────────────────────────────────────────
# 예산 환산에 쓰는 가정. 전부 여기 모아둔다 — 흩어지면 검산이 안 된다.
PIERCE_TARGETS = 2.0       # 관통이 뒤로 추가로 맞히는 평균 적 수
SWARM_DENSITY = 5.0        # 주변 적 평균 수
# 광역기 기본 타격 대상 수 (balance_baseline의 AOE_TARGETS_MIN과 같은 하한).
#
# **실측은 이보다 높다** — 광역 중심이 스킬마다 달라진 뒤 `aoeCenterOf` 기준으로
# 재면 회전 베기(hero) 4.17마리 · 대지 가르기(forward) 잡몹 3.45 + 엘리트 0.38이다.
# 3.0은 보수적인 하한으로 남긴다: 올리면 광역 각인 예산이 전부 내려가는데 실측값은
# 구간·물량에 따라 흔들리므로 하한 쪽이 안전하다.
AOE_TARGETS = 3.0
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
        # log(1+v)/log(ratio)회만큼 더 준다. 그 레벨이 각각 CARD_POWER_PER_LEVELUP만큼
        # 위력을 올린다.
        #
        # **런 평균으로 잡는다** — 효과가 0에서 시작해 종료 시점에 최대가 되므로
        # R_TIDE와 같은 이유로 절반을 쓴다.
        extra_levels = math.log(1 + v) / math.log(LEVEL_NEED_RATIO)
        end_gain = (1 + CARD_POWER_PER_LEVELUP) ** extra_levels - 1
        return total * end_gain / 2
    if rid == "R_FROST":
        return None
    raise KeyError(rid)


# ── 고유 각인 6종 ───────────────────────────────────────────────
UNIQUE = {e["id"]: e for e in _CARDS["unique_engravings"]}
UNIQUE_UNPRICED = {
    "W_FISSURE": "둔화(접근 지연)는 R_FROST와 같은 이유로 DPS 환산 기준이 없다",
}


def _exec_saving(threshold, rate, ttk):
    """처형이 줄이는 시간의 비율.

    임계 아래로 내려간 뒤 **처형 판정이 붙은 공격이 한 번 들어와야** 발동하므로,
    그 대기(지수분포)를 빼야 한다. 이 한 항이 "스킬 발동에 묶으면 전설이 될 수
    없다"를 만든다 — 발동 주기 10.8초가 임계 구간보다 길면 거의 못 쓴다.
    """
    win = ttk * threshold
    return (win - (1 - math.exp(-rate * win)) / rate) / ttk


def unique_delta(uid):
    """고유 각인 1장이 기준선 총 DPS에 더하는 양. 없으면 None (UNPRICED)."""
    e = UNIQUE[uid]
    total, _b, _a = hero_dps()
    skill_dps, proc_per_sec = ref_skill_dps()
    aps = TICK_HZ / HERO["attack_interval_ticks"]
    crit = 1 + HERO["crit_chance"] * (HERO["crit_mult"] - 1)
    # 각인이 붙은 스킬의 대상당 DPS (기준 스킬이 아니라 그 스킬이다)
    mult, weight, _aoe = SKILLS[{"W_SMASH": "분쇄 강타", "W_WHIRL": "회전 베기",
                                "W_CLEAVE": "대지 가르기"}[e["skill"]]]
    own_pps = aps * PROC_RATE * weight
    own_dps = own_pps * HERO["attack_power"] * mult * crit

    if uid == "W_EXECUTE":
        T = _pm(e["threshold_permille"])
        # scope가 all_attacks면 판정 빈도가 공속이다. 스킬에 묶이면 발동 빈도다.
        rate = aps if e["scope"] == "all_attacks" else own_pps
        ttk = {"잡몹": effective_hp(TRASH["hp"], 0) / total,
               "엘리트": EXEC_TTK["엘리트"], "보스": EXEC_TTK["보스"]}
        share = {"잡몹": 1 - ELITE_SHARE,
                 "엘리트": ELITE_SHARE * (1 - BOSS_SHARE_IN_ELITE),
                 "보스": ELITE_SHARE * BOSS_SHARE_IN_ELITE}
        if BOSS_EXECUTE_IMMUNE:
            share["보스"] = 0.0        # 면역이면 보스 몫은 가치가 아니다
        return total * sum(share[k] * _exec_saving(T, rate, ttk[k]) for k in ttk)

    if uid == "W_SHOCKWAVE":
        # 전력 피해로 직선의 적을 추가로 맞힌다. 추가 대상 수는 반폭에 비례한다
        extra = e["width_millitile"] * PIERCE_EXTRA_PER_WIDTH
        return own_dps * extra

    if uid == "W_VORTEX":
        # **즉발을 대체한다** — 잃은 즉발분까지 장판이 메워야 한다
        dur = e["duration_ticks"] / TICK_HZ
        field = own_pps * dur * HERO["attack_power"] * _pm(e["dps_permille"]) * AOE_TARGETS
        return field - own_dps * AOE_TARGETS

    if uid == "W_CENTRIFUGE":
        # 반경 +v/적 → 대상 수는 반경²에 비례 → 기준선 밀도에서의 이득
        step = _pm(e["radius_step_permille"])
        r = 1 + step * min(AOE_TARGETS, e["max_stacks"])
        return own_dps * AOE_TARGETS * (r ** 2 - 1)

    if uid == "W_AFTERSHOCK":
        return own_pps * HERO["attack_power"] * _pm(e["damage_permille"]) * AOE_TARGETS

    if uid == "W_FISSURE":
        return None
    raise KeyError(uid)


# ── 전설 유물 3종 ───────────────────────────────────────────────
def legend_relic_delta(rid):
    total, basic, _a = hero_dps()
    if rid == "RL_ECHO":
        # 기본 공격 1회 추가 발동. **스킬 proc은 굴리지 않는다**(§3)
        return basic
    if rid == "RL_STORM":
        # 반경 안 대상당 초당 공격력의 v%. 들어오는 수는 dc_field 실측이다
        v = _pm(_CARDS["storm_dps_permille"])
        return HERO["attack_power"] * v * STORM_TARGETS
    if rid == "RL_FORGE":
        # **첫 발동 1회만** 센다. 구간마다 반복되므로 누적은 획득 시점에 달렸고,
        # 그 민감도는 verify_item_values.py 목표 5가 따로 본다
        return total * FORGE_FIRST_GAIN
    raise KeyError(rid)


def report():
    ok = True
    total, basic, avg_mult = hero_dps()
    skill_dps, _p = ref_skill_dps()

    print("=== 등급별 파워 예산 (역산) ===")
    avg = sum(RATES[g] * GRADE_UNIT[g] for g in GRADE_UNIT)
    unit = CARD_POWER_PER_LEVELUP / avg
    print(f"  레벨업 1회당 카드 몫 +{CARD_POWER_PER_LEVELUP:.2%} = 카드 한 장의 평균 가치")
    print(f"  등급 가중치 {GRADE_UNIT} → 평균 {avg:.3f}단위 → 1단위 {unit:.2%}")
    print(f"  {'등급':>4} {'역산':>7} {'확정':>7} {'기준선 DPS':>11}")
    for g in GRADE_BUDGET:
        print(f"  {g:>4} {GRADE_UNIT[g]*unit:>7.1%} {GRADE_BUDGET[g]:>7.1%} "
              f"{total*GRADE_BUDGET[g]:>10.2f}")
    got = sum(RATES[g] * GRADE_BUDGET[g] for g in GRADE_BUDGET)
    good = abs(got - CARD_POWER_PER_LEVELUP) / CARD_POWER_PER_LEVELUP < 0.05
    ok &= good
    print(f"  확정값 가중평균 {got:.2%} vs 카드 몫 {CARD_POWER_PER_LEVELUP:.2%}  "
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

    legend_budget = total * GRADE_BUDGET["전설"]
    print("=== 고유 각인 6종 (전설 — 스킬당 2종) ===")
    print(f"  전설 1장 예산 {legend_budget:.2f} DPS (총 DPS의 {GRADE_BUDGET['전설']:.0%})")
    print("  ※ **배수로는 채울 수 없다.** 예산 ÷ 스킬 DPS:")
    for sid, sname in (("W_SMASH", "분쇄 강타"), ("W_WHIRL", "회전 베기"),
                       ("W_CLEAVE", "대지 가르기")):
        mult, weight, _a = SKILLS[sname]
        aps = TICK_HZ / HERO["attack_interval_ticks"]
        crit = 1 + HERO["crit_chance"] * (HERO["crit_mult"] - 1)
        pps = aps * PROC_RATE * weight
        dps = pps * HERO["attack_power"] * mult * crit
        print(f"     {sname:<7} 주기 {1/pps:5.1f}s · 대상당 {dps:4.2f} DPS → 예산의 "
              f"{legend_budget/dps:.2f}배가 필요하다")
    print(f"  {'ID':<14} {'이름':<5} {'스킬':<9} {'수치':>22} {'ΔDPS':>7} {'예산비':>7}")
    print("  " + "-" * 78)
    for uid, e in UNIQUE.items():
        d = unique_delta(uid)
        nums = " · ".join(f"{k.rsplit('_', 1)[0]} {v}"
                          for k, v in e.items()
                          if k.endswith(("_permille", "_ticks", "_millitile", "_stacks")))
        if d is None:
            print(f"  {uid:<14} {e['name']:<5} {e['skill']:<9} {nums:>22} "
                  f"{'—':>7} {'—':>7}  ※ 상황 가치")
            continue
        ratio = d / legend_budget
        flag = ""
        if abs(ratio - 1) > BUDGET_TOL:
            flag = "  ← 편차"
            ok = False
        print(f"  {uid:<14} {e['name']:<5} {e['skill']:<9} {nums:>22} "
              f"{d:>7.2f} {ratio:>6.0%}{flag}")
    print()
    for uid, e in UNIQUE.items():
        key = next((k for k in e if k.endswith("_permille")), None)
        v = _pm(e[key]) if key else 0.0
        print(f"  {uid:<14} " + e["desc"].format(v=v))
    print()
    for uid, why in UNIQUE_UNPRICED.items():
        print(f"  ※ {uid}: {why}")
    print("  ※ **처형이 all_attacks인 것은 예산이 강제한 것이다.** W_SMASH 발동에만")
    print("     묶으면 주기 10.8초가 임계 구간보다 길어 임계 50%에서도 예산의 39%다.")
    print("  ※ 보스 처형 면역(monsters.json)이 없으면 임계 400permille이 페이즈 2의")
    print("     80%를 생략한다 — 전설 각인 하나가 보스 시스템을 무력화한다\n")

    print("=== 전설 유물 3종 — **예산 초과가 못 박혀 있다** ===")
    print("  전설 등급 예산(+15%)과 전설 유물 설계('판의 규칙을 바꾸는 급')가")
    print("  어긋나 있다. FAIL로 두지 않고 배수를 고정해 둔다 — 움직이면 걸린다.")
    print(f"  {'ID':<10} {'ΔDPS':>8} {'예산비':>8} {'못 박은 값':>11} {'판정':>7}")
    print("  " + "-" * 52)
    for rid, pinned in LEGEND_RELIC_OVER.items():
        d = legend_relic_delta(rid)
        got = d / legend_budget
        good = abs(got - pinned) <= LEGEND_OVER_TOL * pinned
        ok &= good
        print(f"  {rid:<10} {d:>8.2f} {got:>7.2f}배 {pinned:>10.1f}배 "
              f"{'PASS' if good else 'FAIL':>7}")
    print("  ※ RL_ECHO는 기본 공격을 한 번 더 넣는다. 기본 공격이 총 DPS의")
    print(f"     {basic/total:.0%}이므로 그대로 +{basic/total:.0%}다 — 예산의 5.4배다.")
    print("  ※ RL_FORGE만 예산 **아래**다(0.5배). 구간마다 반복되므로 누적이 본체이고,")
    print("     그래서 획득 시점에 민감하다 — verify_item_values.py 목표 5가 따로 본다\n")

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
