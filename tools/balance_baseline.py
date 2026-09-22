"""전사 기준선 수치 검산.

수치를 감으로 정하지 않기 위해, **플레이 목표를 먼저 쓰고 그 목표가 지켜지는지**를
계산으로 확인한다. 수치를 바꿀 때마다 재실행할 것.

몬테카를로 하네스(design.md §14-10)의 전신이다. 여기서는 분산 없이 기댓값만 본다 —
분포가 필요해지면 C++ 코어가 나온 뒤 헤드리스로 돌린다.
"""

from gamedata import (
    HERO as _HERO, SKILLS_DATA as _SKILLS, MONSTERS as _MON,
    PROGRESSION as _PROG, SPAWN as _SPAWN, pm,
)

TICK_HZ = _PROG["tick_hz"]
FIXED_MAX = 524288  # Fixed 20.12 (int32_t raw) 표현 상한

# ── 전사 기준선 ────────────────────────────────────────────────
HERO = {
    "attack_power": _HERO["attack_power"],
    "attack_interval_ticks": _HERO["attack_interval_ticks"],
    "crit_chance": pm(_HERO["crit_chance_permille"]),
    "crit_mult": pm(_HERO["crit_mult_permille"]),
    "corruption_max": _HERO["corruption_max"],   # HP가 아니라 **잠식 게이지**
    "armor": _HERO["armor"],
}

# ── 잠식 게이지 (design.md §2) ─────────────────────────────────
# 게임오버 조건 둘(체력 고갈 · 몬스터 수 상한)을 하나로 합친 게이지다.
# 반전된 체력과 동형이라 기존 HP 시스템이 그대로 얹히고, 입력이 하나 더 붙는다:
#
#   충전 = 피격량 + f(전장의 몹 수)
#
# 두 번째 항이 "시간 초과" 게임오버까지 흡수한다 — 가만히 있어도 몹이 있으면 찬다.
CORRUPTION_THRESHOLD = _PROG["corruption_threshold"]
CORRUPTION_PER_MOB = pm(_PROG["corruption_per_mob_permille"])
CORRUPTION_OVERFLOW_MULT = pm(_PROG["corruption_overflow_mult_permille"])


def corruption_from_mass(alive, cap):
    """전장의 몹 수가 만드는 초당 잠식 충전."""
    over = max(alive - CORRUPTION_THRESHOLD, 0)
    rate = CORRUPTION_PER_MOB * over
    if alive > cap:
        rate += CORRUPTION_PER_MOB * (alive - cap) * (CORRUPTION_OVERFLOW_MULT - 1)
    return rate

PROC_RATE = pm(_HERO["proc_rate_permille"])        # 통합 proc (design.md §3)
QTE_COOLDOWN_SEC = _HERO["qte_cooldown_ticks"] / TICK_HZ

SKILLS = {                 # 이름 → (기본 공격 대비 배율, 가중치, 광역 여부)
    k["name"]: (pm(k["mult_permille"]), pm(k["weight_permille"]), k["aoe"])
    for k in _SKILLS["skills"]
}

# ── 일반 몹 (역산 대상) ────────────────────────────────────────
TRASH = dict(_MON["trash"])    # hp / damage / attack_range_millitile / cooldown_ticks

from gamedata import SEGMENTS_DATA as _SEGD
SEGMENT_TRASH_COUNT = (_SEGD["segments"][0]["melee"]
)   # 1구간 몹 수

# ── 연속 스폰 — 동시 생존 상한 (design.md §2) ──────────────────
# 스폰율을 고정하지 않고 동시 생존 수가 상한에 닿도록 채운다. 발산이 원천 차단되고
# 밀집도가 일정해 **광역기가 상시 최대 효율로 작동한다.**
# 계단이 아니라 완만한 램프다. 계단으로 올리면 그 구간에서 광역 효율이 튀어
# 난이도 곡선이 톱니가 된다 (30 → 60 → 150으로 잡아보고 확인했다).
# 상한은 §11의 **일반 구간 30~60**을 따른다. 150은 피크(보스 직전 버스트·돌발
# 이벤트)용이지 정상 상태가 아니다 — 150을 정상 상태로 두면 전장에 떠 있는 몹이
# 그 구간의 처치 목표보다 많아져 구성이 뒤집힌다.
#
# **인덱스는 맵 안의 구간이 아니라 런 전체의 구간 번호(1~24)다.** 맵마다 리셋하면
# 영웅이 58배 강해지는 동안 잠식 압박이 그대로여서 후반 맵이 무위험이 된다.
CONCURRENT_CAP_TOP = _SPAWN["concurrent_cap_top"]
TOTAL_SEGMENTS = _PROG["total_segments"]   # 런 전체 구간 수


