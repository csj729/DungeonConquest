"""전사 기준선 수치 검산.

수치를 감으로 정하지 않기 위해, **플레이 목표를 먼저 쓰고 그 목표가 지켜지는지**를
계산으로 확인한다. 수치를 바꿀 때마다 재실행할 것.

몬테카를로 하네스(design.md §14-10)의 전신이다. 여기서는 분산 없이 기댓값만 본다 —
분포가 필요해지면 C++ 코어가 나온 뒤 헤드리스로 돌린다.
"""

TICK_HZ = 20
FIXED_MAX = 32768  # Fixed 16.16 (int32_t raw) 표현 상한

# ── 전사 기준선 ────────────────────────────────────────────────
HERO = {
    "attack_power": 10,        # 기준 단위 — 모든 수치를 이 배수로 읽는다
    "attack_interval_ticks": 10,  # 0.5초
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
    "hp": 20,                  # 기본 공격 2대
    "damage": 5,
    "windup_ticks": 20,        # 사거리 진입 → 첫 공격까지 1.0초
    "cooldown_ticks": 30,      # 이후 1.5초 주기
}

WAVE_TRASH_COUNT = 38          # 웨이브 전체 몹 수 (동시 표시 30~60과는 다름)
SURROUND_COUNT = 5             # 영웅에게 동시에 붙을 수 있는 몹 수 가정


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

    print("=== 목표 1: 일반 몹은 기본 공격 2대에 죽는다 ===")
    hits = TRASH["hp"] / HERO["attack_power"]
    print(f"  몹 HP {TRASH['hp']} / 공격력 {HERO['attack_power']} = {hits:g}대")
    ok &= (hits == 2)
    print(f"  {'PASS' if hits == 2 else 'FAIL'}\n")

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
    print(f"  ※ 쿨다운이 없으면 {procs:.1f}회로 예산 초과\n")

    print("=== 목표 6: 모든 수치가 Fixed 16.16 범위 안 ===")
    worst = max(HERO["max_hp"], TRASH["hp"] * WAVE_TRASH_COUNT,
                HERO["attack_power"] * max(m for m, _w, _a in SKILLS.values()))
    head = FIXED_MAX / worst
    ok &= (head >= 10)
    print(f"  최대 사용값 {worst} / 상한 {FIXED_MAX} → 여유 {head:.0f}배"
          f"  {'PASS' if head >= 10 else 'FAIL'}")
    print(f"  ※ 보스 HP는 {FIXED_MAX}를 넘을 수 없다 — 성장 곡선의 천장\n")

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
