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
#include "data_files.h"

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
    initWorld(w, seed, dev::data().cfg, dev::data().table, dev::data().hero);
    w.hero.stats.setBase(Stat::MoveSpeed, moveSpeed);

    // **데이터에서 온다.** 전에는 `Fixed(3)`이 박혀 있었는데 실제 설정값은
    // 1.5타일이라, 이 스윕의 광역 비율이 다른 반경을 재고 있었다. 아래 "광역
    // 반경별" 절은 처음부터 설정값을 썼으므로 AOE_TARGET_SHARE 가정(0.13)은
    // 멀쩡하다 — 어긋나 있던 것은 이 칸뿐이다.
    const Fixed aoeRadius = cfg.aoeRadius;
    int64_t contactSum = 0, aliveSum = 0, aoeSum = 0;
    int64_t meleeSum = 0;
    int32_t maxContact = 0, samples = 0;
    int64_t approachTickSum = 0, approachCount = 0;
    const int32_t startCorruption = w.hero.corruption.raw;
    static uint32_t seenGen[config::MAX_ENTITIES];
    for (uint32_t i = 0; i < config::MAX_ENTITIES; ++i) seenGen[i] = 0;

    static SimScratch scratch;
    for (int32_t t = 0; t < ticks; ++t) {
        stepWorld(w, c, dev::data().table, scratch);
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
    const SimConfig& cfg = dev::data().cfg;
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
                initWorld(w, 20250921, dev::data().cfg, dev::data().table, dev::data().hero);
        static SimScratch sc;
        for (int32_t t = 0; t < ticks; ++t) stepWorld(w, cfg, dev::data().table, sc);
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
        printf("  ※ 이 절은 **단일 스냅샷**이다 (한 시드의 마지막 틱). 아래 직선\n");
        printf("     스윕은 시드·틱 평균이라 더 믿을 수 있다\n");
    }

    // ── 직선 관통 반폭별 추가 대상 수 ────────────────────────────────
    //
    // `verify_card_values.py`의 PIERCE_TARGETS = 2.0(반폭 700)과
    // PIERCE_EXTRA_PER_WIDTH(선형 가정)이 **측정된 적이 없다.** W_SHOCKWAVE(충격파)의
    // 예산비 97%가 그 위에 서 있다.
    //
    // `collectInLine`을 그대로 쓴다 — 타겟 **뒤쪽**만, 타겟에서 pierceLength만큼,
    // 반폭 안. 타겟은 영웅 사거리 근처에 서므로 그 뒤는 스폰 링 쪽이고, 거기
    // 밀도가 광역 반경 안과 같을 이유가 없다.
    printf("\n== 직선 관통 반폭별 추가 대상 수 (타겟 뒤, 길이 %.1f타일) ==\n",
           cfg.pierceLength.raw / static_cast<double>(Fixed::ONE_RAW));
    {
        const int32_t kSeeds = 24;
        const int32_t kFirst = 600;    // 전장이 찬 뒤부터
        const int32_t kEvery = 40;
        printf("%10s %12s %12s %10s\n", "반폭", "추가 대상", "선형 가정", "표본");
        for (int32_t wp : {175, 350, 525, 700, 1050, 1400}) {
            const Fixed halfw = Fixed::fromPermille(wp);
            int64_t sum = 0;
            int32_t samples = 0;
            for (int32_t sd = 0; sd < kSeeds; ++sd) {
                World w;
                initWorld(w, 777 + static_cast<uint64_t>(sd) * 31,
                          dev::data().cfg, dev::data().table, dev::data().hero);
                static SimScratch sc2;
                for (int32_t t = 0; t < ticks; ++t) {
                    stepWorld(w, cfg, dev::data().table, sc2);
                    if (t < kFirst || (t - kFirst) % kEvery != 0) continue;
                    const int32_t td = w.entities.denseOf(w.hero.target);
                    if (td < 0 || w.entities.deadAt(static_cast<uint32_t>(td))) continue;
                    uint32_t buf[config::MAX_ENTITIES];
                    sum += collectInLine(w, static_cast<uint32_t>(td), halfw,
                                         cfg.pierceLength, buf, config::MAX_ENTITIES);
                    ++samples;
                }
            }
            const double got = samples ? static_cast<double>(sum) / samples : 0.0;
            printf("%8.3f타일 %12.2f %12.2f %10d\n",
                   wp / 1000.0, got, 2.0 * wp / 700.0, samples);
        }
        printf("  ※ 선형 가정 = PIERCE_TARGETS(2.0) × 반폭 / 700\n");
    }
    return 0;
}
