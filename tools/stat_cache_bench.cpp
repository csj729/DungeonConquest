// 스탯 계산 전략 벤치마크. -O2 실측.
// design.md §9 StatCache(누적 + 지연 dirty-flag)를 기준으로, 대안들과 비교한다.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using Clock = std::chrono::high_resolution_clock;
constexpr int64_t FIXED_ONE = 1 << 16;

template <typename F>
double benchNs(int iters, F&& f) {
    for (int i = 0; i < 1000; ++i) f();
    auto start = Clock::now();
    for (int i = 0; i < iters; ++i) f();
    auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iters;
}

// ── 후보 A: 현재 design.md §9 — 누적(Flat/PercentAdd) + 지연 dirty-flag + bounded mult/override ──
struct StatCacheA {
    int64_t accumFlat = 0, accumPctAdd = 0;
    int64_t mults[4]; uint8_t multCount = 0;
    int64_t overrideVal = 0; bool hasOverride = false;
    int64_t cached = 0;
    bool dirty = true;
};
int64_t recomputeA(const StatCacheA& sc) {
    if (sc.hasOverride) return sc.overrideVal;
    int64_t v = sc.accumFlat;
    v = v + (v * sc.accumPctAdd) / FIXED_ONE;
    for (uint8_t i = 0; i < sc.multCount; ++i) v = (v * sc.mults[i]) / FIXED_ONE;
    return v;
}
int64_t getLazy(StatCacheA& sc) {
    if (sc.dirty) { sc.cached = recomputeA(sc); sc.dirty = false; }
    return sc.cached;
}

// ── 후보 B: 나이브 — 구분 없는 모디파이어 리스트를 매번 처음부터 순회 (누적/캐시 없음) ──
enum ModType : uint8_t { FLAT, PCTADD, PCTMULT, OVERRIDE };
struct RawMod { ModType type; int64_t value; };
int64_t computeNaive(const std::vector<RawMod>& mods) {
    int64_t flatSum = 0, pctSum = 0, multProd = FIXED_ONE;
    bool hasOverride = false; int64_t overrideVal = 0;
    for (auto& m : mods) {
        switch (m.type) {
            case FLAT: flatSum += m.value; break;
            case PCTADD: pctSum += m.value; break;
            case PCTMULT: multProd = (multProd * m.value) / FIXED_ONE; break;
            case OVERRIDE: if (!hasOverride) { hasOverride = true; overrideVal = m.value; } break;
        }
    }
    if (hasOverride) return overrideVal;
    int64_t v = flatSum;
    v = v + (v * pctSum) / FIXED_ONE;
    v = (v * multProd) / FIXED_ONE;
    return v;
}

// ── 후보 C: 즉시(eager) 갱신 — 쓰기 시점마다 바로 재계산, 읽기는 항상 캐시만 반환 ──
struct StatCacheC { StatCacheA base; }; // 구조는 A와 동일, 갱신 시점만 다름
void addFlatEager(StatCacheC& sc, int64_t delta) {
    sc.base.accumFlat += delta;
    sc.base.cached = recomputeA(sc.base); // 쓰기 즉시 재계산
}
int64_t getEager(StatCacheC& sc) { return sc.base.cached; } // 항상 이미 최신

