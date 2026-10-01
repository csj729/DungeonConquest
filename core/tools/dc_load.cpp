// 물량 부하 측정 (§14-12 목표 512 · §11 성능 방침).
//
// **시뮬 틱 비용만 잰다.** 렌더 비용은 Unity에서 재야 하고 여기서 섞으면 둘 다
// 못 고친다 — 프레임이 느릴 때 분리를 고칠지 배칭을 고칠지는 합계로는 답이
// 안 나온다. 이 하네스가 정하는 것은 "시뮬이 틱 예산의 몇 %를 쓰는가"이고,
// 남은 몫이 렌더 예산이 된다.
//
// ## 두 가지 측정 장치를 쓴다
//
//  1. **총 틱 비용** — `stepWorld()`를 그대로 돌려 잰다. 이게 예산에 걸리는 값이다
//  2. **시스템별 비용** — 스냅샷을 복원해 한 시스템만 부른다. 복원은 **타이머
//     밖**에서 하므로 100KB 복사가 측정에 섞이지 않는다
//
// 2번은 `stepWorld`의 호출 순서를 여기 옮겨 적지 않는다(순서는 `sim.h`가 단일
// 정의점이다). 대신 **집합**을 적게 되는데, 시스템이 추가되면 이 목록이 따라오지
// 않아 부분합이 조용히 총합보다 작아진다. 그래서 **잔차를 출력하고 임계를
// 넘으면 경고한다** — 빠뜨린 시스템이 빨간불로 드러나게.
//
// ## 물량을 유지하려고 두 값을 덮어쓴다
//
//   · 잡몹 체력 ↑ — 측정 중에 죽으면 N이 줄어 "N에서의 비용"이 아니게 된다
//   · 잠식 물량 충전 = 0 — **진짜 게임에서는 512마리에 도달할 수 없다.**
//     유입이 `0.6 × (N - 20)`/초라 N=512면 초당 295, 게이지 1250이 4.2초에
//     가득 찬다. 그게 §2가 설계한 대로 "몬스터 수 상한 = 게임오버"가 작동하는
//     모습이다. 즉 512는 **플레이 가능한 물량이 아니라 코드가 견뎌야 하는
//     물량**이다 — 소환 폭주·버스트·미래 기믹이 순간적으로 만들 수 있으므로
//     거기서 터지지 않는 것이 목표다
//
// 정상성(배열 넘침 · Fixed 오버플로 · 결정론)은 `core/tests/test_load.cpp`가
// ASan·UBSan 빌드에서 본다. 여기는 Release 전용 측정 도구다.
#include <chrono>      // 개발 도구다. 시뮬 코어가 아니므로 허용 (dc_recipe_bench와 같다)
#include <cstdio>
#include <cstdlib>

#include "../include/dc/sim.h"
#include "data_files.h"

using namespace dc;
using Clock = std::chrono::steady_clock;

// 틱 예산. 20Hz라 한 틱에 50ms가 전부이고, **시뮬이 그걸 다 쓰면 렌더가 0이다.**
// 시뮬 몫의 목표는 하네스 가정이다(데이터가 아니다) — 10%면 모바일에서 렌더·
// GC·입력에 45ms가 남는다.
constexpr double kTickBudgetNs   = 1e9 / config::TICK_HZ;   // 50,000,000 ns
constexpr double kSimSharePass   = 0.10;
// 부분합이 총합에서 이만큼 이상 벗어나면 시스템을 빠뜨린 것이다.
constexpr double kResidualWarn   = 0.25;

static SimConfig loadConfig(uint32_t n) {
    SimConfig c = dev::data().cfg;
    c.trash.hp = Fixed(100000);              // Fixed(1000000)은 20.12 오버플로다
    c.corruptionPerMobPermille = 0;          // 위 주석 참조 — N을 유지하려는 것이다
    for (uint32_t i = 0; i < 32; ++i) c.capBySegment[i] = static_cast<int32_t>(n);
    return c;
}

