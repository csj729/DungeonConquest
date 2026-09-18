// 고정소수점 20.12 — 시뮬 코어의 모든 수치 타입.
//
// **왜 float가 아닌가**: docs/determinism.md §4. 같은 소스가 Unity(Clang)와
// 서버(GCC/MSVC)에서 따로 컴파일되는데, FMA 합성·재결합·libm 때문에 float는
// 컴파일러마다 다른 비트를 낸다. 정수는 그 전부가 사라진다.
//
// **형식 20.12**: 정수부 20비트 + 소수부 12비트, int32_t 한 칸.
//   범위 ±524,288 / 정밀도 1/4096 (0.000244) / 1% 표현 오차 0.098%
//   보스 실효 HP 49,500이 최대 사용값이므로 10배 여유 (balance_baseline 목표 12)
//
// **이 헤더가 격리하는 것** — CLAUDE.md가 "헬퍼 하나로 격리하라"고 한 지점들:
//   1. 곱셈 오버플로 → int64_t 승격 후 시프트 (부호 있는 오버플로는 UB)
//   2. 음수 우측 시프트 → C++20 이전 구현 정의. shiftDown()으로 격리
//   3. 반올림 방향 → **0방향 절삭으로 통일**. 시프트(바닥)와 나눗셈(절삭)을
//      섞으면 음수에서 결과가 갈린다 (-7>>1 = -4, -7/2 = -3)
#ifndef DC_FIXED_H
#define DC_FIXED_H

#include <cstdint>

namespace dc {

struct Fixed {
    static constexpr int32_t SHIFT = 12;
    static constexpr int32_t ONE_RAW = 1 << SHIFT;          // 4096
    static constexpr int32_t MAX_INT = (1 << (31 - SHIFT));  // 524288

    int32_t raw = 0;

    constexpr Fixed() = default;
    explicit constexpr Fixed(int32_t whole) : raw(whole * ONE_RAW) {}

    // **부동소수점으로부터의 변환을 타입 수준에서 막는다.** 실수로 float가
    // 시뮬에 스며드는 경로를 컴파일 에러로 바꾼다.
    Fixed(float) = delete;
    Fixed(double) = delete;
    Fixed(long double) = delete;

    static constexpr Fixed fromRaw(int32_t r) { Fixed f; f.raw = r; return f; }

    // permille(1/1000) → Fixed. data/*.json이 담는 형식이 이것이다.
    static constexpr Fixed fromPermille(int32_t pm) {
        return fromRaw(static_cast<int32_t>(
            (static_cast<int64_t>(pm) * ONE_RAW) / 1000));
    }

    static constexpr Fixed zero() { return Fixed(); }
    static constexpr Fixed one()  { return fromRaw(ONE_RAW); }
};

// ── 반올림: 0방향 절삭 하나로 통일한다 ────────────────────────
//
// 음수 우측 시프트는 C++20 이전 구현 정의이고, 시프트(바닥)와 나눗셈(절삭)은
// 음수에서 결과가 다르다. 여기 한 곳만 통과시켜 그 차이를 없앤다.
constexpr int64_t shiftDownTrunc(int64_t v, int32_t bits) {
    return v < 0 ? -((-v) >> bits) : (v >> bits);
}

constexpr int32_t toInt(Fixed a) {
    return static_cast<int32_t>(shiftDownTrunc(a.raw, Fixed::SHIFT));
}

// ── 사칙연산 ──────────────────────────────────────────────────
constexpr Fixed operator+(Fixed a, Fixed b) { return Fixed::fromRaw(a.raw + b.raw); }
constexpr Fixed operator-(Fixed a, Fixed b) { return Fixed::fromRaw(a.raw - b.raw); }
constexpr Fixed operator-(Fixed a)          { return Fixed::fromRaw(-a.raw); }

// 곱셈은 **반드시 int64_t로 승격**한다. int32끼리 곱하면 오버플로 = UB이고,
// UB는 값이 틀리는 게 아니라 컴파일러가 코드를 다르게 생성한다.
constexpr Fixed operator*(Fixed a, Fixed b) {
    const int64_t p = static_cast<int64_t>(a.raw) * static_cast<int64_t>(b.raw);
    return Fixed::fromRaw(static_cast<int32_t>(shiftDownTrunc(p, Fixed::SHIFT)));
}

constexpr Fixed operator/(Fixed a, Fixed b) {
    const int64_t n = static_cast<int64_t>(a.raw) << Fixed::SHIFT;
    return Fixed::fromRaw(static_cast<int32_t>(n / b.raw));   // C++11부터 0방향 절삭
}

// 정수 배율은 Fixed로 감싸지 않고 바로 곱한다 (중간 반올림을 줄인다)
constexpr Fixed operator*(Fixed a, int32_t k) { return Fixed::fromRaw(a.raw * k); }
constexpr Fixed operator*(int32_t k, Fixed a) { return a * k; }
constexpr Fixed operator/(Fixed a, int32_t k) { return Fixed::fromRaw(a.raw / k); }

constexpr Fixed& operator+=(Fixed& a, Fixed b) { a.raw += b.raw; return a; }
constexpr Fixed& operator-=(Fixed& a, Fixed b) { a.raw -= b.raw; return a; }

constexpr bool operator==(Fixed a, Fixed b) { return a.raw == b.raw; }
constexpr bool operator!=(Fixed a, Fixed b) { return a.raw != b.raw; }
constexpr bool operator< (Fixed a, Fixed b) { return a.raw <  b.raw; }
constexpr bool operator<=(Fixed a, Fixed b) { return a.raw <= b.raw; }
constexpr bool operator> (Fixed a, Fixed b) { return a.raw >  b.raw; }
constexpr bool operator>=(Fixed a, Fixed b) { return a.raw >= b.raw; }

constexpr Fixed fixedAbs(Fixed a) { return a.raw < 0 ? -a : a; }
constexpr Fixed fixedMin(Fixed a, Fixed b) { return a.raw < b.raw ? a : b; }
constexpr Fixed fixedMax(Fixed a, Fixed b) { return a.raw > b.raw ? a : b; }

// 거리 비교는 **제곱끼리** 한다 (docs/determinism.md §7). 제곱근은 쓰지 않는다.
// 좌표가 커지면 제곱에서 넘칠 수 있으므로 int64_t로 돌려준다.
constexpr int64_t distanceSq(Fixed ax, Fixed ay, Fixed bx, Fixed by) {
    const int64_t dx = static_cast<int64_t>(ax.raw) - bx.raw;
    const int64_t dy = static_cast<int64_t>(ay.raw) - by.raw;
    return dx * dx + dy * dy;
}

}  // namespace dc

#endif  // DC_FIXED_H
