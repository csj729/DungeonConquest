"""전사 기준선 수치 검산.

수치를 감으로 정하지 않기 위해, **플레이 목표를 먼저 쓰고 그 목표가 지켜지는지**를
계산으로 확인한다. 수치를 바꿀 때마다 재실행할 것.

몬테카를로 하네스(design.md §14-10)의 전신이다. 여기서는 분산 없이 기댓값만 본다 —
분포가 필요해지면 C++ 코어가 나온 뒤 헤드리스로 돌린다.
"""

TICK_HZ = 20
FIXED_MAX = 524288  # Fixed 20.12 (int32_t raw) 표현 상한

# ── 전사 기준선 ────────────────────────────────────────────────
HERO = {
    "attack_power": 10,        # 기준 단위 — 모든 수치를 이 배수로 읽는다
    "attack_interval_ticks": 20,  # 1.0초
    "crit_chance": 0.10,
    "crit_mult": 1.5,
    "max_hp": 300,
    "armor": 0,
}

PROC_RATE = 0.15           # 통합 proc (design.md §3)
QTE_COOLDOWN_SEC = 6.0     # 스킬 발동과 별개로 QTE에 거는 자체 쿨다운

SKILLS = {                 # (기본 공격 대비 배율, 가중치, 광역 여부)
    "분쇄 강타":   (3.0, 0.40, False),
    "회전 베기":   (2.5, 0.35, True),
    "대지 가르기": (2.0, 0.25, True),
}

# ── 일반 몹 (역산 대상) ────────────────────────────────────────
TRASH = {
    "hp": 10,                  # 기본 공격 1대 — 물량을 시원하게 정리하는 감각 우선
    "damage": 5,
    "windup_ticks": 20,        # 사거리 진입 → 첫 공격까지 1.0초
    "cooldown_ticks": 30,      # 이후 1.5초 주기
}

WAVE_TRASH_COUNT = 38          # 웨이브 전체 몹 수 (동시 표시 30~60과는 다름)
HITS_TO_KILL_TRASH = 1         # 목표: 일반 몹은 기본 공격 몇 대에 죽는가
SURROUND_COUNT = 5             # 영웅에게 동시에 붙을 수 있는 몹 수 가정

# ── 방어력 감쇠 ────────────────────────────────────────────────
# 비율 감쇠: 실데미지 = 데미지 * K / (K + Armor).  Armor=K면 정확히 절반.
# 감산(데미지 - Armor)을 쓰지 않는 이유: 아이템 조합으로 위력이 곱해지는
# 게임이라 후반에 Armor가 완전히 무의미해진다. 비율은 배율 성장과 무관하게
# 일정 비율을 유지한다.
ARMOR_K = 100


def effective_hp(hp, armor):
    """Armor를 감안한 실효 체력 — 실제로 넣어야 하는 총 데미지."""
    return hp * (ARMOR_K + armor) / ARMOR_K


# ── 웨이브 스케일링 ────────────────────────────────────────────
# 스테이지가 진행될수록 몹 체력이 오른다. 완만한 지수로 잡아 Fixed 상한에
# 부딪히지 않게 한다. 보스는 고정 등장이므로 스케일링 대상이 아니다.
HP_SCALE_PER_WAVE = 1.12
TOTAL_WAVES = 15               # 룸 5개 x 웨이브 3개


def hp_scale(wave):
    return HP_SCALE_PER_WAVE ** (wave - 1)


# ── 엘리트 (등장 웨이브 스케일링 적용 전 기준값) ───────────────
ELITES = {
    "고블린 궁병대장": {"hp": 120, "armor": 0,   "damage": 40, "target_sec": (7, 12)},
    "고블린 방패병":   {"hp": 100, "armor": 200, "damage": 8,  "target_sec": (18, 30)},
    "고블린 주술사":   {"hp": 90,  "armor": 0,   "damage": 5,  "target_sec": (5, 10)},
    "미친 고블린":     {"hp": 150, "armor": 0,   "damage": 3,  "target_sec": (9, 16)},
}

ARCHER_WINDUP_TICKS = 60       # 궁병대장 조준 — QTE를 볼 수 있어야 한다