// N마리를 실제 스폰 경로로 채우고, 분리가 자리를 잡을 만큼 돌린 월드를 준다.
static void fill(World* w, const SimConfig& c, uint32_t n, SimScratch* sc, int32_t settle) {
    initWorld(*w, 20251001ull + n, c, dev::data().table, dev::data().hero);
    spawnTrashBatch(*w, c, static_cast<int32_t>(n), c.trash.hp);
    for (int32_t t = 0; t < settle; ++t) stepWorld(*w, c, dev::data().table, *sc);
}

template <typename F>
static double probe(const World& snap, const SimConfig& c, SimScratch* sc, int iters, F fn) {
    static World t;
    double acc = 0;
    for (int i = 0; i < iters; ++i) {
        t = snap;                                   // **타이머 밖**
        const auto t0 = Clock::now();
        fn(t, c, *sc);
        const auto t1 = Clock::now();
        acc += std::chrono::duration<double, std::nano>(t1 - t0).count();
    }
    return acc / iters;
}

struct Row { const char* name; double ns; };

int main(int argc, char** argv) {
    const int32_t ticks = argc > 1 ? std::atoi(argv[1]) : 400;
    const int32_t iters = argc > 2 ? std::atoi(argv[2]) : 200;

    printf("=== 물량 부하 — 시뮬 틱 비용 (§14-12) ===\n");
    printf("틱 예산 %.1f ms (20Hz) · 시뮬 목표 지분 %.0f%% = %.2f ms\n",
           kTickBudgetNs / 1e6, kSimSharePass * 100,
           kTickBudgetNs * kSimSharePass / 1e6);
    printf("측정 틱 %d · 시스템별 반복 %d\n\n", ticks, iters);

    static SimScratch sc;
    bool ok = true;

    // ── 총 틱 비용 ──────────────────────────────────────────────────────
    printf("== 총 틱 비용 ==\n");
    printf("%6s %8s %10s %10s %9s %8s\n",
           "N", "생존", "평균", "최대", "예산지분", "판정");
    for (uint32_t n : {30u, 60u, 150u, 512u, 1024u}) {
        const SimConfig c = loadConfig(n);
        static World w;
        fill(&w, c, n, &sc, 100);                    // 과도구간은 버린다

        double sum = 0, worst = 0;
        for (int32_t t = 0; t < ticks; ++t) {
            const auto t0 = Clock::now();
            stepWorld(w, c, dev::data().table, sc);
            const auto t1 = Clock::now();
            const double d = std::chrono::duration<double, std::nano>(t1 - t0).count();
            sum += d;
            if (d > worst) worst = d;
        }
        const double mean  = sum / ticks;
        const double share = mean / kTickBudgetNs;
        // **최대 틱도 본다.** 평균이 예산 안이어도 튀는 틱이 있으면 TickDriver의
        // MaxTicksPerFrame(8) 클램프가 걸려 체감이 끊긴다 — 결정론은 그대로지만.
        const bool good = share <= kSimSharePass;
        ok &= good;
        printf("%6u %8u %8.1f μs %8.1f μs %8.2f%% %8s\n",
               n, aliveCount(w.entities), mean / 1000.0, worst / 1000.0,
               share * 100, good ? "PASS" : "FAIL");
    }
    printf("  ※ 최대 틱이 평균의 몇 배인지가 평균보다 중요할 수 있다 —\n");
    printf("     튀는 틱은 틱 소비 클램프를 때려 체감 끊김으로 나온다 (§10)\n\n");

    // ── 시스템별 분해 ───────────────────────────────────────────────────
    printf("== 시스템별 분해 (N=512) ==\n");
    {
        const uint32_t n = 512;
        const SimConfig c = loadConfig(n);
        static World w;
        fill(&w, c, n, &sc, 100);

        // 총합을 먼저 다시 잰다 — 같은 월드 상태에서 비교해야 잔차가 의미를 갖는다
        double total = 0;
        {
            static World t;
            for (int i = 0; i < iters; ++i) {
                t = w;
                const auto t0 = Clock::now();
                stepWorld(t, c, dev::data().table, sc);
                const auto t1 = Clock::now();
                total += std::chrono::duration<double, std::nano>(t1 - t0).count();
            }
            total /= iters;
        }

        // 집합을 여기 적는다(순서는 적지 않는다 — sim.h가 소유한다).
        // 빠뜨리면 아래 잔차가 커져서 드러난다.
        const Row rows[] = {
            {"beginTick",      probe(w, c, &sc, iters, [](World& x, const SimConfig&, SimScratch&) { x.beginTick(); })},
            {"progressRun",    probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { progressRun(x, y, dev::data().table); })},
            {"spawnRun",       probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { spawnRun(x, y); })},
            // selectTarget + notifyTargetChanged. stepWorld 안에서 인라인이라
            // 함수가 없어 같은 두 호출을 여기서 만든다 — **측정 전용 중복이다**.
            // 둘을 **갈라 잰다**: §11이 "1×N은 브루트포스로 수 μs"라고 적어둔
            // 대상은 탐색뿐이고, 조건부 모디파이어 재평가는 N과 무관한 비용이다.
            {"targeting",      probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) {
                                   x.hero.target = selectTarget(x.entities, x.hero.posX, x.hero.posY,
                                                                x.hero.manualTarget,
                                                                x.hero.stats.value(Stat::Range),
                                                                y.targetPriorityFalloffPerTile);
                                   x.notifyTargetChanged(); })},
            {"  ├ selectTarget", probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) {
                                   x.hero.target = selectTarget(x.entities, x.hero.posX, x.hero.posY,
                                                                x.hero.manualTarget,
                                                                x.hero.stats.value(Stat::Range),
                                                                y.targetPriorityFalloffPerTile); })},
            {"  └ notifyTarget", probe(w, c, &sc, iters, [](World& x, const SimConfig&, SimScratch&) {
                                   x.notifyTargetChanged(); })},
            {"  └ grid.build",  probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch& s) {
                                   const int64_t ms = (static_cast<int64_t>(y.separationMilli) * Fixed::ONE_RAW) / 1000;
                                   s.grid.build(x.entities, Fixed::fromRaw(static_cast<int32_t>(ms)),
                                                x.hero.posX, x.hero.posY); })},
            {"separationRun",  probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch& s) { separationRun(x, y, s); })},
            {"movementRun",    probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { movementRun(x, y); })},
            {"decayRun",       probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { decayRun(x, y); })},
            {"boltRun",        probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { boltRun(x, y); })},
            {"stormRun",       probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { stormRun(x, y); })},
            {"orbRun",         probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { orbRun(x, y); })},
            {"qteRun",         probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { qteRun(x, y); })},
            {"combatRun",      probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { combatRun(x, y); })},
            {"bossPhaseRun",   probe(w, c, &sc, iters, [](World& x, const SimConfig& y, SimScratch&) { bossPhaseRun(x, y); })},
            {"endTick",        probe(w, c, &sc, iters, [](World& x, const SimConfig&, SimScratch&) { x.endTick(); })},
        };

        double part = 0;
        printf("%16s %10s %8s\n", "시스템", "비용", "지분");
        for (const Row& r : rows) {
            // grid.build는 separationRun 안에 이미 포함돼 있다 — 부분합에 두 번
            // 더하지 않도록 들여쓴 줄은 제외한다
            const bool nested = r.name[0] == ' ';
            if (!nested) part += r.ns;
            printf("%16s %8.2f μs %7.1f%%\n", r.name, r.ns / 1000.0, r.ns / total * 100);
        }
        const double resid = (total - part) / total;
        printf("%16s %8.2f μs %7.1f%%\n", "총 틱", total / 1000.0, 100.0);
        printf("%16s %8.2f μs %7.1f%%  %s\n", "잔차", (total - part) / 1000.0, resid * 100,
               resid > kResidualWarn || resid < -kResidualWarn
                   ? "← 시스템을 빠뜨렸다. 위 목록을 sim.h와 맞출 것"
                   : "(측정 오버헤드 수준)");
        if (resid > kResidualWarn || resid < -kResidualWarn) ok = false;

        // 체크섬은 틱 루프 밖이다 — 용도별로 호출 빈도가 다르므로 따로 잰다 (§10)
        double cs = 0;
        uint64_t sink = 0;                 // 최적화로 checksum이 사라지지 않게
        for (int i = 0; i < iters; ++i) {
            const auto t0 = Clock::now();
            const uint64_t v = w.checksum();
            const auto t1 = Clock::now();
            cs += std::chrono::duration<double, std::nano>(t1 - t0).count();
            sink ^= v;
        }
        if (sink == 1u) printf("  (도달 불가 — sink 소비)\n");
        printf("%16s %8.2f μs          (틱 루프 밖 — 재현성 하네스만 매 틱 부른다)\n",
               "checksum", cs / iters / 1000.0);
        printf("\n");
    }

    // ── 그리드 점유 ─────────────────────────────────────────────────────
    //
    // **분리 비용의 설명 변수다.** 3×3 이웃을 훑으므로 비용은 N이 아니라
    // "셀당 몇 마리인가"에 비례한다. 셀 크기는 최소 이격(1.5타일)이라 고정이고,
    // 밀도가 오르면 셀당 인원이 올라 쌍 검사가 제곱으로 늘어난다.
    printf("== 그리드 점유 (셀 크기 = 최소 이격 %.2f타일 · 격자 %u×%u) ==\n",
           static_cast<double>(dev::data().cfg.separationMilli) / 1000.0,
           UniformGrid::DIM, UniformGrid::DIM);
    printf("%6s %10s %10s %12s %14s\n",
           "N", "쓰는 셀", "최대 셀", "평균 셀 인원", "쌍 검사/틱");
    for (uint32_t n : {30u, 60u, 150u, 512u, 1024u}) {
        const SimConfig c = loadConfig(n);
        static World w;
        fill(&w, c, n, &sc, 100);
        separationRun(w, c, sc);

        uint32_t used = 0, maxCell = 0;
        for (uint32_t cell = 0; cell < UniformGrid::CELLS; ++cell) {
            const uint32_t k = sc.grid.end(cell) - sc.grid.begin(cell);
            if (k > 0) ++used;
            if (k > maxCell) maxCell = k;
        }
        // 3×3 이웃 안의 후보 수 합 = 분리가 실제로 보는 쌍 수
        int64_t pairs = 0;
        for (uint32_t i = 0; i < w.entities.count(); ++i) {
            if (w.entities.deadAt(i)) continue;
            const uint32_t cell = sc.grid.cellOf(i);
            const int32_t cx = static_cast<int32_t>(sc.grid.cellX(cell));
            const int32_t cy = static_cast<int32_t>(sc.grid.cellY(cell));
            for (int32_t oy = -1; oy <= 1; ++oy) {
                for (int32_t ox = -1; ox <= 1; ++ox) {
                    const int32_t nx = cx + ox, ny = cy + oy;
                    if (nx < 0 || ny < 0 || nx >= static_cast<int32_t>(UniformGrid::DIM)
                        || ny >= static_cast<int32_t>(UniformGrid::DIM)) continue;
                    const uint32_t nc = static_cast<uint32_t>(ny) * UniformGrid::DIM
                                      + static_cast<uint32_t>(nx);
                    pairs += sc.grid.end(nc) - sc.grid.begin(nc);
                }
            }
        }
        const uint32_t alive = aliveCount(w.entities);
        printf("%6u %10u %10u %12.1f %14lld\n", n, used, maxCell,
               used > 0 ? static_cast<double>(alive) / used : 0.0,
               static_cast<long long>(pairs));
    }
    printf("  ※ 격자 비우기는 **N과 무관하게 %u셀**이다 (counting sort의 상수항).\n",
           UniformGrid::CELLS);
    printf("     N이 작을 때는 이 상수가 분리 비용의 대부분이다 — 위 grid.build를 볼 것\n\n");

    // ── 분리 수렴 ───────────────────────────────────────────────────────
    //
    // 성능이 아니라 **시뮬 품질**이다. 분리는 틱당 한 패스만 돌고 그 사이
    // 이동이 다시 겹치게 만든다. 밀도가 오르면 완화가 생성을 못 따라간다.
    printf("== 분리 수렴 (목표 최소 간격 %.2f타일) ==\n",
           static_cast<double>(dev::data().cfg.separationMilli) / 1000.0);
    printf("%6s %12s %10s %12s %12s\n",
           "N", "실제 최소", "위반 쌍", "접촉(몹 사거리)", "광역 반경 안");
    for (uint32_t n : {30u, 60u, 150u, 512u, 1024u}) {
        const SimConfig c = loadConfig(n);
        static World w;
        fill(&w, c, n, &sc, 400);

        const int64_t minSep = (static_cast<int64_t>(c.separationMilli) * Fixed::ONE_RAW) / 1000;
        int64_t minD = minSep * 4;
        int32_t viol = 0, contactMob = 0, inAoe = 0;
        const uint32_t cnt = w.entities.count();
        for (uint32_t i = 0; i < cnt; ++i) {
            if (w.entities.deadAt(i)) continue;
            const int64_t d2 = distanceSq(w.entities.posX[i], w.entities.posY[i],
                                          w.hero.posX, w.hero.posY);
            const int64_t rm = w.entities.attackRange[i].raw;
            if (d2 <= rm * rm) ++contactMob;
            const int64_t ra = c.aoeRadius.raw;
            if (d2 <= ra * ra) ++inAoe;
            for (uint32_t j = i + 1; j < cnt; ++j) {
                if (w.entities.deadAt(j)) continue;
                const int64_t dx = static_cast<int64_t>(w.entities.posX[i].raw) - w.entities.posX[j].raw;
                const int64_t dy = static_cast<int64_t>(w.entities.posY[i].raw) - w.entities.posY[j].raw;
                const int64_t d = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(dx * dx + dy * dy)));
                if (d < minD) minD = d;
                if (d < minSep - 16) ++viol;        // 16 raw = 1/256타일, 절삭 여유
            }
        }
        printf("%6u %10.3f타일 %10d %12d %12d\n", n,
               static_cast<double>(minD) / Fixed::ONE_RAW, viol, contactMob, inAoe);
    }
    printf("  ※ **틱당 한 패스로는 밀도가 오를 때 수렴하지 않는다.** 분리가 밀어내는\n");
    printf("     양보다 이동이 다시 겹치게 만드는 양이 커진다. 패스를 늘리면 단조\n");
    printf("     개선된다 (실측: 4패스 0.89~1.42타일 · 16패스 1.17~1.50타일).\n");
    printf("     영웅 이격을 끄면 거의 달라지지 않으므로 **원인은 완화 속도다.**\n");
    printf("  ※ 그런데 **밸런스 가정 둘은 흔들리지 않는다.** 접촉(몹 사거리 — \n");
    printf("     SURROUND_COUNT=5의 대상)과 광역 반경 안 둘 다 N이 34배 늘어도\n");
    printf("     4~9에 머문다. 간격이 1.5가 아니라 0.2가 되어도 **영웅을 둘러싸는\n");
    printf("     수는 그 원의 기하가 정하기 때문**이다. 즉 이 미수렴은 연출 문제다 —\n");
    printf("     몹이 겹쳐 보인다. 밸런스 모델도 성능 예산도 건드리지 않는다\n\n");

    printf("전체: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
