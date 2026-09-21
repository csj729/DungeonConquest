// **임시 데이터 로더.** `data/*.json`을 읽는 진짜 로더가 붙으면 이 파일은 삭제된다.
//
// 값은 전부 `data/*.json`에서 그대로 옮긴 것이고,
// `tools/verify_core_constants.py`가 이 파일과 데이터의 정합을 자동 대조한다 —
// 한쪽만 고치면 CI에서 걸린다.
//
// **core/ 안이 아니라 tools/ 안에 두는 이유**: 시뮬 코어에 들어가면 "밸런스 수치는
// data/*.json이 단일 진실 원천"(CLAUDE.md)이 흐려지고 삭제 시점을 놓친다.
#ifndef DC_DEV_DATA_H
#define DC_DEV_DATA_H

#include "../include/dc/sim_config.h"
#include "../include/dc/stat_block.h"

namespace dc::dev {

// data/spawn.json — 파이썬이 지수 램프를 미리 푼 정수 표
constexpr int32_t CAP_BY_SEGMENT[24] = {
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 41, 42,
    43, 44, 46, 47, 49, 50, 52, 53, 55, 56, 58, 60};
constexpr int32_t BATCH_BY_SEGMENT[8] = {2, 3, 4, 5, 5, 6, 7, 8};

// data/hero.json
constexpr int32_t  HERO_ATTACK_POWER          = 10;
constexpr int32_t  HERO_ATTACK_INTERVAL_TICKS = 20;
constexpr int32_t  HERO_CRIT_CHANCE_PERMILLE  = 100;
constexpr int32_t  HERO_CRIT_MULT_PERMILLE    = 1500;
constexpr int32_t  HERO_CORRUPTION_MAX        = 1250;
constexpr int32_t  HERO_ARMOR                 = 0;
constexpr uint32_t HERO_PROC_PRD_C_Q16        = 2112;

inline SimConfig devConfig() {
    SimConfig c;
    // data/progression.json
    c.tickHz         = 20;
    c.totalSegments  = 24;
    c.segmentsPerMap = 8;
    c.armorK         = 100;
    c.trashHpScalePerSegmentPermille   = 1090;
    c.corruptionThreshold              = 20;
    c.corruptionPerMobPermille         = 600;
    c.corruptionOverflowMultPermille   = 3000;

    // data/spawn.json
    for (int32_t i = 0; i < 24; ++i) c.capBySegment[i]   = CAP_BY_SEGMENT[i];
    for (int32_t i = 0; i < 8;  ++i) c.batchBySegment[i] = BATCH_BY_SEGMENT[i];
    c.spawnIntervalTicks = 40;
    c.spawnRadiusMilli   = 19800;
    c.minSeparationMilli = 1500;
    c.directions         = 4;

    // data/segments.json
    c.trashPoints           = 1;
    c.elitePoints           = 10;
    c.clearPointsPerSegment = 50;

    // data/hero.json
    c.procPrdCQ16 = HERO_PROC_PRD_C_Q16;

    // data/monsters.json — 잡몹
    c.trash.hp             = Fixed(20);
    c.trash.damage         = Fixed(5);
    c.trash.cooldownTicks  = 30;
    c.trash.targetPriority = 0;
    c.trash.archetype      = Archetype::Trash;
    // 접근 속도는 스폰 반경 19.8타일 / 접근 160틱(8초) = 2.475타일/초 (§2에서 역산)
    c.trash.approachSpeed  = Fixed::fromRaw((19800 * Fixed::ONE_RAW) / 1000 / 8);
    c.trash.attackRange    = Fixed(1);

    // data/monsters.json — 엘리트 (target_priority가 순서를 정한다)
    struct E { int32_t hp, armor, dmg, prio, windup; uint16_t typeId; };
    constexpr E kElites[4] = {
        {120, 0,   40, 35, 60, 1},   // GE_ARCHER  궁병대장 — QTE 소스
        {100, 200,  8, 20,  0, 2},   // GE_SHIELD  방패병   — QTE 없음
        { 90, 0,    5, 40,  0, 3},   // GE_SHAMAN  주술사   — 소환, 방치 비용 최대
        {150, 0,    3, 30, 30, 4},   // GE_MAD     미친 고블린 — QTE 소스
    };
    c.eliteCount = 4;
    for (uint32_t i = 0; i < 4; ++i) {
        MonsterConfig& m  = c.elites[i];
        m.hp              = Fixed(kElites[i].hp);
        m.armor           = Fixed(kElites[i].armor);
        m.damage          = Fixed(kElites[i].dmg);
        m.targetPriority  = kElites[i].prio;
        m.windupTicks     = kElites[i].windup;
        m.typeId          = kElites[i].typeId;
        m.archetype       = Archetype::Elite;
        m.cooldownTicks   = 60;
        m.approachSpeed   = c.trash.approachSpeed;
        m.attackRange     = Fixed(2);
        m.ccGaugeMax      = Fixed(100);
    }

    // data/monsters.json — 보스
    c.boss.hp             = Fixed(2100);      // slice_boss_hp
    c.boss.armor          = Fixed(50);        // slice_boss_armor
    c.boss.damage         = Fixed(63);
    c.boss.targetPriority = 10;
    c.boss.archetype      = Archetype::Boss;
    c.boss.cooldownTicks  = 60;
    c.boss.approachSpeed  = c.trash.approachSpeed;
    c.boss.attackRange    = Fixed(3);
    c.boss.typeId         = 100;

    // data/segments.json — 구간별 엘리트를 클리어 게이지 임계로 옮긴 것
    const EliteSpawnPoint sp[6] = {
        {100, 0},  // 구간 3  궁병대장
        {150, 2},  // 구간 4  주술사
        {200, 1},  // 구간 5  방패병
        {250, 3},  // 구간 6  미친 고블린
        {260, 0},  //         궁병대장
        {300, 2},  // 구간 7  주술사
    };
    c.eliteSpawnCount = 6;
    for (uint32_t i = 0; i < 6; ++i) c.eliteSpawns[i] = sp[i];
    return c;
}

// 영웅 기준선을 StatBlock에 심는다.
inline void applyHeroBaseline(World& w) {
    Fixed      bases[STAT_COUNT];
    StatBounds bounds[STAT_COUNT];
    for (uint32_t i = 0; i < STAT_COUNT; ++i) {
        bases[i]  = Fixed{};
        bounds[i] = StatBounds{Fixed{}, Fixed::fromPermille(-900)};
    }
    bases[statIndex(Stat::AttackPower)]   = Fixed(HERO_ATTACK_POWER);
    bases[statIndex(Stat::AttackSpeed)]   = Fixed(20) / HERO_ATTACK_INTERVAL_TICKS;  // 초당 1회
    bases[statIndex(Stat::Armor)]         = Fixed(HERO_ARMOR);
    bases[statIndex(Stat::Range)]         = Fixed(3);
    bases[statIndex(Stat::CorruptionMax)] = Fixed(HERO_CORRUPTION_MAX);
    bases[statIndex(Stat::CritChance)]    = Fixed::fromPermille(HERO_CRIT_CHANCE_PERMILLE);
    bases[statIndex(Stat::CritMult)]      = Fixed::fromPermille(HERO_CRIT_MULT_PERMILLE);
    bases[statIndex(Stat::MoveSpeed)]     = Fixed(2);
    bounds[statIndex(Stat::CorruptionMax)] = StatBounds{Fixed(1), Fixed::fromPermille(-900)};
    bounds[statIndex(Stat::AttackSpeed)]   = StatBounds{Fixed::fromPermille(100),
                                                        Fixed::fromPermille(-900)};
    w.hero.stats.init(bases, bounds);
    w.refreshAllConditions();
}

}  // namespace dc::dev

#endif  // DC_DEV_DATA_H