def concurrent_cap(segment):
    """런 전체 구간 번호(1~24) 기준 동시 생존 상한."""
    lo = _SPAWN["concurrent_cap_start"]
    t = (segment - 1) / (TOTAL_SEGMENTS - 1)
    return round(lo * (CONCURRENT_CAP_TOP / lo) ** t)


CONCURRENT_CAP = {i: concurrent_cap(i) for i in range(1, 25)}
CONCURRENT_PEAK = _SPAWN["concurrent_peak"]   # 보스 직전 버스트 · 돌발 이벤트(§6)

# 스폰 배치 — 한 번에 몇 마리가 같이 들어오는가.
# **배치는 리듬을, 상한은 밀도를 정한다.** 정상 상태의 스폰량은 처치량과 같으므로
# (죽은 만큼 채운다) 배치를 키운다고 총량이 늘지는 않는다. 대신 초반엔 두세 마리씩
# 꾸준히, 후반엔 여덟 마리씩 우르르 — 같은 총량이 전혀 다르게 체감된다.
SPAWN_INTERVAL_TICKS = _SPAWN["interval_ticks"]
SPAWN_DIRECTIONS = _SPAWN["directions"]   # 사방 스폰 (§13)


def spawn_batch(segment):
    """구간 i의 배치 크기. 2 → 8로 증가한다."""
    lo, hi = _SPAWN["batch_start"], _SPAWN["batch_end"]
    return round(lo + (hi - lo) * (segment - 1) / 7)


def spawn_per_direction(segment):
    """배치를 4방향에 나눈 수. 나머지는 북 → 동 → 남 → 서 고정 순서로 배분한다.

    나머지 배분 순서를 고정하는 것이 결정론 요구사항이다 — 무작위로 돌리면
    서버·클라이언트의 스폰 위치가 갈린다.
    """
    b = spawn_batch(segment)
    base, rem = divmod(b, SPAWN_DIRECTIONS)
    return [base + (1 if d < rem else 0) for d in range(SPAWN_DIRECTIONS)]

# 광역 1회가 전장의 몹 중 몇 %를 맞히는가. 반경과 맵 크기에 달렸으므로
# **§14-10 하네스에서 실측할 항목**이다. 여기서는 보수적으로 잡는다.
AOE_TARGET_SHARE = 0.13   # 실측 확인: 광역 반경 1.5타일에서 0.138 (core/tools/dc_field)
AOE_TARGETS_MIN = 3.0          # 웨이브 방식의 옛 가정 — 하한으로 남긴다


def aoe_targets(segment):
    """해당 구간에서 광역기 1회가 때리는 몹 수."""
    return max(AOE_TARGETS_MIN, concurrent_cap(segment) * AOE_TARGET_SHARE)


def effective_targets(segment):
    """기본 공격 1회당 실효 타격 대상 수 (광역 proc 포함)."""
    aoe_weight = sum(w for _m, w, aoe in SKILLS.values() if aoe)
    return 1 + PROC_RATE * aoe_weight * (aoe_targets(segment) - 1)
HITS_TO_KILL_TRASH = 2         # 목표: 일반 몹은 기본 공격 몇 대에 죽는가
SURROUND_COUNT = 5             # 실측 확인: 영웅 이격 1.0타일에서 최대 5 · 평균 3.3 (dc_field)
# 주의: 탑다운 전환으로 영웅이 이동하게 되면서 이 가정이 약해졌다.
# 실제로는 포위를 벗어날 수 있으므로 목표 4는 "최악의 경우" 하한으로 읽어야 한다.
# 이동속도 대 몹 추격속도 관계가 새 밸런싱 축이며, C++ 코어가 나온 뒤 실측한다.

# ── 방어력 감쇠 ────────────────────────────────────────────────
# 비율 감쇠: 실데미지 = 데미지 * K / (K + Armor).  Armor=K면 정확히 절반.
# 감산(데미지 - Armor)을 쓰지 않는 이유: 아이템 조합으로 위력이 곱해지는
# 게임이라 후반에 Armor가 완전히 무의미해진다. 비율은 배율 성장과 무관하게
# 일정 비율을 유지한다.
ARMOR_K = _PROG["armor_k"]


