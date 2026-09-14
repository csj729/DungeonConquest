"""수직 슬라이스 웨이브 구성 검증.

맵 1개 = 웨이브 8개 + 보스. 각 웨이브가 무엇을 새로 시험하는지를 먼저 정하고,
그 구성이 시간·난이도 목표를 지키는지 계산으로 확인한다.

**영웅이 웨이브 도중에 강해진다는 사실을 모델에 넣는다.** 레벨업마다 위력이
`POWER_PER_LEVELUP`만큼 오르므로, 같은 몹 수라도 뒤 웨이브가 더 빨리 정리된다.
이걸 빼고 계산하면 난이도 곡선이 실제보다 가파르게 보인다 — 이전 버전의 오류다.

`balance_baseline.py`의 영웅·몬스터·경험치 수치를 그대로 가져다 쓴다.
수치를 바꾸면 양쪽 다 재실행할 것.
"""
import sys
sys.path.insert(0, "tools")

from balance_baseline import (
    hero_dps, TRASH, hp_scale, PROC_RATE, SKILLS, ELITES,
    effective_hp, TICK_HZ, level_need, power_mult, monster_exp,
    EXP_PER_EHP, CARD_PICK_SEC,
)

# ── 웨이브 구성 ────────────────────────────────────────────────
# (근접, 원거리, [엘리트], 이 웨이브가 새로 시험하는 것)
#
# 몹 수가 웨이브마다 늘어난다. 영웅이 레벨업으로 강해지는 만큼 물량을 더 얹지
# 않으면 뒤 웨이브가 오히려 쉬워진다 — 수를 고정했던 이전 구성의 문제였다.
WAVES = [
    (38,  0, [],                               "기본 전투 · 사방 스폰"),
    (29, 13, [],                               "원거리 견제 — 사거리 이탈이 안 통하는 적"),
    (29, 16, ["고블린 궁병대장"],               "첫 QTE — 텔레그래프 읽기"),
    (29, 19, ["고블린 주술사"],                 "처치 우선순위 — 방치하면 물량이 불어난다"),
    (30, 24, ["고블린 방패병"],                 "광역·방어 관통의 필요성"),
    (32, 28, ["미친 고블린", "고블린 궁병대장"], "종합 — 지속 압박 + QTE"),
    (35, 33, ["고블린 주술사", "고블린 방패병"], "최악의 조합 — 계속 불어나는데 잘 안 죽는다"),
    (41, 41, ["고블린 궁병대장", "미친 고블린",
              "고블린 주술사"],                  "최종 압박 — 보스 직전"),
]

# 수직 슬라이스용 보스 (풀 게임 최종 보스와 다르다)
SLICE_BOSS_HP = 2800
SLICE_BOSS_ARMOR = 50

WAVE_SEC_RANGE = (20, 50)   # 웨이브 하나의 목표 클리어 시간
RUN_MIN_RANGE = (5, 8)      # 한 판 목표 길이(분)
SLICE_LEVELUPS = (20, 30)   # 맵 1개에서 기대하는 레벨업 횟수


def elite_exp(names, wave_idx):
    return sum(monster_exp(effective_hp(ELITES[n]["hp"], ELITES[n]["armor"]), wave_idx)
               for n in names)


def simulate():
    """웨이브를 순서대로 돌며 클리어 시간·누적 레벨업을 같이 굴린다."""
    dps, _b, _a = hero_dps()
    aoe_weight = sum(w for _m, w, aoe in SKILLS.values() if aoe)
    eff_targets = 1 + PROC_RATE * aoe_weight * (3 - 1)

    rows, lv, carry = [], 0, 0
    for i, (melee, ranged, elites, _note) in enumerate(WAVES, start=1):
        n = melee + ranged
        trash_ehp = n * TRASH["hp"] * hp_scale(i)
        growth = power_mult(lv)
        # 클리어 조건은 **일반 몹 전멸**이다. 엘리트는 남아서 누적되므로
        # 클리어 시간에 넣지 않는다 (monsters_vertical_slice.md §5).
        sec = trash_ehp / (dps * growth * eff_targets)

        carry += n * monster_exp(TRASH["hp"], i) + elite_exp(elites, i)
        gained = 0
        while carry >= level_need(lv + 1):
            carry -= level_need(lv + 1)
            lv += 1
            gained += 1
        rows.append((i, melee, ranged, n, elites, growth, sec, gained, lv))

    boss_growth = power_mult(lv)
    boss_ehp = effective_hp(SLICE_BOSS_HP, SLICE_BOSS_ARMOR)
    boss_sec = boss_ehp / (dps * boss_growth)
    carry += int(boss_ehp * EXP_PER_EHP)
    while carry >= level_need(lv + 1):
        carry -= level_need(lv + 1)
        lv += 1
    return rows, boss_sec, boss_growth, lv


