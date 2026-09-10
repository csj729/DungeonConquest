"""items_vertical_slice.md의 조합 트리 규칙 검증.

design.md §5 규칙 1(재료당 최소 2회 등장)과 규칙 4(경로 간 재료 공유)를
수작업 계산이 아니라 스크립트로 확인한다. 트리 내용을 바꿀 때마다 재실행할 것.
"""
from collections import Counter

RECIPES_A = {  # 흔함 → 안흔함
    "A1": ["C1", "C2"], "A2": ["C1", "C3"], "A3": ["C2", "C7"], "A4": ["C3", "C4"],
    "A5": ["C4", "C5"], "A6": ["C5", "C8"], "A7": ["C6", "C7"], "A8": ["C6", "C8"],
}
RECIPES_B = {  # 안흔함(+흔함 C9) → 특별함
    "B1": ["A1", "A2", "C9"], "B2": ["A6", "A1"], "B3": ["A4", "A5"],
    "B4": ["A5", "A7"], "B5": ["A3", "A7", "C9"], "B6": ["A8", "A6"],
}
RECIPES_D = {  # 특별함 → 희귀함
    "D1": ["B1", "B2", "B6"], "D2": ["B3", "B4", "B6"],
    "D3": ["B4", "B5", "B2"], "D4": ["B5", "B3", "B1"],
}
RECIPES_E = {  # 희귀함 → 전설적인
    "E1": ["D1", "D2"], "E2": ["D3", "D4"],
}

COMMONS = [f"C{i}" for i in range(1, 10)]


def coverage(recipes, pool):
    c = Counter()
    for ingredients in recipes.values():
        for i in ingredients:
            if i in pool:
                c[i] += 1
    return c


def main():
    cov_common_in_a = coverage(RECIPES_A, COMMONS)
    cov_c9_in_b = coverage(RECIPES_B, ["C9"])
    cov_common_total = {**cov_common_in_a, **cov_c9_in_b}

    print("=== 규칙 1: 흔함 9종은 각각 최소 2개 조합식에 등장해야 한다 ===")
    for c in COMMONS:
        print(f"  {c}: {cov_common_total.get(c, 0)}회")
    assert all(cov_common_total.get(c, 0) >= 2 for c in COMMONS), "규칙 1 위반"
    print("  PASS\n")

    cov_special_in_rare = coverage(RECIPES_D, list(RECIPES_B.keys()))
    print("=== 규칙 4: 특별함 6종은 희귀함 단계에서 재사용돼야 한다 ===")
    for b in RECIPES_B:
        print(f"  {b}: {cov_special_in_rare.get(b, 0)}회")
    assert all(cov_special_in_rare.get(b, 0) >= 2 for b in RECIPES_B), "규칙 4 위반"
    print("  PASS\n")

    counts = [len(COMMONS), len(RECIPES_A), len(RECIPES_B), len(RECIPES_D), len(RECIPES_E)]
    print("=== 규칙 3: 등급이 올라갈수록 엄격히 감소해야 한다 ===")
    print(f"  {counts}")
    assert all(counts[i] > counts[i + 1] for i in range(len(counts) - 1)), "규칙 3 위반"
    print("  PASS\n")

    total_recipes = len(RECIPES_A) + len(RECIPES_B) + len(RECIPES_D) + len(RECIPES_E)
    print(f"총 조합식: {total_recipes}개")


if __name__ == "__main__":
    main()
