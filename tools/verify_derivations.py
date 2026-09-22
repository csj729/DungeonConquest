"""파생 값이 그 도출식과 맞는가.

## 왜 이 도구가 생겼나

`verify_core_constants.py`는 **`data/*.json`과 `core/tools/dev_data.h`가 같은
값을 들고 있는가**를 본다. 그건 "한쪽만 고치는 사고"를 막지만, **그 JSON 값
자체가 올바르게 도출됐는가는 아무도 보지 않는다.** C++와 파이썬이 사이좋게
같은 틀린 값을 쓰고 있으면 74/74 일치로 통과한다.

실제로 한 세션에서 같은 종류의 버그를 다섯 번 만났다:

1. **엘리트 등장 임계** — 구간 경계가 균등에서 비균등으로 바뀔 때 손으로 적은
   표가 따라오지 않아 19마리 전부 구간 8 이전에 몰렸다. 구간 8이 혼자 설계의
   1.9배였다
2. **`verify_segments`의 구간 통과 시간** — 실측 보정(`COMBAT_EFFICIENCY`)이
   빠져 `balance_baseline`과 같은 구간 1을 21.7초 대 16.7초로 다르게 봤다
3. **`verify_exp_curve`의 몹 수 역산** — 같은 보정이 빠져 구간 1이 21마리 대신
   31마리를 요구했다. 있지도 않은 몹에서 경험치를 받고 있었다
4. **`slice_boss_hp`** — 같은 보정이 빠져 보스전이 설계의 1.8배(111초)였다
5. **`monsters.json:boss.hp`** — 도출 없이 33000을 ×1.538 했다. 이 도구가 처음
   돌 때 50769 대 도출값 27061로 잡혔다 (1.9배)

다섯 중 넷이 한 문장으로 요약된다 — **파생 값이 그 파생의 전제를 빼먹고
계산되어 있었다.** 특히 `COMBAT_EFFICIENCY`처럼 **여러 도출식이 공유하는
보정**은 한 번 바뀌면 그걸 쓰는 값이 전부 따라와야 하는데, 사람이 기억해서
따라가는 구조였다.

## 이 도구가 하는 일

각 파생 값을 **도출식으로 다시 계산해 저장값과 대조한다.** 그래서
`COMBAT_EFFICIENCY`를 0.77에서 0.56으로 바꾸면 그걸 쓰는 값이 전부 여기서
빨갛게 뜬다 — 기억에 의존하지 않는다.

**도출식의 입력도 전부 데이터여야 한다.** 손으로 적은 목표치가 도출식 안에
있으면 그 값이 다시 검사 밖으로 나간다. 그래서 `slice_boss_target_sec` 같은
값을 `data/*.json`으로 끌어올렸다.

## 여기에 없는 것

이미 고유 도구가 도출까지 검사하는 값은 중복하지 않는다:

- `proc_prd_c_q16` → `verify_prd.py` (PRD 상수 C를 목표 발동률에서 푼다)
- `tier_power_permille` → `verify_item_values.py` (조합 사다리)
- 카드 등급 예산 → `verify_card_values.py` (레벨업 위력에서 역산)
- `trash.hp` → `balance_baseline.py` 목표 1 (공격력 × 타수를 등식으로 본다)

엘리트 체력은 목표 7이 **밴드만** 보므로 여기서 역산까지 한다.
"""
import math
import sys

sys.path.insert(0, "tools")

from balance_baseline import (
    ARMOR_K, COMBAT_EFFICIENCY, ELITES, concurrent_cap, elite_damage_share,
    hero_dps, spawn_batch,
)
from verify_spawn import SCREEN_TILES, SPAWN_MARGIN
from verify_segments import simulate

import gamedata as gd

_SP, _SG, _PR, _MN = gd.SPAWN, gd.SEGMENTS_DATA, gd.PROGRESSION, gd.MONSTERS


# ── 도출식 ──────────────────────────────────────────────────────
# 각 함수는 **저장값을 읽지 않고** 입력에서 값을 다시 만든다.
# 저장값을 참조하면 검사가 자기 자신을 통과시킨다.

def derive_segment_starts():
    """구간이 시작되는 누적 클리어 포인트 ← 구간 구성."""
    cum, out = 0, []
    for seg in _SG["segments"]:
        out.append(cum)
        cum += (seg["melee"] * _SG["trash_points"]
                + len(seg["elites"]) * _SG["elite_points"])
    return out, cum


