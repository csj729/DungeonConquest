// 전설 각인별 실측 하네스 — **클리어율을 "도달 × 생존"으로 분해한다.**
//
// 전설 9칸의 수치는 `verify_card_values.py`가 **DPS 예산**으로 검산한다. 그런데
// 실측하면 예산비가 95~100%로 같은 각인들이 클리어율에서는 전혀 다르게 나온다.
// 두 축이 다른 이야기를 하고 있고, 그 격차를 재는 것이 이 도구다.
//
// ## 왜 분해해야 하는가
//
// 클리어는 관문이 둘이다 — 게이지를 채워 보스에 **도달**하고, 그 보스를 **잡는다.**
// 잠식 유입이 살아 있는 잡몹 수에 비례하므로(§2) 두 관문이 서로 다른 효과에
// 반응한다. 합계만 보면 어느 관문이 움직였는지 알 수 없다.
//
// ## 도달률이 오르면 생존율이 내려간다 — 모집단 효과다
//
// **"도달 후 생존"을 품질로 읽으면 안 된다.** 도달률이 오르면 전에는 보스 전에
// 죽던 약한 빌드가 보스전 모집단에 섞여 생존율을 끌어내린다.
// `verify_recovery.py`가 "게임이 나빠진 게 아니라 **모집단이 바뀐 것**이었다"로
// 적어 둔 함정과 같은 자리다. 그래서 `도달 시 잠식` 열을 같이 낸다 — 도달한
// 판들이 평균적으로 얼마나 성한 상태로 도달했는지가 그 교란의 크기다.
//
// 판정에 쓸 단일 지표는 **클리어율**(둘의 곱)이고, 분해는 *왜*를 설명한다.
//
// ## 비교가 깨끗한 범위
//
// 전설 하나를 틱 0에 쥐여 준다. 추첨은 생존 목록을 세어 `rngCards.range(n)`를
// **한 번** 부르므로(levelup.h), 전설 하나를 쥔 **6종끼리는 n이 같아 난수 소비가
// 동일하다** — 서로 비교가 깨끗하다. "없음"은 n이 9라 소비가 어긋나므로
// **느슨한 기준선**으로만 읽는다.
//
// 표본: 클리어율 p≈0.15에서 n=1000이면 표준오차 1.1%p, 차이의 3σ가 4.8%p다.
// 그 아래 차이는 결론이 아니다 — 한때 n=60에서 18.3% vs 13.3%를 보고 "전설이
// 판을 나쁘게 만든다"로 읽었는데 n=200에서 전부 사라졌다.
//
//   ./build/release/core/dc_legend [시드 수]
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../include/dc/sim.h"
#include "data_files.h"

using namespace dc;

struct Row {
    double  sec = 0;
    int32_t reached = 0, cleared = 0, trash = 0, elites = 0;
    long    ccElite = 0, ccTrash = 0;
    int32_t arriveCorruption = -1;   // 보스 등장 틱의 잠식. 도달하지 않으면 -1
};

// QTE는 항상 완벽, 카드는 무작위 — `dc_montecarlo`의 무입력 정책과 달리
// **숙련 플레이**다. 전설의 값어치를 재는 데는 QTE를 치는 쪽이 기준이어야 한다
// (못 치는 플레이에서는 보스 패턴 피해가 결과를 덮어버린다).
static Row runOne(uint64_t seed, int32_t legend) {
    const SimConfig& cfg = dev::data().cfg;
    World w;
    initWorld(w, seed, cfg, dev::data().table, dev::data().hero);
    if (legend >= 0) w.cards.legendTake(static_cast<uint32_t>(legend));

    static SimScratch sc;
    Row r;
    for (int32_t t = 0; t < 30000 && !w.run.over(); ++t) {
        if (w.hero.qte.open() && w.tickCount() == w.hero.qte.perfectTo) {
            InputEvent e;
            e.tick  = w.tickCount();
            e.kind  = InputKind::QteGrade;
            e.value = static_cast<int32_t>(QteGrade::Perfect);
            (void)w.applyInput(e);
        }
        if (w.cards.offer.open()) {
            InputEvent e;
            e.tick  = w.tickCount();
            e.kind  = InputKind::CardChoice;
            e.value = static_cast<int32_t>(w.rngEvents.range(w.cards.offer.count));
            (void)applyInput(w, cfg, dev::data().table, e);
        }
        const bool wasSpawned = w.run.bossSpawned;
        stepWorld(w, cfg, dev::data().table, sc);
        if (!wasSpawned && w.run.bossSpawned) r.arriveCorruption = w.hero.corruption.raw;

        for (uint32_t i = 0; i < w.entities.count(); ++i) {
            if (w.entities.deadAt(i) || w.entities.groggyLeft[i] <= 0) continue;
            if (w.entities.archetype[i] == Archetype::Trash) ++r.ccTrash;
            else ++r.ccElite;
        }
    }
    r.sec     = static_cast<double>(w.tickCount()) / cfg.tickHz;
    r.reached = w.run.bossSpawned ? 1 : 0;
    r.cleared = w.run.outcome == RunOutcome::Cleared ? 1 : 0;
    r.trash   = w.run.killedTrash;
    r.elites  = w.run.killedElite;
    return r;
}

