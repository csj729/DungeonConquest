#!/usr/bin/env bash
# C++ 코어 빌드·테스트 일괄 실행. CI 진입점.
#
# CLAUDE.md가 요구하는 세 가지를 한 번에 본다:
#   1) UBSan / ASan 빌드에서 테스트 통과
#   2) Debug / Release 결과 일치
#   3) 최적화 레벨과 무관한 시뮬 결과
set -u
cd "$(dirname "$0")/.."

TESTS="test_fixed test_rng test_entity_id test_entity_store test_world"
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

# Debug ↔ Release 출력 일치. 체크섬이 붙기 전까지는 테스트 출력이 그 대역이다.
echo "== Debug / Release 일치"
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
