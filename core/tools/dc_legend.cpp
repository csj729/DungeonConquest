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
// 표본: n=1000. 차이의 3σ는 **행마다 계산한다** — 이항 분산이 p(1−p)라 p와 함께
// 움직이므로 상수로 박으면 비율이 내려갈 때 유의한 차이를 놓친다.
// 그 아래 차이는 결론이 아니다 — 한때 n=60에서 18.3% vs 13.3%를 보고 "전설이
// 판을 나쁘게 만든다"로 읽었는데 n=200에서 전부 사라졌다.
//
//   ./build/release/core/dc_legend [시드 수]
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <vector>

#include "../include/dc/sim.h"
#include "data_files.h"

using namespace dc;

struct Row {
    double  sec = 0;
    int32_t reached = 0, cleared = 0, trash = 0, elites = 0;
    long    ccElite = 0, ccTrash = 0;
    int32_t arriveCorruption = -1;   // 보스 등장 틱의 잠식. 도달하지 않으면 -1

    // ── 구간 분리 ───────────────────────────────────────────────────
    //
    // **런 전체 집계로는 메커니즘을 짚을 수 없다.** heroes_vertical_slice.md §4가
    // 그 실패를 두 번 기록한다 — 처치율·런 길이를 한 덩이로 보고 "전환율 절벽"의
    // 원인을 경험치로, 다음엔 장판 기하로 짚었는데 둘 다 틀렸다. 접근 구간에서
    // 일하는 효과와 보스전에서 일하는 효과가 같은 숫자에 섞여 들어오기 때문이다.
    //
    // 그래서 **보스 등장 틱에서 자른다.**
    double  approachSec = 0;     // 0 → 보스 등장
    double  bossSec     = 0;     // 보스 등장 → 종료
    int32_t approachKills = 0;   // 잡몹 + 엘리트
    int32_t bossKills     = 0;
    // **도달 시 레벨.** 처형은 접근 처치 수가 기준선과 같은데도 보스전이 길다.
    // 경험치는 처치 **수**가 아니라 `처치 × 실효 체력`이고 웨이브마다 체력이
    // 오르므로, 일찍 도달하면 같은 수라도 약한 웨이브에서 받은 것이다 —
    // 그 가설을 검사할 수 있는 유일한 열이다.
    int32_t arriveLevel = 0;

    // ── 잠식 채널 (접근 구간) ──────────────────────────────────────
    //
    // **게이지 최종값만으로는 카드가 무엇을 했는지 모른다.** 유입이 세 곳에서
    // 따로 들어오고(적의 공격 · 전장 물량 · 보스 장판) 정화도 세 곳이다
    // (구슬 · 흡혈 · 구간 돌파). 충격파가 처치율을 +10% 올리면서 도달 시 잠식을
    // 1도 못 움직인 것(404 대 기준 403)이 이 열들이 생긴 이유다 — 어느 채널에도
    // 닿지 않았다는 가설을 **이 숫자 없이는 검사할 수 없다.**
    //
    // 보스 장판은 접근 구간에 없으므로 여기 열에 넣지 않는다.
    int64_t corrAttacks = 0, corrMass = 0;
    int64_t purgeOrbs = 0, purgeLeech = 0, purgeSegment = 0;
    int64_t attacksTaken = 0;
    // 평균 생존 적 수. **"물량 채널이 상한에 못 박혀 있는가"를 묻는 열이다** —
    // 스폰이 처치를 즉시 메우면 처치율을 올려도 물량 유입이 움직이지 않는다.
    double  aliveAvg = 0;
    // 기하별 추가 타격 — **구간별로 본다.** 접근 구간 값과 런 전체 값을 따로
    // 베껴서 차이를 보스전 몫으로 읽는다.
    int64_t scA = 0, shA = 0, aoeA = 0, zoneA = 0;   // 접근 구간까지 누적
    int64_t scT = 0, shT = 0, aoeT = 0, zoneT = 0;   // 런 전체 누적
    // 기하 후보 탐침 — 띠별 적 수 (접근 / 런 전체)
    int64_t bhA = 0, btA = 0, bkA = 0;
    int64_t bhT = 0, btT = 0, bkT = 0;
    int64_t perpA[dc::Metrics::PERP_BUCKETS] = {0};
    int64_t perpT[dc::Metrics::PERP_BUCKETS] = {0};
};

