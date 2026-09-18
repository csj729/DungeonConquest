"""경험치 · 레벨업 곡선 검증.

레벨업은 이 게임에서 **뽑기와 스펙업의 유일한 관문**이다 (design.md §4).
따라서 "레벨업을 자주 하고 싶다"는 요구는 곧 곡선 설계 요구가 된다.

설계 순서를 뒤집어 잡았다 — 필요 경험치 공식을 먼저 정하고 결과를 보는 게 아니라,
**원하는 레벨업 횟수와 모달 점유율을 먼저 쓰고 곡선을 역산**한다.

핵심 제약 세 가지:

1. **모달이 시뮬을 멈춘다** (§10). 레벨업이 잦을수록 전투가 자주 끊기므로,
   레벨업 횟수의 상한은 재미가 아니라 **끊김 예산**이 정한다.
2. **경험치 수입이 지수로 는다.** 몹 체력이 웨이브당 ×1.18이고 경험치는 체력에
   비례하므로, 필요 경험치도 지수여야 레벨업 간격이 일정하게 유지된다.
   선형 곡선을 쓰면 후반에 레벨업이 폭주한다.
3. **레벨업 횟수가 카드 등급 확률을 흔든다.** 전설에는 천장이 없으므로(§4)
   카드 장수가 늘면 전설이 흔해져 잭팟이 죽는다 → verify_card_rates.py와 연동.
"""
import sys
sys.path.insert(0, "tools")

from balance_baseline import (
    hero_dps, TRASH, hp_scale, effective_hp, ELITES, SKILLS, PROC_RATE,
    level_need, power_mult, EXP_PER_EHP, LEVEL_NEED_BASE, LEVEL_NEED_RATIO,
    POWER_PER_LEVELUP, CARD_PICK_SEC, MODAL_BUDGET, HP_SCALE_PER_WAVE,
    TOTAL_WAVES, TOTAL_LEVELUPS, ARMOR_K, effective_targets,
)
from verify_segments import (
    SEGMENTS as WAVES, SLICE_BOSS_HP, SLICE_BOSS_ARMOR, SEGMENT_TARGET_SEC,
)

CARDS_PER_LEVEL = 3

# 목표
RUN_LEVELUPS = (45, 65)        # 풀 게임 한 판 총 레벨업 횟수
LEVEL_INTERVAL = (12, 25)      # 평균 레벨업 간격(초)
MAP_LEVEL_DECAY = 0.45         # 맵3 레벨업 / 맵1 레벨업의 하한
                               # (맵이 갈수록 줄어드는 건 정상이나 절반 아래로
                               #  떨어지면 후반이 성장 없이 길기만 해진다)

# 풀 게임 투영: 맵 2·3의 웨이브 구성은 아직 미정이므로 맵 1 구성을 재사용하고
# 웨이브 인덱스만 이어붙인다. 몹 수가 아니라 **곡선의 모양**을 보기 위한 투영이다.
MAP_COUNT = 3
BOSS_TARGET_SEC = 75.0   # 맵 보스 목표 처치 시간 — HP는 여기서 역산된다


def monster_exp(base_ehp, wave):
    """몹 1마리가 주는 경험치. 런타임에서는 int32_t 테이블 × 정수 배율이다."""
    return int(base_ehp * hp_scale(wave) * EXP_PER_EHP)


