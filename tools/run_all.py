"""검증 도구 전체 실행 — CI 진입점.

수치를 바꾸면 `data/*.json` 한 곳만 고치고 이걸 돌린다.
하나라도 FAIL이면 종료 코드가 0이 아니다.
"""
import importlib
import io
import sys
import contextlib

sys.path.insert(0, "tools")

MODULES = [
    ("gamedata", "데이터 로드 · 정수 검사"),
    ("verify_core_constants", "C++ 구조 상수 대조"),
    ("balance_baseline", "영웅·몬스터 기준선 12목표"),
    ("verify_segments", "구간 구성 6목표"),
    ("verify_exp_curve", "경험치 곡선 6목표"),
    ("verify_spawn", "스폰 좌표 · 전장 구성"),
    ("verify_targeting", "타겟 우선순위 · QTE 예산"),
    ("verify_card_rates", "카드 등급 확률 4목표"),
    ("verify_prd", "PRD 상수 · 손익분기"),
    ("verify_card_values", "각인·유물 수치"),
    ("verify_duplicate_rules", "중복 규칙 4목표"),
    ("verify_crit_axis", "치명타 축 구조"),
    ("verify_item_tree", "조합 트리 규칙"),
    ("verify_item_values", "아이템 수치 사다리 4목표"),
    # **지금 FAIL한다 — 의도된 것이다.** 회복 예산이 유입을 22%밖에 덮지 못하고
    # 구간 체류가 설계의 2배다. 원인은 회복 수치가 아니라 처치율이라(도구 목표 4)
    # 밸런스 판단이 필요하다. 도구를 빼서 초록으로 만들지 말 것 — 회복이 한 줄도
    # 구현되지 않은 채 13개가 전부 PASS했던 것이 이 도구가 생긴 이유다.
    ("verify_recovery", "잠식 회복 예산 4목표"),
]


def main(verbose=False):
    failed = []
    for name, desc in MODULES:
        mod = importlib.import_module(name)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            ok = mod.report()
        out = buf.getvalue()
        ok = True if ok is None else ok
        mark = "PASS" if ok else "FAIL"
        print(f"  [{mark}] {name:<24} {desc}")
        if not ok:
            failed.append(name)
        if verbose or not ok:
            print("\n".join("        " + l for l in out.splitlines()))
    print()
    if failed:
        print(f"FAIL — {', '.join(failed)}")
        return 1
    print(f"전체 PASS ({len(MODULES)}개 도구)")
    return 0


if __name__ == "__main__":
    sys.exit(main("-v" in sys.argv))