// 접근 구간 끝(보스 등장 또는 사망)에서 계측을 베낀다.
static void captureChannels(Row& r, const dc::World& w) {
    r.corrAttacks  = w.metrics.corrAttacks;
    r.corrMass     = w.metrics.corrMass;
    r.purgeOrbs    = w.metrics.purgeOrbs;
    r.purgeLeech   = w.metrics.purgeLeech;
    r.purgeSegment = w.metrics.purgeSegment;
    r.attacksTaken = w.metrics.attacksTaken;
    r.aliveAvg     = w.metrics.aliveTicks > 0
                   ? static_cast<double>(w.metrics.aliveSum) / w.metrics.aliveTicks
                   : 0.0;
    r.scA  = w.metrics.shockCalls;
    r.shA  = w.metrics.shockHits;
    r.aoeA = w.metrics.aoeExtraHits;
    r.zoneA = w.metrics.zoneTickHits;
    r.bhA = w.metrics.shockBehind;
    r.btA = w.metrics.shockBetween;
    r.bkA = w.metrics.shockBack;
    for (int32_t b = 0; b < dc::Metrics::PERP_BUCKETS; ++b) {
        r.perpA[b] = w.metrics.heroBandPerp[b];
    }
}

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
        if (!wasSpawned && w.run.bossSpawned) {
            r.arriveCorruption = w.hero.corruption.raw;
            // 자르는 지점. 여기까지가 접근 구간이다
            r.approachSec   = static_cast<double>(w.tickCount()) / cfg.tickHz;
            r.approachKills = w.run.killedTrash + w.run.killedElite;
            r.arriveLevel   = w.hero.level;
            captureChannels(r, w);
        }

        for (uint32_t i = 0; i < w.entities.count(); ++i) {
            if (w.entities.deadAt(i) || w.entities.groggyLeft[i] <= 0) continue;
            if (w.entities.archetype[i] == Archetype::Trash) ++r.ccTrash;
            else ++r.ccElite;
        }
    }
    r.scT  = w.metrics.shockCalls;
    r.shT  = w.metrics.shockHits;
    r.aoeT = w.metrics.aoeExtraHits;
    r.zoneT = w.metrics.zoneTickHits;
    r.bhT = w.metrics.shockBehind;
    r.btT = w.metrics.shockBetween;
    r.bkT = w.metrics.shockBack;
    for (int32_t b = 0; b < dc::Metrics::PERP_BUCKETS; ++b) {
        r.perpT[b] = w.metrics.heroBandPerp[b];
    }
    r.sec     = static_cast<double>(w.tickCount()) / cfg.tickHz;
    r.reached = w.run.bossSpawned ? 1 : 0;
    r.cleared = w.run.outcome == RunOutcome::Cleared ? 1 : 0;
    r.trash   = w.run.killedTrash;
    r.elites  = w.run.killedElite;
    if (r.reached) {
        r.bossSec   = r.sec - r.approachSec;
        r.bossKills = (w.run.killedTrash + w.run.killedElite) - r.approachKills;
    } else {
        // 도달하지 못한 런은 **전부 접근 구간이다.** 0으로 두면 보스전 평균이
        // 도달한 런만의 값인지 전체인지 모르게 된다
        r.approachSec   = r.sec;
        r.approachKills = w.run.killedTrash + w.run.killedElite;
        captureChannels(r, w);
    }
    return r;
}