def effective_hp(hp, armor):
    """Armor를 감안한 실효 체력 — 실제로 넣어야 하는 총 데미지."""
    return hp * (ARMOR_K + armor) / ARMOR_K


# ── 경험치 · 레벨업 ────────────────────────────────────────────
# 레벨업 1회 = 카드 선택 1회 = 뽑기 기회 1회다 (design.md §4). "자주 레벨업"이
# 설계 목표이므로, 곡선은 **레벨업 횟수를 먼저 정하고 역산**한다.
#
# 경험치 수입은 몹 체력에 비례하고 몹 체력은 웨이브마다 지수로 오른다.
# 따라서 필요 경험치도 지수여야 레벨업 간격이 일정하게 유지된다 —
# 선형 곡선을 쓰면 후반에 레벨업이 폭주한다.
EXP_PER_EHP = pm(_PROG["exp_per_ehp_permille"])   # 몹 경험치 = 실효 체력 × 이 계수
LEVEL_NEED_BASE = _PROG["level_need_base"]
LEVEL_NEED_RATIO = pm(_PROG["level_need_ratio_permille"])

# 레벨업 1회당 유효 위력 성장. 레벨업이 아이템 뽑기/스펙업 카드의 **유일한**
# 관문이므로(§4), 이 한 수치가 런 전체의 성장을 전부 담는다.
# **카드 몫**이다. 성장의 전부가 아니다 (design.md §4: 아이템 70 : 카드 30).
CARD_POWER_PER_LEVELUP = pm(_PROG["power_per_levelup_permille"])
CARD_POWER_SHARE = pm(_PROG["card_power_share_permille"])

# 이 모델이 쓰는 것은 **전체 성장**이다 — 카드 + 아이템 조합.
# 곱셈 성장의 로그 지분이므로 카드 몫에서 역산한다: (1+card)^(1/share) - 1.
# 아이템이 아직 구현되지 않았어도 모델은 설계값을 쓴다 — 그래야 "아이템이 없어서
# 영웅이 안 큰다"가 "카드 수치가 틀렸다"로 잘못 나오지 않는다.
POWER_PER_LEVELUP = (1 + CARD_POWER_PER_LEVELUP) ** (1 / CARD_POWER_SHARE) - 1

# 그 성장이 **어디로 가는가.** 공격력으로 가면 잡몹은 계속 원샷이라 치명타도
# 공격력 성장도 오버킬로 버려진다(tools/verify_crit_axis.py). 공속으로 실으면
# 같은 성장이 "더 자주 때린다"가 되어, 잡몹이 여러 대 맞고 죽는 구조가 유지된다.
#
# **닫힌 식으로는 이 값의 효과를 정확히 못 잰다.** 기본 공격마다 proc을 굴리므로
# 공속이 오르면 스킬 발동도 같이 늘고, 광역 스킬은 여러 마리를 동시에 때린다 —
# 밀집도에 따라 클리어 속도가 공속보다 더 빠르게 오를 수 있다. 여기서는 선형으로만
# 잡고, 실제 값은 §14-10 몬테카를로 하네스에서 측정한다.
# **배분은 웨이브 시간을 바꾸지 못한다.** 처리량 = 공속 × 한 대 피해 = 성장 총량이므로
# 어떻게 나누든 클리어 시간은 같다. 배분이 바꾸는 것은 **잡몹 타수**뿐이고,
# 타수가 치명타·공격력 성장이 오버킬로 버려지는 정도를 정한다.
#
# **공속 몫이 곧 처치율 성장이다.** 처리량 = 공속 × 한 대 피해인데, 잡몹 HP는 한 대
# 피해를 따라가야(타수 유지) 하므로 처치율은 정확히 공속 배율만큼 오른다.
# 처치율이 안 오르면 전장이 고인다(평균 생존 = 동시 생존 상한 / 처치율).
# **기준선 공속은 1초 1회 그대로다** — 성장분만 공속으로 실린다.
SPEED_GROWTH_SHARE = pm(_PROG["speed_growth_share_permille"])
TRASH_HITS_BAND = (1.8, 5.0)   # 판 내내 유지되어야 할 잡몹 타수

