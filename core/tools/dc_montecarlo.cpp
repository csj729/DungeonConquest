// 몬테카를로 밸런싱 하네스 (§14-11).
//
// 헤드리스로 N판을 돌려 **세 지표**를 집계한다.
//
//   1. 전설 획득 횟수별 클리어율 — 천장이 없으므로 이 지표가 전설의 파워 상한을 정한다
//   2. 빌드별 클리어율 분포 — 상위 빌드와 중앙값의 격차. 벌어지면 지배 빌드가 있다
//   3. 초반 상태 → 최종 클리어율 상관도 — **리셋 유인을 직접 재는 지표다.**
//      초반 정보로 결과를 예측할 수 있을수록 플레이어는 초반에 리셋한다
//
// ## 지금 읽을 수 있는 것과 없는 것
//
// 각인·유물의 개별 효과가 아직 구현되지 않았으므로(레벨업 카드 단계의 명시적 범위),
// **지표 1은 지금 음성 대조군이다** — 전설에 효과가 없으니 상관이 0이어야 맞다.
// 0이 아니면 어딘가 새고 있다는 뜻이다. 효과가 붙으면 같은 코드가 본래 의미를 갖는다.
//
// 지표 2는 일반 등급 스탯 카드 축(화력 vs 생존)에서 **지금도 실제로 갈린다.**
// 지표 3은 지금도 완전히 의미가 있다 — 측정 대상이 초반 운이기 때문이다.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "../include/dc/sim.h"
#include "card_policy.h"
#include "dev_data.h"

using namespace dc;

constexpr int32_t MAX_RUN_TICKS = 30000;   // 25분. 넘으면 미완으로 센다

struct RunResult {
    RunOutcome outcome     = RunOutcome::Running;
    int32_t    endTick     = 0;
    int32_t    clearPoints = 0;
    int32_t    level       = 0;
    int32_t    legends     = 0;
    // 초반 스냅샷 (지표 3) — 체크포인트 시점의 성장·위험도
    int32_t    earlyPower  = 0;   // 공격력 raw
    int32_t    earlyRisk   = 0;   // 잠식 비율 permille
    bool       reachedCheckpoint = false;
};

static RunResult runOnce(const SimConfig& cfg, uint64_t seed, dev::Policy policy,
                         int32_t checkpointTick) {
    World w;
    w.init(seed);
    dev::applyHeroBaseline(w);
    static SimScratch scratch;
    Rng choiceRng = Rng::derive(seed, RngStream::Cards);

    RunResult r;
    for (int32_t t = 0; t < MAX_RUN_TICKS && !w.run.over(); ++t) {
        if (w.cards.offer.open()) {
            InputEvent e;
            e.tick  = w.tickCount();
            e.kind  = InputKind::CardChoice;
            e.value = dev::choose(policy, w.cards.offer, choiceRng);
            (void)applyInput(w, cfg, e);
        }
        stepWorld(w, cfg, scratch);

        if (!r.reachedCheckpoint && w.tickCount() >= checkpointTick) {
            r.reachedCheckpoint = true;
            r.earlyPower = w.hero.stats.value(Stat::AttackPower).raw;
            r.earlyRisk  = w.hero.corruptionPermille();
        }
    }
    r.outcome     = w.run.outcome;
    r.endTick     = w.run.over() ? w.run.endTick : MAX_RUN_TICKS;
    r.clearPoints = w.run.clearPoints;
    r.level       = w.hero.level;
    for (uint32_t i = 0; i < cfg.legendPoolSize; ++i) if (w.cards.legendHas(i)) ++r.legends;
    return r;
}

static double clearRate(const RunResult* r, int32_t n) {
    if (n <= 0) return 0.0;
    int32_t c = 0;
    for (int32_t i = 0; i < n; ++i) if (r[i].outcome == RunOutcome::Cleared) ++c;
    return static_cast<double>(c) / n;
}

