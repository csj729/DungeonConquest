// 보스전 실측 하네스 — `tools/verify_recovery.py` 목표 6이 쓰는 상수를 만든다.
//
// ## 왜 도구로 만드는가
//
// BOSS_SEC · BOSS_ARRIVE · BOSS_INFLOW · BOSS_ORB · BOSS_LEECH ·
// BOSS_SURVIVE_MEASURED는 한 번 손으로 재서 파이썬에 박아 둔 값이었다.
// **보스 패턴 순환이 붙어 보스 화력이 2배가 됐는데도 run_all은 전부 초록불이었다** —
// 검사가 낡은 상수를 검사하고 있었기 때문이다. 이 프로젝트에서 반복된 고장 방식은
// 빨간불이 아니라 아무것도 안 보는 초록불이고, 재현 가능한 도구가 그 해독제다.
//
// ## 분해 방법 — 보스 등장 틱에 월드를 복제한다
//
// 잠식 수지는 유입 하나가 아니라 유입 − (구슬 + 흡혈 + 페이즈2)다. 코어에 계측
// 훅을 박지 않고 이걸 가르는 방법은 **같은 판을 조건만 바꿔 여러 벌 돌리는 것**이다.
//
//   A 기준        네 갈래의 기준선
//   B 흡혈 0      A와의 차 = 흡혈 회복률
//   C 구슬 정화 0 A와의 차 = 구슬 회복률
//   D 셋 다 0     그 자체가 순수 유입 (페이즈2 정화까지 끈다)
//
// World는 trivially copyable이고 시뮬은 결정론이므로 **복제가 곧 같은 판**이다.
// 구슬은 드랍 확률이 아니라 **정화량을 0으로 둔다** — 굴림 횟수가 그대로라야
// 네 갈래의 난수 소비가 어긋나지 않는다(확률을 0으로 두면 판이 갈라진다).
//
// 수지는 네 갈래가 **모두 살아 있는 구간**에서만 잰다. D는 회복이 없어 가장 먼저
// 죽는데, 죽은 뒤 구간까지 포함하면 게이지 상한에 눌려 유입이 과소평가된다.
#include <cstdio>
#include <cstdlib>

#include "../include/dc/sim.h"
#include "card_policy.h"
#include "data_files.h"

using namespace dc;

constexpr int32_t MAX_RUN_TICKS = 30000;   // 25분. dc_montecarlo와 같은 상한

