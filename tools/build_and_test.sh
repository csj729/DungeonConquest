#!/usr/bin/env bash
# C++ 코어 빌드·테스트 일괄 실행. CI 진입점.
#
# CLAUDE.md가 요구하는 세 가지를 한 번에 본다:
#   1) UBSan / ASan 빌드에서 테스트 통과
#   2) Debug / Release 결과 일치
#   3) 최적화 레벨과 무관한 시뮬 결과
set -u
cd "$(dirname "$0")/.."

TESTS="test_fixed test_rng test_entity_id test_entity_store test_world test_checksum test_stat_block test_condition test_prd test_inventory"
FAILED=0

build() {   # build <dir> <build-type> <sanitizer>
    cmake -S . -B "build/$1" -DCMAKE_BUILD_TYPE="$2" -DDC_SANITIZE="$3" >/dev/null || return 1
    cmake --build "build/$1" -j >/dev/null || return 1
}

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