def derive_elite_spawns():
    """엘리트 등장 임계 ← 구간 구성 + 구간 경계.

    구간 i가 [S, E)를 차지하고 엘리트가 n마리면 k번째를 S + (E-S)·(k+1)/(n+1)에
    놓는다 — 구간 안에 고르게 퍼뜨린다. 마지막이 5/6 지점이라 구간의 6분의 1이
    남고, 그 몫으로 잡몹을 마저 정리해 구간을 넘긴다.
    """
    names = [e["name"] for e in _MN["elites"]]
    starts, target = derive_segment_starts()
    rows = []
    for i, seg in enumerate(_SG["segments"]):
        lo = starts[i]
        hi = starts[i + 1] if i + 1 < len(starts) else target
        n = len(seg["elites"])
        for k, nm in enumerate(seg["elites"]):
            rows.append([lo + round((hi - lo) * (k + 1) / (n + 1)), names.index(nm)])
    return rows


def derive_level_need():
    """레벨 n의 필요 경험치 ← base × ratio^(n-1)."""
    ratio = _PR["level_need_ratio_permille"] / 1000
    return [round(_PR["level_need_base"] * ratio ** (n - 1)) for n in range(1, 81)]


def derive_spawn_radius_milli():
    """스폰 반경 ← 화면 대각의 절반 × 여유.

    **화면 밖에서 나타나야 한다.** 여유가 1.0이면 화면 경계에서 튀어나온다.
    """
    return math.hypot(*[x / 2 for x in SCREEN_TILES]) * SPAWN_MARGIN * 1000


def derive_slice_boss_hp():
    """슬라이스 보스 체력 ← 목표 처치 시간에서 역산.

    **실측 보정을 건다.** 보스전에도 전장은 동시 생존 상한까지 차 있어 광역
    프록과 이동이 출력을 희석한다 — 이 보정을 빼고 역산했을 때 실측이 111초로
    모델(61초)의 1.8배였다. 성장 배율은 구간 레벨업 누적에서 나오므로
    보스 체력에 의존하지 않는다 (순환 없음).
    """
    _rows, _bs, growth, _lv = simulate()
    dps, _b, _a = hero_dps()
    ehp = _MN["slice_boss_target_sec"] * dps * growth * COMBAT_EFFICIENCY
    return ehp * ARMOR_K / (ARMOR_K + _MN["slice_boss_armor"])


def derive_elite_hp():
    """엘리트 체력 ← target_sec 밴드의 중앙에서 역산.

    hp = 중앙초 × DPS × 엘리트 피해 비중 × ARMOR_K/(ARMOR_K + armor).

    **밴드만 보는 검사로는 부족하다.** `balance_baseline` 목표 7은 처치 시간이
    밴드 안인지만 보는데 밴드가 7~12초처럼 넓어서 그 안에서 값이 흘러도 안
    잡힌다 — 공속을 20 → 13틱으로 올릴 때 체력을 같은 배율로 곱해 두었더니
    중앙에서 ±5% 어긋난 채 통과하고 있었다.

    **중앙을 점 목표로 쓰는 것은 새 규약이 아니다** — `elite_pressure()`가
    이미 `kill_sec = 밴드 평균`으로 쓰고 있다. 두 곳이 같은 점을 봐야
    압박 예산과 체력이 따로 놀지 않는다.

    보정(COMBAT_EFFICIENCY)을 걸지 않는 이유: 목표 7이 쓰는 `elite_damage_share`가
    이미 "타겟 우선순위 때문에 엘리트가 받는 피해 비중"이라 같은 희석을 담고 있다.
    둘을 겹쳐 걸면 두 번 깎인다.
    """
    dps, _b, _a = hero_dps()
    share = elite_damage_share()
    out = []
    for e in _MN["elites"]:
        mid = (e["target_sec_min"] + e["target_sec_max"]) / 2
        out.append(round(mid * dps * share * ARMOR_K / (ARMOR_K + e["armor"])))
    return out