int main(int argc, char** argv) {
    const int32_t N = argc > 1 ? std::atoi(argv[1]) : 1000;
    const SimConfig& cfg = dev::data().cfg;

    // 전설 풀 인덱스. 유물 3칸(0~2)은 이미 다른 도구가 재므로 고유 각인만 본다.
    struct Variant { const char* name; int32_t legend; };
    const Variant kVariants[] = {
        {"없음(기준)",  -1},
        {"처형",         static_cast<int32_t>(legendIndexOf(LegendId::Execute))},
        {"충격파",       static_cast<int32_t>(legendIndexOf(LegendId::Shockwave))},
        {"소용돌이",     static_cast<int32_t>(legendIndexOf(LegendId::Vortex))},
        {"원심력",       static_cast<int32_t>(legendIndexOf(LegendId::Centrifuge))},
        {"여진",         static_cast<int32_t>(legendIndexOf(LegendId::Aftershock))},
        {"균열",         static_cast<int32_t>(legendIndexOf(LegendId::Fissure))},
    };

    printf("=== 고유 각인별 실측 (시드 %d · QTE 항상 완벽) ===\n", N);
    printf("클리어 = 도달 × 도달 후 생존. **도달 후 생존은 품질이 아니다** —\n");
    printf("도달률이 오르면 약한 빌드가 보스전 모집단에 섞여 내려간다.\n\n");
    printf("%-12s %8s %11s %10s %10s %11s %10s %10s\n",
           "전설", "도달률", "도달후생존", "클리어율", "Δ클리어",
           "도달시잠식", "잡몹처치", "기절틱(E)");
    printf("  %s\n", "-------------------------------------------------------"
                     "--------------------------------");

    const double sed = 100.0 * std::sqrt(2 * 0.15 * 0.85 / N);   // 차이의 표준오차
    double baseClear = 0;
    bool ok = true;
    for (const Variant& v : kVariants) {
        double sec = 0;
        long tr = 0, el = 0, ce = 0, arrive = 0;
        int32_t re = 0, cl = 0, n = 0, arriveN = 0;
        for (int32_t s = 1; s <= N; ++s) {
            const Row r = runOne(static_cast<uint64_t>(s) * 7919ull, v.legend);
            sec += r.sec; re += r.reached; cl += r.cleared;
            tr += r.trash; el += r.elites; ce += r.ccElite; ++n;
            if (r.arriveCorruption >= 0) { arrive += r.arriveCorruption; ++arriveN; }
        }
        const double clear = 100.0 * cl / n;
        if (v.legend < 0) baseClear = clear;
        const bool sig = v.legend >= 0 && std::fabs(clear - baseClear) > 3 * sed;
        printf("%-12s %7.1f%% %10.1f%% %9.1f%% %+8.1f%%p%s %9.0f %10.1f %10.0f\n",
               v.name, 100.0 * re / n, re > 0 ? 100.0 * cl / re : 0.0, clear,
               v.legend < 0 ? 0.0 : clear - baseClear, sig ? " *" : "  ",
               arriveN > 0 ? static_cast<double>(arrive) / arriveN / Fixed::ONE_RAW : 0.0,
               static_cast<double>(tr) / n, static_cast<double>(ce) / n);
        (void)el;
    }
    printf("\n  * = 기준선과의 차이가 3σ(%.1f%%p)를 넘는다. 그 아래는 결론이 아니다.\n", 3 * sed);
    printf("  전설 풀 %u칸 중 고유 각인 6칸만 본다 — 유물 3종은 dc_boss·dc_montecarlo 몫이다.\n",
           cfg.legendPoolSize);
    return ok ? 0 : 1;
}
