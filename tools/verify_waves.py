"""수직 슬라이스 웨이브 구성 검증.

맵 1개 = 웨이브 8개 + 보스. 각 웨이브가 무엇을 새로 시험하는지를 먼저 정하고,
그 구성이 시간·난이도 목표를 지키는지 계산으로 확인한다.

`balance_baseline.py`의 영웅·몬스터 수치를 그대로 가져다 쓴다.
수치를 바꾸면 양쪽 다 재실행할 것.
"""
import sys
sys.path.insert(0, "tools")

from balance_baseline import (
    hero_dps, TRASH, hp_scale, PROC_RATE, SKILLS, ELITES,
    effective_hp, TICK_HZ,
)

# ── 웨이브 구성 ────────────────────────────────────────────────
# (근접, 원거리, [엘리트], 이 웨이브가 새로 시험하는 것)
WAVES = [
    (38,  0, [],                               "기본 전투 · 사방 스폰"),
    (26, 12, [],                               "원거리 견제 — 사거리 이탈이 안 통하는 적"),
    (22, 12, ["고블린 궁병대장"],               "첫 QTE — 텔레그래프 읽기"),
    (20, 12, ["고블린 주술사"],                 "처치 우선순위 — 방치하면 물량이 불어난다"),
    (18, 14, ["고블린 방패병"],                 "광역·방어 관통의 필요성"),
    (20, 16, ["미친 고블린", "고블린 궁병대장"], "종합 — 지속 압박 + QTE"),
    (18, 15, ["고블린 주술사", "고블린 방패병"], "최악의 조합 — 계속 불어나는데 잘 안 죽는다"),
    (15, 15, ["고블린 궁병대장", "미친 고블린",
              "고블린 주술사"],                  "최종 압박 — 보스 직전"),
]

# 수직 슬라이스용 보스 (풀 게임 최종 보스와 다르다)
SLICE_BOSS_HP = 1300
SLICE_BOSS_ARMOR = 50
SLICE_GROWTH = 2.0          # 8웨이브 동안의 영웅 위력 성장 가정

WAVE_SEC_RANGE = (20, 45)   # 웨이브 하나의 목표 클리어 시간
RUN_MIN_RANGE = (5, 8)      # 한 판 목표 길이(분)


def trash_kill_sec(wave_idx):
    """해당 웨이브에서 일반 몹 1마리를 잡는 데 걸리는 시간."""
    dps, _b, _a = hero_dps()
    aoe_weight = sum(w for _m, w, aoe in SKILLS.values() if aoe)
    eff_targets = 1 + PROC_RATE * aoe_weight * (3 - 1)
    return TRASH["hp"] * hp_scale(wave_idx) / (dps * eff_targets)


def report():
    ok = True
    dps, _b, _a = hero_dps()

    print("=== 웨이브 구성 ===")
    print(f"{'W':>2} {'근접':>4} {'원거리':>5} {'합':>4} {'원거리%':>7} {'클리어':>7}  엘리트")
    print("-" * 78)

    total_sec = 0.0
    prev_sec = 0.0
    for i, (melee, ranged, elites, _note) in enumerate(WAVES, start=1):
        n = melee + ranged
        sec = n * trash_kill_sec(i)
        total_sec += sec
        lo, hi = WAVE_SEC_RANGE
        good = lo <= sec <= hi
        ok &= good
        # 난이도 단조 증가
        if sec < prev_sec:
            ok = False
            good = False
        prev_sec = sec
        mark = "" if good else "  ← FAIL"
        print(f"{i:>2} {melee:>4} {ranged:>5} {n:>4} {ranged/n:>6.0%} {sec:>6.1f}초  "
              f"{', '.join(elites) if elites else '—'}{mark}")

    print()
    print("=== 목표 1: 각 웨이브 클리어 20~45초, 난이도 단조 증가 ===")
    print(f"  {'PASS' if ok else 'FAIL'}\n")

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
    boss_sec = effective_hp(SLICE_BOSS_HP, SLICE_BOSS_ARMOR) / (dps * SLICE_GROWTH)
    run_min = (total_sec + boss_sec) / 60
    lo, hi = RUN_MIN_RANGE
    good = lo <= run_min <= hi
    ok &= good
    print(f"  웨이브 {total_sec:.0f}초 + 보스 {boss_sec:.0f}초 = {run_min:.1f}분  "
          f"{'PASS' if good else 'FAIL'}")
    print(f"  (보스 HP {SLICE_BOSS_HP}, 성장 {SLICE_GROWTH:g}배 가정)\n")

    print("=== 목표 5: 원거리 비율이 단조 증가한다 ===")
    ratios = [r / (m + r) for m, r, _e, _n in WAVES]
    good = all(b >= a for a, b in zip(ratios, ratios[1:]))
    ok &= good
    print(f"  {' → '.join(f'{r:.0%}' for r in ratios)}  {'PASS' if good else 'FAIL'}")
    print("  ※ 원거리가 늘수록 사거리 이탈로 피하기 어려워진다 — 압박 곡선\n")

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