# **닫힌 식이 실측보다 낙관적인 몫.** 이 모델은 영웅 DPS가 전부 잡몹에 들어간다고
# 보지만, 실제로는 엘리트가 출력의 70~80%를 먹고(타겟 우선순위) 전장이 상한까지
# 포화된다. 그래서 모델이 구간 1을 25.7초로 볼 때 하네스는 33.3초를 잰다.
#
# 이 계수 없이 두면 처치율 손잡이를 돌릴 때마다 **모델과 하네스가 반대 방향을
# 가리킨다** — 모델은 "너무 빠르다"고 하고 하네스는 "여전히 느리다"고 한다.
# 실측으로 보정해 두 도구가 같은 게임을 검산하게 만든다.
# 출처: core/tools/dc_montecarlo (12시드 × 맵 완주)
COMBAT_EFFICIENCY = 0.77

CARD_PICK_SEC = 2.5            # 카드 1회 선택에 쓰는 시간 가정 (UI 요구사항)
MODAL_BUDGET = 0.15            # 런 전체에서 선택 모달이 차지해도 되는 비율 상한


def level_need(n):
    """레벨 n → n+1에 필요한 경험치. 런타임에서는 이 값을 int32_t 테이블로 굽는다."""
    return round(LEVEL_NEED_BASE * LEVEL_NEED_RATIO ** (n - 1))


def power_mult(levelups):
    """레벨업 n회 시점의 영웅 위력 배율 (공속 × 공격력 합산)."""
    return (1 + POWER_PER_LEVELUP) ** levelups


def speed_mult(levelups):
    """그중 공속 배율. 잡몹 타수를 좌우한다."""
    return power_mult(levelups) ** SPEED_GROWTH_SHARE


def damage_mult(levelups):
    """그중 한 대의 피해 배율. 공속과 곱해 power_mult가 된다."""
    return power_mult(levelups) ** (1 - SPEED_GROWTH_SHARE)


def monster_exp(base_ehp, wave):
    """몹 1마리가 주는 경험치.

    런타임에서는 `int32_t expValue × 웨이브 배율(정수 퍼밀) / 1000`이며
    **0방향 절삭**으로 고정한다. 여기서도 같은 절삭을 적용해 검산이 어긋나지
    않게 한다 — 실수로 계산하면 레벨업 타이밍이 한두 웨이브씩 밀린다.
    """
    return int(base_ehp * hp_scale(wave) * EXP_PER_EHP)


# ── 웨이브 스케일링 ────────────────────────────────────────────
# 스테이지가 진행될수록 몹 체력이 오른다.
# **이 값은 POWER_PER_LEVELUP에 종속이다.** 클리어 시간이 웨이브마다
# 완만히 늘어나려면 몹 체력 증가율이 영웅 성장률을 조금 웃돌아야 한다:
#   클리어 시간 배율/웨이브 = HP_SCALE_PER_WAVE / (1+g)^(웨이브당 레벨업)
# 웨이브당 약 1.8~3회 레벨업이므로 성장은 웨이브당 약 1.15배,
# 여기에 1.026배를 더 얹어 1.18로 잡았다.
HP_SCALE_PER_WAVE = pm(_PROG["trash_hp_scale_per_segment_permille"])
TOTAL_WAVES = _PROG["total_segments"]   # 풀 게임 = 맵 3개 × 8구간 (design.md §2)
                               # 수직 슬라이스는 맵 1개 = 8구간


def hp_scale(wave):
    """잡몹 체력 배율. **한 대 피해 성장**을 따라가 타수를 일정하게 유지한다."""
    return HP_SCALE_PER_WAVE ** (wave - 1)


def elite_scale(segment):
    """엘리트 체력 배율 — 잡몹과 다른 곡선을 탄다.

    잡몹은 타수를 유지하려고 **한 대 피해**를 따라가지만, 엘리트는 **처치 시간**을
    유지해야 한다. 처치 시간이 짧아지면 텔레그래프가 다 돌기 전에 죽어 QTE가
    아예 발생하지 않는다 — 잡몹 배율(1.09)을 쓰면 마지막 구간에서 1.4초에 죽는다.

    그래서 영웅의 **총 성장**을 그대로 따라간다. 보스 체력을 목표 처치 시간에서
    역산하는 것과 같은 논리다.
    """
    lv = round(TOTAL_LEVELUPS * (segment - 1) / TOTAL_SEGMENTS)
    return power_mult(lv)


# ── 엘리트 (등장 웨이브 스케일링 적용 전 기준값) ───────────────
ELITES = {
    e["name"]: {"hp": e["hp"], "armor": e["armor"], "damage": e["damage"],
                "target_sec": (e["target_sec_min"], e["target_sec_max"])}
    for e in _MON["elites"]
}