int main(int argc, char** argv) {
    const int32_t N = argc > 1 ? std::atoi(argv[1]) : 1000;
    const SimConfig& cfg = dev::data().cfg;

    // 전설 풀 인덱스. 유물 3칸(0~2)은 이미 다른 도구가 재므로 고유 각인만 본다.
    // `tag`는 **매트릭스 전용 ASCII 약칭**이다. 한글은 멀티바이트라 printf의
    // `%9.9s`가 바이트로 자르고 폭도 어긋난다 — 실제로 "소용돌이"가 "소용돌"로
    // 잘려 표가 밀렸다. 표 머리글·행 이름에만 쓰고 아래 범례로 잇는다.
    struct Variant { const char* name; const char* tag; int32_t legend; };
    const Variant kVariants[] = {
        {"없음(기준)",  "BASE", -1},
        {"처형",         "EXE",  static_cast<int32_t>(legendIndexOf(LegendId::Execute))},
        {"충격파",       "SHK",  static_cast<int32_t>(legendIndexOf(LegendId::Shockwave))},
        {"소용돌이",     "VTX",  static_cast<int32_t>(legendIndexOf(LegendId::Vortex))},
        {"원심력",       "CEN",  static_cast<int32_t>(legendIndexOf(LegendId::Centrifuge))},
        {"여진",         "AFT",  static_cast<int32_t>(legendIndexOf(LegendId::Aftershock))},
        {"균열",         "FIS",  static_cast<int32_t>(legendIndexOf(LegendId::Fissure))},
    };

    printf("=== 고유 각인별 실측 (시드 %d · QTE 항상 완벽) ===\n", N);
    printf("클리어 = 도달 × 도달 후 생존. **도달 후 생존은 품질이 아니다** —\n");
    printf("도달률이 오르면 약한 빌드가 보스전 모집단에 섞여 내려간다.\n\n");
    printf("%-12s %8s %11s %10s %10s %9s %11s %10s %10s\n",
           "전설", "도달률", "도달후생존", "클리어율", "Δ클리어", "런 길이",
           "도달시잠식", "잡몹처치", "기절틱(E)");
    printf("  %s\n", "-------------------------------------------------------"
                     "------------------------------------------");

    // 차이의 표준오차 — **p를 박지 말 것.** 이항 비율의 분산은 p(1−p)라 p와 함께
    // 움직인다. 전에는 p = 0.15를 박아 3σ = 4.8%p로 찍었는데, 수치가 바뀌어 실제
    // 비율이 9~14%로 내려간 뒤에는 임계값이 너무 보수적이어서 유의한 각인을
    // 놓쳤다(verify_card_values.py의 clear_3sigma와 같은 고장이었다).
    // 각 행의 비율로 계산한다.
    const auto sigma3 = [N](double pa, double pb) {
        const double a = pa / 100.0, b = pb / 100.0;
        return 3.0 * 100.0 * std::sqrt(a * (1 - a) / N + b * (1 - b) / N);
    };
    // **행을 저장해 둔다.** 아래 '공통 창' 절이 시드별로 대조해야 한다 —
    // 즉석 집계로는 "모든 변종에서 도달한 시드"를 알 수 없다.
    constexpr size_t kVarCount = sizeof(kVariants) / sizeof(kVariants[0]);
    std::vector<std::vector<Row>> rows(kVarCount);
    for (auto& v : rows) v.reserve(static_cast<size_t>(N));

    double baseClear = 0;
    for (size_t vi = 0; vi < kVarCount; ++vi) {
        const Variant& v = kVariants[vi];
        double sec = 0;
        long tr = 0, el = 0, ce = 0, arrive = 0;
        int32_t re = 0, cl = 0, n = 0, arriveN = 0;
        for (int32_t s = 1; s <= N; ++s) {
            const Row r = runOne(static_cast<uint64_t>(s) * 7919ull, v.legend);
            rows[vi].push_back(r);
            sec += r.sec; re += r.reached; cl += r.cleared;
            tr += r.trash; el += r.elites; ce += r.ccElite; ++n;
            if (r.arriveCorruption >= 0) { arrive += r.arriveCorruption; ++arriveN; }
        }
        const double clear = 100.0 * cl / n;
        if (v.legend < 0) baseClear = clear;
        const bool sig = v.legend >= 0
                      && std::fabs(clear - baseClear) > sigma3(baseClear, clear);
        // **런 길이를 같이 찍는다.** 다만 이 열로 메커니즘을 짚으려는 시도는
        // 두 번 틀렸다(heroes_vertical_slice.md §4) — "런이 짧으면 경험치가 덜
        // 쌓인다"는 처치 수가 반박하고, "장판이 보스에 안 닿는다"는 소용돌이·
        // 원심력이 둘 다 안 닿아 차이를 설명하지 못한다. **구간을 나눠 재야
        // 한다**는 것이 그 결론이고, 아래 두 절이 그 일을 한다.
        printf("%-12s %7.1f%% %10.1f%% %9.1f%% %+8.1f%%p%s %8.1fs %10.0f %10.1f %10.0f\n",
               v.name, 100.0 * re / n, re > 0 ? 100.0 * cl / re : 0.0, clear,
               v.legend < 0 ? 0.0 : clear - baseClear, sig ? " *" : "  ",
               sec / n,
               arriveN > 0 ? static_cast<double>(arrive) / arriveN / Fixed::ONE_RAW : 0.0,
               static_cast<double>(tr) / n, static_cast<double>(ce) / n);
        (void)el;
    }
    printf("\n  * = 기준선과의 차이가 3σ를 넘는다 (행마다 계산 — 기준선 %.1f%%에서"
           " 약 %.2f%%p). 그 아래는 결론이 아니다.\n",
           baseClear, sigma3(baseClear, baseClear));
    printf("  전설 풀 %u칸 중 고유 각인 6칸만 본다 — 유물 3종은 dc_boss·dc_montecarlo 몫이다.\n",
           cfg.legendPoolSize);

    // ── 구간 분리 ───────────────────────────────────────────────────
    //
    // 접근 구간과 보스전을 나눠 본다. 위 표의 '잡몹처치'·'런 길이'는 둘을 합친
    // 값이라, 접근에서 일하는 효과와 보스전에서 일하는 효과가 섞인다.
    printf("\n== 구간 분리 (도달한 런만) ==\n");
    printf("%-12s %10s %12s %10s %10s %12s\n",
           "전설", "접근 초", "접근 처치", "도달 레벨", "보스전 초", "보스전 처치/초");
    printf("  %s\n", "--------------------------------------------------------------------------");
    for (size_t vi = 0; vi < kVarCount; ++vi) {
        double aSec = 0, bSec = 0;
        long   aK = 0, bK = 0, lv = 0;
        int32_t n = 0;
        for (const Row& r : rows[vi]) {
            if (!r.reached) continue;
            aSec += r.approachSec; bSec += r.bossSec;
            aK += r.approachKills; bK += r.bossKills; lv += r.arriveLevel; ++n;
        }
        if (!n) continue;
        printf("%-12s %9.1fs %12.1f %10.2f %9.1fs %12.3f\n", kVariants[vi].name,
               aSec / n, static_cast<double>(aK) / n, static_cast<double>(lv) / n,
               bSec / n, bSec > 0 ? bK / bSec : 0.0);
    }
    // ── 잠식 채널 분리 (접근 구간) ───────────────────────────────────
    //
    // **처치율은 클리어율의 대리 지표가 아니다**(§4). 그 이유를 짚으려면 잠식
    // 시계의 입력을 쪼개야 한다. 충격파가 처치율 +10%에 도달 시 잠식 +0이라는
    // 조합을 설명할 수 있는 것은 "어느 채널에도 닿지 않았다" 하나뿐인데,
    // 채널을 나눠 찍지 않으면 그게 가설인지 사실인지 알 수 없다.
    //
    // 도달한 런만 센다 — 죽은 런은 접근 구간 길이가 사인(死因)에 딸려 있어
    // 채널 비율이 "얼마나 버텼나"로 오염된다.
    printf("\n== 잠식 채널 (접근 구간 · 도달한 런만 · 초당) ==\n");
    printf("%-12s %9s %9s %9s %9s %9s %9s %9s\n",
           "전설", "피격", "물량", "구슬", "흡혈", "구간", "피격/초", "평균 적");
    printf("  %s\n", "------------------------------------------------------------------------------");
    double baseAtk = 0, baseMass = 0;
    for (size_t vi = 0; vi < kVarCount; ++vi) {
        double atk = 0, mass = 0, orb = 0, lee = 0, segp = 0, sec = 0, alive = 0, hits = 0;
        int32_t n = 0;
        for (const Row& r : rows[vi]) {
            if (!r.reached || r.approachSec <= 0) continue;
            atk  += static_cast<double>(r.corrAttacks)  / Fixed::ONE_RAW / r.approachSec;
            mass += static_cast<double>(r.corrMass)     / Fixed::ONE_RAW / r.approachSec;
            orb  += static_cast<double>(r.purgeOrbs)    / Fixed::ONE_RAW / r.approachSec;
            lee  += static_cast<double>(r.purgeLeech)   / Fixed::ONE_RAW / r.approachSec;
            segp += static_cast<double>(r.purgeSegment) / Fixed::ONE_RAW / r.approachSec;
            hits += static_cast<double>(r.attacksTaken) / r.approachSec;
            alive += r.aliveAvg;
            sec  += r.approachSec;
            ++n;
        }
        if (!n) continue;
        if (kVariants[vi].legend < 0) { baseAtk = atk / n; baseMass = mass / n; }
        printf("%-12s %9.2f %9.2f %9.2f %9.2f %9.2f %9.2f %9.1f\n", kVariants[vi].name,
               atk / n, mass / n, orb / n, lee / n, segp / n, hits / n, alive / n);
        (void)sec;
    }
    printf("  ※ 유입 둘(피격 · 물량)과 정화 셋(구슬 · 흡혈 · 구간)이다. 보스 장판은\n");
    printf("     접근 구간에 없다. 기준선 피격 %.2f/초 · 물량 %.2f/초\n", baseAtk, baseMass);
    printf("  ※ **물량 채널은 위치와 무관하다** — 전장의 생존 적 수만 본다. 그래서\n");
    printf("     '멀리 있는 적을 죽였다'도 물량에는 똑같이 듣는다. 다만 스폰이 처치를\n");
    printf("     메우면 '평균 적'이 움직이지 않고, 그때는 처치율을 올려도 유입이 그대로다\n");
    // ── 기하별 추가 타격 — 구간별 ────────────────────────────────────
    //
    // **예산식의 입력을 그대로 측정한다.** 충격파 예산은 `발동 주기`와 `추가
    // 대상 수` 둘로 환산되는데, 둘 다 게임 밖에서 잰 값이었다 — 주기는 설계
    // 수치(10.8초), 대상 수는 dc_field의 기하 스윕(반폭 1150에서 1.62명)이다.
    // 실제 런에서 그 둘이 얼마인지 보지 않았다.
    //
    // 직선(타겟 뒤)과 원형(영웅 주변)을 나란히 둔다. 보스전에 들어가면 영웅이
    // 보스를 치므로 타겟 뒤는 스폰 링 바깥이다 — 같은 '추가 1.6명'이 거기서도
    // 성립하는지는 재 봐야 안다.
    printf("\n== 충격파 예산의 입력 (도달한 런만) ==\n");
    printf("%-12s %9s %9s %9s %9s %9s %9s\n",
           "전설", "발동/초(접)", "대상/발동(접)", "발동/초(보)", "대상/발동(보)",
           "광역 대상/초", "장판 대상/초");
    printf("  %s\n", "--------------------------------------------------------------------------------");
    for (size_t vi = 0; vi < kVarCount; ++vi) {
        double ca = 0, ha = 0, cb = 0, hb = 0, aoe = 0, zone = 0;
        int32_t n = 0, nb = 0;
        for (const Row& r : rows[vi]) {
            if (!r.reached || r.approachSec <= 0 || r.bossSec <= 0) continue;
            ca += static_cast<double>(r.scA) / r.approachSec;
            aoe += static_cast<double>(r.aoeT - r.aoeA) / r.bossSec;
            zone += static_cast<double>(r.zoneT - r.zoneA) / r.bossSec;
            if (r.scA > 0) { ha += static_cast<double>(r.shA) / r.scA; ++nb; }
            cb += static_cast<double>(r.scT - r.scA) / r.bossSec;
            if (r.scT > r.scA) {
                hb += static_cast<double>(r.shT - r.shA) / (r.scT - r.scA);
            }
            ++n;
        }
        if (!n) continue;
        printf("%-12s %11.3f %13.2f %11.3f %13.2f %13.3f %13.2f\n", kVariants[vi].name,
               ca / n, nb > 0 ? ha / nb : 0.0, cb / n, nb > 0 ? hb / nb : 0.0,
               aoe / n, zone / n);
    }
    // ── 기하 후보 — 같은 발동에서 세 띠를 동시에 센다 ────────────────
    //
    // 충격파 행만 의미가 있다(다른 행은 발동이 거의 없다). 세 띠의 합이 "선을
    // 어디까지 보느냐"의 선택지다 — 지금은 '뒤'만 때린다.
    printf("\n  기하 후보 — 발동당 대상 수 (충격파 행만 읽는다)\n");
    printf("  ※ 충격파가 때리는 띠는 **앞의 둘(타겟 뒤 + 영웅~타겟)**이다.\n");
    printf("     '타겟 뒤'만 쓰던 시절 그 띠가 보스전에서 62%% 무너졌다\n");
    printf("  %-12s %12s %12s %12s %12s\n",
           "구간", "타겟 뒤(관통)", "영웅~타겟", "영웅 뒤", "셋 합");
    printf("  %s\n", "--------------------------------------------------------------------------");
    for (size_t vi = 0; vi < kVarCount; ++vi) {
        if (kVariants[vi].legend != static_cast<int32_t>(dc::LegendId::Shockwave)) continue;
        double ah = 0, at = 0, ab = 0, bh = 0, bt = 0, bb = 0;
        int32_t na = 0, nb = 0;
        for (const Row& r : rows[vi]) {
            if (!r.reached || r.bossSec <= 0) continue;
            if (r.scA > 0) {
                ah += static_cast<double>(r.bhA) / r.scA;
                at += static_cast<double>(r.btA) / r.scA;
                ab += static_cast<double>(r.bkA) / r.scA;
                ++na;
            }
            if (r.scT > r.scA) {
                const double d = static_cast<double>(r.scT - r.scA);
                bh += static_cast<double>(r.bhT - r.bhA) / d;
                bt += static_cast<double>(r.btT - r.btA) / d;
                bb += static_cast<double>(r.bkT - r.bkA) / d;
                ++nb;
            }
        }
        if (na > 0) {
            printf("  %-12s %12.2f %12.2f %12.2f %12.2f\n", "접근",
                   ah / na, at / na, ab / na, (ah + at + ab) / na);
        }
        if (nb > 0) {
            printf("  %-12s %12.2f %12.2f %12.2f %12.2f\n", "보스전",
                   bh / nb, bt / nb, bb / nb, (bh + bt + bb) / nb);
        }
    }
    // ── 반폭 → 대상 수 (영웅에서 시작하는 경로) ──────────────────────
    //
    // 폭은 **예산이 정한다.** 그런데 그 환산에 쓸 '반폭 → 대상 수' 곡선은
    // dc_field가 **타겟 뒤 띠**에서 잰 것이라, 띠를 바꾸면 쓸 수 없다. 그래서
    // 같은 런에서 수직 거리 히스토그램을 받아 누적합으로 곡선을 만든다.
    printf("\n  반폭 → 발동당 대상 수 (영웅에서 시작하는 경로)\n");
    printf("  %-10s", "반폭(타일)");
    for (int32_t b = 0; b < dc::Metrics::PERP_BUCKETS; ++b) {
        printf(" %6.1f", (b + 1) * dc::Metrics::PERP_BUCKET_MILLI / 1000.0);
    }
    printf("\n");
    for (size_t vi = 0; vi < kVarCount; ++vi) {
        if (kVariants[vi].legend != static_cast<int32_t>(dc::LegendId::Shockwave)) continue;
        for (int32_t phase = 0; phase < 2; ++phase) {
            double acc[dc::Metrics::PERP_BUCKETS] = {0};
            int32_t n = 0;
            for (const Row& r : rows[vi]) {
                if (!r.reached || r.bossSec <= 0) continue;
                const double calls = phase == 0 ? static_cast<double>(r.scA)
                                                : static_cast<double>(r.scT - r.scA);
                if (calls <= 0) continue;
                int64_t run = 0;
                for (int32_t b = 0; b < dc::Metrics::PERP_BUCKETS; ++b) {
                    run += phase == 0 ? r.perpA[b] : (r.perpT[b] - r.perpA[b]);
                    acc[b] += static_cast<double>(run) / calls;
                }
                ++n;
            }
            if (!n) continue;
            printf("  %-10s", phase == 0 ? "접근" : "보스전");
            for (int32_t b = 0; b < dc::Metrics::PERP_BUCKETS; ++b) {
                printf(" %6.2f", acc[b] / n);
            }
            printf("\n");
        }
    }
    printf("  ※ 누적합이다 — 각 칸은 '반폭 이 값 이하'의 대상 수다\n");
    printf("  ※ **탐침은 피해를 주지 않는다** — 세기만 한다. 그래서 이 숫자를\n");
    printf("     보고 기하를 고르는 동안 측정은 한 판으로 끝난다\n");
    printf("  ※ **충격파를 들지 않은 행은 발동 0이다** — 각인이 없으면\n");
    printf("     applyShockwave가 돌지 않는다. 그래서 이 표는 충격파 행만 읽는다\n");
    printf("  ※ 뒤 두 열은 비교용이다: 광역(발동당 원형)과 장판(20Hz 틱당)이\n");
    printf("     보스전에서 대상을 얼마나 찾는가 — 같은 '추가 대상'이 기하에 따라\n");
    printf("     보스전에서 어떻게 달라지는지 보는 자리다\n");
    printf("  ※ **피격 채널은 카드로 거의 움직이지 않는다** — 실측에서 처형만\n");
    printf("     −16%%(13.23 → 11.10/초)이고 나머지 다섯은 ±2%% 안이다. 둔화\n");
    printf("     500‰인 균열도 −2%%뿐이다. 사거리 안의 적을 치워도 다음 적이\n");
    printf("     곧 들어오므로 **유입률이 아니라 노출 시간이 줄어드는 쪽**으로\n");
    printf("     일한다 — 그래서 '초당'이 아니라 도달 시 누적을 같이 봐야 한다\n");
    printf("  ※ 보스전 처치율은 보스전 **동안 죽인 잡몹·엘리트**다 — 보스전 중에도\n");
    printf("     스폰이 계속되므로 잠식 시계가 거기서도 돈다\n");
    printf("  ※ **'보스전 초'는 DPS 지표가 아니라 생존 시간이다.** 런은 클리어로도\n");
    printf("     끝나고 죽어서도 끝난다 — 길다는 것은 '오래 버텼다'이고, 보스를\n");
    printf("     빨리 깼다면 오히려 짧다. 승패와 섞이는 열이므로 단독으로 읽지 말 것\n");
    printf("  ※ **도달 레벨이 카드와 무관하게 상수다**(17.4~17.5). 보스는 clearPoints\n");
    printf("     (처치)로 열리므로 도달에 필요한 처치 수가 고정이고, 따라서 도달 시\n");
    printf("     경험치도 고정이다 — '런이 짧으면 경험치가 덜 쌓인다'는 축은 데이터가\n");
    printf("     지지하지 않는 수준이 아니라 **구조적으로 존재할 수 없다**\n");

    // ── 공통 창 ─────────────────────────────────────────────────────
    //
    // **'도달 후 생존'은 변종마다 모집단이 달라 비교할 수 없다.** 도달률이 오르면
    // 약한 판이 보스전 모집단에 섞여 들어와 생존율을 끌어내린다 — 처형이 도달률
    // 99.5%에 생존 12.6%(기준 13.7%)인 것이 그 효과이고, 카드가 나쁘다는 뜻이
    // 아니다. 위 표가 그 주의를 머리글에 적어 두지만, 숫자 자체는 교란된 채다.
    //
    // `dc_boss`가 쓰는 방법을 가져온다: **모든 변종에서 보스에 도달한 시드만**
    // 모아 그 집합에서만 비교한다. 모집단이 같아지므로 생존율 차이가 카드의
    // 효과다.
    printf("\n== 공통 창 — 모든 변종에서 도달한 시드만 ==\n");
    {
        std::vector<int32_t> common;
        for (int32_t i = 0; i < N; ++i) {
            bool all = true;
            for (size_t vi = 0; vi < kVarCount && all; ++vi) {
                if (!rows[vi][static_cast<size_t>(i)].reached) all = false;
            }
            if (all) common.push_back(i);
        }
        printf("  공통 %zu판 / %d시드 (%.0f%%)\n",
               common.size(), N, 100.0 * static_cast<double>(common.size()) / N);
        if (common.empty()) {
            printf("  ※ 공통 집합이 비었다 — 시드를 늘리거나 변종을 줄일 것\n");
        } else {
            printf("%-12s %12s %10s %7s %7s %8s %12s\n",
                   "전설", "보스전 생존", "Δ생존", "이김", "짐", "McNemar", "보스전 처치/초");
            printf("  %s\n", "----------------------------------------------------------------------------");
            double baseSurv = 0;
            for (size_t vi = 0; vi < kVarCount; ++vi) {
                double bSec = 0;
                long   bK = 0;
                int32_t cl = 0, win = 0, lose = 0;
                for (int32_t i : common) {
                    const Row& r = rows[vi][static_cast<size_t>(i)];
                    cl += r.cleared; bSec += r.bossSec; bK += r.bossKills;
                    // **짝지은 비교.** 같은 시드에서 기준선과 결과가 갈린 판만 센다
                    const int32_t b0 = rows[0][static_cast<size_t>(i)].cleared;
                    if (r.cleared && !b0) ++win;
                    if (!r.cleared && b0) ++lose;
                }
                const double nn = static_cast<double>(common.size());
                const double surv = 100.0 * cl / nn;
                if (vi == 0) baseSurv = surv;
                // McNemar: z = (b − c) / sqrt(b + c). **일치하는 판은 정보가 없다** —
                // 양쪽 다 깨거나 양쪽 다 죽은 시드는 카드 차이를 말해 주지 않는다.
                // 독립 표본 SE는 그 판들까지 분산에 넣으므로 과하게 보수적이다.
                const int32_t disc = win + lose;
                const double mz = disc > 0
                    ? (win - lose) / std::sqrt(static_cast<double>(disc)) : 0.0;
                if (vi == 0) {
                    printf("%-12s %11.1f%% %10s %7s %7s %8s %12.3f\n",
                           kVariants[vi].name, surv, "—", "—", "—", "—",
                           bSec > 0 ? bK / bSec : 0.0);
                } else {
                    printf("%-12s %11.1f%% %+9.1f%%p %7d %7d %7.2fσ%s %11.3f\n",
                           kVariants[vi].name, surv, surv - baseSurv, win, lose, mz,
                           std::fabs(mz) > 3.0 ? "*" : " ",
                           bSec > 0 ? bK / bSec : 0.0);
                }
            }
            printf("  ※ 모집단이 같으므로 **이 Δ생존은 카드의 효과다.** 위 표의\n");
            printf("     '도달 후 생존'과 달리 희석에 교란되지 않는다\n");
            printf("  ※ **McNemar는 짝지은 검정이다.** 같은 시드에서 결과가 갈린 판만\n");
            printf("     쓴다(이김 = 카드는 깼고 기준선은 못 깼다, 짐 = 반대).\n");
            printf("     양쪽 다 깨거나 양쪽 다 죽은 시드는 카드 차이를 말해 주지\n");
            printf("     않으므로 분산에 넣지 않는다 — 독립 표본 SE보다 검정력이 높다.\n");
            printf("     * = 3σ 초과\n");

            // ── 변종끼리 짝 비교 ────────────────────────────────────
            //
            // 기준선 대비만으로는 **그룹이 갈리는지** 알 수 없다. 균열(+8.0)과
            // 소용돌이(+2.8)가 둘 다 기준선과 비교됐을 뿐, 서로 다른지는 따로
            // 재야 한다. 같은 공통 창이므로 여기서도 짝 검정을 쓴다.
            //
            // **순위를 매기려는 것이 아니다.** 상위 둘(균열 8.0 · 원심력 7.8)을
            // 가르려면 0.2%p를 분해해야 하고 그건 329만 시드다 — 측정의 한계가
            // 아니라 두 카드가 실질적으로 같다는 뜻이다. 이 표가 답하는 것은
            // "어느 쌍이 실제로 다른가"뿐이다.
            printf("\n  변종끼리 짝 비교 (McNemar z · 행 − 열 · * = 3σ 초과)\n");
            printf("  %-6s", "");
            for (size_t cj = 1; cj < kVarCount; ++cj) {
                printf("%8s", kVariants[cj].tag);
            }
            printf("\n");
            for (size_t ri = 1; ri < kVarCount; ++ri) {
                printf("  %-6s", kVariants[ri].tag);
                for (size_t cj = 1; cj < kVarCount; ++cj) {
                    if (cj <= ri) { printf("%8s", "-"); continue; }
                    int32_t win = 0, lose = 0;
                    for (int32_t i : common) {
                        const int32_t a = rows[ri][static_cast<size_t>(i)].cleared;
                        const int32_t b = rows[cj][static_cast<size_t>(i)].cleared;
                        if (a && !b) ++win;
                        if (!a && b) ++lose;
                    }
                    const int32_t disc = win + lose;
                    const double z = disc > 0
                        ? (win - lose) / std::sqrt(static_cast<double>(disc)) : 0.0;
                    char cell[16];
                    std::snprintf(cell, sizeof(cell), "%+.2f%s", z,
                                  std::fabs(z) > 3.0 ? "*" : "");
                    printf("%8s", cell);
                }
                printf("\n");
            }
            printf("  ");
            for (size_t vi = 1; vi < kVarCount; ++vi) {
                printf("%s=%s%s", kVariants[vi].tag, kVariants[vi].name,
                       vi + 1 < kVarCount ? " · " : "\n");
            }
            printf("  ※ 양수면 행이 더 많이 깼다. **3σ를 넘는 칸만 '다르다'고 말할 수\n");
            printf("     있고**, 나머지는 구분되지 않는다 — 순위가 아니라 그룹이다\n");
        }
    }
    return 0;
}