def run_sim(maps):
    """maps개 맵을 이어서 굴리고 (구간별 기록, 맵별 레벨업, 총시간) 반환.

    **구간 통과 시간이 설계값이고 몹 수는 거기서 역산된다.** 맵 1의 구성을 그렇게
    풀었으므로(verify_segments.SEGMENTS), 맵 2·3도 같은 방식으로 푼다.
    맵 1의 몹 수를 그대로 재사용하면 체력 스케일링이 계속 붙어 판 길이가 과대평가된다.
    """
    dps, _b, _a = hero_dps()

    lv, carry, total_sec = 0, 0, 0.0
    per_map, rows = [], []
    for m in range(maps):
        lv0 = lv
        for j, (_melee, _ranged, elites, _note) in enumerate(WAVES):
            w = m * len(WAVES) + j + 1
            et = effective_targets(j + 1)          # 동시 생존 상한은 맵마다 같은 곡선
            sec = SEGMENT_TARGET_SEC[j]
            growth = power_mult(lv)
            # 목표 시간을 채우는 잡몹 수를 역산한다
            n = round(sec * dps * growth * et / (TRASH["hp"] * hp_scale(w)))
            total_sec += sec

            carry += n * monster_exp(TRASH["hp"], w)
            carry += sum(monster_exp(effective_hp(ELITES[e]["hp"], ELITES[e]["armor"]), w)
                         for e in elites)
            gained = 0
            while carry >= level_need(lv + 1):
                carry -= level_need(lv + 1)
                lv += 1
                gained += 1
            rows.append((w, n, growth, sec, gained, lv))

        # **보스 HP는 잡몹 스케일링이 아니라 목표 처치 시간에서 역산한다.**
        # 잡몹 배율에 묶으면 영웅 성장(25배)을 못 따라가 맵 2·3 보스가 무너진다.
        bsec = BOSS_TARGET_SEC
        behp = bsec * dps * power_mult(lv)
        bhp = behp * ARMOR_K / (ARMOR_K + SLICE_BOSS_ARMOR)
        total_sec += bsec
        carry += int(behp * EXP_PER_EHP)
        while carry >= level_need(lv + 1):
            carry -= level_need(lv + 1)
            lv += 1
        rows.append((f"B{m+1}", round(bhp), power_mult(lv), bsec, 0, lv))
        per_map.append(lv - lv0)
    return rows, per_map, lv, total_sec