def derive_full_boss_hp():
    """풀 게임 최종 보스 체력 ← 맵 3 보스의 투영.

    맵 2·3의 구성은 미정이므로 `verify_exp_curve`가 구간 목표 시간에서 몹 수를
    역산해 이어 붙인 투영을 쓴다. **투영이지만 도출은 있다** — 저장값 50769는
    도출 없이 33000을 공속 배율만큼 곱한 값이었고, 그 33000도 근거가 없었다.

    엘리트 체력이 바뀌면 여기도 움직인다 — 엘리트 실효 체력이 곧 경험치이고,
    경험치가 레벨업을, 레벨업이 성장 배율을, 성장 배율이 보스 체력을 정한다.
    체력을 밴드 중앙으로 다시 맞췄을 때 이 값이 29002 → 27061로 따라왔다.
    """
    import verify_exp_curve as ec
    rows, _per_map, _lv, _total = ec.run_sim(_PR["maps"])
    bosses = [r for r in rows if isinstance(r[0], str)]
    return bosses[-1][1]


# ── 검사 ────────────────────────────────────────────────────────
# (이름, 저장값, 도출값, 허용 오차, 근거)
def checks():
    starts, target = derive_segment_starts()
    return [
        ("segment_start_points", _SG["segment_start_points"], starts, 0,
         "구간 구성 누적 (잡몹 1점 · 엘리트 10점)"),
        ("clear_target_points", _SG["clear_target_points"], target, 0,
         "구간 구성 총합"),
        ("elite_spawn_points", [list(r) for r in _SG["elite_spawn_points"]],
         derive_elite_spawns(), 0, "구간 안에 고르게 — S + L·(k+1)/(n+1)"),
        ("level_need_table", _PR["level_need_table"], derive_level_need(), 0,
         f"{_PR['level_need_base']} × {_PR['level_need_ratio_permille'] / 1000}^(n-1)"),
        ("concurrent_cap_by_segment", _SP["concurrent_cap_by_segment"],
         [concurrent_cap(i) for i in range(1, _PR["total_segments"] + 1)], 0,
         f"{_SP['concurrent_cap_start']} → {_SP['concurrent_cap_top']} 지수 램프"),
        ("batch_by_segment", _SP["batch_by_segment"],
         [spawn_batch(i) for i in range(1, _PR["segments_per_map"] + 1)], 0,
         f"{_SP['batch_start']} → {_SP['batch_end']} 선형 램프"),
        # 반경만 반올림을 허용한다 — 19.8타일은 도출값 19.83을 읽기 좋게 끊은 것이고,
        # 0.03타일은 몹 이격(1.5타일)의 2%라 전장 구조를 바꾸지 않는다.
        ("radius_millitile", _SP["radius_millitile"], derive_spawn_radius_milli(), 50,
         f"화면 {SCREEN_TILES[0]}×{SCREEN_TILES[1]} 대각 절반 × 여유 {SPAWN_MARGIN}"),
        ("slice_boss_hp", _MN["slice_boss_hp"], derive_slice_boss_hp(), 1,
         f"목표 {_MN['slice_boss_target_sec']}초 × DPS × 성장 × 보정 {COMBAT_EFFICIENCY}"),
        ("elites[].hp", [e["hp"] for e in _MN["elites"]], derive_elite_hp(), 1,
         "target_sec 밴드 중앙 × DPS × 엘리트 피해 비중 / Armor"),
        ("boss.hp (풀 게임)", _MN["boss"]["hp"], derive_full_boss_hp(), 1,
         f"맵 {_PR['maps']} 보스 투영 (verify_exp_curve)"),
    ]


def _fmt(v):
    if isinstance(v, list):
        body = ", ".join(_fmt(x) for x in v[:6])
        return f"[{body}{', …' if len(v) > 6 else ''}]"
    if isinstance(v, float):
        return f"{v:.1f}"
    return str(v)


def _diff(stored, derived, tol):
    """저장값과 도출값이 허용 오차 안인가. 리스트는 원소별로 본다."""
    if isinstance(stored, list) != isinstance(derived, list):
        return False, "형태가 다르다"
    if isinstance(stored, list):
        if len(stored) != len(derived):
            return False, f"길이 {len(stored)} != {len(derived)}"
        bad = [i for i, (a, b) in enumerate(zip(stored, derived))
               if not _diff(a, b, tol)[0]]
        return (not bad), ("" if not bad else
                           f"{len(bad)}칸 어긋남 (첫 자리 #{bad[0]}: "
                           f"{_fmt(stored[bad[0]])} != {_fmt(derived[bad[0]])})")
    return abs(stored - derived) <= tol, ""


