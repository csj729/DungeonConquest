"""items_vertical_slice.md의 조합 트리 규칙 검증.

design.md §5 규칙 1(재료당 최소 2회 등장), 2(축당 최소 3경로),
4(경로 간 재료 공유)를 수작업 계산이 아니라 스크립트로 확인한다.
트리 내용을 바꿀 때마다 재실행할 것.
"""
import sys
sys.path.insert(0, "tools")

from collections import Counter

from gamedata import load as _load

_ITEMS = _load("items")
_BY_GRADE = {}
for _r in _ITEMS["recipes"]:
    _BY_GRADE.setdefault(_r["grade"], {})[_r["id"]] = (_r["ingredients"], _r["axis"])

COMMONS = [c["id"] for c in _ITEMS["commons"]]
UNCOMMON = _BY_GRADE["안흔함"]
SPECIAL = _BY_GRADE["특별함"]
RARE = _BY_GRADE["희귀함"]
LEGEND = _BY_GRADE["전설적인"]

# ── 화력 축 하위 테마 (치명타 경로 확인용) ────────────────────
# design.md §5의 축은 화력·CC·광역·생존 넷이고 **치명타 축은 없다.**
# 치명타는 화력 축 안의 하위 테마로 들어가야 한다 — cards_vertical_slice.md §1의
# `E_CRIT`이 빌드 종속 각인이므로, 치명타에 투자할 경로가 실제로 있어야 한다.
#
# 전설적인 화력 3종이 희귀함 화력 3종 {Cf1, Cf2, Rskip_f}의 **3가지 쌍**이라는
# 구조를 이용한다. 희귀함 셋에 하위 테마를 하나씩 배정하면 전설 3종이 자동으로
# "테마 2개씩 조합"이 되고, 각 테마가 3경로 중 2경로에 실린다.
FIRE_SUBTHEME = _ITEMS["fire_subthemes"]

# 치명타 계보 — 흔함부터 이어지는 사슬. 조합식으로 실제로 연결되는지 검증한다
CRIT_LINEAGE = _ITEMS["crit_lineage"]   # 짐승의 이빨 → 톱니 발톱 → 광전사의 쌍검 → 처형인의 낫

NAMES = {**{c["id"]: c["name"] for c in _ITEMS["commons"]},
         **{r["id"]: r["name"] for r in _ITEMS["recipes"]}}


def all_items():
    d = {}
    for t in (UNCOMMON, SPECIAL, RARE, LEGEND):
        d.update(t)
    return d


def check_crit_path():
    """치명타 경로가 트리에 실제로 성립하는지 확인한다."""
    ok = True
    items = all_items()

    print("=== 치명타 경로 확인 ===")
    axes = sorted({ax for _i, ax in items.values()})
    print(f"  축 체계: {' · '.join(axes)}")
    print("  → **치명타 축은 없다.** 화력 축 안의 하위 테마로 들어가야 한다\n")

    fire = [k for k, (_i, ax) in items.items() if ax == "화력"]
    print(f"  화력 축 {len(fire) + 2}종 (흔함 C1·C2 포함): "
          f"{', '.join(NAMES.get(k, k) for k in ['C1', 'C2'] + fire)}\n")

    print("  [1] 치명타 계보가 조합식으로 이어지는가")
    chain_ok = True
    for a, b in zip(CRIT_LINEAGE, CRIT_LINEAGE[1:]):
        linked = a in items[b][0]
        chain_ok &= linked
        print(f"    {NAMES[a]} → {NAMES[b]}: "
              f"{'연결' if linked else '끊김'} (조합식 {'+'.join(items[b][0])})")
    ok &= chain_ok
    print(f"    {'PASS' if chain_ok else 'FAIL'}\n")

    print("  [2] 전설적인 화력 3종에 하위 테마가 어떻게 실리는가")
    counts = {t: 0 for t in set(FIRE_SUBTHEME.values())}
    for lid, (ing, ax) in LEGEND.items():
        if ax != "화력":
            continue
        themes = [FIRE_SUBTHEME[m] for m in ing if m in FIRE_SUBTHEME]
        for t in themes:
            counts[t] += 1
        print(f"    {NAMES[lid]:<12} = {' + '.join(themes)}")
    good = all(c == 2 for c in counts.values())
    ok &= good
    print(f"    경로 수: {counts}  {'PASS' if good else 'FAIL'}")
    print("    ※ 각 테마가 3경로 중 2경로 — **강하지만 필수는 아니다.**")
    print("       치명타를 안 타는 화력 빌드(타오르는 폭군)가 남아야 선택이 된다\n")

    print("  [3] 치명타 수치가 배정되어 있는가")
    print("    아이템 51종의 구체 효과는 전부 자리표시자다 (items_vertical_slice.md §8).")
    print("    **경로의 자리는 있으나 수치는 아직 없다** — 이 검증은 구조만 확인한다\n")
    return ok


def coverage(recipes, pool):
    c = Counter()
    for ingredients, _tag in recipes.values():
        for i in ingredients:
            if i in pool:
                c[i] += 1
    return c


def report():
    crit_ok = check_crit_path()
    print("=== 규칙 1: 흔함 9종은 각각 최소 2개 조합식에 등장해야 한다 ===")
    cov_uncommon = coverage(UNCOMMON, COMMONS)
    cov_c9_special = coverage(SPECIAL, ["C9"])
    cov_common_total = {**cov_uncommon, **cov_c9_special}
    for c in COMMONS:
        print(f"  {c}: {cov_common_total.get(c, 0)}회")
    assert all(cov_common_total.get(c, 0) >= 2 for c in COMMONS), "규칙 1 위반"
    print("  PASS\n")

    print("=== 규칙 2: 전설적인 단계에서 각 축은 최소 3경로여야 한다 ===")
    axis_count = Counter(tag for _ing, tag in LEGEND.values())
    for axis, n in sorted(axis_count.items()):
        print(f"  {axis}: {n}")
    assert all(n >= 3 for n in axis_count.values()), "규칙 2 위반"
    print("  PASS\n")

    print("=== 규칙 4 참고: 특별함 8종이 희귀함 단계에서 재사용되는 횟수 ===")
    cov_special_in_rare = coverage(RARE, list(SPECIAL.keys()))
    for b in SPECIAL:
        print(f"  {b}: {cov_special_in_rare.get(b, 0)}회")
    print()

    counts = {
        "흔함": len(COMMONS), "안흔함": len(UNCOMMON), "특별함": len(SPECIAL),
        "희귀함": len(RARE), "전설적인": len(LEGEND),
    }
    print("=== 등급별 개수 (증감 자유) ===")
    for k, v in counts.items():
        print(f"  {k}: {v}")

    total_items = sum(counts.values())
    total_recipes = len(UNCOMMON) + len(SPECIAL) + len(RARE) + len(LEGEND)
    print(f"\n총 아이템 {total_items}종, 총 조합식 {total_recipes}개")

    return crit_ok


if __name__ == "__main__":
    report()