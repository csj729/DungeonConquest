// PRD 분산 측정 하네스 — `tools/prd_variance.py`의 C++ 이식 (§14-6).
//
// **파이썬 이식이 의미 있는 이유는 측정 대상이 다르기 때문이다.**
// 파이썬은 Mersenne Twister와 float로 *모델*을 측정했다. 여기서는 실제로 출시될
// 경로를 측정한다 — 코어의 xorshift64* `Rng`, q16 절삭된 상수 C, `PrdChannel`.
// 모델이 맞아도 구현이 틀릴 수 있고, 그 차이는 여기서만 드러난다.
//
// ## 통계 정의 — 길이 0인 공백을 포함한다
//
// "연속 실패 K회 이상"을 잴 때 **성공이 곧바로 나온 경우(공백 0)도 분모에 넣는다.**
// 파이썬 원본은 0을 빼고 셌는데, 그러면 P(공백>=K | 공백>=1)을 재는 것이 되어
// 모든 수치가 위로 부풀고 손익분기 K도 밀린다. 플레이어는 공백 0도 겪으므로
// 무조건 확률이 맞다.
#include <cstdio>
#include <cstdlib>

#include <initializer_list>

#include "../include/dc/prd.h"
#include "../include/dc/rng.h"

using namespace dc;

constexpr int32_t MAX_K = 64;

struct Stats {
    int64_t procs      = 0;
    int64_t gaps       = 0;
    int32_t maxStreak  = 0;
    int64_t gapAtLeast[MAX_K + 1] = {0};   // 공백 >= k 인 횟수
};

static void record(Stats& s, int32_t gap) {
    ++s.gaps;
    if (gap > s.maxStreak) s.maxStreak = gap;
    const int32_t top = gap < MAX_K ? gap : MAX_K;
    for (int32_t k = 0; k <= top; ++k) ++s.gapAtLeast[k];
}

// 순수 확률 — 비교군. 같은 q16 스케일을 써서 조건을 맞춘다.
static Stats runPure(uint64_t seed, int64_t trials, uint32_t rateQ16) {
    Stats s;
    Rng rng = Rng::derive(seed, RngStream::Combat);
    int32_t gap = 0;
    for (int64_t i = 0; i < trials; ++i) {
        if (rng.chanceQ16(rateQ16)) { ++s.procs; record(s, gap); gap = 0; }
        else { ++gap; }
    }
    return s;
}

// PRD — 실제 출시 경로 (PrdChannel + Rng::chanceQ16)
static Stats runPrd(uint64_t seed, int64_t trials, uint32_t cQ16) {
    Stats s;
    Rng rng = Rng::derive(seed, RngStream::Combat);
    PrdChannel ch;
    int32_t gap = 0;
    for (int64_t i = 0; i < trials; ++i) {
        if (ch.roll(rng, cQ16)) { ++s.procs; record(s, gap); gap = 0; }
        else { ++gap; }
    }
    return s;
}

static double ratePerTrial(const Stats& s, int64_t trials) {
    return static_cast<double>(s.procs) / static_cast<double>(trials);
}
static double gapGE(const Stats& s, int32_t k) {
    if (s.gaps == 0) return 0.0;
    const int32_t idx = k < MAX_K ? k : MAX_K;
    return static_cast<double>(s.gapAtLeast[idx]) / static_cast<double>(s.gaps);
}

int main(int argc, char** argv) {
    const int64_t  trials  = argc > 1 ? std::atoll(argv[1]) : 2000000;
    const uint32_t rateQ16 = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 9830;  // 15%
    const uint32_t cQ16    = argc > 3 ? static_cast<uint32_t>(std::atoi(argv[3])) : 2112;
    const uint64_t seed    = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 20250918ull;

    printf("PRD 분산 측정 — 코어 Rng(xorshift64*) + PrdChannel, 시행 %lld회\n",
           static_cast<long long>(trials));
    printf("  목표 발동률 %u/65536 = %.4f   PRD 상수 C %u/65536 = %.6f\n\n",
           rateQ16, rateQ16 / 65536.0, cQ16, cQ16 / 65536.0);

    const Stats pure = runPure(seed, trials, rateQ16);
    const Stats prd  = runPrd(seed, trials, cQ16);

    printf("  %-12s %12s %12s\n", "", "순수", "PRD");
    printf("  %-12s %12.4f %12.4f\n", "실측 발동률",
           ratePerTrial(pure, trials), ratePerTrial(prd, trials));
    printf("  %-12s %12d %12d\n", "최대 연속실패", pure.maxStreak, prd.maxStreak);
    printf("  %-12s %12s %12d\n", "이론 상한", "없음", prdMaxStreak(cQ16));
    printf("\n");

    printf("  연속 실패 K회 이상 확률 (공백 0 포함 — 본문 주석 참조)\n");
    printf("  %4s %12s %12s %12s\n", "K", "순수", "PRD", "감소배수");
    for (int32_t k : {5, 7, 8, 9, 10, 15, 20, 30}) {
        const double a = gapGE(pure, k);
        const double b = gapGE(prd, k);
        if (b > 0.0) printf("  %4d %12.5f %12.5f %11.1fx\n", k, a, b, a / b);
        else         printf("  %4d %12.5f %12.5f %12s\n", k, a, b, "∞");
    }

    int32_t breakeven = -1;
    for (int32_t k = 1; k <= MAX_K; ++k) {
        if (gapGE(prd, k) < gapGE(pure, k)) { breakeven = k; break; }
    }
    printf("\n  손익분기 K = %d  (이 이상에서 PRD가 순수 확률보다 유리)\n", breakeven);
    printf("  억까 체감선 K=15 앞에 있는가: %s\n",
           (breakeven > 0 && breakeven < 15) ? "예" : "아니오");
    return 0;
}