def report():
    ok = True

    print("=== 곡선 정의 ===")
    print(f"  필요 경험치  need(n) = {LEVEL_NEED_BASE} × {LEVEL_NEED_RATIO}^(n-1)")
    print(f"  몹 경험치    = 실효 체력 × {EXP_PER_EHP:g} (웨이브 배율 포함, 정수 절삭)")
    print(f"  레벨업 1회당 위력 +{POWER_PER_LEVELUP:.0%}")
    print()
    print(f"  {'Lv':>3} {'필요':>8} {'누적':>9}   기준 웨이브 잡몹 환산")
    cum = 0
    for n in (1, 2, 3, 5, 10, 20, 30, 40, 56):
        cum = sum(level_need(k) for k in range(1, n + 1))
        print(f"  {n:>3} {level_need(n):>8,} {cum:>9,}   {level_need(n)/TRASH['hp']:>6.0f}마리(W1 기준)")
    print()
    print("  ※ 경험치는 `Fixed`가 아니라 **int32_t 카운터**다. 시뮬 물리량이 아니라")
    print("     세는 값이므로 고정소수점을 쓸 이유가 없고, 상한 문제도 사라진다")
    print(f"     최대 누적 {cum:,} — int32_t 여유 {2**31 / cum:.0f}배\n")

    print("=== 수직 슬라이스 (맵 1개) ===")
    rows, per_map, lv1, sec1 = run_sim(1)
    print(f"  {'W':>3} {'몹':>4} {'성장':>7} {'클리어':>8} {'Lv+':>4} {'누적':>5}")
    for w, n, g, sec, gained, lv in rows:
        print(f"  {str(w):>3} {str(n):>4} {g:>6.2f}배 {sec:>7.1f}초 {gained:>4} {lv:>5}")
    print(f"  → 레벨업 {lv1}회, 카드 {lv1 * CARDS_PER_LEVEL}장, "
          f"전투 {sec1/60:.1f}분\n")

    print(f"=== 풀 게임 투영 (맵 {MAP_COUNT}개 / {TOTAL_WAVES}구간) ===")
    print("  ※ 맵 2·3의 구간 구성은 미정이다. **구간 목표 시간에서 몹 수를 역산**해")
    print("     맵 1과 같은 방식으로 푼다 — 맵 1 몹 수를 재사용하면 체력 스케일링이")
    print("     계속 붙어 판 길이가 과대평가된다\n")
    rows, per_map, lv_all, sec_all = run_sim(MAP_COUNT)
    for m, c in enumerate(per_map, 1):
        print(f"  맵 {m}: 레벨업 {c}회")
    modal_sec = lv_all * CARD_PICK_SEC
    run_sec = sec_all + modal_sec
    print(f"  총 레벨업 {lv_all}회, 카드 {lv_all * CARDS_PER_LEVEL}장")
    print(f"  총 길이 {run_sec/60:.1f}분 (전투 {sec_all/60:.1f}분 + 모달 {modal_sec/60:.1f}분)\n")

    print(f"=== 목표 1: 한 판 레벨업 {RUN_LEVELUPS[0]}~{RUN_LEVELUPS[1]}회 ===")
    lo, hi = RUN_LEVELUPS
    good = lo <= lv_all <= hi
    ok &= good
    print(f"  {lv_all}회  {'PASS' if good else 'FAIL'}\n")

    print(f"=== 목표 2: 평균 레벨업 간격 {LEVEL_INTERVAL[0]}~{LEVEL_INTERVAL[1]}초 ===")
    interval = sec_all / lv_all
    lo, hi = LEVEL_INTERVAL
    good = lo <= interval <= hi
    ok &= good
    print(f"  {interval:.1f}초  {'PASS' if good else 'FAIL'}")
    print("  ※ 너무 짧으면 카드 선택이 전투를 덮고, 너무 길면 성장이 체감되지 않는다\n")

    print(f"=== 목표 3: 선택 모달이 런의 {MODAL_BUDGET:.0%} 이내 ===")
    share = modal_sec / run_sec
    good = share <= MODAL_BUDGET
    ok &= good
    print(f"  카드 1회 {CARD_PICK_SEC:g}초 × {lv_all}회 = {modal_sec:.0f}초 "
          f"→ {share:.1%}  {'PASS' if good else 'FAIL'}")
    print(f"  ※ 이게 레벨업 횟수의 실질 상한이다. {CARD_PICK_SEC:g}초 안에 4장을 읽고")
    print("     고를 수 있어야 한다는 **UI 요구사항**이 여기서 나온다\n")

    print("=== 목표 4: 맵이 갈수록 레벨업이 줄되 반 토막 나지 않는다 ===")
    decay = per_map[-1] / per_map[0]
    good = MAP_LEVEL_DECAY <= decay <= 1.0
    ok &= good
    print(f"  맵3/맵1 = {per_map[-1]}/{per_map[0]} = {decay:.2f}  "
          f"{'PASS' if good else 'FAIL'}")
    print("  ※ 초반이 빠른 건 의도다(빌드가 빨리 잡힌다). 후반이 마르면 곤란하다\n")

    print("=== 목표 5: 필요 경험치 증가율이 수입 증가율과 맞물린다 ===")
    # 수입은 몹 체력 배율이 아니라 **구간이 품는 총 체력**에서 나온다.
    # 연속 스폰에서는 몹 수가 처리량을 따라 늘어나므로, 수입은 성장 배율을 탄다.
    seg_rows = [r for r in rows if isinstance(r[0], int)]
    inc0 = seg_rows[0][1] * TRASH["hp"] * hp_scale(seg_rows[0][0])
    inc1 = seg_rows[-1][1] * TRASH["hp"] * hp_scale(seg_rows[-1][0])
    n_steps = seg_rows[-1][0] - seg_rows[0][0]
    income_per_seg = (inc1 / inc0) ** (1 / n_steps)
    lv_per_seg = lv_all / TOTAL_WAVES
    need_per_seg = LEVEL_NEED_RATIO ** lv_per_seg
    good = abs(need_per_seg - income_per_seg) / income_per_seg < 0.08
    ok &= good
    print(f"  수입 ×{income_per_seg:.3f}/구간 (구간이 품는 총 체력 기준) vs "
          f"필요치 ×{need_per_seg:.3f}/구간")
    print(f"  (레벨업 {lv_per_seg:.2f}회/구간)  {'PASS' if good else 'FAIL'}")
    print("  ※ 두 지수가 어긋나면 레벨업이 후반에 폭주하거나 말라붙는다\n")

    print("=== 목표 6: 성장 배율이 balance_baseline의 보스 가정과 일치한다 ===")
    good = abs(lv_all - TOTAL_LEVELUPS) <= 3
    ok &= good
    print(f"  투영 {lv_all}회 vs balance_baseline TOTAL_LEVELUPS {TOTAL_LEVELUPS}회  "
          f"{'PASS' if good else 'FAIL'}")
    print(f"  최종 위력 {power_mult(lv_all):.1f}배 vs 몹 체력 {hp_scale(TOTAL_WAVES):.1f}배\n")

    print("=== 연동: 카드 등급 확률 ===")
    print(f"  카드 {lv_all * CARDS_PER_LEVEL}장 기준으로 전설 확률을 다시 잡아야 한다")
    print("  → tools/verify_card_rates.py\n")

    print("전체:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    report()