def report():
    ok = True
    print("=== 파생 값이 도출식과 맞는가 ===")
    print("  **`verify_core_constants`는 C++과 JSON이 같은지만 본다.** 그 JSON 값이")
    print("  올바르게 도출됐는지는 이 도구가 본다 — 둘이 사이좋게 같이 틀리면")
    print("  74/74 일치로 통과하기 때문이다.")
    print()
    print(f"  {'값':<26} {'허용':>4}  도출식")
    print("  " + "-" * 92)
    for name, stored, derived, tol, why in checks():
        good, msg = _diff(stored, derived, tol)
        ok &= good
        print(f"  {'OK ' if good else 'X  '} {name:<24} {tol:>4}  {why}")
        if not good:
            print(f"        저장 {_fmt(stored)}")
            print(f"        도출 {_fmt(derived)}")
            if msg:
                print(f"        → {msg}")
    print()
    print("  ※ **도출식의 입력도 전부 데이터여야 한다.** 손으로 적은 목표치가 식 안에")
    print("     있으면 그 값이 다시 검사 밖으로 나간다 — slice_boss_target_sec를")
    print("     monsters.json으로 끌어올린 이유다")
    print(f"  ※ COMBAT_EFFICIENCY({COMBAT_EFFICIENCY})를 쓰는 도출이 둘 있다")
    print("     (slice_boss_hp · boss.hp). 이 보정을 바꾸면 여기서 같이 걸린다 —")
    print("     0.77 → 0.56으로 바꿀 때 보스 체력이 따라오지 않은 것이 이 도구가")
    print("     생긴 이유 중 하나다\n")
    print("전체:", "PASS" if ok else "FAIL")
    return ok


# ── 자기 검사 ───────────────────────────────────────────────────
# **검사가 조용히 무뎌지는 것이 이 도구의 고장 방식이다.** 도출식을 저장값에서
# 읽도록 바꾸거나 허용 오차를 키우면 전부 PASS인 채로 아무것도 안 잡는다.
# 그래서 값을 하나씩 흔들어 해당 검사가 실제로 물리는지 본다.
#
# `python3 tools/verify_derivations.py --selftest`
_POKES = [
    ("data/segments.json", '"melee": 21,', '"melee": 22,', "segment_start_points"),
    ("data/segments.json", '"clear_target_points": 484,',
     '"clear_target_points": 485,', "clear_target_points"),
    ("data/segments.json", "[37, 0],", "[38, 0],", "elite_spawn_points"),
    ("data/progression.json", '"level_need_base": 380,',
     '"level_need_base": 381,', "level_need_table"),
    ("data/spawn.json", '"concurrent_cap_top": 60,',
     '"concurrent_cap_top": 61,', "concurrent_cap_by_segment"),
    ("data/spawn.json", '"batch_end": 8,', '"batch_end": 9,', "batch_by_segment"),
    ("data/spawn.json", '"radius_millitile": 19800,',
     '"radius_millitile": 19000,', "radius_millitile"),
    ("data/monsters.json", '"slice_boss_target_sec": 70,',
     '"slice_boss_target_sec": 80,', "slice_boss_hp"),
    ("data/monsters.json", '"target_sec_max": 12,\n      "windup_ticks": 60,',
     '"target_sec_max": 13,\n      "windup_ticks": 60,', "elites[].hp"),
    ("data/monsters.json", '"hp": 27061,', '"hp": 27200,', "boss.hp"),
]


def selftest():
    import subprocess

    ok = True
    print("=== 자기 검사: 값을 흔들면 그 검사가 물리는가 ===")
    print("  검사가 조용히 무뎌지는 것이 이 도구의 고장 방식이다 —")
    print("  도출식이 저장값을 읽게 되면 전부 PASS인 채로 아무것도 안 잡는다.\n")
    for path, pat, rep, label in _POKES:
        before = open(path, encoding="utf-8").read()
        if before.count(pat) != 1:
            print(f"  X   {label:<26} 앵커를 못 찾았다: {pat!r}")
            ok = False
            continue
        try:
            open(path, "w", encoding="utf-8").write(before.replace(pat, rep))
            out = subprocess.run([sys.executable, __file__],
                                 capture_output=True, text=True).stdout
        finally:
            open(path, "w", encoding="utf-8").write(before)
        hit = any(line.strip().startswith("X ") and label in line
                  for line in out.splitlines())
        ok &= hit
        print(f"  {'OK ' if hit else 'X  '} {label:<26} {'잡힘' if hit else '못 잡음'}")
    print("\n자기 검사:", "PASS" if ok else "FAIL")
    return ok


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(0 if selftest() else 1)
    report()
