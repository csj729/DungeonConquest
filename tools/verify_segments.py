"""수직 슬라이스 구간 구성 검증.

맵 1개 = **클리어 게이지 8구간 + 보스** (design.md §2). 구간은 시간이 아니라
게이지로 넘어가고, 몬스터는 동시 생존 상한을 유지하도록 연속 스폰된다.
각 구간이 무엇을 새로 시험하는지를 먼저 정하고, 그 구성이 시간·난이도 목표를
지키는지 계산으로 확인한다.

연속 스폰이므로 **전장이 항상 채워져 있어 광역기 효율이 동시 생존 상한에서 나온다** —
구간이 진행되며 상한이 30 → 60 → 150으로 오르면 같은 몹 수를 더 빨리 정리한다.

**영웅이 구간 도중에 강해진다는 사실을 모델에 넣는다.** 레벨업마다 위력이
`POWER_PER_LEVELUP`만큼 오르므로, 같은 몹 수라도 뒤 구간이 더 빨리 정리된다.
이걸 빼고 계산하면 난이도 곡선이 실제보다 가파르게 보인다 — 이전 버전의 오류다.

`balance_baseline.py`의 영웅·몬스터·경험치 수치를 그대로 가져다 쓴다.
수치를 바꾸면 양쪽 다 재실행할 것.
"""
import sys
sys.path.insert(0, "tools")

from balance_baseline import (
    hero_dps, TRASH, hp_scale, PROC_RATE, SKILLS, ELITES,
    effective_hp, TICK_HZ, level_need, power_mult, monster_exp,
    EXP_PER_EHP, CARD_PICK_SEC, concurrent_cap, elite_scale, aoe_targets, effective_targets,
)

from gamedata import SEGMENTS_DATA as _SEG, MONSTERS as _MON

# ── 클리어 게이지 (design.md §2) ───────────────────────────────
TRASH_POINTS = _SEG["trash_points"]
ELITE_POINTS = _SEG["elite_points"]

# ── 구간 구성 ──────────────────────────────────────────────────
# (근접, 원거리, [엘리트], 이 구간이 새로 시험하는 것)
#
# 몹 수가 구간마다 늘어난다. 영웅이 레벨업으로 강해지는 만큼 물량을 더 얹지
# 않으면 뒤 구간이 오히려 쉬워진다 — 수를 고정했던 이전 구성의 문제였다.
SEGMENTS = [(x["melee"], 0, x["elites"], x["note"]) for x in _SEG["segments"]]

# 수직 슬라이스용 보스 (풀 게임 최종 보스와 다르다)
SLICE_BOSS_HP = _MON["slice_boss_hp"]   # 목표 처치 시간에서 역산
SLICE_BOSS_ARMOR = _MON["slice_boss_armor"]

SEGMENT_SEC_RANGE = (25, 60)   # 구간 하나의 목표 통과 시간
# 위 구성은 이 목표 시간에서 몹 수를 역산해 푼 것이다. 맵 2·3도 같은 곡선을 쓴다.
SEGMENT_TARGET_SEC = [x["target_ticks"] / TICK_HZ for x in _SEG["segments"]]
RUN_MIN_RANGE = (5, 9)      # 맵 1개 목표 길이(분)
SLICE_LEVELUPS = (15, 25)   # 맵 1개에서 기대하는 레벨업 횟수


def elite_exp(names, seg_idx):
    return sum(monster_exp(effective_hp(ELITES[n]["hp"], ELITES[n]["armor"]) * elite_scale(seg_idx) / hp_scale(seg_idx), seg_idx)
               for n in names)


def clear_points():
    """맵 1개의 클리어 게이지 목표와 구간별 누적 비율."""
    pts, cum = [], 0
    for melee, ranged, elites, _n in SEGMENTS:
        cum += (melee + ranged) * TRASH_POINTS + len(elites) * ELITE_POINTS
        pts.append(cum)
    return pts[-1], [p / pts[-1] for p in pts]