namespace {

// QTE를 어떻게 치는가. **밸런싱의 전제라서 인자로 뺀다.**
//
// 기존 하네스들(dc_montecarlo·dc_field)은 QTE 입력을 아예 넣지 않는다 = 항상 빗나감.
// 보스 패턴이 붙기 전에는 보스에 텔레그래프가 없어 차이가 없었지만, 이제 대곤봉
// 강타(사이클 피해의 45%)가 회피 가능해져 **무입력과 숙련 플레이의 간격이 곧
// 보스전 난이도의 폭**이 된다. 둘 다 재서 밴드가 그 사이에 있는지 본다.
bool g_qtePerfect = false;

// 카드·조합 입력을 넣고 한 틱 전진한다. dc_montecarlo의 런 루프와 같은 정책이라야
// 여기 수치가 그 하네스의 클리어율과 같은 판을 가리킨다.
void stepOnce(World& w, const SimConfig& cfg, dev::Policy policy, Rng& choiceRng,
              SimScratch& scratch) {
    // **완벽 구간에 들어온 첫 틱에 누른다.** 창이 열리자마자 누르면 판정이
    // Success로 강등되므로(qte.judge), 상한을 재려면 구간을 기다려야 한다.
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
    for (;;) {
        const int32_t r = dev::chooseCraft(policy, cfg, dev::data().meta, w.inventory, dev::data().table, choiceRng);
        if (r < 0) break;
        InputEvent ce;
        ce.tick  = w.tickCount();
        ce.kind  = InputKind::Craft;
        ce.value = static_cast<uint32_t>(r);
        if (!applyInput(w, cfg, dev::data().table, ce)) break;
    }
    stepWorld(w, cfg, dev::data().table, scratch);
}

enum Variant { A_BASE = 0, B_NO_LEECH, C_NO_ORB, D_INFLOW, VARIANTS };

struct Sample {
    bool    reached   = false;
    bool    cleared   = false;      // A 기준이 보스를 잡았는가
    bool    phase2    = false;      // A 기준이 페이즈 2에 들어갔는가
    double  phase2Sec = 0;          // 페이즈 2 진입 → 런 종료
    bool    clearedV[4]{};          // 갈래별 — "이 회복이 없었어도 잡았는가"
    double  arriveSec = 0;   // 보스 등장까지 걸린 초
    double  bossSec   = 0;   // 보스 등장 → 런 종료
    double  arrive    = 0;   // 등장 시 잠식 (raw가 아니라 정수 단위)
    double  net[VARIANTS]{}; // 공통 창에서의 잠식 수지 /초 (+ = 차오른다)
    bool    netValid  = false;
};

Sample measureOne(const SimConfig& cfg, uint64_t seed, dev::Policy policy) {
    Sample s;
    static SimScratch scratch;

    World w;
    initWorld(w, seed, dev::data().cfg, dev::data().table, dev::data().hero);
    Rng choiceRng = Rng::derive(seed, RngStream::Cards);

    // ── 1단계: 보스가 나올 때까지 평범하게 돌린다 ──
    while (w.tickCount() < MAX_RUN_TICKS && !w.run.over() && !w.run.bossSpawned) {
        stepOnce(w, cfg, policy, choiceRng, scratch);
    }
    if (!w.run.bossSpawned) return s;          // 게이지를 못 채우고 끝난 판

    s.reached   = true;
    s.arriveSec = static_cast<double>(w.tickCount()) / config::TICK_HZ;
    s.arrive    = static_cast<double>(w.hero.corruption.raw) / Fixed::ONE_RAW;
    const int32_t arriveTick = w.tickCount();
    const double  arriveRaw  = w.hero.corruption.raw;

    // ── 2단계: 네 갈래로 복제해 끝까지 돌린다 ──
    SimConfig vcfg[VARIANTS];
    for (int32_t v = 0; v < VARIANTS; ++v) vcfg[v] = cfg;
    // 구슬은 **양만** 0으로 — 드랍 굴림은 그대로 둔다
    vcfg[C_NO_ORB].orbTrashAmount = vcfg[C_NO_ORB].orbEliteAmount = 0;
    vcfg[D_INFLOW].orbTrashAmount = vcfg[D_INFLOW].orbEliteAmount = 0;
    vcfg[D_INFLOW].bossPhase2Purge = 0;

    World  wv[VARIANTS];
    Rng    rv[VARIANTS];
    for (int32_t v = 0; v < VARIANTS; ++v) {
        wv[v] = w;                 // trivially copyable — 같은 판이 네 벌
        rv[v] = choiceRng;
    }

    // **네 갈래를 나란히 한 틱씩 돌린다.** 따로 끝까지 돌린 뒤 창을 다시 재려면
    // 같은 구간을 두 번 시뮬해야 한다 — 나란히 돌리면 첫 갈래가 끝나는 틱이 곧
    // 공통 창이고, 그 자리에서 네 값을 한 번에 집을 수 있다.
    const uint32_t leechIdx = engraveIndex(EngraveId::Leech);
    int32_t endTickA    = MAX_RUN_TICKS;
    int32_t phase2Tick  = -1;       // A 기준이 HP 50%를 넘긴 틱
    int32_t window      = -1;
    double  corrAt[VARIANTS]{};

    for (int32_t t = wv[0].tickCount(); t < MAX_RUN_TICKS; ++t) {
        int32_t alive = 0;
        for (int32_t v = 0; v < VARIANTS; ++v) {
            if (wv[v].run.over()) continue;
            if (v == B_NO_LEECH || v == D_INFLOW) wv[v].cards.engrave[leechIdx] = Fixed{};
            stepOnce(wv[v], vcfg[v], policy, rv[v], scratch);
            if (!wv[v].run.over()) ++alive;
            else if (v == A_BASE) endTickA = wv[v].tickCount();
            if (v == A_BASE && phase2Tick < 0 && wv[v].run.bossPhase2) {
                phase2Tick = wv[v].tickCount();
            }
        }
        if (window < 0 && alive < VARIANTS) {
            window = wv[0].tickCount();
            for (int32_t v = 0; v < VARIANTS; ++v) corrAt[v] = wv[v].hero.corruption.raw;
        }
        if (alive == 0) break;
    }
    if (window < 0) {                      // 넷 다 상한까지 살아남았다
        window = MAX_RUN_TICKS;
        for (int32_t v = 0; v < VARIANTS; ++v) corrAt[v] = wv[v].hero.corruption.raw;
    }

    s.bossSec = static_cast<double>(endTickA - arriveTick) / config::TICK_HZ;
    if (phase2Tick >= 0) {
        s.phase2    = true;
        s.phase2Sec = static_cast<double>(endTickA - phase2Tick) / config::TICK_HZ;
    }
    s.cleared = wv[A_BASE].run.outcome == RunOutcome::Cleared;
    for (int32_t v = 0; v < VARIANTS; ++v) {
        s.clearedV[v] = wv[v].run.outcome == RunOutcome::Cleared;
    }

    const int32_t span = window - arriveTick;
    if (span >= config::TICK_HZ) {         // 1초 미만이면 비율이 의미 없다
        const double sec = static_cast<double>(span) / config::TICK_HZ;
        for (int32_t v = 0; v < VARIANTS; ++v) {
            s.net[v] = (corrAt[v] - arriveRaw) / (static_cast<double>(Fixed::ONE_RAW) * sec);
        }
        s.netValid = true;
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const int32_t seeds = argc > 1 ? std::atoi(argv[1]) : 100;
    g_qtePerfect = argc > 2 && std::atoi(argv[2]) != 0;
    const SimConfig& cfg = dev::data().cfg;

    printf("보스전 실측 하네스 — 시드 %d × 정책 %d종 · QTE %s\n",
           seeds, static_cast<int32_t>(dev::Policy::Count),
           g_qtePerfect ? "항상 완벽(숙련 상한)" : "무입력(빗나감 하한)");
    printf("  패턴 %u종 · 사이클 ", cfg.bossPatternCount);
    int32_t cycle = 0;
    for (uint32_t k = 0; k < cfg.bossPatternCount; ++k) {
        cycle += cfg.bossPatterns[k].cooldownTicks + cfg.bossPatterns[k].windupTicks + 1;
    }
    printf("%d틱(%.1f초) · 피해 합 ", cycle, static_cast<double>(cycle) / config::TICK_HZ);
    int32_t dsum = 0;
    for (uint32_t k = 0; k < cfg.bossPatternCount; ++k) dsum += toInt(cfg.bossPatterns[k].damage);
    printf("%d (경감 전 %.1f/초)\n\n",
           dsum, static_cast<double>(dsum) * config::TICK_HZ / (cycle > 0 ? cycle : 1));

    int32_t runs = 0, reached = 0, cleared = 0, netRuns = 0;
    int32_t clearedV[VARIANTS] = {0, 0, 0, 0};
    int32_t phase2Runs = 0;
    double  phase2SecSum = 0;
    double arriveSum = 0, bossSecSum = 0, arriveSecSum = 0;
    double netSum[VARIANTS] = {0, 0, 0, 0};

    printf("%10s %8s %9s %10s %10s %10s\n",
           "정책", "도달", "생존", "도달 잠식", "보스전 초", "유입/초");
    for (int32_t p = 0; p < static_cast<int32_t>(dev::Policy::Count); ++p) {
        const dev::Policy policy = static_cast<dev::Policy>(p);
        int32_t pr = 0, pc = 0, pn = 0;
        double pa = 0, ps = 0, pin = 0;
        for (int32_t i = 0; i < seeds; ++i) {
            const Sample s = measureOne(cfg, 20250918ull + static_cast<uint64_t>(i) * 7919ull,
                                        policy);
            ++runs;
            if (!s.reached) continue;
            ++reached; ++pr;
            arriveSum += s.arrive;  pa += s.arrive;
            bossSecSum += s.bossSec; ps += s.bossSec;
            arriveSecSum += s.arriveSec;
            if (s.cleared) { ++cleared; ++pc; }
            for (int32_t v = 0; v < VARIANTS; ++v) if (s.clearedV[v]) ++clearedV[v];
            if (s.phase2) { ++phase2Runs; phase2SecSum += s.phase2Sec; }
            if (s.netValid) {
                ++netRuns; ++pn;
                for (int32_t v = 0; v < VARIANTS; ++v) netSum[v] += s.net[v];
                pin += s.net[D_INFLOW];
            }
        }
        printf("%10s %7.0f%% %8.1f%% %10.0f %10.1f %10.1f\n",
               dev::policyName(policy),
               pr * 100.0 / seeds, pr ? pc * 100.0 / pr : 0.0,
               pr ? pa / pr : 0.0, pr ? ps / pr : 0.0, pn ? pin / pn : 0.0);
    }

    if (reached == 0 || netRuns == 0) {
        printf("\n보스에 도달한 판이 없다 — 측정 불가\n");
        return 1;
    }

    const double bossSec = bossSecSum / reached;
    const double arrive  = arriveSum / reached;
    const double survive = static_cast<double>(cleared) / reached;
    const double netA    = netSum[A_BASE]    / netRuns;
    const double inflow  = netSum[D_INFLOW]  / netRuns;
    const double leech   = netSum[B_NO_LEECH] / netRuns - netA;
    const double orb     = netSum[C_NO_ORB]   / netRuns - netA;
    const double ph2     = static_cast<double>(cfg.bossPhase2Purge) / bossSec;

    printf("\n== 분해 (공통 창 %d판) ==\n", netRuns);
    printf("  A 기준        %7.1f/초\n", netA);
    printf("  D 순수 유입   %7.1f/초\n", inflow);
    printf("  구슬 (C−A)    %7.1f/초\n", orb);
    printf("  흡혈 (B−A)    %7.1f/초\n", leech);
    printf("  페이즈2       %7.1f/초  (%d 1회 ÷ %.0f초)\n",
           ph2, cfg.bossPhase2Purge, bossSec);
    printf("  검산: 유입 − (구슬+흡혈+페이즈2) = %.1f  vs  A %.1f\n",
           inflow - (orb + leech + ph2), netA);
    printf("  ※ 차가 남는 것은 정화가 0에서 잘리기 때문이다 — 잠식이 0인 구간의\n");
    printf("     회복은 버려지므로 분해가 완전 가법이 아니다\n");

    printf("\n== 갈래별 도달 후 생존 (%d판 도달 · 도달까지 평균 %.0f초) ==\n",
           reached, arriveSecSum / reached);
    printf("  A 기준         %5.1f%%\n", clearedV[A_BASE]     * 100.0 / reached);
    printf("  B 흡혈 없음    %5.1f%%   <- 회복 카드가 보스전에서 실제로 일하는가\n",
           clearedV[B_NO_LEECH] * 100.0 / reached);
    printf("  C 구슬 없음    %5.1f%%\n", clearedV[C_NO_ORB]   * 100.0 / reached);
    printf("  D 회복 전무    %5.1f%%   <- 기저만으로는 못 버티는가\n",
           clearedV[D_INFLOW]   * 100.0 / reached);

    printf("\n== 페이즈 2 ==\n");
    printf("  진입 %5.1f%% (도달 판 기준) · 진입 후 평균 %.1f초\n",
           phase2Runs * 100.0 / reached,
           phase2Runs ? phase2SecSum / phase2Runs : 0.0);
    printf("  ※ 보스전 전체 %.1f초 중 뒷부분이다 — 페이즈 2 가산 수치는 이 길이로 역산한다\n",
           bossSec);

    printf("\n== tools/verify_recovery.py 에 넣을 값 ==\n");
    printf("BOSS_SEC        = %.1f\n", bossSec);
    printf("BOSS_INFLOW     = %.1f\n", inflow);
    printf("BOSS_ORB        = %.1f\n", orb);
    printf("BOSS_LEECH      = %.1f\n", leech);
    printf("BOSS_ARRIVE     = %.1f\n", arrive);
    printf("BOSS_SURVIVE_%-9s = %.3f   # 도달 %d/%d판\n",
           g_qtePerfect ? "SKILLED" : "NOINPUT", survive, reached, runs);
    if (g_qtePerfect) {
        printf("BOSS_SURVIVE_NOLEECH  = %.3f\n", clearedV[B_NO_LEECH] * 1.0 / reached);
        printf("BOSS_SURVIVE_NOORB    = %.3f\n", clearedV[C_NO_ORB]   * 1.0 / reached);
        printf("BOSS_SURVIVE_BARE     = %.3f\n", clearedV[D_INFLOW]   * 1.0 / reached);
    }
    return 0;
}
