// 최소 테스트 하네스. 외부 의존성을 두지 않는다 — 코어는 Unity·서버 양쪽
// 툴체인에서 빌드되므로 의존성 하나가 곧 이식 비용이다.
#ifndef DC_TEST_MAIN_H
#define DC_TEST_MAIN_H

#include <cstdio>
#include <cstdint>

namespace dctest {
inline int g_pass = 0;
inline int g_fail = 0;
inline const char* g_section = "";

inline void section(const char* name) { g_section = name; printf("  [%s]\n", name); }

inline void check(bool ok, const char* expr, const char* file, int line) {
    if (ok) { ++g_pass; return; }
    ++g_fail;
    printf("    FAIL %s:%d  %s\n", file, line, expr);
}

template <typename T>
inline void checkEq(T a, T b, const char* expr, const char* file, int line) {
    if (a == b) { ++g_pass; return; }
    ++g_fail;
    printf("    FAIL %s:%d  %s   (좌 %lld, 우 %lld)\n", file, line, expr,
           static_cast<long long>(a), static_cast<long long>(b));
}

// uint64 전용. long long으로 캐스팅하면 2^63 이상이 구현 정의 변환을 타므로
// 해시·난수 고정값 비교는 이쪽을 쓴다.
inline void checkEqU(uint64_t a, uint64_t b, const char* expr, const char* file, int line) {
    if (a == b) { ++g_pass; return; }
    ++g_fail;
    printf("    FAIL %s:%d  %s   (좌 %llu, 우 %llu)\n", file, line, expr,
           static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
}

inline int summary(const char* name) {
    printf("%s: %d 통과, %d 실패\n", name, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
}  // namespace dctest

#define CHECK(x)        dctest::check((x), #x, __FILE__, __LINE__)
#define CHECK_EQ(a, b)  dctest::checkEq<long long>((a), (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_EQU(a, b) dctest::checkEqU((a), (b), #a " == " #b, __FILE__, __LINE__)

#endif