# ── 보스 ───────────────────────────────────────────────────────
BOSS = {
    "hp": 3000,
    "armor": 50,
    "phase2_at": 0.5,          # HP 50%에서 페이즈 2 추가
    "patterns": {              # (타수, 타당 데미지)
        "삼연격": (3, 15),
        "대곤봉 강타": (1, 70),
        "돌진 찌르기": (2, 20),
    },
}
BOSS_GROWTH_MULT = 4.0         # 보스 조우 시점의 영웅 위력 성장 배율 가정
BOSS_TARGET_SEC = (60, 90)


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

    print("=== 목표 2: 광역기는 일반 몹을 한 방에 정리한다 ===")
    for name, (mult, _w, is_aoe) in SKILLS.items():
        if not is_aoe:
            continue
        dmg = HERO["attack_power"] * mult
        good = dmg >= TRASH["hp"]
        ok &= good
        print(f"  {name}: {dmg:g} vs 몹 HP {TRASH['hp']} → {'PASS' if good else 'FAIL'}")
    print()

    print("=== 목표 3: 웨이브 클리어 20~30초 ===")
    # 광역기가 평균 몇 마리를 함께 정리하는지는 밀집도에 달렸다. 보수적으로 3마리 가정.
    aoe_weight = sum(w for _m, w, aoe in SKILLS.values() if aoe)
    effective_targets = 1 + PROC_RATE * aoe_weight * (3 - 1)
    clear = TRASH["hp"] * WAVE_TRASH_COUNT / (dps * effective_targets)
    ok &= (20 <= clear <= 30)
    print(f"  몹 {WAVE_TRASH_COUNT}마리 × HP {TRASH['hp']} / (DPS {dps:.1f} × 동시타격 {effective_targets:.2f})")
    print(f"  → {clear:.1f}초  {'PASS' if 20 <= clear <= 30 else 'FAIL'}\n")

    print("=== 목표 4: 둘러싸여도 15초 이상 버틴다 ===")
    incoming = TRASH["damage"] * SURROUND_COUNT / (TRASH["cooldown_ticks"] / TICK_HZ)
    survive = HERO["max_hp"] / incoming
    ok &= (survive >= 15)
    print(f"  {SURROUND_COUNT}마리 × {TRASH['damage']}딜 / {TRASH['cooldown_ticks']/TICK_HZ}초 = 초당 {incoming:.1f}")
    print(f"  HP {HERO['max_hp']} → {survive:.1f}초  {'PASS' if survive >= 15 else 'FAIL'}\n")

    print("=== 목표 5: QTE 웨이브당 3~5회 ===")
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

    print("=== 목표 6: 엘리트 처치 시간 ===")
    for name, e in ELITES.items():
        ehp = effective_hp(e["hp"], e["armor"])
        sec = ehp / dps
        lo, hi = e["target_sec"]
        good = lo <= sec <= hi
        ok &= good
        armor_note = f", Armor {e['armor']} → 실효 {ehp:.0f}" if e["armor"] else ""
        print(f"  {name}: HP {e['hp']}{armor_note} → {sec:.1f}초 "
              f"(목표 {lo}~{hi}) {'PASS' if good else 'FAIL'}")
    print()

    print("=== 목표 7: 궁병대장이 조준을 마치기 전에 죽지 않는다 ===")
    a = ELITES["고블린 궁병대장"]
    kill_sec = effective_hp(a["hp"], a["armor"]) / dps
    windup_sec = ARCHER_WINDUP_TICKS / TICK_HZ
    shots = int(kill_sec / windup_sec)
    ok &= (shots >= 2)
    print(f"  처치 {kill_sec:.1f}초 / 조준 {windup_sec:.1f}초 → 조준 완료 {shots}회")
    print(f"  {'PASS' if shots >= 2 else 'FAIL'}  (QTE를 최소 2회는 볼 수 있어야 한다)\n")

    print("=== 목표 8: 보스 처치 시간 ===")
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

    print("=== 목표 9: 보스 패턴 한 사이클이 영웅을 즉사시키지 않는다 ===")
    cycle = sum(n * d for n, d in BOSS["patterns"].values())
    ratio = cycle / HERO["max_hp"]
    ok &= (ratio < 1.0)
    for pname, (n, d) in BOSS["patterns"].items():
        print(f"  {pname}: {n}타 x {d} = {n*d}")
    print(f"  한 사이클 합 {cycle} / 영웅 HP {HERO['max_hp']} = {ratio:.0%}"
          f"  {'PASS' if ratio < 1.0 else 'FAIL'}\n")

    print("=== 목표 10: 웨이브 스케일링 ===")
    final = hp_scale(TOTAL_WAVES)
    final_hp = TRASH["hp"] * final
    print(f"  웨이브당 x{HP_SCALE_PER_WAVE}, {TOTAL_WAVES}웨이브 → 최종 {final:.2f}배")
    for w in (1, 5, 10, TOTAL_WAVES):
        h = TRASH["hp"] * hp_scale(w)
        print(f"    웨이브 {w:>2}: 몹 HP {h:>5.1f}  (기본 공격 {h/HERO['attack_power']:.1f}대)")
    print(f"  → 마지막 웨이브에서도 원샷하려면 공격력 {final:.1f}배 성장이 필요하다")
    print(f"     **잡몹 원샷 구조에서 공격력 성장이 체감되는 지점이 여기다**\n")

    print("=== 목표 11: 모든 수치가 Fixed 20.12 범위 안 ===")
    worst = max(HERO["max_hp"],
                final_hp * WAVE_TRASH_COUNT,
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
