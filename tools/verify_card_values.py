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

## 축이 둘이다 — 피해가 게이트, 클리어율은 기록

**피해 예산이 판정하고 클리어율은 판정하지 않는다.** 둘을 한 밴드로 묶으면 각인
하나를 고칠 때마다 5000시드(74분)를 돌려야 하고, 클리어율 3σ가 약 2%p라 그보다
촘촘한 조정은 측정으로 분간되지도 않는다. 그래서 **피해 예산을 게이트로 두고**
(`run_all.py` 3초) 클리어율은 **지배 빌드 감시용 기록**으로 나란히 적는다.

그런데 피해 예산이 놓치는 항이 하나 **도출된다** — 잠식 되먹임이다.

### 잠식 보정 — 런이 짧아지면 누적 잠식이 줄어든다

잡몹을 빨리 치우면 클리어 게이지가 빨리 차고 런이 짧아진다. 동시 생존은 스폰이
상한을 유지하므로 **줄지 않는다** — 줄어드는 것은 `유입률 × 시간`의 시간 쪽이다.
회피한 잠식은 `회복 1 = 피해 1` 규약으로 피해에 환산한다 (`E_LEECH`가 이미 쓰는
환산이다).

**이 항이 작지 않다.** 실측 런 단축으로 환산하면 소용돌이에 예산의 +70%가 얹혀
합계가 169%가 된다 — DPS만 보면 100%라 멀쩡해 보이는 각인이다.

원심력은 이 항으로 처음 잡혔는데, 추적해 보니 **별도 축이 아니라 피해 게이트가
잘못 매겨진 것**이었다(예산식이 다중 패스를 한 번만 셌다 — `centrifuge_targets`
주석 참조). 게이트를 고치고 상한을 10 → 3으로 내린 뒤 보정은 +25%로 내려왔다.
보정 항이 큰 값을 가리킬 때 **먼저 피해 게이트를 의심할 이유**가 생긴 사례다.

### 보정으로도 남는 잔차

보정은 **순위를 맞추지 못한다.** 소용돌이는 보정 +70%인데 Δ클리어가 +2.5%p이고,
여진은 보정 +13%인데 +4.5%p다 — 순서가 뒤집혀 있다. 설명되지 않는 축이 최소 둘 더
있다 (`heroes_vertical_slice.md` §4):

- **성장 되먹임** — 런이 짧으면 경험치가 덜 쌓여 보스 앞에서 레벨이 낮다. 처형이
  도달률을 99.2%로 올리고도 도달 후 생존이 내려가는 이유다. 런 단축이 **양쪽으로**
  작용하므로 단일 계수로 담을 수 없다
- **관문 비대칭** — 보스는 처형 면역이고 실효 체력이 커서 "잡몹을 빨리 치우는" 효과가
  거기서는 값이 없다

그래서 아래 표의 `Δ클리어`는 **밴드가 아니라 기록**이다. 걸리는 것은 지배 빌드
하나뿐이다 — 한 각인이 나머지를 압도하면 선택이 사라진다.

## 이 도구가 재지 않는 축이 있다

**여기 통과가 "밸런스가 맞다"를 뜻하지 않는다.** 피해 예산 + 잠식 보정까지 맞아도
위 잔차 두 축은 모델 밖이다.

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
# **잠식 유입률은 verify_recovery의 실측에서 가중평균한다.** 여기 숫자를 다시
# 적으면 두 곳에 사는 값이 되고, 한쪽만 고치는 사고가 난다.
from verify_recovery import MEASURED as _RECOVERY_MEASURED

MAP_INFLOW = (sum(i * s for i, s in _RECOVERY_MEASURED)
              / sum(s for _, s in _RECOVERY_MEASURED))

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
# ── 직선 관통 추가 대상 수 — `core/tools/dc_field` 실측 ──────────
#
# **가정이 2.3배 틀려 있었다.** 전에는 `PIERCE_TARGETS = 2.0`(반폭 700)을 "기준선
# 전투 가정"으로 두고 반폭에 선형 외삽했다. 실측하니 반폭 700에서 **0.86명**이다.
#
# 이유는 기하다. `collectInLine`은 타겟 **뒤쪽만** 센다 — 타겟에서 밖으로 3.0타일,
# 반폭 안. 영웅은 사거리 근처(2.6타일)에서 멈추므로 그 뒤는 스폰 링 쪽이고, 거기
# 밀도가 영웅 주변(광역 반경 안)과 같을 이유가 없었다.
#
# **선형 형태 자체는 맞았다** — 175~700 구간에서 반폭당 0.0012명/permille로 거의
# 일정하다. 틀린 것은 기준점이다. 큰 반폭에서는 살짝 초선형이라(링을 더 많이
# 걸친다) 표를 그대로 두고 보간한다.
#
# 재측정: `cmake --build build/release --target dc_field && ./build/release/core/dc_field`
# (시드 24개 × 틱 표본 1999개 평균. 광역 반경 절은 단일 스냅샷이라 더 약하다)
PIERCE_EXTRA_MEASURED = [
    # 반폭(millitile), 추가 대상 수
    (175, 0.20), (350, 0.42), (525, 0.63), (700, 0.86), (1050, 1.41), (1400, 2.16),
]


# ── 충격파 기하 — **관통과 다른 표를 쓴다** ──────────────────────
#
# 충격파의 경로는 **영웅에서** 시작한다(관통은 타겟 뒤부터다). 위 표는 dc_field가
# 타겟 뒤 띠에서 잰 것이라 쓸 수 없다 — 띠가 다르면 밀도가 다르다.
#
# 이 표는 `dc_legend 400`이 **런 안에서** 수직 거리 히스토그램으로 잰 값이다.
# 폭 스윕을 코어에 박지 않으려고 히스토그램으로 받아 누적합을 낸다.
#
# 접근 구간 값을 쓴다 — 런 시간의 대부분이다. **보스전 값이 이제 14% 안에 있다**:
# 반폭 850에서 실제 발동당 1.55(접근) · 1.33(보스전)이다(5000시드). 같은 반폭에서
# 타겟 뒤 띠만 쓰면 0.95 → 0.36으로 62% 무너진다 — 그것이 이 각인의 고장이었다.
SHOCK_EXTRA_MEASURED = [
    # 반폭(millitile), 발동당 추가 대상 수 (접근 구간)
    (100, 0.21), (200, 0.41), (300, 0.61), (400, 0.78), (500, 0.95),
    (600, 1.11), (700, 1.28), (800, 1.47), (900, 1.76), (1000, 2.63),
    (1100, 3.04), (1200, 3.46),
]
# **900 위는 절벽이다.** 0.9~1.0타일 칸 하나에 0.87명이 몰려 있다 — 분리 거리가
# 만든 껍질이다. 그 위에서 폭을 고르면 예산이 분리 거리 튜닝에 딸려 가므로,
# 이 선 아래에서 고른다. 넘으면 아래 가드가 걸린다.
SHOCK_WIDTH_CLIFF = 900


