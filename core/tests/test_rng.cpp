// Rng 테스트 — 재현성 · 스트림 독립 · 편향 없음.
//
// 여기서 지키려는 것은 "확률이 그럴듯하다"가 아니라
// **같은 시드 → 같은 열**과 **한 시스템의 호출 횟수가 다른 시스템을 흔들지 않음**이다.
// 앞은 리플레이 검증의 전제, 뒤는 밸런스를 만질 때마다 리플레이가 깨지지 않게 하는 전제.
#include "../include/dc/rng.h"
#include "test_main.h"

using namespace dc;

// 난수열을 한 값으로 접는다. 열 전체를 비교하려면 이게 제일 싸다.
static uint64_t digest(Rng r, int n) {
    uint64_t h = FNV_OFFSET;
    for (int i = 0; i < n; ++i) {
        const uint64_t v = r.nextU64();
        for (int b = 0; b < 8; ++b) h = fnv1a(h, static_cast<uint8_t>(v >> (b * 8)));
    }
    return h;
}

int main() {
    printf("test_rng\n");

    dctest::section("재현성");
    {
        // 같은 시드·스트림이면 몇 번을 다시 만들어도 같은 열이어야 한다.
        const uint64_t a = digest(Rng::derive(12345, RngStream::Combat), 1000);
        const uint64_t b = digest(Rng::derive(12345, RngStream::Combat), 1000);
        CHECK_EQU(a, b);

        // 시드가 다르면 열도 달라야 한다 (같으면 시드가 먹히지 않는 것).
        CHECK(digest(Rng::derive(12346, RngStream::Combat), 1000) != a);

        // 스트림이 다르면 같은 시드라도 달라야 한다.
        CHECK(digest(Rng::derive(12345, RngStream::Spawn), 1000) != a);

        // 회귀 고정값 — 알고리즘이 바뀌면 여기서 걸린다.
        // (값이 바뀌면 기존 리플레이가 전부 무효가 된다는 뜻이므로 의도적 변경일 때만 갱신)
        CHECK_EQU(digest(Rng::derive(1, RngStream::Spawn), 16), 17200449293806869421ull);
    }

    dctest::section("스트림 독립");
    {
        // Spawn에서 난수를 아무리 더 뽑아도 Combat 열은 불변이어야 한다.
        // 이게 깨지면 "스폰 좌표를 하나 더 뽑는 변경"이 전투 판정을 바꾼다.
        Rng spawn  = Rng::derive(777, RngStream::Spawn);
        Rng combat = Rng::derive(777, RngStream::Combat);

        const uint64_t combatBefore = digest(combat, 64);
        for (int i = 0; i < 500; ++i) (void)spawn.nextU64();
        const uint64_t combatAfter = digest(Rng::derive(777, RngStream::Combat), 64);
        CHECK_EQU(combatBefore, combatAfter);
    }

    dctest::section("range 편향");
    {
        // n이 2의 거듭제곱이 아닌 경우가 핵심. 모듈로를 그냥 쓰면 여기서 앞쪽이 더 나온다.
        const uint32_t N = 5;
        const int TRIALS = 500000;
        int bucket[N] = {0};
        Rng r = Rng::derive(99, RngStream::Cards);
        for (int i = 0; i < TRIALS; ++i) bucket[r.range(N)]++;

        const int expect = TRIALS / static_cast<int>(N);
        for (uint32_t i = 0; i < N; ++i) {
            const int diff = bucket[i] > expect ? bucket[i] - expect : expect - bucket[i];
            // 허용 오차 1.5% — 균등이면 통계 변동은 0.3% 수준이다.
            CHECK(diff * 1000 < expect * 15);
        }
        printf("    버킷: %d %d %d %d %d (기대 %d)\n",
               bucket[0], bucket[1], bucket[2], bucket[3], bucket[4], expect);
    }

    dctest::section("range 경계");
    {
        Rng r = Rng::derive(5, RngStream::Items);
        CHECK_EQ(r.range(0), 0u);   // 0칸 요청은 0
        CHECK_EQ(r.range(1), 0u);   // 1칸이면 항상 0
        bool inRange = true;
        for (int i = 0; i < 10000; ++i) if (r.range(7) >= 7) inRange = false;
        CHECK(inRange);
    }

    dctest::section("chancePermille");
    {
        Rng r = Rng::derive(31, RngStream::Combat);
        CHECK(!r.chancePermille(0));
        CHECK(!r.chancePermille(-50));
        CHECK(r.chancePermille(1000));
        CHECK(r.chancePermille(2000));

        // data/hero.json의 proc_rate_permille 150이 실제로 15%로 나오는지.
        const int TRIALS = 200000;
        int hit = 0;
        for (int i = 0; i < TRIALS; ++i) if (r.chancePermille(150)) ++hit;
        const int pm = hit * 1000 / TRIALS;
        printf("    150permille 실측: %dpermille\n", pm);
        CHECK(pm >= 145 && pm <= 155);
    }

    dctest::section("weighted");
    {
        // data/skills.json의 통합 proc 가중치: W_SMASH 400 · W_WHIRL 350 · W_CLEAVE 250
        const int32_t w[3] = {400, 350, 250};
        const int TRIALS = 200000;
        int cnt[3] = {0, 0, 0};
        Rng r = Rng::derive(2024, RngStream::Combat);
        for (int i = 0; i < TRIALS; ++i) cnt[r.weighted(w, 3)]++;

        const int32_t expectPm[3] = {400, 350, 250};
        for (int i = 0; i < 3; ++i) {
            const int pm = cnt[i] * 1000 / TRIALS;
            printf("    w[%d] 실측 %dpermille (기대 %dpermille)\n", i, pm, expectPm[i]);
            const int diff = pm > expectPm[i] ? pm - expectPm[i] : expectPm[i] - pm;
            CHECK(diff <= 5);
        }

        // 가중치가 전부 0이면 0번을 돌려준다 (무한루프·나눗셈 0 방지).
        const int32_t zero[2] = {0, 0};
        CHECK_EQ(r.weighted(zero, 2), 0u);
    }

    dctest::section("상태 저장·복원");
    {
        // 스냅샷에서 이어 돌리면 한 번도 안 끊긴 것과 같은 열이 나와야 한다.
        // 리플레이 중간 지점 복원이 이 성질 위에 선다.
        Rng r = Rng::derive(4242, RngStream::Events);
        for (int i = 0; i < 37; ++i) (void)r.nextU64();
        const uint64_t saved = r.state();
        const uint64_t straight = digest(r, 100);

        Rng restored;
        restored.setState(saved);
        CHECK_EQU(straight, digest(restored, 100));
    }

    dctest::section("해시 헬퍼");
    {
        // std::hash 대체품이므로 값이 플랫폼에 상관없이 고정이어야 한다.
        CHECK_EQU(splitMix64(0), 16294208416658607535ull);
        CHECK_EQU(fnv1a(FNV_OFFSET, 0), 4953163356653287321ull);
        CHECK(splitMix64(1) != splitMix64(2));
    }

    dctest::section("constexpr 평가");
    {
        // 컴파일 타임에 돌아야 한다 — 런타임 초기화 순서 의존을 원천 차단.
        constexpr Rng cr = Rng::derive(9, RngStream::Spawn);
        static_assert(cr.state() != 0, "파생 시드가 0이면 xorshift가 고착된다");
        static_assert(splitMix64(0) != 0, "finalizer가 0을 0으로 보내면 시드 파생이 무너진다");
        CHECK(true);
    }

    return dctest::summary("test_rng");
}
