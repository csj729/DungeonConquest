#!/usr/bin/env bash
# docs/determinism.md의 재현 코드. 같은 소스·같은 기계·같은 컴파일러에서
# 플래그만 바꿔 결과가 갈리는지 확인한다.
set -u
cd "$(dirname "$0")"
CC=${CC:-gcc}
echo "컴파일러: $($CC --version | head -1)"

echo
echo "=== 1. FMA 합성 — double a*b+c 의 마지막 비트 ==="
for f in "-O0" "-O2 -ffp-contract=off" "-O2 -mfma -ffp-contract=fast" "-O3 -march=native" "-Os"; do
  $CC $f sim3.c -o /tmp/_d1 2>/dev/null && printf "  %-32s " "$f" && /tmp/_d1
done

echo
echo "=== 2. 같은 계산을 Fixed 20.12로 ==="
for f in "-O0" "-O2 -ffp-contract=off" "-O2 -mfma -ffp-contract=fast" "-O3 -march=native" "-Os"; do
  $CC $f cmp.c -o /tmp/_d2 2>/dev/null && printf "  %-32s " "$f" && /tmp/_d2
done

echo
echo "=== 3. 재결합 — 부동소수 덧셈에는 결합법칙이 없다 ==="
for f in "-O2" "-O2 -ffast-math"; do
  $CC $f reassoc.c -o /tmp/_d3 2>/dev/null && printf "  %-20s " "$f" && /tmp/_d3
done

echo
echo "=== 4. 정수도 UB를 밟으면 갈린다 (부호 있는 오버플로) ==="
for f in "-O0" "-O2" "-O2 -fwrapv"; do
  $CC $f ub2.c -o /tmp/_d4 2>/dev/null && printf "  %-20s " "$f" \
    && (timeout 3 /tmp/_d4 || echo "무한 루프 (UB를 참으로 접음)")
done
