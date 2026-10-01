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
//
// ## QTE 플레이가 인자인 이유 (세 번째 인자, 기본 0)
//
// 보스 패턴 순환이 붙으면서 **대곤봉 강타가 회피 가능한 피해**가 됐다 —
// 사이클 피해의 45%다. 그래서 "QTE를 아예 안 치는 하네스"의 클리어율은 이제
// 게임의 난이도가 아니라 **하한**이다(실측 12% 대 35%). 둘 다 볼 수 있게 열어 둔다.
// 자세한 분해는 `core/tools/dc_boss`가 한다.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "../include/dc/sim.h"
#include "card_policy.h"
#include "data_files.h"

using namespace dc;

constexpr int32_t MAX_RUN_TICKS = 30000;   // 25분. 넘으면 미완으로 센다

// QTE를 치는가. 세 번째 인자로 켠다 — 파일 머리의 설명 참조.
static bool g_qtePerfect = false;

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
    initWorld(w, seed, dev::data().cfg, dev::data().table, dev::data().hero);
    static SimScratch scratch;
    Rng choiceRng = Rng::derive(seed, RngStream::Cards);

    RunResult r;
    for (int32_t t = 0; t < MAX_RUN_TICKS && !w.run.over(); ++t) {
        // **완벽 구간에 들어온 첫 틱에 누른다.** 창이 열리자마자 누르면 판정이
        // Success로 강등된다 (QteWindow::judge).
        if (g_qtePerfect && w.hero.qte.open() && !w.hero.qte.hasInput
            && w.tickCount() >= w.hero.qte.perfectFrom
            && w.tickCount() <= w.hero.qte.perfectTo) {
            InputEvent qe;
            qe.tick  = w.tickCount();
            qe.kind  = InputKind::QteGrade;
            qe.value = static_cast<uint32_t>(QteGrade::Perfect);
            (void)applyInput(w, cfg, dev::data().table, qe);
        }
        if (w.cards.offer.open()) {
            InputEvent e;
            e.tick  = w.tickCount();
            e.kind  = InputKind::CardChoice;
            e.value = dev::choose(policy, cfg, dev::data().meta, w.cards.offer, choiceRng);
            (void)applyInput(w, cfg, dev::data().table, e);
        }
        // **조합은 언제든** (§5). 플레이어가 상시 인벤토리를 보고 있다고 보고,
        // 만들 수 있으면 바로 만든다 — 재료를 쌓아둘 이유가 없다(사다리가 항상 이득).
        for (;;) {
            const int32_t r = dev::chooseCraft(policy, cfg, dev::data().meta, w.inventory, dev::data().table,
                                               choiceRng);
            if (r < 0) break;
            InputEvent ce;
            ce.tick  = w.tickCount();
            ce.kind  = InputKind::Craft;
            ce.value = static_cast<uint32_t>(r);
            if (!applyInput(w, cfg, dev::data().table, ce)) break;
        }

        stepWorld(w, cfg, dev::data().table, scratch);

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
    g_qtePerfect = argc > 3 && std::atoi(argv[3]) != 0;
    const SimConfig& cfg = dev::data().cfg;

    printf("몬테카를로 밸런싱 하네스 — 정책 %d종 × %d판, 체크포인트 %d틱(%.0f초)\n",
           static_cast<int32_t>(dev::Policy::Count), runs,
           checkpointTick, static_cast<double>(checkpointTick) / cfg.tickHz);
    printf("QTE %s\n", g_qtePerfect ? "항상 완벽 (숙련 상한)" : "무입력 (빗나감 하한)");
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
        printf("  상위-하위 격차 %.1f%%p — 벌어지면 지배 빌드가 있다는 뜻이다\n",
               (hi - lo) * 100.0);

        // **최고 빌드 클리어율 목표 — 기준을 함께 적는다.**
        //
        // 한때 25~35%로 뒀는데, 그 값은 **QTE 입력이 아예 없던 하네스**에서 28.2%가
        // 나올 때 정한 것이다. 보스 패턴이 붙어 대곤봉 강타(사이클 피해의 45%)가
        // 회피 가능해지자 같은 게임이 무입력 5% · 숙련 23%로 갈렸다 — 목표가
        // 틀린 게 아니라 **어느 플레이를 재는지가 빠져 있었다.**
        //
        // 그래서 밴드를 숙련(QTE 완벽) 기준으로 다시 세운다. 무입력 쪽은 하한이라
        // 밴드를 걸지 않는다 — 거기에 걸면 QTE를 무의미하게 만드는 방향으로 끌린다.
        //
        // **여기서 재고 여기서 판정한다.** 파이썬 도구로 옮겨 적으면 수치가 낡아도
        // 초록불이 뜬다 — verify_recovery의 보스 상수가 실제로 그렇게 낡았었다.
        const double lo_t = g_qtePerfect ? 0.20 : 0.03;
        const double hi_t = g_qtePerfect ? 0.30 : 0.10;
        const bool   good = hi >= lo_t && hi <= hi_t;
        printf("  최고 빌드 %.1f%% (목표 %.0f~%.0f%% · %s 기준)  %s\n\n",
               hi * 100.0, lo_t * 100.0, hi_t * 100.0,
               g_qtePerfect ? "숙련" : "무입력", good ? "PASS" : "FAIL");
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
    printf("  ※ **이 상관은 아직 교란돼 있다.** 전설 풀 9칸 중 효과가 붙은 것은\n");
    printf("     전설 유물 3종(RL_ECHO·RL_STORM·RL_FORGE)뿐이고 고유 각인 6칸은 미구현이다.\n");
    printf("     게다가 오래 버틴 판이 레벨을 더 올려 전설을 더 뽑으므로 **역인과**가\n");
    printf("     섞인다 — 위 숫자를 전설의 힘으로 읽으면 안 된다. 풀이 다 차고 나서\n");
    printf("     레벨을 통제해 다시 재야 의미가 생긴다\n\n");

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
        // **중앙값 분할은 동점에 무너진다.** 초반 공격력은 카드 선택 수가 적어
        // 같은 값이 몰리는데, `v >= median`으로 가르면 전부 상위로 몰려 하위가
        // 0판이 된다(실제로 그랬다). 3분위 경계를 쓰면 동점이 있어도 양쪽에 표본이 남는다.
        const int32_t loCut = vals[m / 3];
        const int32_t hiCut = vals[(m * 2) / 3];

        int32_t hiN = 0, hiC = 0, loN = 0, loC = 0;
        for (int32_t p = 0; p < static_cast<int32_t>(dev::Policy::Count); ++p) {
            for (int32_t i = 0; i < n; ++i) {
                if (!all[p][i].reachedCheckpoint) continue;
                const int32_t v = metric == 0 ? all[p][i].earlyPower : all[p][i].earlyRisk;
                const bool cleared = all[p][i].outcome == RunOutcome::Cleared;
                if (v >= hiCut)      { ++hiN; if (cleared) ++hiC; }
                else if (v <= loCut) { ++loN; if (cleared) ++loC; }
            }
        }
        if (hiN == 0 || loN == 0) {
            printf("  %-10s 표본이 한쪽에 몰려 있다 (상위 %d · 하위 %d) — 판정 보류\n",
                   metric == 0 ? "공격력" : "잠식비율", hiN, loN);
            continue;
        }
        const double hiR = hiN ? static_cast<double>(hiC) * 100.0 / hiN : 0.0;
        const double loR = loN ? static_cast<double>(loC) * 100.0 / loN : 0.0;
        printf("  %-10s 3분위 %d~%d | 상위 %5.1f%% (%d판) · 하위 %5.1f%% (%d판) "
               "· 격차 %5.1f%%p\n",
               metric == 0 ? "공격력" : "잠식비율", loCut, hiCut, hiR, hiN, loR, loN,
               hiR - loR);
    }
    printf("  ※ 격차가 클수록 초반 정보로 결과를 예측할 수 있다 = 리셋 유인이 크다\n");
    return 0;
}