ARCHER_WINDUP_TICKS = _MON["archer_windup_ticks"]   # 궁병대장 조준

# ── 타겟 우선순위 (design.md §3) ───────────────────────────────
# **오토 배틀이므로 플레이어는 대상을 고르지 않는다.** 그래서 타겟 규칙이 곧
# 밸런싱 손잡이다. 단일 공격은 보스 > 엘리트 > 최근접 잡몹 순으로 간다.
#
# 최근접 우선으로 두면 잡몹 30~60마리에 피해가 분산돼 엘리트 처치가 12초 →
# 100초가 되고, QTE가 구간당 4회 → 33회로 예산(3~5회)을 8배 넘긴다.
ELITE_PRIORITY = True


# 엘리트 공격 주기(초). 궁병대장 조준 60틱이 기준이며 나머지도 같은 리듬으로 본다.
# **모델 가정**이지 데이터가 아니다.
ELITE_CYCLE_SEC = 3.0


def elite_pressure(segment_index):
    """구간 내 엘리트가 만드는 초당 잠식.

    평균 상주 수 = 등장 수 × 처치 시간 / 구간 시간. 엘리트는 등장 즉시 최우선으로
    맞으므로(§3) 처치 시간이 곧 상주 시간이다.

    **원거리 잡몹을 폐지하면서 이 축이 필수가 됐다.** 근접 피격은 패킹 한계 5로
    막혀 있어 동시 생존 상한이 올라도 늘지 않는다 — 맵 안의 난이도 곡선을
    만들 수 있는 건 엘리트뿐이다.
    """
    seg = _SEGD["segments"][segment_index - 1]
    segsec = seg["target_ticks"] / TICK_HZ
    total = 0.0
    for name in seg["elites"]:
        e = ELITES[name]
        kill_sec = sum(e["target_sec"]) / 2
        total += (kill_sec / segsec) * e["damage"] / ELITE_CYCLE_SEC
    return total


def elite_damage_share():
    """엘리트가 실제로 받는 피해 비중. 단일 공격은 고정, 광역은 전장에 퍼진다."""
    aoe_weight = sum(w for _m, w, aoe in SKILLS.values() if aoe)
    return (1 - PROC_RATE) + PROC_RATE * aoe_weight if ELITE_PRIORITY else 0.11

# ── 보스 ───────────────────────────────────────────────────────
BOSS = {
    "hp": _MON["boss"]["hp"],
    "armor": _MON["boss"]["armor"],
    "phase2_at": pm(_MON["boss"]["phase2_at_permille"]),
    "patterns": {p["name"]: (p["hits"], p["damage"]) for p in _MON["boss"]["patterns"]},
}
# 최종 보스 조우 시점의 레벨업 누적 횟수 — verify_exp_curve.py의 풀 게임 투영값.
# 성장 배율은 감이 아니라 이 횟수에서 파생된다.
TOTAL_LEVELUPS = 60
BOSS_LEVELUPS = 58
BOSS_GROWTH_MULT = power_mult(BOSS_LEVELUPS)
BOSS_TARGET_SEC = (_MON["boss"]["target_sec_min"], _MON["boss"]["target_sec_max"])


def hero_dps():
    """기본 공격 + 스킬 발동분을 합친 단일 대상 기대 DPS."""
    aps = TICK_HZ / HERO["attack_interval_ticks"]
    hit = HERO["attack_power"] * (1 + HERO["crit_chance"] * (HERO["crit_mult"] - 1))
    basic = hit * aps
    # proc으로 스킬이 나가면 그 타격이 스킬 배율로 대체된다고 본다
    avg_mult = sum(mult * weight for mult, weight, _ in SKILLS.values())
    skill_bonus = basic * PROC_RATE * (avg_mult - 1.0)
    return basic + skill_bonus, basic, avg_mult