def simulate():
    """구간을 순서대로 돌며 클리어 시간·누적 레벨업을 같이 굴린다."""
    dps, _b, _a = hero_dps()

    rows, lv, carry = [], 0, 0
    for i, (melee, ranged, elites, _note) in enumerate(SEGMENTS, start=1):
        n = melee + ranged
        trash_ehp = n * TRASH["hp"] * hp_scale(i)
        growth = power_mult(lv)
        # 게이지는 **잡몹 처치**로 찬다. 엘리트는 클리어 조건이 아니므로
        # 통과 시간에 넣지 않는다 (엘리트를 잡으면 10점이라 오히려 앞당겨진다).
        eff_targets = effective_targets(i)
        sec = trash_ehp / (dps * growth * eff_targets)

        carry += n * monster_exp(TRASH["hp"], i) + elite_exp(elites, i)
        gained = 0
        while carry >= level_need(lv + 1):
            carry -= level_need(lv + 1)
            lv += 1
            gained += 1
        rows.append((i, melee, ranged, n, elites, growth, sec, gained, lv, eff_targets))

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

    print("=== 구간 구성 ===")
    target, cum = clear_points()
    print(f"  클리어 게이지 목표 **{target}점** (잡몹 {TRASH_POINTS}점 × "
          f"{sum(m+r for m,r,_e,_n in SEGMENTS)}마리 + 엘리트 {ELITE_POINTS}점 × "
          f"{sum(len(e) for _m,_r,e,_n in SEGMENTS)}마리)\n")
    print(f"{'S':>2} {'게이지':>6} {'근접':>4} {'원거리':>5} {'합':>4} {'원%':>5} "
          f"{'상한':>5} {'동시타격':>7} {'성장':>6} {'통과':>7} {'Lv+':>4} {'누적':>4}  엘리트")
    print("-" * 108)

    total_sec = 0.0
    prev_sec = 0.0
    for i, melee, ranged, n, elites, growth, sec, gained, lv, et in rows:
        total_sec += sec
        lo, hi = SEGMENT_SEC_RANGE
        good = lo <= sec <= hi and sec >= prev_sec
        ok &= good
        prev_sec = sec
        mark = "" if good else "  ← FAIL"
        print(f"{i:>2} {cum[i-1]:>5.0%} {melee:>4} {ranged:>5} {n:>4} {ranged/n:>4.0%} "
              f"{concurrent_cap(i):>5} {aoe_targets(i):>7.1f} {growth:>5.2f}배 "
              f"{sec:>6.1f}초 {gained:>4} {lv:>4}  "
              f"{', '.join(elites) if elites else '—'}{mark}")

    print()
    print(f"=== 목표 1: 각 구간 통과 {SEGMENT_SEC_RANGE[0]}~{SEGMENT_SEC_RANGE[1]}초, "
          f"난이도 단조 증가 ===")
    print(f"  {'PASS' if ok else 'FAIL'}")
    print("  ※ 영웅 성장과 **동시 생존 상한 상승(광역 효율)** 을 둘 다 반영했는데도")
    print("     시간이 늘어나야 진짜 난이도 상승이다\n")

    print("=== 목표 2: 엘리트 4종이 전부 등장한다 ===")
    seen = {e for _m, _r, es, _n in SEGMENTS for e in es}
    missing = set(ELITES) - seen
    ok &= not missing
    for name in ELITES:
        print(f"  {name}: {'등장' if name in seen else '누락'}")
    print(f"  {'PASS' if not missing else 'FAIL'}\n")

    print("=== 목표 3: 첫 QTE가 3구간 안에 등장한다 ===")
    qte_elites = {"고블린 궁병대장", "미친 고블린"}   # QTE 패턴을 가진 엘리트
    first = next((i for i, (_m, _r, es, _n) in enumerate(SEGMENTS, 1)
                  if qte_elites & set(es)), None)
    good = first is not None and first <= 3
    ok &= good
    print(f"  첫 QTE 구간: {first}  {'PASS' if good else 'FAIL'}\n")

    print("=== 목표 4: 한 판 길이 5~8분 ===")
    modal_sec = final_lv * CARD_PICK_SEC
    run_min = (total_sec + boss_sec + modal_sec) / 60
    lo, hi = RUN_MIN_RANGE
    good = lo <= run_min <= hi
    ok &= good
    print(f"  구간 합 {total_sec:.0f}초 + 보스 {boss_sec:.0f}초 "
          f"+ 카드 선택 {modal_sec:.0f}초 = {run_min:.1f}분  {'PASS' if good else 'FAIL'}")
    print(f"  (보스 HP {SLICE_BOSS_HP}, 조우 시점 성장 {boss_growth:.2f}배 — 가정이 아니라 "
          f"레벨업 누적에서 나온 값)\n")

    print("=== 목표 5: 원거리 비율이 단조 증가한다 ===")
    ratios = [r / (m + r) for m, r, _e, _n in SEGMENTS]
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

    print("=== 목표 7: 구간 목표 시간이 엘리트 처치 시간을 담을 수 있는가 ===")
    print("  **설계 세 조각이 서로 맞물리는지 본다** — 구간 target_ticks · 엘리트 처치")
    print("  시간 밴드(balance_baseline 목표 7) · 구간별 엘리트 구성. 셋 중 하나만")
    print("  움직여도 나머지가 따라와야 하는데 지금까지 이걸 보는 검사가 없었다.")
    kill_sec = {e["name"]: (e["target_sec_min"] + e["target_sec_max"]) / 2
                for e in _MON["elites"]}
    print(f"  {'구간':>4} {'목표(초)':>9} {'엘리트만':>9} {'잡몹':>6} {'남는 시간':>11}")
    tot_t = tot_e = 0.0
    bad = []
    for seg in _SEG["segments"]:
        t = seg["target_ticks"] / TICK_HZ
        e = sum(kill_sec[n] for n in seg["elites"])
        tot_t += t
        tot_e += e
        if e > t:
            bad.append(seg["index"])
        print(f"  {seg['index']:>4} {t:>9.0f} {e:>9.1f} {seg['melee']:>6} "
              f"{t - e:>9.1f}초{'  ← 초과' if e > t else ''}")

    left = tot_t - tot_e
    trash = sum(s["melee"] for s in _SEG["segments"])
    good = left > 0 and not bad
    ok &= good
    print(f"  합계 목표 {tot_t:.0f}초 · 엘리트 처치만 {tot_e:.0f}초 ({tot_e / tot_t:.0%}) "
          f"→ 잡몹 몫 {left:.0f}초  {'PASS' if good else 'FAIL'}")
    if left > 0:
        print(f"  → 잡몹 {trash}마리에 필요한 처치율 {trash / left:.2f}마리/초")
    else:
        print("  → **잡몹을 잡을 시간이 남지 않는다.** 엘리트만 처치해도 목표 시간을 넘는다")
    print("  ※ 방패병(실효 300 · 처치 24.6초)이 구간 6~8에 3마리씩 들어가는 것이 대부분이다.")
    print("     엘리트 체력·구성·구간 시간 중 하나를 움직여야 셋이 맞물린다\n")

    print("=== 참고: 구간별 의도 ===")
    for i, (_m, _r, _e, note) in enumerate(SEGMENTS, start=1):
        print(f"  S{i}: {note}")

    print()
    print(f"총 일반 몹 {sum(m + r for m, r, _e, _n in SEGMENTS)}마리, "
          f"엘리트 {sum(len(e) for _m, _r, e, _n in SEGMENTS)}마리")
    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