int main(int argc, char** argv) {
    const int32_t runs = argc > 1 ? std::atoi(argv[1]) : 400;
    const int32_t checkpointTick = argc > 2 ? std::atoi(argv[2]) : 3000;   // 150초
    const SimConfig cfg = dev::devConfig();

    printf("몬테카를로 밸런싱 하네스 — 정책 %d종 × %d판, 체크포인트 %d틱(%.0f초)\n",
           static_cast<int32_t>(dev::Policy::Count), runs,
           checkpointTick, static_cast<double>(checkpointTick) / cfg.tickHz);
    printf("클리어 목표 %d점 · 런 상한 %d틱\n\n", cfg.clearTargetPoints, MAX_RUN_TICKS);

    static RunResult all[static_cast<int32_t>(dev::Policy::Count)][4096];
    const int32_t n = runs < 4096 ? runs : 4096;

    // ── 지표 2: 빌드별 클리어율 분포 ──
    printf("== 지표 2: 빌드별 클리어율 ==\n");
    printf("%10s %10s %10s %12s %10s %10s\n",
           "정책", "클리어율", "사망률", "평균 게이지", "평균 레벨", "평균 초");
    double rate[static_cast<int32_t>(dev::Policy::Count)];
    for (int32_t p = 0; p < static_cast<int32_t>(dev::Policy::Count); ++p) {
        const dev::Policy pol = static_cast<dev::Policy>(p);
        int64_t pts = 0, lv = 0, ticks = 0;
        int32_t dead = 0;
        for (int32_t i = 0; i < n; ++i) {
            all[p][i] = runOnce(cfg, 0x5EED0000ull + static_cast<uint64_t>(i), pol, checkpointTick);
            pts   += all[p][i].clearPoints;
            lv    += all[p][i].level;
            ticks += all[p][i].endTick;
            if (all[p][i].outcome == RunOutcome::Dead) ++dead;
        }
        rate[p] = clearRate(all[p], n);
        printf("%10s %9.1f%% %9.1f%% %12.1f %10.1f %10.1f\n",
               dev::policyName(pol), rate[p] * 100.0,
               static_cast<double>(dead) * 100.0 / n,
               static_cast<double>(pts) / n, static_cast<double>(lv) / n,
               static_cast<double>(ticks) / n / cfg.tickHz);
    }
    {
        double hi = rate[0], lo = rate[0];
        for (int32_t p = 1; p < static_cast<int32_t>(dev::Policy::Count); ++p) {
            if (rate[p] > hi) hi = rate[p];
            if (rate[p] < lo) lo = rate[p];
        }
        printf("  상위-하위 격차 %.1f%%p — 벌어지면 지배 빌드가 있다는 뜻이다\n\n",
               (hi - lo) * 100.0);
    }

    // ── 지표 1: 전설 획득 횟수별 클리어율 ──
    printf("== 지표 1: 전설 획득 횟수별 클리어율 (전 정책 합산) ==\n");
    printf("%8s %8s %12s\n", "전설", "판수", "클리어율");
    for (int32_t k = 0; k <= 5; ++k) {
        int32_t cnt = 0, cleared = 0;
        for (int32_t p = 0; p < static_cast<int32_t>(dev::Policy::Count); ++p) {
            for (int32_t i = 0; i < n; ++i) {
                const int32_t g = all[p][i].legends;
                if (g != k && !(k == 5 && g >= 5)) continue;
                ++cnt;
                if (all[p][i].outcome == RunOutcome::Cleared) ++cleared;
            }
        }
        if (cnt == 0) continue;
        printf("%7d%s %8d %11.1f%%\n", k, k == 5 ? "+" : " ", cnt,
               static_cast<double>(cleared) * 100.0 / cnt);
    }
    printf("  ※ 각인·유물 효과가 미구현이라 지금은 **음성 대조군**이다 —\n");
    printf("     전설에 효과가 없으므로 상관이 없어야 맞다\n\n");

    // ── 지표 3: 초반 상태 → 최종 클리어율 상관도 (리셋 유인) ──
    printf("== 지표 3: 초반 %.0f초 상태 → 최종 클리어율 (리셋 유인) ==\n",
           static_cast<double>(checkpointTick) / cfg.tickHz);
    for (int32_t metric = 0; metric < 2; ++metric) {
        // 체크포인트에 도달한 판만 모아 지표 중앙값으로 두 집단을 가른다.
        static int32_t vals[16384];
        int32_t m = 0;
        for (int32_t p = 0; p < static_cast<int32_t>(dev::Policy::Count); ++p) {
            for (int32_t i = 0; i < n; ++i) {
                if (!all[p][i].reachedCheckpoint) continue;
                vals[m++] = metric == 0 ? all[p][i].earlyPower : all[p][i].earlyRisk;
            }
        }
        if (m == 0) { printf("  체크포인트 도달 판이 없다\n"); break; }
        // 중앙값 — 삽입 정렬로 충분하다 (개발 도구다)
        for (int32_t i = 1; i < m; ++i) {
            const int32_t v = vals[i];
            int32_t j = i - 1;
            while (j >= 0 && vals[j] > v) { vals[j + 1] = vals[j]; --j; }
            vals[j + 1] = v;
        }
        const int32_t median = vals[m / 2];

        int32_t hiN = 0, hiC = 0, loN = 0, loC = 0;
        for (int32_t p = 0; p < static_cast<int32_t>(dev::Policy::Count); ++p) {
            for (int32_t i = 0; i < n; ++i) {
                if (!all[p][i].reachedCheckpoint) continue;
                const int32_t v = metric == 0 ? all[p][i].earlyPower : all[p][i].earlyRisk;
                const bool cleared = all[p][i].outcome == RunOutcome::Cleared;
                if (v >= median) { ++hiN; if (cleared) ++hiC; }
                else             { ++loN; if (cleared) ++loC; }
            }
        }
        const double hiR = hiN ? static_cast<double>(hiC) * 100.0 / hiN : 0.0;
        const double loR = loN ? static_cast<double>(loC) * 100.0 / loN : 0.0;
        printf("  %-10s 중앙값 %7d | 상위 %5.1f%% (%d판) · 하위 %5.1f%% (%d판) "
               "· 격차 %5.1f%%p\n",
               metric == 0 ? "공격력" : "잠식비율", median, hiR, hiN, loR, loN,
               hiR - loR);
    }
    printf("  ※ 격차가 클수록 초반 정보로 결과를 예측할 수 있다 = 리셋 유인이 크다\n");
    return 0;
}