int main() {
    std::mt19937 rng(777);
    std::uniform_int_distribution<int64_t> valDist(-FIXED_ONE, FIXED_ONE);

    // ===== 실험 1: A(누적+캐시) vs B(나이브 순회) — 안정 상태(변경 없이 반복 읽기) =====
    {
        printf("=== 실험 1: 안정 상태에서 반복 읽기 (모디파이어 6개, 변경 없음) ===\n");
        StatCacheA a; a.accumFlat = 500 * FIXED_ONE; a.accumPctAdd = FIXED_ONE / 5;
        a.multCount = 2; a.mults[0] = FIXED_ONE + FIXED_ONE/10; a.mults[1] = FIXED_ONE - FIXED_ONE/20;
        a.dirty = true;

        std::vector<RawMod> mods = {
            {FLAT, 300*FIXED_ONE}, {FLAT, 200*FIXED_ONE}, {PCTADD, FIXED_ONE/5},
            {PCTMULT, FIXED_ONE+FIXED_ONE/10}, {PCTMULT, FIXED_ONE-FIXED_ONE/20}, {FLAT, 50*FIXED_ONE}
        };

        double tA = benchNs(2000000, [&]{ volatile int64_t v = getLazy(a); (void)v; });
        printf("A) 누적+지연캐시 (dirty 이후 첫 읽기 1회 제외 항상 캐시 hit): %8.2f ns\n", tA);

        double tB = benchNs(2000000, [&]{ volatile int64_t v = computeNaive(mods); (void)v; });
        printf("B) 나이브 전체 순회 (매번 6개 모디파이어 다시 훑음):          %8.2f ns  (A 대비 %.0fx)\n\n", tB, tB/tA);
    }

    // ===== 실험 2: A(지연) vs C(즉시) — 쓰기:읽기 비율에 따른 총 비용 =====
    // recompute 비용이 사실상 공짜인 경우(모디파이어 없음)와, 실제 재계산 비용이 있는 경우
    // (mult 2개)를 둘 다 잰다 — recompute 비용이 결과를 뒤집는지 확인.
    {
        auto setupHeavy = [](StatCacheA& sc){
            sc.accumPctAdd = FIXED_ONE/5;
            sc.multCount = 2; sc.mults[0] = FIXED_ONE+FIXED_ONE/10; sc.mults[1] = FIXED_ONE-FIXED_ONE/20;
        };
        for (bool heavy : {false, true}) {
            printf("=== 실험 2: 지연 vs 즉시 — 쓰기 W번 후 읽기 1번, 반복 (recompute %s) ===\n",
                   heavy ? "비용 있음: mult 2개" : "거의 공짜: 모디파이어 없음");
            for (int W : {1, 3, 5, 10}) {
                StatCacheA a; a.dirty = false; a.cached = 0;
                if (heavy) setupHeavy(a);
                double tLazy = benchNs(500000, [&]{
                    for (int i = 0; i < W; ++i) { a.accumFlat += 10; a.dirty = true; }
                    volatile int64_t v = getLazy(a); (void)v;
                });

                StatCacheC c; c.base.cached = 0;
                if (heavy) setupHeavy(c.base);
                double tEager = benchNs(500000, [&]{
                    for (int i = 0; i < W; ++i) addFlatEager(c, 10);
                    volatile int64_t v = getEager(c); (void)v;
                });
                printf("  W=%2d: 지연 %7.2f ns   즉시 %7.2f ns   (즉시/지연 = %.2fx)\n", W, tLazy, tEager, tEager/tLazy);
            }
            printf("\n");
        }
    }

    // ===== 실험 3: 틱 단위 — 엔티티별 dirty-skip vs 전체 강제 재계산(SoA, 벡터화 우호적) =====
    {
        printf("=== 실험 3: N 엔티티, 매 틱 각자 스탯 1회 읽음. churn=이번 틱에 실제로 바뀐 비율 ===\n");
        for (int N : {512, 4096}) {
            std::vector<int64_t> accumFlat(N), accumPctAdd(N), cached(N, 0);
            std::vector<std::array<int64_t,2>> mults(N);
            std::vector<uint8_t> multCount(N, 2);
            std::vector<uint8_t> dirty(N, 1);
            for (int i = 0; i < N; ++i) {
                accumFlat[i] = 100 * FIXED_ONE + i;
                accumPctAdd[i] = FIXED_ONE/10;
                mults[i] = { FIXED_ONE + FIXED_ONE/10, FIXED_ONE - FIXED_ONE/20 };
            }

            auto recomputeOne = [&](int i){
                int64_t v = accumFlat[i];
                v = v + (v * accumPctAdd[i]) / FIXED_ONE;
                for (uint8_t k = 0; k < multCount[i]; ++k) v = (v * mults[i][k]) / FIXED_ONE;
                cached[i] = v;
            };

            printf("-- N=%d --\n", N);
            for (double churn : {0.01, 0.10, 0.50, 1.00}) {
                std::fill(dirty.begin(), dirty.end(), 0);
                int changed = static_cast<int>(N * churn);
                std::vector<int> idx(N); for (int i=0;i<N;i++) idx[i]=i;
                std::shuffle(idx.begin(), idx.end(), rng);
                for (int i = 0; i < changed; ++i) dirty[idx[i]] = 1;

                double tSkip = benchNs(2000, [&]{
                    for (int i = 0; i < N; ++i) if (dirty[i]) { recomputeOne(i); dirty[i] = 0; }
                    for (int i = 0; i < changed; ++i) dirty[idx[i]] = 1; // 다음 반복을 위해 복원
                });

                double tAll = benchNs(2000, [&]{
                    for (int i = 0; i < N; ++i) recomputeOne(i); // 전부 무조건 재계산, 분기 없음
                });

                printf("  churn=%4.0f%%  dirty-skip %8.1f ns   전체 강제재계산 %8.1f ns   (강제/skip = %.2fx)\n",
                       churn*100, tSkip, tAll, tAll/tSkip);
            }
        }
    }

    return 0;
}
