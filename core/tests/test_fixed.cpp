// Fixed 20.12 — CLAUDE.md가 "단위 테스트로 동작을 고정하라"고 한 지점들이
// 이 파일의 존재 이유다. 반올림 방향과 오버플로 처리를 여기서 못 박는다.
#include "dc/fixed.h"
#include "test_main.h"

using namespace dc;

int main() {
    printf("=== Fixed 20.12 ===\n");

    dctest::section("형식");
    CHECK_EQ(Fixed::ONE_RAW, 4096);
    CHECK_EQ(Fixed::MAX_INT, 524288);
    CHECK_EQ(Fixed(1).raw, 4096);
    CHECK_EQ(Fixed(-3).raw, -12288);
    CHECK_EQ(toInt(Fixed(7)), 7);

    dctest::section("permille — data/*.json이 담는 형식");
    CHECK_EQ(Fixed::fromPermille(1000).raw, 4096);   // 1.0
    CHECK_EQ(Fixed::fromPermille(500).raw, 2048);    // 0.5
    CHECK_EQ(Fixed::fromPermille(100).raw, 409);     // 10% — 0.098% 오차
    CHECK_EQ(Fixed::fromPermille(1090).raw, 4464);   // 구간 HP 배율 1.09

    dctest::section("반올림 — 0방향 절삭으로 통일");
    // 여기가 핵심이다. 시프트(바닥)와 나눗셈(절삭)은 음수에서 결과가 다르다.
    // shiftDownTrunc는 양쪽 다 0방향으로 맞춘다.
    CHECK_EQ(shiftDownTrunc(7, 1), 3);
    CHECK_EQ(shiftDownTrunc(-7, 1), -3);      // 산술 시프트라면 -4였을 값
    CHECK_EQ(shiftDownTrunc(-1, 12), 0);
    CHECK_EQ(shiftDownTrunc(-4095, 12), 0);
    CHECK_EQ(shiftDownTrunc(-4096, 12), -1);
    // 대칭성: f(-x) == -f(x). 부호에 따라 치우치지 않는다
    for (int64_t v = -20000; v <= 20000; v += 7)
        CHECK_EQ(shiftDownTrunc(-v, 12), -shiftDownTrunc(v, 12));

    dctest::section("사칙연산");
    CHECK(Fixed(3) + Fixed(4) == Fixed(7));
    CHECK(Fixed(3) - Fixed(10) == Fixed(-7));
    CHECK(Fixed(6) * Fixed(7) == Fixed(42));
    CHECK(Fixed(42) / Fixed(7) == Fixed(6));
    CHECK(-Fixed(5) == Fixed(-5));
    CHECK(Fixed(3) * 4 == Fixed(12));
    // 0.5 × 0.5 = 0.25
    CHECK_EQ((Fixed::fromPermille(500) * Fixed::fromPermille(500)).raw, 1024);

    dctest::section("곱셈 오버플로 — int64 승격이 없으면 UB");
    // 1000 × 500 = 500,000. int32끼리 곱하면 raw가 4096000×2048000으로 넘친다.
    CHECK(Fixed(1000) * Fixed(500) == Fixed(500000));
    // 범위 상한 근처
    CHECK(Fixed(524287) + Fixed::fromRaw(1) > Fixed(524286));

    dctest::section("비교 · 보조");
    CHECK(Fixed(3) < Fixed(4));
    CHECK(Fixed(-4) < Fixed(-3));
    CHECK(fixedAbs(Fixed(-9)) == Fixed(9));
    CHECK(fixedMin(Fixed(2), Fixed(5)) == Fixed(2));
    CHECK(fixedMax(Fixed(2), Fixed(5)) == Fixed(5));

    dctest::section("거리 — 제곱근을 쓰지 않는다");
    // (0,0)에서 (3,4)까지: 3²+4² = 25. raw 스케일이므로 25 × 4096²
    CHECK_EQ(distanceSq(Fixed(0), Fixed(0), Fixed(3), Fixed(4)),
             static_cast<int64_t>(25) * 4096 * 4096);
    // 비교만 하면 제곱근이 필요 없다
    CHECK(distanceSq(Fixed(0), Fixed(0), Fixed(3), Fixed(0))
        < distanceSq(Fixed(0), Fixed(0), Fixed(0), Fixed(4)));

    dctest::section("컴파일 타임 상수로 쓸 수 있다");
    constexpr Fixed a = Fixed(3) * Fixed(4);
    static_assert(a.raw == 12 * 4096, "constexpr 곱셈");
    CHECK(a == Fixed(12));

    return dctest::summary("test_fixed");
}
