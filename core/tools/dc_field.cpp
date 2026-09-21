#include <initializer_list>
// 전장 구성 측정 — `balance_baseline.py`의 **미검증 모델 가정 3개를 실측으로 대체한다.**
//
//     SURROUND_COUNT   = 5     영웅에게 동시에 붙을 수 있는 몹 수 가정
//     AOE_TARGET_SHARE = 0.13  광역기 1회가 때리는 비율 가정
//     APPROACH_SEC     = 8     스폰에서 접촉까지 걸리는 시간 가정
//
// 셋 다 **영웅 이동 AI가 정하는 값**이다. 닫힌 식으로는 알 수 없으므로 실제
// 시뮬을 돌려 센다. 이동 파라미터를 바꾸면 여기 숫자가 바로 움직인다.
#include <cstdio>
#include <cstdlib>

#include "../include/dc/sim.h"
#include "dev_data.h"

using namespace dc;

struct FieldStats {
    double meanContact  = 0;   // 사거리 안에 있는 몹 수 (SURROUND_COUNT)
    int32_t maxContact  = 0;
    double meanMelee    = 0;   // 근접 중 사거리 안
    double meanAlive    = 0;
    double aoeShare     = 0;   // 광역 반경 안 비율 (AOE_TARGET_SHARE)
    double meanApproach = 0;   // 스폰 → 첫 접촉까지 초 (APPROACH_SEC)
    double corruption   = 0;   // 100틱당 잠식 증가 raw
};

static FieldStats measure(const SimConfig& cfg, Fixed moveSpeed,
                          int32_t, int32_t, int32_t ticks, uint64_t seed) {
    SimConfig c = cfg;

    World w;
    w.init(seed);
    dev::applyHeroBaseline(w);
    w.hero.stats.setBase(Stat::MoveSpeed, moveSpeed);

    const Fixed aoeRadius = Fixed(3);        // §9 광역기 기준 반경
    int64_t contactSum = 0, aliveSum = 0, aoeSum = 0;
    int64_t meleeSum = 0;
    int32_t maxContact = 0, samples = 0;
    int64_t approachTickSum = 0, approachCount = 0;
    const int32_t startCorruption = w.hero.corruption.raw;
    static uint32_t seenGen[config::MAX_ENTITIES];
    for (uint32_t i = 0; i < config::MAX_ENTITIES; ++i) seenGen[i] = 0;

    static SimScratch scratch;
    for (int32_t t = 0; t < ticks; ++t) {
        stepWorld(w, c, scratch);
        if (t < ticks / 4) continue;          // 초반 과도구간은 버린다

        int32_t contact = 0, alive = 0, inAoe = 0;
        int32_t melee = 0;
        for (uint32_t i = 0; i < w.entities.count(); ++i) {
            if (w.entities.deadAt(i)) continue;
            ++alive;
            const int64_t d2 = distanceSq(w.entities.posX[i], w.entities.posY[i],
                                          w.hero.posX, w.hero.posY);
            const int64_t r  = w.entities.attackRange[i].raw;
            if (d2 <= r * r) {
                ++contact;
                if (w.entities.archetype[i] == Archetype::Trash) ++melee;
                // **첫 접촉만 센다.** 쿨다운 리셋으로 세면 생존 기간 평균이 나온다.
                const uint32_t slot = w.entities.idAt(i).index();
                const uint32_t gen  = w.entities.idAt(i).generation();
                if (seenGen[slot] != gen) {
                    seenGen[slot] = gen;
                    approachTickSum += (w.tickCount() - w.entities.spawnTick[i]);
                    ++approachCount;
                }
            }
            if (d2 <= static_cast<int64_t>(aoeRadius.raw) * aoeRadius.raw) ++inAoe;
        }
        contactSum += contact;
        meleeSum   += melee;
        aliveSum   += alive;
        aoeSum     += inAoe;
        if (contact > maxContact) maxContact = contact;
        ++samples;
    }

    FieldStats s;
    if (samples > 0) {
        s.meanContact = static_cast<double>(contactSum) / samples;
        s.meanAlive   = static_cast<double>(aliveSum) / samples;
        s.aoeShare    = s.meanAlive > 0 ? (static_cast<double>(aoeSum) / samples) / s.meanAlive : 0;
        s.meanMelee   = static_cast<double>(meleeSum) / samples;
    }
    s.maxContact = maxContact;
    if (approachCount > 0) {
        s.meanApproach = static_cast<double>(approachTickSum) / approachCount / cfg.tickHz;
    }
    s.corruption = static_cast<double>(w.hero.corruption.raw - startCorruption) * 100.0 / ticks;
    return s;
}