def report():
    ok = True
    dps, basic_dps, avg_mult = hero_dps()
    aps = TICK_HZ / HERO["attack_interval_ticks"]

    print("=== 전사 기준선 ===")
    print(f"  공격력 {HERO['attack_power']} / 간격 {HERO['attack_interval_ticks']}틱"
          f"({HERO['attack_interval_ticks']/TICK_HZ}초, 초당 {aps:g}회)")
    print(f"  기본 공격 DPS {basic_dps:.1f}, 스킬 평균 배율 {avg_mult:.2f}배")
    print(f"  → 단일 대상 총 DPS {dps:.1f}")
    print()

    print(f"=== 목표 1: 일반 몹은 기본 공격 {HITS_TO_KILL_TRASH}대에 죽는다 ===")
    hits = TRASH["hp"] / HERO["attack_power"]
    print(f"  몹 HP {TRASH['hp']} / 공격력 {HERO['attack_power']} = {hits:g}대")
    ok &= (hits == HITS_TO_KILL_TRASH)
    print(f"  {'PASS' if hits == HITS_TO_KILL_TRASH else 'FAIL'}\n")

    print("=== 목표 2: 스킬이 일반 몹을 2대 이내에 정리한다 ===")
    for name, (mult, _w, is_aoe) in SKILLS.items():
        hits = TRASH["hp"] / (HERO["attack_power"] * mult)
        good = hits <= 2.0
        ok &= good
        tag = "광역" if is_aoe else "단일"
        print(f"  {name}({tag}): {HERO['attack_power']*mult:g} vs 몹 HP {TRASH['hp']} "
              f"→ {hits:.1f}대  {'PASS' if good else 'FAIL'}")
    print("  ※ 잡몹이 기본 공격 3대짜리가 되면서 **'스킬 한 방 = 원샷'은 포기했다.**")
    print("     광역 축의 존재 이유는 원샷이 아니라 동시 타격 수다 — 구간 8에서")
    print(f"     광역 1회가 {aoe_targets(8):.1f}마리를 때리므로 기본 공격보다 훨씬 효율적이다\n")

    print(f"=== 목표 3: 잡몹 타수가 판 내내 {TRASH_HITS_BAND[0]:g}~{TRASH_HITS_BAND[1]:g}대 ===")
    lo_h, hi_h = TRASH_HITS_BAND
    print(f"  공속 몫 {SPEED_GROWTH_SHARE:.0%} → 런 종료 시 공속 "
          f"{speed_mult(BOSS_LEVELUPS):.1f}배 / 한 대 피해 {damage_mult(BOSS_LEVELUPS):.1f}배")
    print(f"  {'구간':>6} {'몹 HP':>9} {'한 대 피해':>10} {'타수':>6}")
    hits_ok = True
    for w in (1, 8, 16, TOTAL_WAVES):
        lv = round(TOTAL_LEVELUPS * (w - 1) / TOTAL_WAVES)
        dmg = HERO["attack_power"] * damage_mult(lv)
        h = TRASH["hp"] * hp_scale(w) / dmg
        hits_ok &= lo_h <= h <= hi_h
        print(f"  {w:>6} {TRASH['hp']*hp_scale(w):>9.0f} {dmg:>10.1f} {h:>6.1f}"
              f"{'' if lo_h <= h <= hi_h else '  ← 밴드 밖'}")
    ok &= hits_ok
    print(f"  {'PASS' if hits_ok else 'FAIL'}")
    print("  ※ 타수가 1이면 치명타도 공격력 성장도 전부 오버킬로 버려진다")
    print("     (tools/verify_crit_axis.py). 그래서 잡몹 HP를 10 → 30으로 올렸다\n")

    print("=== 목표 4: 1구간 통과 20~35초 ===")
    # 연속 스폰이라 전장이 항상 채워져 있다 → 광역 효율이 동시 생존 상한에서 나온다
    et = effective_targets(1)
    clear = TRASH["hp"] * SEGMENT_TRASH_COUNT / (dps * et) / COMBAT_EFFICIENCY
    good = 20 <= clear <= 35
    ok &= good
    print(f"  몹 {SEGMENT_TRASH_COUNT}마리 × HP {TRASH['hp']} / (DPS {dps:.1f} × 동시타격 {et:.2f})")
    print(f"  (구간 1 동시 생존 상한 {CONCURRENT_CAP[1]} → 광역 1회 {aoe_targets(1):.1f}마리)")
    print(f"  → {clear:.1f}초  {'PASS' if good else 'FAIL'}")
    if not good:
        need = HERO["attack_interval_ticks"] * 30.0 / clear
        print(f"  ※ 30초로 되돌리려면 기준선 공격 간격이 "
              f"{HERO['attack_interval_ticks']}틱 → **{need:.0f}틱**, 또는")
        print(f"     구간 1 몹 수를 {SEGMENT_TRASH_COUNT} → "
              f"{SEGMENT_TRASH_COUNT*30/clear:.0f}마리로 줄인다\n")
    else:
        print()

    print("=== 목표 5: 잠식이 가득 차기까지 15초 이상 ===")
    print(f"  {'구간':>4} {'상한':>4} {'피격':>7} {'물량':>7} {'엘리트':>8} {'합':>7} {'생존':>7}")
    worst = 1e9
    for i in range(1, len(_SEGD["segments"]) + 1):
        cap = CONCURRENT_CAP[i]
        hits = SURROUND_COUNT * TRASH["damage"] / (TRASH["cooldown_ticks"] / TICK_HZ)
        ms = corruption_from_mass(cap, cap)
        ep = elite_pressure(i)
        inc = hits + ms + ep
        worst = min(worst, HERO["corruption_max"] / inc)
        print(f"  {i:>4} {cap:>4} {hits:>7.1f} {ms:>7.1f} {ep:>8.1f} {inc:>7.1f} "
              f"{HERO['corruption_max'] / inc:>6.1f}초")
    ok &= (worst >= 15)
    print(f"  최악 {worst:.1f}초 (목표 15초)  {'PASS' if worst >= 15 else 'FAIL'}")
    print("  ※ 근접 피격은 패킹 한계 5로 고정된다 — 상한이 올라도 늘지 않으므로")
    print("     맵 안의 난이도 곡선은 물량 충전과 엘리트가 만든다\n")

    print("=== 목표 6: QTE 구간당 3~5회 ===")
    procs = aps * clear * PROC_RATE
    qte = min(procs, clear / QTE_COOLDOWN_SEC)
    ok &= (3 <= qte <= 5)
    print(f"  스킬 발동 {procs:.1f}회 (초당 {aps:g}회 공격 × {clear:.1f}초 × proc {PROC_RATE:.0%})")
    print(f"  QTE 쿨다운 {QTE_COOLDOWN_SEC:g}초 적용 → {qte:.1f}회  {'PASS' if 3 <= qte <= 5 else 'FAIL'}")
    if procs > 5:
        print(f"  ※ 쿨다운이 상한을 강제하고 있다 (없으면 {procs:.1f}회로 예산 초과)")
    else:
        # 기준선에서는 proc 빈도 자체가 예산 안이라 쿨다운이 거의 걸리지 않는다.
        # 공속이 오르면(전투 광란·아이템) 그때부터 상한을 잡는 안전장치로 작동한다.
        speedup = 5 / procs
        print(f"  ※ 기준선에서는 쿨다운이 거의 걸리지 않는다. 공속이 {speedup:.1f}배 오르면 작동")
    print()

    print("=== 목표 7: 엘리트 처치 시간 ===")
    share = elite_damage_share()
    print(f"  타겟 우선순위 적용 → 엘리트가 받는 피해 비중 {share:.0%}")
    for name, e in ELITES.items():
        ehp = effective_hp(e["hp"], e["armor"])
        sec = ehp / (dps * share)
        lo, hi = e["target_sec"]
        good = lo <= sec <= hi
        ok &= good
        armor_note = f", Armor {e['armor']} → 실효 {ehp:.0f}" if e["armor"] else ""
        print(f"  {name}: HP {e['hp']}{armor_note} → {sec:.1f}초 "
              f"(목표 {lo}~{hi}) {'PASS' if good else 'FAIL'}")
    print()

    print("=== 목표 8: 궁병대장이 조준을 마치기 전에 죽지 않는다 (런 전체) ===")
    a = ELITES["고블린 궁병대장"]
    windup_sec = ARCHER_WINDUP_TICKS / TICK_HZ
    print(f"  {'구간':>4} {'성장':>8} {'엘리트 HP':>9} {'처치':>8} {'조준 완료':>9}")
    shot_ok = True
    for w in (1, 8, 16, TOTAL_SEGMENTS):
        lv = round(TOTAL_LEVELUPS * (w - 1) / TOTAL_SEGMENTS)
        ehp = effective_hp(a["hp"], a["armor"]) * elite_scale(w)
        sec = ehp / (dps * power_mult(lv) * share)
        shots = sec / windup_sec
        shot_ok &= shots >= 2
        print(f"  {w:>4} {power_mult(lv):>7.1f}배 {ehp:>9.0f} {sec:>7.1f}초 {shots:>8.1f}회")
    ok &= shot_ok
    print(f"  {'PASS' if shot_ok else 'FAIL'}  (QTE를 최소 2회는 볼 수 있어야 한다)")
    print("  ※ 엘리트 체력이 잡몹 배율(1.09)을 타면 마지막 구간에서 1.4초에 죽어")
    print("     텔레그래프가 다 돌기 전에 사라진다 — QTE 시스템이 후반에 소멸한다\n")

    print("=== 목표 9: 보스 처치 시간 ===")
    bhp = effective_hp(BOSS["hp"], BOSS["armor"])
    grown_dps = dps * BOSS_GROWTH_MULT
    bsec = bhp / grown_dps
    lo, hi = BOSS_TARGET_SEC
    ok &= (lo <= bsec <= hi)
    print(f"  HP {BOSS['hp']}, Armor {BOSS['armor']} → 실효 {bhp:.0f}")
    print(f"  성장 {BOSS_GROWTH_MULT:g}배 가정 DPS {grown_dps:.1f} → {bsec:.1f}초 "
          f"(목표 {lo}~{hi}) {'PASS' if lo <= bsec <= hi else 'FAIL'}")
    print(f"  페이즈 2 진입: HP {BOSS['hp'] * BOSS['phase2_at']:.0f} "
          f"({bsec * BOSS['phase2_at']:.0f}초 지점)\n")

    print("=== 목표 10: 보스 패턴 한 사이클이 잠식을 다 채우지 않는다 ===")
    cycle = sum(n * d for n, d in BOSS["patterns"].values())
    ratio = cycle / HERO["corruption_max"]
    ok &= (ratio < 1.0)
    for pname, (n, d) in BOSS["patterns"].items():
        print(f"  {pname}: {n}타 x {d} = {n*d}")
    print(f"  한 사이클 합 {cycle} / 잠식 최대치 {HERO['corruption_max']} = {ratio:.0%}"
          f"  {'PASS' if ratio < 1.0 else 'FAIL'}\n")

    print("=== 목표 11: 구간 스케일링 ===")
    final = hp_scale(TOTAL_WAVES)
    final_hp = TRASH["hp"] * final
    print(f"  구간당 x{HP_SCALE_PER_WAVE}, {TOTAL_WAVES}구간 → 최종 {final:.2f}배")
    for w in (1, 5, 10, TOTAL_WAVES):
        h = TRASH["hp"] * hp_scale(w)
        print(f"    구간 {w:>2}: 몹 HP {h:>5.1f}  (기본 공격 {h/HERO['attack_power']:.1f}대)")
    print(f"  → 마지막 구간에서도 원샷하려면 공격력 {final:.1f}배 성장이 필요하다")
    print(f"     **잡몹 원샷 구조에서 공격력 성장이 체감되는 지점이 여기다**")
    # **비교 대상은 총 성장이 아니라 한 대 피해 성장이다.** 성장의 절반이 공속으로
    # 가므로(SPEED_GROWTH_SHARE), 몹 체력은 공속이 아니라 타격당 피해를 따라가야
    # 타수가 유지된다. 공속 성장은 체력이 아니라 **처치율**과 맞물린다.
    grown = damage_mult(TOTAL_LEVELUPS)
    gap = final / grown
    good = 0.8 <= gap <= 1.25
    ok &= good
    print(f"  몹 체력 {final:.1f}배 vs 한 대 피해 성장 {grown:.1f}배 "
          f"(총 성장 {power_mult(TOTAL_LEVELUPS):.1f}배 중) → 격차 {gap:.2f}배  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 이 격차가 곧 잡몹 타수의 변화다. 1에서 벗어나면 타수가 흐른다")
    print(f"  ※ 나머지 성장({speed_mult(TOTAL_LEVELUPS):.1f}배)은 공속으로 가서 "
          "처치율을 올린다 — 전장이 고이지 않게 하는 축이다\n")

    print("=== 목표 12: 모든 수치가 Fixed 20.12 범위 안 ===")
    worst = max(HERO["corruption_max"],
                final_hp * SEGMENT_TRASH_COUNT,
                effective_hp(BOSS["hp"], BOSS["armor"]),
                HERO["attack_power"] * max(m for m, _w, _a in SKILLS.values()))
    head = FIXED_MAX / worst
    ok &= (head >= 5)
    print(f"  최대 사용값 {worst:.0f} / 상한 {FIXED_MAX} → 여유 {head:.1f}배"
          f"  {'PASS' if head >= 5 else 'FAIL'}")
    print(f"  ※ 보스 실효 HP {effective_hp(BOSS['hp'], BOSS['armor']):.0f}가 단일 최대값이다\n")

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
