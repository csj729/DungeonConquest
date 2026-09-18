// PRD 테스트 (§7 ①).
//
// PRD는 **기대값을 유지한 채 꼬리만 자르는** 장치다. 그래서 검증도 두 축이다:
//   1. 기대값이 정말 유지되는가 (목표 발동률과 일치)
//   2. 꼬리가 정말 잘리는가 (최대 연속 실패 상한이 수학적으로 성립)
// 하나만 맞으면 밸런스가 틀어지거나 "억까 제거"가 거짓말이 된다.
#include <initializer_list>

#include <utility>

#include "../include/dc/prd.h"
#include "../include/dc/world.h"
#include "test_main.h"

using namespace dc;

// data/hero.json — proc_rate_permille 150 → proc_prd_c_q16 2112
constexpr uint32_t PROC_C_Q16    = 2112;
constexpr uint32_t PROC_RATE_Q16 = 9830;   // 0.15 × 65536

int main() {
    printf("test_prd\n");

    dctest::section("chanceQ16 — 낮은 확률 스케일");
    {
        Rng r = Rng::derive(1, RngStream::Combat);
        CHECK(!r.chanceQ16(0));
        CHECK(r.chanceQ16(Q16_ONE));
        CHECK(r.chanceQ16(Q16_ONE + 1000));   // 포화

        // 2^16이 2^64를 정확히 나누므로 편향이 없어야 한다.
        // permille(1000)은 2의 거듭제곱이 아니라 기각 표집이 필요했던 것과 대비된다.
        const int TRIALS = 2000000;
        int hit = 0;
        for (int i = 0; i < TRIALS; ++i) if (r.chanceQ16(PROC_C_Q16)) ++hit;
        const double got = static_cast<double>(hit) / TRIALS;
        const double want = PROC_C_Q16 / 65536.0;
        printf("    C=%u/65536 실측 %.5f (기대 %.5f)\n", PROC_C_Q16, got, want);
        CHECK(got > want * 0.99 && got < want * 1.01);
    }

    dctest::section("prdChanceQ16 — 확률이 선형으로 오른다");
    {
        CHECK_EQ(prdChanceQ16(PROC_C_Q16, 0), PROC_C_Q16);          // 첫 시행 = C
        CHECK_EQ(prdChanceQ16(PROC_C_Q16, 1), PROC_C_Q16 * 2);
        CHECK_EQ(prdChanceQ16(PROC_C_Q16, 9), PROC_C_Q16 * 10);

        // **1.0에서 포화한다.** 넘치면 chanceQ16이 무조건 참이 되어 같지만,
        // 값 자체를 UI에 노출하므로(§8) 여기서 막아둔다.
        CHECK_EQ(prdChanceQ16(PROC_C_Q16, 1000), Q16_ONE);
        CHECK_EQ(prdChanceQ16(Q16_ONE, 0), Q16_ONE);
        CHECK_EQ(prdChanceQ16(0, 100), 0u);

        // 큰 trials에서도 오버플로가 없어야 한다 (uint64 승격).
        CHECK_EQ(prdChanceQ16(PROC_C_Q16, 2000000000), Q16_ONE);
    }

    dctest::section("최대 연속 실패 상한 — 수학적으로 보장된다");
    {
        // C × n >= 1.0 이 되는 n에서 확률이 100%가 되므로 그 이상은 불가능하다.
        // **순수 확률에는 이런 상한이 아예 없다.** PRD가 파는 게 정확히 이것이다.
        const int32_t cap = prdMaxStreak(PROC_C_Q16);
        printf("    C=%u → 연속 실패 상한 %d회\n", PROC_C_Q16, cap);
        CHECK_EQ(cap, 31);
        // cap회 실패한 시점의 굴림은 **확정 성공**이다. 그래서 공백이 cap을
        // 넘을 수 없다. 그 직전(cap-1회 실패)까지는 아직 100% 미만이다.
        CHECK(prdChanceQ16(PROC_C_Q16, cap - 1) < Q16_ONE);
        CHECK_EQ(prdChanceQ16(PROC_C_Q16, cap), Q16_ONE);

        // 실제로 넘지 못하는지 확인한다.
        Rng r = Rng::derive(7, RngStream::Combat);
        PrdChannel ch;
        int32_t worst = 0, gap = 0;
        for (int i = 0; i < 2000000; ++i) {
            if (ch.roll(r, PROC_C_Q16)) { if (gap > worst) worst = gap; gap = 0; }
            else ++gap;
        }
        printf("    200만 시행 관측 최대 %d회 (상한 %d)\n", worst, cap);
        CHECK(worst <= cap);
        CHECK(ch.trials <= cap);
    }

    dctest::section("기대값 유지 — 목표 발동률과 일치");
    {
        // PRD의 존재 이유는 "기대값은 그대로인데 꼬리만 잘린다"이다.
        // 기대값이 틀리면 §7의 전제가 무너지고 밸런스 전체가 밀린다.
        const int TRIALS = 4000000;
        Rng r = Rng::derive(20250918, RngStream::Combat);
        PrdChannel ch;
        int procs = 0;
        for (int i = 0; i < TRIALS; ++i) if (ch.roll(r, PROC_C_Q16)) ++procs;
        const double rate = static_cast<double>(procs) / TRIALS;
        printf("    PRD 실측 발동률 %.5f (목표 0.15000)\n", rate);
        CHECK(rate > 0.1485 && rate < 0.1515);
    }

    dctest::section("꼬리가 실제로 잘린다");
    {
        // 순수와 PRD를 같은 시행 수로 돌려 K=15(억까 체감선) 이상 공백을 센다.
        // **길이 0인 공백도 분모에 넣는다** — 빼면 P(>=K | >=1)이 되어 위로 부푼다.
        const int TRIALS = 2000000;
        auto measure = [&](bool prd) {
            Rng r = Rng::derive(555, RngStream::Combat);
            PrdChannel ch;
            int gaps = 0, ge15 = 0, ge20 = 0, worst = 0, gap = 0;
            for (int i = 0; i < TRIALS; ++i) {
                const bool hit = prd ? ch.roll(r, PROC_C_Q16) : r.chanceQ16(PROC_RATE_Q16);
                if (hit) {
                    ++gaps;
                    if (gap >= 15) ++ge15;
                    if (gap >= 20) ++ge20;
                    if (gap > worst) worst = gap;
                    gap = 0;
                } else ++gap;
            }
            struct R { double ge15, ge20; int worst; };
            return R{static_cast<double>(ge15) / gaps, static_cast<double>(ge20) / gaps, worst};
        };
        const auto pure = measure(false);
        const auto prd  = measure(true);
        printf("    K>=15  순수 %.5f → PRD %.5f (%.0f배 감소)\n",
               pure.ge15, prd.ge15, pure.ge15 / prd.ge15);
        printf("    K>=20  순수 %.5f → PRD %.5f\n", pure.ge20, prd.ge20);
        printf("    최대   순수 %d회 → PRD %d회\n", pure.worst, prd.worst);
        CHECK(prd.ge15 < pure.ge15 / 5.0);      // 최소 5배는 줄어야 한다
        CHECK(prd.ge20 < pure.ge20 / 50.0);
        CHECK(prd.worst < pure.worst);
    }

    dctest::section("PrdChannel 상태 · 결정론");
    {
        PrdChannel ch;
        CHECK_EQ(ch.trials, 0);
        CHECK_EQ(ch.chanceQ16(PROC_C_Q16), PROC_C_Q16);

        // 같은 시드면 같은 열. 카운터까지 일치해야 한다.
        auto play = [](int n) {
            Rng r = Rng::derive(31337, RngStream::Combat);
            PrdChannel c;
            int hits = 0;
            for (int i = 0; i < n; ++i) if (c.roll(r, PROC_C_Q16)) ++hits;
            return std::pair<int, int32_t>{hits, c.trials};
        };
        CHECK(play(10000) == play(10000));

        // 성공하면 리셋, 실패하면 증가 — 그 외 경로가 없어야 한다.
        Rng r = Rng::derive(3, RngStream::Combat);
        PrdChannel c;
        int32_t prev = c.trials;
        for (int i = 0; i < 5000; ++i) {
            const bool hit = c.roll(r, PROC_C_Q16);
            if (hit) CHECK_EQ(c.trials, 0);
            else CHECK_EQ(c.trials, prev + 1);
            prev = c.trials;
        }
    }

    dctest::section("체크섬 — PRD 카운터는 [상태]");
    {
        // §10이 "PRD 카운터도 체크섬 입력에 포함"이라고 명시한 부분.
        // 빠지면 서버와 클라의 다음 발동 확률이 달라지는데 해시는 통과한다.
        World a, b;
        a.init(11);
        b.init(11);
        CHECK_EQU(a.checksum(), b.checksum());

        a.hero.proc.trials = 7;
        CHECK(a.checksum() != b.checksum());
        b.hero.proc.trials = 7;
        CHECK_EQU(a.checksum(), b.checksum());

        // 굴림이 상태를 움직이므로 체크섬도 따라 움직인다.
        const uint64_t before = a.checksum();
        (void)a.hero.proc.roll(a.rngCombat, PROC_C_Q16);
        CHECK(a.checksum() != before);
    }

    dctest::section("채널 분리");
    {
        // 채널을 나누는 이유는 RNG 스트림을 나누는 이유와 같다 —
        // 한쪽의 시행 횟수가 다른 쪽 확률을 바꾸면 안 된다.
        PrdChannel procCh, dropCh;
        Rng r = Rng::derive(9, RngStream::Combat);
        for (int i = 0; i < 100; ++i) (void)procCh.roll(r, PROC_C_Q16);
        CHECK_EQ(dropCh.trials, 0);
        CHECK_EQ(dropCh.chanceQ16(PROC_C_Q16), PROC_C_Q16);
    }

    return dctest::summary("test_prd");
}