int main(int argc, char** argv) {
    const int32_t ticks = argc > 1 ? std::atoi(argv[1]) : 4000;
    const SimConfig cfg = dev::devConfig();
    const double mobSpeed = static_cast<double>(cfg.trash.approachSpeed.raw) / Fixed::ONE_RAW;

    printf("전장 구성 실측 — %d틱, 몹 접근속도 %.3f타일/초\n", ticks, mobSpeed);
    printf("모델 가정: SURROUND_COUNT 5 · AOE_TARGET_SHARE 0.13 · APPROACH_SEC 8\n\n");

    printf("== 영웅 이동속도 스윕 (추격 전용) ==\n");
    printf("%8s %6s | %8s %6s %8s %9s %10s\n",
           "속도", "비율", "근접", "최대", "생존", "접근초", "잠식/100틱");
    for (int32_t pm : {0, 500, 1000, 1500, 2000, 2400, 2800, 3500}) {
        const FieldStats s = measure(cfg, Fixed::fromPermille(pm), 700, 300, ticks, 20250921);
        printf("%6.2f/s %5.2fx | %8.2f %6d %8.1f %9.1f %10.0f\n",
               pm / 1000.0, (pm / 1000.0) / mobSpeed,
               s.meanMelee, s.maxContact, s.meanAlive, s.meanApproach, s.corruption);
    }

    printf("\n== 영웅 이격 거리 스윕 (속도 2.00/s) — 첫 링에 몇 마리가 붙는가 ==\n");
    printf("%14s | %8s %6s %8s %9s %10s\n",
           "영웅이격", "접촉", "최대", "생존", "접근초", "잠식/100틱");
    for (int32_t sep : {600, 800, 1000, 1200, 1500}) {
        SimConfig c2 = cfg;
        c2.heroSeparationMilli = sep;
        const FieldStats s = measure(c2, Fixed::fromPermille(2000), 0, 0, ticks, 20250921);
        printf("%11.2f타일 | %8.2f %6d %8.1f %9.1f %10.0f\n",
               sep / 1000.0, s.meanContact, s.maxContact, s.meanAlive, s.meanApproach, s.corruption);
    }

    printf("\n== 근접 사거리 스윕 — 링 구조에서 유효 밴드 찾기 (영웅 이격 1.0타일) ==\n");
    printf("%12s | %8s %6s %10s\n", "근접 사거리", "접촉", "최대", "잠식/100틱");
    for (int32_t rp : {900, 1000, 1100, 1200, 1400, 1600, 1800, 2000, 2400, 3000}) {
        SimConfig c2 = cfg;
        c2.trash.attackRange = Fixed::fromPermille(rp);
        const FieldStats s = measure(c2, Fixed::fromPermille(2000), 0, 0, ticks, 20250921);
        printf("%9.2f타일 | %8.2f %6d %10.0f\n",
               rp / 1000.0, s.meanContact, s.maxContact, s.corruption);
    }

    printf("\n== 광역 반경별 타격 수 (AOE_TARGET_SHARE 가정 0.13 → 상한 30에서 3.9마리) ==\n");
    {
        World w;
        w.init(20250921);
        dev::applyHeroBaseline(w);
        static SimScratch sc;
        for (int32_t t = 0; t < ticks; ++t) stepWorld(w, cfg, sc);
        printf("%10s %10s %10s\n", "반경", "타격 수", "비율");
        for (int32_t rp : {1000, 1500, 2000, 2500, 3000, 4000}) {
            const Fixed r = Fixed::fromPermille(rp);
            const int64_t r2 = static_cast<int64_t>(r.raw) * r.raw;
            int32_t hit = 0, alive = 0;
            for (uint32_t i = 0; i < w.entities.count(); ++i) {
                if (w.entities.deadAt(i)) continue;
                ++alive;
                if (distanceSq(w.entities.posX[i], w.entities.posY[i],
                               w.hero.posX, w.hero.posY) <= r2) ++hit;
            }
            printf("%8.1f타일 %10d %10.3f\n", rp / 1000.0, hit,
                   alive ? static_cast<double>(hit) / alive : 0.0);
        }
    }
    return 0;
}
