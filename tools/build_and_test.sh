#!/usr/bin/env bash
# C++ 코어 빌드·테스트 일괄 실행. CI 진입점.
#
# CLAUDE.md가 요구하는 세 가지를 한 번에 본다:
#   1) UBSan / ASan 빌드에서 테스트 통과
#   2) Debug / Release 결과 일치
#   3) 최적화 레벨과 무관한 시뮬 결과
set -u
cd "$(dirname "$0")/.."

TESTS="test_fixed test_json test_config_loader test_rng test_entity_id test_entity_store test_world test_checksum test_stat_block test_condition test_prd test_inventory test_systems test_qte test_card test_legend test_load test_abi"
FAILED=0

build() {   # build <dir> <build-type> <sanitizer>
    cmake -S . -B "build/$1" -DCMAKE_BUILD_TYPE="$2" -DDC_SANITIZE="$3" >/dev/null || return 1
    cmake --build "build/$1" -j >/dev/null || return 1
}

# **clang 축이 있다.** `-Wunused-but-set-variable` 같은 경고를 gcc는 놓치고 clang은
# 잡는다. 전에 두 번 CI에서만 빨간불이 났다 — dc_boss의 arriveSecSum, dc_legend의
# sec. 둘 다 "열을 지우고 누적만 남긴" 같은 모양이라 로컬에서도 보게 한다.
#
# clang은 **빌드만** 한다. 테스트 실행은 gcc 네 설정이 이미 덮으므로 중복이고,
# 여기서 보려는 것은 컴파일러가 다르면 다른 것을 잡는다는 사실뿐이다.
if command -v clang++ >/dev/null 2>&1; then
    printf '== clang 빌드 (경고 축이 gcc와 다르다)\n'
    if cmake -S . -B build/clang -DCMAKE_BUILD_TYPE=Release -DDC_SANITIZE=none \
             -DCMAKE_CXX_COMPILER=clang++ >/dev/null \
       && cmake --build build/clang -j >/dev/null; then
        echo "   통과"
    else
        echo "   빌드 실패 — CI의 clang 잡이 여기서 먼저 걸렸다"; FAILED=1
    fi
else
    echo "== clang 없음 — 건너뜀 (CI가 본다)"
fi

for cfg in "release Release none" "debug Debug none" "ubsan Debug undefined" "asan Debug address"; do
    set -- $cfg
    printf '== %s (%s / sanitize=%s)\n' "$1" "$2" "$3"
    if ! build "$1" "$2" "$3"; then
        echo "   빌드 실패"; FAILED=1; continue
    fi
    for t in $TESTS; do
        if out=$("./build/$1/core/$t" 2>&1); then
            echo "   $(echo "$out" | tail -1)"
        else
            echo "   FAIL $t"; echo "$out" | sed 's/^/     /'; FAILED=1
        fi
    done
done

# CLAUDE.md: "Debug 빌드와 Release 빌드의 체크섬 일치를 CI에서 검증할 것."
# 헤드리스 러너를 두 빌드로 돌려 매 구간 체크섬을 통째로 비교한다.
echo "== Debug / Release 체크섬 일치"
for seed in 1 20250918 18446744073709551615; do
    d=$(./build/debug/core/dc_checksum   "$seed" 4000 500 | md5sum | cut -d' ' -f1)
    r=$(./build/release/core/dc_checksum "$seed" 4000 500 | md5sum | cut -d' ' -f1)
    u=$(./build/ubsan/core/dc_checksum   "$seed" 4000 500 | md5sum | cut -d' ' -f1)
    if [ "$d" = "$r" ] && [ "$d" = "$u" ]; then
        echo "   seed=$seed 일치 (4000틱)"
    else
        echo "   seed=$seed 불일치  Debug=$d  Release=$r  UBSan=$u"; FAILED=1
    fi
done

# 테스트 출력도 함께 본다 — 체크섬이 못 보는 경로(경계 조건·거부 코드)를 덮는다.
echo "== Debug / Release 테스트 출력 일치"
for t in $TESTS; do
    d=$(./build/debug/core/$t | md5sum | cut -d' ' -f1)
    r=$(./build/release/core/$t | md5sum | cut -d' ' -f1)
    if [ "$d" = "$r" ]; then
        echo "   $t 일치"
    else
        echo "   $t 불일치  Debug=$d  Release=$r"; FAILED=1
    fi
done

[ "$FAILED" -eq 0 ] && echo "전부 통과" || echo "실패 있음"
exit "$FAILED"