def _interp(table, x):
    """실측표 선형 보간. 표 아래는 원점에서 선형, 위는 마지막 기울기로 외삽."""
    if x <= table[0][0]:
        return table[0][1] * x / table[0][0]
    for (x0, y0), (x1, y1) in zip(table, table[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    x0, y0 = table[-2]
    x1, y1 = table[-1]
    return y1 + (y1 - y0) * (x - x1) / (x1 - x0)


def shock_extra(width_millitile):
    """충격파 반폭 → 발동당 추가 대상 수 (영웅에서 시작하는 경로)."""
    return _interp(SHOCK_EXTRA_MEASURED, width_millitile)


def pierce_extra(width_millitile):
    """반폭에서 타겟 뒤로 추가로 맞는 평균 적 수. 실측표를 선형 보간한다."""
    t = PIERCE_EXTRA_MEASURED
    if width_millitile <= t[0][0]:
        return t[0][1] * width_millitile / t[0][0]      # 원점에서 선형
    for (w0, v0), (w1, v1) in zip(t, t[1:]):
        if width_millitile <= w1:
            return v0 + (v1 - v0) * (width_millitile - w0) / (w1 - w0)
    w0, v0 = t[-2]
    w1, v1 = t[-1]
    return v1 + (v1 - v0) * (width_millitile - w1) / (w1 - w0)   # 바깥은 외삽

# 보스 처형 면역은 **몬스터 데이터가 정한다** (각인이 "보스면 제외"를 알지 않는다)
BOSS_EXECUTE_IMMUNE = bool(gd.load("monsters")["boss_execute_immune"])

# RL_STORM 반경 안 평균 대상 수 — dc_field 실측 (cards.json `_storm` 참조)
STORM_TARGETS = 5.4
# RL_FORGE 1회 발동의 위력 증가. 흔함 환산 1.64개분 × 흔함 1개의 위력
_ITEMS = gd.load("items")
FORGE_FIRST_GAIN = 1.64 * _pm(_ITEMS["tier_power_permille"][0])

# ── 기준선 전투 가정 ───────────────────────────────────────────
# 예산 환산에 쓰는 가정. 전부 여기 모아둔다 — 흩어지면 검산이 안 된다.
# 관통이 뒤로 추가로 맞히는 평균 적 수 — **데이터의 반폭에서 실측값을 읽는다**
PIERCE_TARGETS = pierce_extra(_CARDS["pierce_width_millitile"])
SWARM_DENSITY = 5.0        # 주변 적 평균 수
# ── 고유 각인 실측 (`core/tools/dc_legend` 5000시드 · QTE 항상 완벽) ──────
#
# **손으로 적은 값이 아니라 도구가 낸다.** 수치를 바꾸면 다시 잰다:
#   `cmake --build build/release --target dc_legend && ./build/release/core/dc_legend 5000`
#
# 1000시드 아래로 내려가지 말 것 — 절대 클리어율이 시드 집합에 민감하다(같은
# 설정에서 200시드 15.5% · 400시드 13.2% · 1000시드 9.6%). 한 실행 안의 Δ만
# 비교할 수 있고 실행 사이 절대값은 비교할 수 없다.
#
# **5000시드로 올렸다**(74분). 1000시드에서 3σ가 약 4%p였는데 실제 Δ가 3~5%p라
# 여섯 종 중 둘만 유의했다 — 순위를 말할 해상도가 아니었다. 5000시드에서 3σ가
# 약 2%p로 내려가 다섯 종이 유의해졌다.
LEGEND_BASE_SEC   = 354.2   # 각인 없음 — 런 길이(초)
LEGEND_BASE_CLEAR = 9.6     # 각인 없음 — 클리어율(%)
LEGEND_MEASURED = {
    # id            런 길이  클리어율
    "W_EXECUTE":    (327.3, 13.1),
    "W_SHOCKWAVE":  (344.9, 14.9),
    "W_VORTEX":     (328.9, 15.2),
    "W_CENTRIFUGE": (341.3, 13.2),
    "W_AFTERSHOCK": (350.1, 14.3),
    "W_FISSURE":    (352.3, 14.6),
}
# **이 지문과 위 표는 같은 실행에서 함께 적는다.** 하나만 고치면 거짓말이 된다.
#
# 범위 구멍으로 **세 번** 당했다. 손으로 고른 키 목록(고유 각인만 담아 E_PIERCE
# 변경을 놓쳤다) → 데이터 전체 해시 → 그래도 **코드가 빠져 있었다**: 장판 흡혈
# 버그를 고쳤을 때 `data/*.json`은 한 글자도 바뀌지 않아 지문이 그대로였고, 이
# 도구는 고치기 전 소용돌이 행(+2.2%p)을 들고 PASS를 찍었다. 실제로는 +5.5%p로
# 움직였다 — 같은 종류의 구멍을 세 번째로 밟았다.
#
# 그래서 지금은 `gd.sim_fingerprint()`를 쓴다 — `data/*.json` + 코어 헤더
# (주석 제외)다. **측정을 무효화하는 것은 데이터만이 아니라 시뮬 코드다.**
# 입막음은 여전히 가능하지만, 지문이 손으로 지어낼 수 없는 값이라 "측정 없이
# 갱신"이 눈에 띈다.
LEGEND_MEASURED_FINGERPRINT = "3990042d641e79b8"
# **표 전체가 한 실행에서 나온다.** 원심력만 바꿨는데 다른 행도 ±0.5초 움직이는데,
# 강제 부여는 틱 0의 한 장뿐이고 **나머지 런에서도 원심력이 나중에 뽑힐 수 있기**
# 때문이다. 그래서 한 행만 갈아 끼우면 안 되고 기준선까지 같이 다시 적는다.
#
# 소용돌이 행만 두 실행에서 소수점까지 동일했다 — 소용돌이가 즉발을 장판으로
# **대체**하며 early return하므로 원심력이 아무 일도 하지 않는다(둘 다 회전 베기다).
# 측정이 깨끗한지 보는 교차 확인으로도 쓰인다. **그리고 그때 그 조합은 죽은 조합이었다**
# — 둘을 같이 뽑으면 원심력이 무효였다. 이후 원심력이 장판 반경을 키우도록 고쳤다
# (`centrifugeRadius`). 2장 예산 대비 50% → 122%다.
#
# **조합은 여기서 판정하지 않는다.** 이 도구는 카드 한 장씩 값을 매기고 dc_legend도
# 전설을 하나만 강제하므로, 전설 × 전설은 어느 쪽에서도 보이지 않는다. 짝 하나만
# 게이트에 넣으면 나머지를 안 보는 것이 더 또렷해지므로 넣지 않았다 — 조합 전반은
# dc_montecarlo(§14-10) 몫이다.
# 측정에 쓴 시드 수. 3σ가 여기서 나온다.
LEGEND_SEEDS = 5000


def clear_3sigma(p_base, p_var, n=LEGEND_SEEDS):
    """두 클리어율 차이의 3σ(%p). **고정 상수로 두면 안 된다.**

    전에는 `LEGEND_CLEAR_3SIGMA = 4.8`을 박아 뒀는데 그건 p≈0.15에서 계산한
    값이다. 원심력 상한을 내린 뒤 실제 비율이 9.3~13.8%로 내려가면서 **임계값이
    너무 보수적**이 됐고, 도구가 "유의한 각인 0종"이라고 찍었다 — 실제 3σ는
    3.95~4.28%p이고 여진(+4.5%p)은 3.15σ로 유의하다.

    이항 비율의 분산은 p(1−p)라 **p에 따라 움직인다.** 임계값을 상수로 박으면
    수치를 고칠 때마다 조용히 틀어진다. 그래서 측정값에서 매번 계산한다.

    같은 시드 집합을 쓰므로(공통 난수) 실제 분산은 이보다 **작다** — 독립 표본
    가정이라 보수적인 쪽이다. 런이 틱 0 이후 갈라지므로 상관이 완전하지는 않아
    짝지은 분산을 쓰지 않는다. 보수적인 쪽으로 틀리는 편을 고른다.
    """
    a, b = p_base / 100.0, p_var / 100.0
    return 3.0 * math.sqrt(a * (1 - a) / n + b * (1 - b) / n) * 100.0



def _measurement_is_current():
    """`LEGEND_MEASURED`가 현재 데이터에서 나온 값인지. (어긋남, 설명) 목록."""
    live = gd.sim_fingerprint()
    if live == LEGEND_MEASURED_FINGERPRINT:
        return []
    return [(live, LEGEND_MEASURED_FINGERPRINT)]

# **지배 빌드 감시** — 클리어율에서 판정하는 유일한 것이다 (나머지는 기록).
#
# 분모를 두 번 갈았다. 처음엔 "1위 ÷ 2위"였는데 2위가 3σ 아래라 분모가 노이즈였고,
# 그래서 **3σ를 눈금으로** 썼다(Δ ÷ 3σ). 그런데 그 눈금은 **시드 수에 딸려 있다** —
# 1000 → 5000시드로 올리기만 해도 3σ가 √5배 작아져 지배도가 1.25배 → 2.80배로
# 뛴다. 밸런스는 한 글자도 안 바뀌었는데 게이트가 상한(3.0)에 바짝 붙는다.
#
# **측정을 더 정밀하게 했다는 이유로 게임이 더 지배적으로 보이는 지표는 지표가
# 아니다.** 상수가 아니라 전제가 낡는, 이 파일이 세 번 밟은 그 종류다.
#
# 5000시드에서 2위(균열 +4.8%p)가 7σ 넘게 유의하므로 **원래 분모로 돌아간다**:
# 지배도 = 1위 Δ ÷ 2위 Δ. 비율이라 시드 수에 무관하다. 지금 5.5 ÷ 4.8 = 1.15배이고,
# 짝 검정도 소용돌이 vs 균열을 +1.00σ(구분되지 않음)로 읽는다 — 지배는 없다.
#
# 2위가 유의하지 않은 상태로 돌아가면 분모가 다시 노이즈가 되므로, 그때는 비율을
# 내지 않고 3σ 눈금으로 떨어뜨리고 **그렇게 적었다고 밝힌다**(아래 출력).
#
# **유의하지 않은 것과 약한 것을 섞지 말 것.** 절대 %p는 측정의 해상도이고 설계
# 판단은 상대로 한다 — 유의하지 않은 쪽은 지금 충격파(+8% 상대) 하나다.
DOMINANT_RATIO_MAX = 1.50   # 1위 Δ ÷ 2위 Δ (2위가 유의할 때)
DOMINANT_SIGMA_MAX = 3.0    # 대체 눈금: 1위 Δ ÷ 3σ (2위가 유의하지 않을 때)

# ── 전설은 평균이 아니라 바닥과 상한으로 본다 ──────────────────
#
# 전설을 동일성(가중평균)에서 뺐다. 1.5% 확률에 10배 분산인 등급을 평균 기반
# 페이싱 식에 넣는 것은 모델링 오류다 — 전설은 페이싱이 아니라 스파이크다.
# 게다가 그 식은 **선언 예산**으로 돌아서 유물이 3~5배인 것을 한 번도 보지 않았다.
#
# 빼고 보면 문제의 성격이 드러난다. 평균이 아니라 둘이다:
#
#   · **바닥** — 전설 0회 판(약 6.6%)은 비전설만 받으므로 곡선의 88%다.
#     전설 풀이 곡선을 떠받치는 구조이고, §4 불변조건("전설 0회 판도 클리어
#     가능해야 한다")이 걸리는 자리가 여기다.
#   · **상한** — 전설이 뜨면 곡선이 남겨 둔 몫의 1.59배를 공급한다.
#
# 아래 둘은 **유도한 값이 아니라 못 박은 값이다**(LEGEND_RELIC_OVER와 같은 방식).
# 지금 상태를 고정해서 더 나빠지면 걸리게 하는 것이 목적이다.
NONLEGEND_FLOOR_MIN = 0.80   # 전설 0회 판이 곡선의 이 비율 이상을 받아야 한다
LEGEND_SUPPLY_MAX   = 2.00   # 전설이 공급하는 몫 ÷ 곡선이 남겨 둔 몫
# 전설 풀 내부 격차(최대 ÷ 최소). 현재 5.39 ÷ 0.54 = 10.0배다. **이 결정이
# 격차를 해소하지는 않는다** — 회계를 정리하고 격차를 보이게 만들 뿐이다.
LEGEND_SPREAD_MAX   = 12.0

# 피해 + 잠식 보정 합계의 천장. **현재 1위는 소용돌이 169%다** — 못 박아 두어
# 더 나빠지면 걸리게 한다 (LEGEND_RELIC_OVER와 같은 방식).
#
# 원심력이 329%로 이 천장을 넘긴 적이 있고, 그때 걸린 것은 수치가 아니라 **예산식**
# 이었다. 천장은 "수치를 깎아라"만 뜻하지 않는다 — 식이 틀렸을 수도 있다.
UNIQUE_TOTAL_MAX = 2.5

# 광역기 기본 타격 대상 수 — **`dc_field` 실측의 런 가중 평균이다.**
#
# 전에는 3.0을 "보수적인 하한"으로 박아 두고, 올리지 않는 이유로 "실측값이 구간·
# 물량에 따라 흔들린다"를 적어 뒀다. **그 흔들림을 재 본 적이 없었다.** 재 보니:
#
#   틱 구간      평균   최소  최대
#   0~665        0.94     0     4     ← 전장이 차는 중
#   666~1332     3.74     0     6
#   1333~1999    4.29     0     6
#   2000~2665    4.34     0     6
#   2666~3332    4.35     0     6
#   3333~3999    4.36     1     6
#
# **포화 후에는 ±2% 안이다.** 흔들리는 것은 초반 33초뿐이고, 런이 약 340초이므로
# 램프는 10%다. 런 전체 시간가중 평균이 3.95이고 포화값은 4.34다 — 보수적인 쪽을
# 골라 **4.0**을 쓴다.
#
# 올리면 광역 카드의 예산비가 **오른다**(대상 수에 비례하므로) → 수치를 깎아야
# 한다. 실제로 소용돌이 100 → 133% · 여진 98 → 131% · 균열 92 → 123%가 되어
# 셋을 깎았다. 반대로 **원심력은 75 → 100%로 제자리를 찾았다** — 오버킬 보정이
# 내린 것을 대상 수가 되돌린 것이고, 두 모델 오류가 서로를 가리고 있었다.
#
# 재측정: `cmake --build build/release --target dc_field && ./build/release/core/dc_field`
AOE_TARGETS = 4.0
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
    # 피해는 이제 환산한다. **둔화 쪽만** 상황 가치로 남는다
    "W_FISSURE+둔화": "둔화(접근 지연)는 R_FROST와 같은 이유로 DPS 환산 기준이 없다."
                      " 실측 처치율 0.850 vs 기준 0.851 — 잠식 유입이 생존 몹 수의"
                      " 함수라 죽이지 않는 효과는 시계를 늦추지 못한다",
}


def _exec_saving(threshold, rate, ttk):
    """처형이 줄이는 시간의 비율.

    임계 아래로 내려간 뒤 **처형 판정이 붙은 공격이 한 번 들어와야** 발동하므로,
    그 대기(지수분포)를 빼야 한다. 이 한 항이 "스킬 발동에 묶으면 전설이 될 수
    없다"를 만든다 — 발동 주기 10.8초가 임계 구간보다 길면 거의 못 쓴다.
    """
    win = ttk * threshold
    return (win - (1 - math.exp(-rate * win)) / rate) / ttk


def overkill_keep(damage_per_hit):
    """한 대 피해 중 **실제로 전달되는** 비율. 잡몹 실효 체력을 넘는 몫은 버려진다.

    예산식은 `대상당 DPS × 추가 대상 수`로 세는데, 실제로 들어가는 것은
    `min(한 대 피해, 남은 체력)`이다. **전력 피해를 넣는 각인에서 이 차이가 크다.**

    충격파가 그 사례다. 반폭을 실측으로 고쳐 예산비를 99%로 맞췄는데도 실측
    Δ처치율이 +6.4%에 그쳤다(같은 99%인 원심력은 +9.7%). 분쇄 강타 한 대가
    31.5인데 잡몹 실효 체력이 20이라 **37%가 버려진다.**

    보정이 맞다는 증거는 **예측력**이다. `Δ처치율 ÷ (예산비 × 전달률)`이:

        여진   0.095   (폭발 17.85 < 20 → 손실 없음)
        균열   0.097   (틱당 0.21 → 손실 없음)
        충격파 0.102   (전력 31.50 → 전달률 63%)

    세 카드가 7% 안에서 같은 계수로 모인다. 보정 전에는 충격파만 혼자 벗어나
    있었다.

    맞지 않는 둘은 **구조가 다르다** — 소용돌이(0.220)는 장판이 캐스트 시점에
    없던 적까지 잡아서 AOE_TARGETS보다 많이 때리고(별도 저평가다), 처형(0.403)은
    체력을 건너뛰므로 오버킬 개념이 없다. 둘은 이 보정의 대상이 아니다.

    **엘리트·보스에는 손실이 없다**(실효 체력이 훨씬 크다). 그래서 보수적으로
    잡몹 기준만 쓴다 — 전투 시간의 70%가 잡몹이고(1 − ELITE_SHARE), 광역·직선
    추가 대상은 거의 전부 잡몹이다.
    """
    if damage_per_hit <= 0:
        return 1.0
    return min(1.0, effective_hp(TRASH["hp"], 0) / damage_per_hit)


def centrifuge_targets(e, density):
    """원심력 한 발동이 최종적으로 때리는 적 수. `applyAoeHits`를 그대로 옮긴다.

    **패스를 한 번만 세면 이 카드는 게이트에 보이지 않는다.** 코어는 벤 수만큼
    반경을 키워 다시 훑으므로 중첩이 누적되고, 상한(`max_stacks`)에 닿을 때까지
    면적이 복리로 커진다. 이전 식은 `min(AOE_TARGETS, max_stacks)` 한 번이라
    상한 10을 **한 번도 세지 않았다** — 밀도 3에서 4.39마리로 읽었는데 실제는
    6.12마리였고, 그 차이(2.2배)가 PR #17의 '잠식 보정 +108%'로 새어 나왔다.
    보정이 잡아낸 것은 다른 축이 아니라 **잘못 매긴 이 게이트**였다.

    대상 수는 반경²에 비례한다고 본다. `dc_field` 실측(반경 1.5타일 4마리 →
    2.5타일 13마리)은 이보다 **더 급하므로** 이 가정은 보수적이다.

    상한이 밀도에 따라 다른 패스에서 물기 때문에 `max_stacks <= density`면
    첫 패스에서 바로 물고, 그러면 결과가 밀도와 무관한 고정 배율이 된다.
    그 경계(현재 밀도 3 / 4)를 넘나들면 값이 튄다 — 그래서 루프로 재야 한다.
    """
    step, cap = _pm(e["radius_step_permille"]), e["max_stacks"]
    hit, stacks, r = 0.0, 0.0, 1.0
    for _ in range(64):                 # 코어는 `fresh == 0`에서 멈춘다
        reach = density * r * r
        fresh = reach - hit
        hit = reach
        if fresh <= 1e-6 or stacks >= cap:
            break
        stacks = min(stacks + fresh, cap)
        r = 1 + step * stacks
    return hit


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
        boss_share = share["보스"]
        if BOSS_EXECUTE_IMMUNE:
            share["보스"] = 0.0        # 면역이면 즉시 처치 몫은 가치가 아니다
        kill = total * sum(share[k] * _exec_saving(T, rate, ttk[k]) for k in ttk)

        # ── 임계 이하 피해 증폭 ──
        #
        # **면역인 대상만 실제로 받는다** — 아닌 적은 같은 타격에서 즉사한다.
        # 임계 아래 구간이 TTK의 T만큼이고 그 구간이 1/(1+b)로 줄므로 아끼는
        # 비율은 `T × b/(1+b)`다. 처형 판정과 달리 **대기 항이 없다** — 발동이
        # 아니라 상시 배수다.
        #
        # **예산이 이 축을 거의 못 본다.** 보스 몫이 평균 8.4%라 b를 무한대로
        # 올려도 예산비가 117%에서 멈춘다. 보스전이 승패를 가르는데 예산은 런
        # 전체 평균이라 그렇다 — 충격파에서 배운 것과 같은 구조다. 그래서 이
        # 수치는 예산이 아니라 **측정**으로 고른다.
        b = _pm(e["weak_damage_bonus_permille"])
        weaken = total * boss_share * (T * b / (1 + b)) if BOSS_EXECUTE_IMMUNE else 0.0
        return kill + weaken

    if uid == "W_SHOCKWAVE":
        # 전력 피해로 직선의 적을 추가로 맞힌다. 추가 대상 수는 반폭에 비례한다.
        # **전력 피해이므로 오버킬 보정을 먹는다** — 분쇄 강타 한 대가 잡몹 실효
        # 체력의 1.6배라 37%가 버려진다.
        # **관통 표가 아니라 충격파 표를 쓴다** — 경로가 영웅에서 시작하므로
        # 띠가 다르고, 같은 반폭에서 대상 수가 1.4배다.
        extra = shock_extra(e["width_millitile"])
        return own_dps * extra * overkill_keep(HERO["attack_power"] * mult * crit)

    if uid == "W_VORTEX":
        # **즉발을 대체한다** — 잃은 즉발분까지 장판이 메워야 한다
        dur = e["duration_ticks"] / TICK_HZ
        field = own_pps * dur * HERO["attack_power"] * _pm(e["dps_permille"]) * AOE_TARGETS
        return field - own_dps * AOE_TARGETS

    if uid == "W_CENTRIFUGE":
        # **패스가 반복된다** — `applyAoeHits`는 벤 수만큼 반경을 키워 다시 훑는다.
        # 예산식이 패스를 한 번만 세면 상한이 보이지 않는다 (아래 주석 참조).
        extra = centrifuge_targets(e, AOE_TARGETS) - AOE_TARGETS
        # 회전 베기도 전력 피해다 — 한 대 26.25 vs 실효 체력 20 → 전달률 76%
        return own_dps * extra * overkill_keep(HERO["attack_power"] * mult * crit)

    if uid == "W_AFTERSHOCK":
        return own_pps * HERO["attack_power"] * _pm(e["damage_permille"]) * AOE_TARGETS

    if uid == "W_FISSURE":
        # **피해만 환산한다.** 둔화는 그대로 환산 기준이 없다 (R_FROST와 같은 이유,
        # 그리고 실측으로 처치율에 0이었다 — 잠식 유입이 생존 몹 수의 함수이고
        # 스포너가 그 수를 상한에 유지하므로 "죽이지 않는 효과"는 시계를 늦추지
        # 못한다). 그래서 지속 피해를 얹었고, 값매기는 것은 그 항이다.
        #
        # 소용돌이와 같은 식인데 **가산**이라 즉발분을 빼지 않는다 — 소용돌이는
        # 즉발을 대체하므로 잃은 몫까지 메워야 해서 수치가 훨씬 크다(96.5%/초).
        dur = e["duration_ticks"] / TICK_HZ
        return own_pps * dur * HERO["attack_power"] * _pm(e["dps_permille"]) * AOE_TARGETS
    raise KeyError(uid)


def corruption_credit(uid):
    """런 단축이 회피하는 누적 잠식을 **DPS로 환산**한다.

    잡몹을 빨리 치우면 게이지가 빨리 차고 런이 짧아진다. 동시 생존은 스폰이 상한을
    유지하므로 줄지 않는다 — 줄어드는 것은 `유입률 × 시간`의 시간 쪽이다.
    `회복 1 = 피해 1`은 `E_LEECH`가 이미 쓰는 환산이다.

    런이 **길어지는** 각인(균열)은 음수가 나온다. 자르지 않는다 — 부호가 곧 정보다.
    """
    if LEGEND_MEASURED.get(uid) is None:
        return None            # 미측정. 0.0으로 돌려주면 "보정 없음"과 구분되지 않는다
    sec, _clear = LEGEND_MEASURED[uid]
    dt = LEGEND_BASE_SEC - sec
    return MAP_INFLOW * dt / sec


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
    # ── 동일성은 **비전설만** 본다 ──
    #
    # 전에는 전설까지 넣어 가중평균을 냈다. 그런데 그 합은 **선언 예산**(15%)으로
    # 돌았고 전설 카드의 실제 값은 들어오지 않았다 — 유물이 3~5배인 것이 평균에
    # 전혀 반영되지 않았다. 실제 값을 넣으면 2.25%로 허용 5%를 넘는다.
    #
    # 그래서 전설을 평균에서 뺀다. **1.5% 확률에 10배 분산인 등급을 평균 기반
    # 페이싱 식에 넣는 것이 모델링 오류다** — 전설은 페이싱이 아니라 스파이크다.
    # 빼고 보면 문제의 성격이 평균이 아니라 **바닥과 상한**이라는 게 드러난다.
    nonl_rate   = sum(RATES[g] for g in GRADE_BUDGET if g != "전설")
    nonl_got    = sum(RATES[g] * GRADE_BUDGET[g] for g in GRADE_BUDGET if g != "전설")
    legend_need = CARD_POWER_PER_LEVELUP - nonl_got     # 곡선이 전설에 남겨 둔 몫
    good = abs(nonl_got - (CARD_POWER_PER_LEVELUP
                           - RATES["전설"] * GRADE_BUDGET["전설"])) \
           / CARD_POWER_PER_LEVELUP < 0.05
    ok &= good
    print(f"  비전설 가중평균 {nonl_got:.3%} (확률 {nonl_rate:.1%})"
          f" vs 곡선이 비전설에 남겨 둔 몫 "
          f"{CARD_POWER_PER_LEVELUP - RATES['전설']*GRADE_BUDGET['전설']:.3%}  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 이 한 줄이 경험치 곡선과 **비전설** 카드 수치를 잇는다.")
    print("     전설은 여기 없다 — 아래 '전설은 평균이 아니라 바닥과 상한'이 본다\n")

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
    print(f"  {'ID':<14} {'이름':<5} {'ΔDPS':>7} {'피해':>6} "
          f"{'잠식보정':>8} {'합계':>6}   {'런':>7} {'Δ클리어':>8}")
    print("  " + "-" * 76)
    print("  (피해 = 게이트 · 잠식 보정 = 런 단축 환산 · Δ클리어 = 기록, 판정 안 함)")
    clears = {}
    pending = []
    for uid, e in UNIQUE.items():
        d = unique_delta(uid)
        credit = corruption_credit(uid)
        if credit is None:
            # **미측정이다.** 0으로 깔면 "보정이 없는 각인"과 구분되지 않고, 옛
            # 숫자를 남기면 더 나쁘다 — 둘 다 조용히 통과하므로 여기서 세운다.
            pending.append(uid)
            ratio = d / legend_budget if d is not None else None
            rs = f"{ratio:>5.0%}" if ratio is not None else f"{'—':>6}"
            ds = f"{d:.2f}" if d is not None else "—"
            print(f"  {uid:<14} {e['name']:<5} {ds:>7} {rs} "
                  f"{'대기':>6} {'대기':>5}   {'—':>7} {'—':>8}  ← 재측정 대기")
            if ratio is not None and abs(ratio - 1) > BUDGET_TOL:
                ok = False
            continue
        sec, clear = LEGEND_MEASURED.get(uid, (LEGEND_BASE_SEC, LEGEND_BASE_CLEAR))
        dclear = clear - LEGEND_BASE_CLEAR
        clears[uid] = dclear
        cr = credit / legend_budget
        if d is None:
            print(f"  {uid:<14} {e['name']:<5} {'—':>7} {'—':>6} "
                  f"{cr:>7.0%} {'—':>6}   {sec:>6.1f}s {dclear:>+7.1f}p  ※ 상황 가치")
            continue
        ratio = d / legend_budget
        # **`total`을 쓰면 안 된다** — 바깥의 영웅 총 DPS를 덮어쓴다. 실제로 그랬고,
        # 그 뒤의 보고 줄들이 전부 이 루프의 마지막 값으로 나눠서 "기본 공격이 총
        # DPS의 1656%" 같은 숫자를 찍었다. 게이트가 아니라 출력만 틀렸지만,
        # 검증 보고서에 20배 틀린 수를 찍는 것은 그 자체로 고장이다.
        combined = ratio + cr
        flag = ""
        if abs(ratio - 1) > BUDGET_TOL:
            flag = "  ← 피해 편차"
            ok = False
        elif combined > UNIQUE_TOTAL_MAX:
            flag = "  ← 합계 천장 초과"
            ok = False
        elif combined > 1.5:
            flag = "  ← 합계가 예산의 1.5배 위"
        print(f"  {uid:<14} {e['name']:<5} {d:>7.2f} {ratio:>5.0%} "
              f"{cr:>+7.0%} {combined:>5.0%}   {sec:>6.1f}s {dclear:>+7.1f}p{flag}")
    print()
    print("  ※ **잠식 보정은 포화를 세지 않는다.** 런이 짧아 잠식이 낮으면 정화가")
    print("     넘쳐 버려지므로 그만큼은 가치가 아니다 — 처형의 구슬 실효율이")
    print("     52.6%(기준선 67.2%)로 여섯 중 최저다. 처형의 보정은 그만큼 과대평가다")
    print(f"  ※ 잠식 유입 {MAP_INFLOW:.1f}/초 (verify_recovery 실측의 가중평균) ·"
          f" 전설 예산 {legend_budget:.2f} DPS")
    # ── 충격파 반폭 절벽 가드 ──
    #
    # **예산비만 보면 이 선을 못 본다.** 1000‰에서도 161%라 ±30% 밴드가 걸러
    # 주긴 하지만, 걸린 뒤 "조금 줄이면 되겠네"로 950을 고르면 밴드는 통과하고
    # 값은 분리 거리 튜닝에 딸려 간다. 그래서 선 자체를 못 박는다.
    shock_w = UNIQUE["W_SHOCKWAVE"]["width_millitile"]
    cliff_ok = shock_w <= SHOCK_WIDTH_CLIFF
    ok &= cliff_ok
    print(f"  충격파 반폭 {shock_w}‰ ≤ 절벽 {SHOCK_WIDTH_CLIFF}‰  "
          f"{'PASS' if cliff_ok else 'FAIL'}")
    print("     0.9~1.0타일 칸 하나에 0.87명이 몰려 있다(분리 거리가 만든 껍질).")
    print("     그 위에서 폭을 고르면 예산이 기하가 아니라 분리 거리에 딸려 간다")
    print(f"  ※ 합계 천장 {UNIQUE_TOTAL_MAX:.1f}배. 넘으면 FAIL —"
          f" **DPS만 보면 멀쩡해 보이는 각인을 잡는 자리다**")

    if pending:
        ok = False
        print(f"  ← **{', '.join(pending)} 재측정 대기.** 수치를 바꿨으므로 런 길이가")
        print("     달라졌다. `./build/release/core/dc_legend 5000`을 돌려")
        print("     LEGEND_MEASURED를 채울 것 — 옛 값을 그대로 두면 잠식 보정이")
        print("     **다른 수치의 런 길이로** 계산되고, 그래도 전부 PASS가 된다")

    # ── 측정이 현재 데이터에서 나온 것인가 ──
    stale = _measurement_is_current()
    if stale:
        ok = False
        live, pinned = stale[0]
        print("  ← **측정이 낡았다.** 런 길이·클리어율이 지금 데이터의 것이 아니다.")
        print(f"       시뮬 지문  측정 당시 {pinned} → 지금 {live}")
        print("     `./build/release/core/dc_legend 5000`을 다시 돌려")
        print("     LEGEND_MEASURED와 LEGEND_MEASURED_FINGERPRINT를 **같이** 고칠 것")
        print("     ※ 지문은 data/*.json + 코어 헤더(core/include/dc/*.h)의 해시다 —")
        print("        어느 파일의 어느 수치든, **그리고 시뮬 코드 변경도** 측정을")
        print("        무효화한다. 주석은 양쪽 모두 빼므로 설명 수정은 괜찮다")

    # ── 클리어율 — 유의 판정과 지배 빌드 ──
    #
    # **절대 %p와 상대 변화를 같이 본다.** 기준선이 9.3%라 +4.5%p는 상대로 +48%다.
    # 절대만 보면 "작다"로 읽히는데 플레이어가 느끼는 쪽은 상대다. 판정(3σ)은
    # 측정의 해상도라 절대로 하고, **설계 판단은 상대로 한다.**
    print()
    print(f"  {'전설':<14} {'Δ%p':>6} {'상대':>7} {'3σ':>6} {'z':>6}  유의")
    print("  " + "-" * 52)
    # **두 값을 섞지 말 것.** `z = Δ / σ`가 유의 판정(z > 3)이고, 지배도는
    # `Δ / 3σ`다. 한때 둘을 같은 열에 찍어 유의 판정이 z > 9가 됐다.
    sigma = {}
    for uid in clears:
        _sec, clear = LEGEND_MEASURED[uid]
        s3 = clear_3sigma(LEGEND_BASE_CLEAR, clear)      # 3σ (%p)
        z  = 3.0 * clears[uid] / s3 if s3 > 0 else 0.0   # Δ ÷ σ
        sigma[uid] = (clears[uid], z, s3)
    for uid, (d, z, s3) in sorted(sigma.items(), key=lambda kv: -kv[1][0]):
        rel = d / LEGEND_BASE_CLEAR if LEGEND_BASE_CLEAR > 0 else 0.0
        print(f"  {UNIQUE[uid]['name']:<14} {d:>+6.1f} {rel:>+7.0%} "
              f"{s3:>6.2f} {z:>5.2f}σ  {'유의' if z > 3.0 else '  —'}")
    print("  ※ 3σ는 **측정값에서 계산한다.** 상수로 박으면 비율이 움직일 때 조용히")
    print("     틀어진다 — 전에 4.8%p(p≈0.15)로 박아 둬서 '유의 0종'이 나왔는데")
    print("     실제 비율이 9.3~13.8%로 내려가 실제 3σ는 3.95~4.28%p였다")
    # **옛 상수가 틀렸던 게 아니다 — p가 움직였다.** 같은 공식에 p=15%를 넣으면
    # 4.79%p가 나와 상수와 일치한다. 이 한 줄이 공식 자체의 검산이고, 동시에
    # "상수를 박는 방식"이 왜 위험한지를 보여준다: 유도는 맞았고 전제가 낡았다.
    # **n을 명시한다.** 옛 상수 4.8은 p=15% **그리고 n=1000**에서 나온 값이다.
    # 기본값(LEGEND_SEEDS)으로 두면 시드를 5000으로 올린 순간 검산이 2.14를 내고
    # FAIL한다 — 공식이 틀린 게 아니라 검산이 전제를 흘린 것이다.
    chk = clear_3sigma(15.0, 15.0, n=1000)
    chk_ok = abs(chk - 4.79) < 0.02
    ok &= chk_ok
    print(f"  ※ 검산: p=15% · n=1000을 넣으면 {chk:.2f}%p — 옛 상수 4.8과 일치  "
          f"{'PASS' if chk_ok else 'FAIL'}")
    print("     상수의 유도가 틀린 게 아니라 **전제(p)가 낡았다.** 그래서 계산으로 옮긴다")

    # **분모는 2위다 — 2위가 유의할 때만.** Δ ÷ 3σ는 시드 수를 올리기만 해도
    # 커지므로(√n) 지배도의 분모로 쓸 수 없다. 2위가 노이즈면 그때만 3σ로 떨어진다.
    sig = [uid for uid, (_d, z, _s) in sigma.items() if z > 3.0]
    order = sorted(sigma, key=lambda u: -sigma[u][0])
    top_uid = order[0] if order else None
    second = order[1] if len(order) > 1 else None
    paired = second is not None and sigma[second][1] > 3.0 and sigma[second][0] > 0
    if top_uid and paired:
        dom = sigma[top_uid][0] / sigma[second][0]
        dom_cap, dom_what = DOMINANT_RATIO_MAX, f"2위 {UNIQUE[second]['name']}"
    elif top_uid:
        dom = sigma[top_uid][0] / sigma[top_uid][2] if sigma[top_uid][2] > 0 else 0.0
        dom_cap, dom_what = DOMINANT_SIGMA_MAX, "3σ(2위가 유의하지 않아 대체 눈금)"
    else:
        dom, dom_cap, dom_what = 0.0, DOMINANT_RATIO_MAX, "—"
    good = dom <= dom_cap
    ok &= good
    # 대기 행이 있으면 이 판정은 **그 행을 빼고** 낸 값이다. 그대로 찍으면 지배
    # 후보가 빠진 채 "지배 없음"으로 읽힌다.
    note = f"  ← {len(pending)}종 대기 중이라 **결론이 아니다**" if pending else ""
    name = UNIQUE[top_uid]["name"] if top_uid else "—"
    topd  = sigma[top_uid][0] if top_uid else 0.0
    print(f"\n  지배 빌드: 1위 {name} {topd:+.1f}%p ÷ {dom_what} = {dom:.2f}배"
          f" (상한 {dom_cap:.2f}배)  {'PASS' if good else 'FAIL'}{note}")
    if not paired:
        print("     ※ **2위가 유의하지 않아 3σ 눈금으로 적었다.** 이 값은 시드 수에")
        print("        딸려 있으므로 실행 사이에 비교할 수 없다")
    print(f"  유의한 각인 {len(sig)}종 / {len(UNIQUE) - len(pending)}")
    if not pending:
        # **유의하지 않은 것과 약한 것은 다르다.** 다만 유의하지 않은 카드의
        # 상대값도 확립된 값이 아니다 — 한때 이 줄이 상대 +15%를 기준으로
        # "진짜 약한 쪽"을 분류했는데, 균열이 z = 1.35에서 상대 +20%라는 이유로
        # 목록에서 빠졌다. **확립되지 않은 차이로 분류하고 있었다.**
        #
        # 그래서 둘을 분리해 찍는다: 유의한 것은 결론, 나머지는 **방향**이다.
        weak = [uid for uid, (d, _z, _s) in sigma.items()
                if LEGEND_BASE_CLEAR > 0 and d / LEGEND_BASE_CLEAR < 0.15]
        if weak:
            names = ", ".join(f"{UNIQUE[u]['name']}(z={sigma[u][1]:.2f})" for u in weak)
            print(f"  ※ 상대 +15% 아래: {names}")
        print("  ※ **유의하지 않은 행의 상대값은 결론이 아니라 방향이다.** 위 목록도"
              " 그렇다 —")
        print(f"     n={LEGEND_SEEDS}에서 3σ를 넘지 못한 차이는 순위를 매길 근거가"
              " 되지 못한다. 방향을")
        print("     좁히는 데만 쓰고, 수치를 고친 뒤에는 다시 잰다")
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

    print("=== 전설은 평균이 아니라 **바닥과 상한**이다 ===")
    print("  전설을 동일성에서 뺐다. 1.5% 확률에 10배 분산인 등급은 페이싱이 아니라")
    print("  스파이크이고, 평균에 넣으면 선언 예산으로만 돌아 유물 초과가 안 보인다.")
    nonl_got = sum(RATES[g] * GRADE_BUDGET[g] for g in GRADE_BUDGET if g != "전설")
    need     = CARD_POWER_PER_LEVELUP - nonl_got
    mults    = [unique_delta(u) / legend_budget for u in UNIQUE
                if unique_delta(u) is not None]
    mults   += [legend_relic_delta(r) / legend_budget for r in LEGEND_RELIC_OVER]
    pool     = sum(mults) / len(mults)
    supply   = RATES["전설"] * GRADE_BUDGET["전설"] * pool
    floor    = nonl_got / CARD_POWER_PER_LEVELUP
    spread   = max(mults) / min(mults) if min(mults) > 0 else float("inf")
    zero_run = (1 - RATES["전설"]) ** 180        # 레벨업 60회 × 3장

    f_ok = floor >= NONLEGEND_FLOOR_MIN
    s_ok = supply / need <= LEGEND_SUPPLY_MAX
    p_ok = spread <= LEGEND_SPREAD_MAX
    ok &= f_ok and s_ok and p_ok
    print(f"  바닥 — 전설 0회 판({zero_run:.1%})이 받는 몫 {nonl_got:.3%}"
          f" = 곡선의 **{floor:.0%}**  (하한 {NONLEGEND_FLOOR_MIN:.0%})"
          f"  {'PASS' if f_ok else 'FAIL'}")
    print(f"  상한 — 전설이 공급하는 몫 {supply:.3%} ÷ 곡선이 남겨 둔 {need:.3%}"
          f" = **{supply/need:.2f}배**  (상한 {LEGEND_SUPPLY_MAX:.2f}배)"
          f"  {'PASS' if s_ok else 'FAIL'}")
    print(f"  격차 — 풀 {len(mults)}칸 최대 {max(mults):.2f}배 ÷ 최소 {min(mults):.2f}배"
          f" = **{spread:.1f}배**  (상한 {LEGEND_SPREAD_MAX:.1f}배)"
          f"  {'PASS' if p_ok else 'FAIL'}")
    print("  ※ 세 값은 **유도가 아니라 못 박은 것**이다. 지금을 고정해 더 나빠지면")
    print("     걸리게 하는 목적이고, 격차 10배 자체는 이 결정이 해소하지 않는다 —")
    print("     회계를 정리하고 격차를 **보이게** 만들었을 뿐이다")
    print("  ※ 바닥이 하한을 깨면 '전설이 없으면 클리어가 안 된다'는 뜻이고, 그건")
    print("     §4 불변조건 위반이다. 전설 수치를 올릴 때 여기가 먼저 걸린다\n")

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