def report():
    ok = True
    rows, boss_sec, boss_growth, final_lv = simulate()

    print("=== 웨이브 구성 ===")
    print(f"{'W':>2} {'근접':>4} {'원거리':>5} {'합':>4} {'원거리%':>7} {'성장':>6} "
          f"{'클리어':>7} {'Lv+':>4} {'누적':>4}  엘리트")
    print("-" * 92)

    total_sec = 0.0
    prev_sec = 0.0
    for i, melee, ranged, n, elites, growth, sec, gained, lv in rows:
        total_sec += sec
        lo, hi = WAVE_SEC_RANGE
        good = lo <= sec <= hi and sec >= prev_sec
        ok &= good
        prev_sec = sec
        mark = "" if good else "  ← FAIL"
        print(f"{i:>2} {melee:>4} {ranged:>5} {n:>4} {ranged/n:>6.0%} {growth:>5.2f}배 "
              f"{sec:>6.1f}초 {gained:>4} {lv:>4}  "
              f"{', '.join(elites) if elites else '—'}{mark}")

    print()
    print(f"=== 목표 1: 각 웨이브 클리어 {WAVE_SEC_RANGE[0]}~{WAVE_SEC_RANGE[1]}초, "
          f"난이도 단조 증가 ===")
    print(f"  {'PASS' if ok else 'FAIL'}")
    print("  ※ 영웅 성장을 반영했는데도 시간이 늘어나야 진짜 난이도 상승이다\n")

    print("=== 목표 2: 엘리트 4종이 전부 등장한다 ===")
    seen = {e for _m, _r, es, _n in WAVES for e in es}
    missing = set(ELITES) - seen
    ok &= not missing
    for name in ELITES:
        print(f"  {name}: {'등장' if name in seen else '누락'}")
    print(f"  {'PASS' if not missing else 'FAIL'}\n")

    print("=== 목표 3: 첫 QTE가 3웨이브 안에 등장한다 ===")
    qte_elites = {"고블린 궁병대장", "미친 고블린"}   # QTE 패턴을 가진 엘리트
    first = next((i for i, (_m, _r, es, _n) in enumerate(WAVES, 1)
                  if qte_elites & set(es)), None)
    good = first is not None and first <= 3
    ok &= good
    print(f"  첫 QTE 웨이브: {first}  {'PASS' if good else 'FAIL'}\n")

    print("=== 목표 4: 한 판 길이 5~8분 ===")
    modal_sec = final_lv * CARD_PICK_SEC
    run_min = (total_sec + boss_sec + modal_sec) / 60
    lo, hi = RUN_MIN_RANGE
    good = lo <= run_min <= hi
    ok &= good
    print(f"  웨이브 {total_sec:.0f}초 + 보스 {boss_sec:.0f}초 "
          f"+ 카드 선택 {modal_sec:.0f}초 = {run_min:.1f}분  {'PASS' if good else 'FAIL'}")
    print(f"  (보스 HP {SLICE_BOSS_HP}, 조우 시점 성장 {boss_growth:.2f}배 — 가정이 아니라 "
          f"레벨업 누적에서 나온 값)\n")

    print("=== 목표 5: 원거리 비율이 단조 증가한다 ===")
    ratios = [r / (m + r) for m, r, _e, _n in WAVES]
    good = all(b >= a for a, b in zip(ratios, ratios[1:]))
    ok &= good
    print(f"  {' → '.join(f'{r:.0%}' for r in ratios)}  {'PASS' if good else 'FAIL'}")
    print("  ※ 원거리가 늘수록 사거리 이탈로 피하기 어려워진다 — 압박 곡선\n")

    print(f"=== 목표 6: 맵 1개에서 레벨업 {SLICE_LEVELUPS[0]}~{SLICE_LEVELUPS[1]}회 ===")
    lo, hi = SLICE_LEVELUPS
    good = lo <= final_lv <= hi
    ok &= good
    interval = (total_sec + boss_sec) / final_lv
    print(f"  {final_lv}회 (평균 {interval:.1f}초 간격, 카드 {final_lv * 3}장)  "
          f"{'PASS' if good else 'FAIL'}")
    print(f"  모달 점유율 {modal_sec / (total_sec + boss_sec + modal_sec):.0%}\n")

    print("=== 참고: 웨이브별 의도 ===")
    for i, (_m, _r, _e, note) in enumerate(WAVES, start=1):
        print(f"  W{i}: {note}")

    print()
    print(f"총 일반 몹 {sum(m + r for m, r, _e, _n in WAVES)}마리, "
          f"엘리트 {sum(len(e) for _m, _r, e, _n in WAVES)}마리")
    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
