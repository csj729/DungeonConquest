"""items_vertical_slice.md의 조합 트리 규칙 검증.

design.md §5 규칙 1(재료당 최소 2회 등장), 2(축당 최소 3경로),
4(경로 간 재료 공유)를 수작업 계산이 아니라 스크립트로 확인한다.
트리 내용을 바꿀 때마다 재실행할 것.
"""
from collections import Counter

# 흔함 9종
COMMONS = ["C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9"]

# 안흔함 12종 — 흔함 2개 조합, (재료, 축)
UNCOMMON = {
    "Af1": (["C1", "C2"], "화력"), "Af2": (["C1", "C3"], "화력"), "Af3": (["C2", "C5"], "화력"),
    "Ac1": (["C4", "C5"], "CC"),   "Ac2": (["C4", "C6"], "CC"),   "Ac3": (["C5", "C7"], "CC"),
    "Aa1": (["C3", "C4"], "광역"), "Aa2": (["C3", "C6"], "광역"), "Aa3": (["C6", "C1"], "광역"),
    "As1": (["C7", "C8"], "생존"), "As2": (["C8", "C2"], "생존"), "As3": (["C7", "C1"], "생존"),
}

# 특별함 8종 — 안흔함 2개(+일부 C9) 조합
SPECIAL = {
    "Bf1": (["Af1", "Af2", "C9"], "화력"), "Bf2": (["Af3", "Af1"], "화력"),
    "Bc1": (["Ac1", "Ac2", "C9"], "CC"),   "Bc2": (["Ac2", "Ac3"], "CC"),
    "Ba1": (["Aa1", "Aa2"], "광역"),       "Ba2": (["Aa2", "Aa3"], "광역"),
    "Bs1": (["As1", "As2"], "생존"),       "Bs2": (["As2", "As3"], "생존"),
}

# 희귀함 10종 — 특별함 2개 조합(8) + 안흔함×3 스킵(2, 특별함을 건너뜀)
RARE = {
    "Cf1": (["Bf1", "Bf2"], "화력"),           "Cf2": (["Bf2", "Ba2"], "화력"),
    "Cc1": (["Bc1", "Bc2"], "CC"),             "Cc2": (["Bc2", "Ba1"], "CC"),
    "Ca1": (["Ba1", "Ba2"], "광역"),           "Ca2": (["Ba1", "Bf2"], "광역"),
    "Cs1": (["Bs1", "Bs2"], "생존"),           "Cs2": (["Bs1", "Ba2"], "생존"),
    "Rskip_f": (["Af3", "Af3", "Af3"], "화력"),  # 3연성 스킵: 안흔함 → 희귀함
    "Rskip_s": (["As3", "As3", "As3"], "생존"),  # 3연성 스킵: 안흔함 → 희귀함
}

# 전설적인 12종 — 희귀함 2개 조합(8) + 특별함×3 스킵(2, 희귀함을 건너뜀)
LEGEND = {
    "Ef1": (["Cf1", "Cf2"], "화력"),
    "Ef2": (["Cf1", "Rskip_f"], "화력"),
    "Ef3": (["Cf2", "Rskip_f"], "화력"),
    "Ec1": (["Cc1", "Cc2"], "CC"),
    "Ec2": (["Cc2", "Ca1"], "CC"),
    "Ea1": (["Ca1", "Ca2"], "광역"),
    "Ea2": (["Ca2", "Cs1"], "광역"),
    "Es1": (["Cs1", "Cs2"], "생존"),
    "Es2": (["Cs1", "Rskip_s"], "생존"),
    "Es3": (["Cs2", "Rskip_s"], "생존"),
    "Eskip_c": (["Bc1", "Bc1", "Bc1"], "CC"),    # 3연성 스킵: 특별함 → 전설적인
    "Eskip_a": (["Ba2", "Ba2", "Ba2"], "광역"),  # 3연성 스킵: 특별함 → 전설적인
}


def coverage(recipes, pool):
    c = Counter()
    for ingredients, _tag in recipes.values():
        for i in ingredients:
            if i in pool:
                c[i] += 1
    return c


def main():
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


if __name__ == "__main__":
    main()
