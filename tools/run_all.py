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
    # **위 도구 바로 뒤에 둔다.** 저 도구는 C++와 JSON이 같은지만 보고,
    # 이 도구는 그 JSON 값이 올바르게 도출됐는지를 본다. 둘이 사이좋게 같이
    # 틀리면 앞 도구는 74/74 일치로 통과한다 — 실제로 다섯 번 그랬다.
    # 이 도구 자신이 무뎌지는 것도 검사한다 (값을 흔들어 검사가 물리는지):
    #   python3 tools/verify_derivations.py --selftest
    ("verify_derivations", "파생 값 도출식 대조"),
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
    # 회복이 한 줄도 구현되지 않은 채 13개가 전부 PASS했던 것이 이 도구가 생긴
    # 이유다. 한동안 의도적으로 FAIL이었고(기저 정화 21%), 지금은 통과한다 —
    # 빼서 초록으로 만들지 말 것.
    ("verify_recovery", "잠식 회복 예산 6목표"),
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
