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
constexpr int32_t  HERO_MOVE_SPEED_PERMILLE   = 2000;   // 타일/초 × 1000
constexpr int32_t  HERO_AOE_RADIUS_MILLITILE  = 1500;
constexpr int32_t  HERO_QTE_COOLDOWN_TICKS    = 120;
constexpr int32_t  HERO_QTE_SUCCESS_PERMILLE  = 1150;
constexpr int32_t  HERO_QTE_PERFECT_PERMILLE  = 1400;
constexpr int32_t  HERO_QTE_PERFECT_WINDOW    = 1;
constexpr int32_t  HERO_QTE_PERFECT_CC_PERMILLE = 1000;
constexpr int32_t  HERO_GROGGY_TICKS          = 40;

// data/progression.json — 파이썬이 지수 곡선을 미리 푼 정수 표
constexpr int64_t LEVEL_NEED[80] = {
    380, 410, 442, 476, 513, 553, 596, 643, 693, 747, 805, 868,
    936, 1009, 1088, 1172, 1264, 1362, 1469, 1583, 1707, 1840, 1983, 2138,
    2305, 2485, 2678, 2887, 3112, 3355, 3617, 3899, 4203, 4531, 4885, 5266,
    5676, 6119, 6596, 7111, 7665, 8263, 8908, 9603, 10352, 11159, 12029, 12968,
    13979, 15070, 16245, 17512, 18878, 20351, 21938, 23649, 25494, 27482, 29626, 31937,
    34428, 37113, 40008, 43129, 46493, 50119, 54028, 58243, 62785, 67683, 72962, 78653,
    84788, 91401, 98531, 106216, 114501, 123432, 133060, 143438};
// data/cards.json
constexpr int32_t CARD_GRADE_RATE[5]   = {420, 300, 190, 75, 15};
constexpr int32_t CARD_GRADE_BUDGET[5] = { 9, 15, 30, 60, 150 };
constexpr int32_t CARD_GRADE_STEP[5]   = {600, 1000, 2000, 4000, 10000};   // 증가량 비 0.6 : 1 : 2 : 4 : 10
constexpr int32_t ENGRAVE_BASE[8] = { 75, 135, 90, 150, 27, 65, 75, 60 };
constexpr int32_t RELIC_BASE[6]   = { 3, 120, 30, 45, 1, 113 };
constexpr int32_t  DECAY_TICKS            = 80;     // 4초 (20Hz)
constexpr int32_t  RAGE_DURATION_TICKS    = 100;    // 5초
constexpr int32_t  RAGE_MAX_STACKS        = 10;     // 등급 무관 고정
constexpr int32_t  BOLT_INTERVAL_TICKS    = 120;    // 6초
constexpr int32_t  FROST_RADIUS_MILLITILE = 4000;
constexpr int32_t  STORM_RADIUS_MILLITILE = 2000;
constexpr int32_t  STORM_DPS_PERMILLE     = 190;
constexpr int32_t  PIERCE_WIDTH_MILLITILE  = 700;   // 직선 판정 반폭
constexpr int32_t  PIERCE_LENGTH_MILLITILE = 3000;  // 타겟 뒤로 닿는 거리
constexpr int32_t  SWARM_RADIUS_MILLITILE = 2500;
constexpr int32_t  SWARM_MAX_STACKS       = 10;
constexpr uint32_t LEGEND_POOL_SIZE = 9;
constexpr uint32_t CARDS_PER_LEVEL  = 3;
constexpr int32_t  EXP_PER_EHP_PERMILLE = 1000;
// data/segments.json — 잡몹 294 × 1점 + 엘리트 19 × 10점
constexpr int32_t  CLEAR_TARGET_POINTS  = 484;
// 구간 시작 누적 포인트. 잡몹 21→66 · 엘리트 0→5로 늘어 **균등하지 않다.**
constexpr int32_t  SEGMENT_START_POINTS[8] = {0, 21, 54, 99, 148, 202, 275, 368};
// 일반 등급 스탯 카드 풀. **여기서 빌드 축이 갈린다** —
// 화력(공격력·공속)이냐 생존(방어력·잠식 최대치)이냐.
constexpr uint8_t  STAT_CARD_POOL[4] = {
    static_cast<uint8_t>(Stat::AttackPower),
    static_cast<uint8_t>(Stat::AttackSpeed),
    static_cast<uint8_t>(Stat::Armor),
    static_cast<uint8_t>(Stat::CorruptionMax)};

constexpr int32_t  TRASH_ATTACK_RANGE_MILLITILE = 1400;
// data/monsters.json — 엘리트 공통값 · 수직 슬라이스 보스
constexpr int32_t  TARGET_PRIORITY_FALLOFF_PER_TILE = 10;
constexpr int32_t  ELITE_COOLDOWN_TICKS           = 60;
constexpr int32_t  ELITE_ATTACK_RANGE_MILLITILE   = 3500;
constexpr int32_t  ELITE_CC_GAUGE_MAX             = 100;
constexpr int32_t  SLICE_BOSS_DAMAGE              = 63;
constexpr int32_t  SLICE_BOSS_COOLDOWN_TICKS      = 60;
constexpr int32_t  SLICE_BOSS_ATTACK_RANGE_MILLITILE = 3500;
constexpr int32_t  MOB_SEPARATION_MILLITILE   = 1500;   // 몹 ↔ 몹
constexpr int32_t  HERO_SEPARATION_MILLITILE  = 1000;   // 몹 ↔ 영웅 (첫 링 반지름)
// 추격 정지 여유. 1400(잡몹 사거리) - 400 = 1000이라 첫 링 반지름이 변하지 않는다.
constexpr int32_t  APPROACH_MARGIN_MILLITILE  = 400;

inline SimConfig devConfig() {
    SimConfig c;
    // data/progression.json
    c.tickHz         = 20;
    c.totalSegments  = 24;
    c.segmentsPerMap = 8;
    c.armorK         = 100;
    c.trashHpScalePerSegmentPermille   = 1100;
    c.corruptionThreshold              = 20;
    c.corruptionPerMobPermille         = 600;
    c.corruptionOverflowMultPermille   = 3000;
    c.segmentClearPurge                = 150;    // progression.json
    c.orbTrashDropPermille             = 200;    // progression.json
    c.orbTrashAmount                   = 45;
    c.orbEliteAmount                   = 150;
    c.orbPickupRadiusMilli             = 1500;
    c.orbLifetimeTicks                 = 200;

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
    // data/segments.json — 구성에서 누적한 구간 시작 포인트. 균등 분할이 아니다.
    for (uint32_t i = 0; i < 8; ++i) c.segmentStartPoints[i] = SEGMENT_START_POINTS[i];

    // data/hero.json
    c.procPrdCQ16 = HERO_PROC_PRD_C_Q16;
    c.aoeRadius   = Fixed::fromPermille(HERO_AOE_RADIUS_MILLITILE);
    c.swarmRadius    = Fixed::fromPermille(SWARM_RADIUS_MILLITILE);
    c.swarmMaxStacks = SWARM_MAX_STACKS;
    c.decayTicks   = DECAY_TICKS;
    c.pierceWidth  = Fixed::fromPermille(PIERCE_WIDTH_MILLITILE);
    c.pierceLength = Fixed::fromPermille(PIERCE_LENGTH_MILLITILE);
    c.rageDurationTicks = RAGE_DURATION_TICKS;
    c.rageMaxStacks     = RAGE_MAX_STACKS;
    c.boltIntervalTicks = BOLT_INTERVAL_TICKS;
    c.frostRadius       = Fixed::fromPermille(FROST_RADIUS_MILLITILE);
    c.stormRadius       = Fixed::fromPermille(STORM_RADIUS_MILLITILE);
    c.stormDpsPermille  = STORM_DPS_PERMILLE;
    c.targetPriorityFalloffPerTile = TARGET_PRIORITY_FALLOFF_PER_TILE;
    c.qteCooldownTicks          = HERO_QTE_COOLDOWN_TICKS;
    c.qtePerfectWindowTicks     = HERO_QTE_PERFECT_WINDOW;
    c.qteSuccessMult            = Fixed::fromPermille(HERO_QTE_SUCCESS_PERMILLE);
    c.qtePerfectMult            = Fixed::fromPermille(HERO_QTE_PERFECT_PERMILLE);
    c.qtePerfectCcGainPermille  = HERO_QTE_PERFECT_CC_PERMILLE;
    c.groggyTicks               = HERO_GROGGY_TICKS;

    // data/progression.json · data/cards.json — 레벨업 카드
    for (uint32_t i = 0; i < 80; ++i) c.levelNeed[i] = LEVEL_NEED[i];
    for (uint32_t i = 0; i < 5; ++i) {
        c.cardGradeRate[i]   = CARD_GRADE_RATE[i];
        c.cardGradeBudget[i] = CARD_GRADE_BUDGET[i];
        c.cardGradeStep[i]   = CARD_GRADE_STEP[i];
    }
    c.engraveCount = sizeof(ENGRAVE_BASE) / sizeof(ENGRAVE_BASE[0]);
    c.relicCount   = sizeof(RELIC_BASE) / sizeof(RELIC_BASE[0]);
    for (uint32_t i = 0; i < c.engraveCount; ++i) c.engraveBase[i] = ENGRAVE_BASE[i];
    for (uint32_t i = 0; i < c.relicCount; ++i)   c.relicBase[i]   = RELIC_BASE[i];
    c.legendPoolSize     = LEGEND_POOL_SIZE;
    c.cardsPerLevel      = CARDS_PER_LEVEL;
    c.expPerEhpPermille  = EXP_PER_EHP_PERMILLE;
    c.clearTargetPoints  = CLEAR_TARGET_POINTS;
    c.statCardPoolSize   = sizeof(STAT_CARD_POOL) / sizeof(STAT_CARD_POOL[0]);
    for (uint32_t i = 0; i < c.statCardPoolSize; ++i) c.statCardPool[i] = STAT_CARD_POOL[i];

    // data/skills.json — 전사 액티브 3종
    struct S { int32_t mult, weight; bool aoe; };
    constexpr S kSkills[3] = {
        {3000, 400, false},   // W_SMASH  분쇄 강타
        {2500, 350, true },   // W_WHIRL  회전 베기
        {2000, 250, true },   // W_CLEAVE 대지 가르기
    };
    c.skillCount = 3;
    for (uint32_t i = 0; i < 3; ++i) {
        c.skills[i].mult   = Fixed::fromPermille(kSkills[i].mult);
        c.skills[i].weight = kSkills[i].weight;
        c.skills[i].aoe    = kSkills[i].aoe;
    }
    c.separationMilli     = MOB_SEPARATION_MILLITILE;
    c.heroSeparationMilli = HERO_SEPARATION_MILLITILE;
    c.approachMarginMilli = APPROACH_MARGIN_MILLITILE;

    // data/monsters.json — 잡몹
    c.trash.hp             = Fixed(20);
    c.trash.damage         = Fixed(5);
    c.trash.cooldownTicks  = 30;
    c.trash.targetPriority = 0;
    c.trash.archetype      = Archetype::Trash;
    // 접근 속도는 스폰 반경 19.8타일 / 접근 160틱(8초) = 2.475타일/초 (§2에서 역산)
    c.trash.approachSpeed  = Fixed::fromRaw((19800 * Fixed::ONE_RAW) / 1000 / 8);
    c.trash.attackRange    = Fixed::fromPermille(TRASH_ATTACK_RANGE_MILLITILE);

    // data/monsters.json — 엘리트 (target_priority가 순서를 정한다)
    struct E { int32_t hp, armor, dmg, prio, windup; uint16_t typeId; };
    // **damage는 data/monsters.json 그대로다.** 한때 40/8/5/3이 들어 있었는데
    // 그건 어디에도 근거가 없는 값이었고, 파이썬 검증(167/33/21/13)과 C++ 시뮬이
    // 4.2배 다른 상태로 몬테카를로를 돌리고 있었다. verify_core_constants.py가
    // 이제 이 표 전체를 대조한다.
    constexpr E kElites[4] = {
        {120, 0,  167, 35, 60, 1},   // GE_ARCHER  궁병대장 — QTE 소스
        {100, 200, 33, 20,  0, 2},   // GE_SHIELD  방패병   — QTE 없음
        { 90, 0,   21, 40,  0, 3},   // GE_SHAMAN  주술사   — 소환, 방치 비용 최대
        {150, 0,   13, 30, 30, 4},   // GE_MAD     미친 고블린 — QTE 소스
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
        m.cooldownTicks   = ELITE_COOLDOWN_TICKS;
        m.approachSpeed   = c.trash.approachSpeed;
        m.attackRange     = Fixed::fromPermille(ELITE_ATTACK_RANGE_MILLITILE);
        m.ccGaugeMax      = Fixed(ELITE_CC_GAUGE_MAX);
    }

    // data/monsters.json — 보스
    c.boss.hp             = Fixed(2100);      // slice_boss_hp
    c.boss.armor          = Fixed(50);        // slice_boss_armor
    c.boss.damage         = Fixed(SLICE_BOSS_DAMAGE);
    c.boss.targetPriority = 10;
    c.boss.archetype      = Archetype::Boss;
    c.boss.cooldownTicks  = SLICE_BOSS_COOLDOWN_TICKS;
    c.boss.approachSpeed  = c.trash.approachSpeed;
    c.boss.attackRange    = Fixed::fromPermille(SLICE_BOSS_ATTACK_RANGE_MILLITILE);
    c.boss.typeId         = 100;

    // data/segments.json — 구간별 엘리트를 클리어 게이지 임계로 옮긴 것.
    // 원거리 잡몹 폐지로 빠진 압박을 엘리트 증량(10 → 19마리)이 대신한다.
    // 인덱스: 0 궁병대장 · 1 방패병 · 2 주술사 · 3 미친 고블린
    const EliteSpawnPoint sp[19] = {
        { 25, 0},
        { 50, 0}, { 55, 2},
        { 80, 3}, { 85, 1},
        {115, 0}, {120, 1},
        {160, 0}, {165, 1}, {170, 1},
        {215, 3}, {220, 1}, {225, 1}, {230, 1},
        {285, 0}, {290, 1}, {295, 1}, {300, 1}, {305, 2},
    };
    c.eliteSpawnCount = 19;
    for (uint32_t i = 0; i < 19; ++i) c.eliteSpawns[i] = sp[i];
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
    bases[statIndex(Stat::MoveSpeed)]     = Fixed::fromPermille(HERO_MOVE_SPEED_PERMILLE);
    bounds[statIndex(Stat::CorruptionMax)] = StatBounds{Fixed(1), Fixed::fromPermille(-900)};
    bounds[statIndex(Stat::AttackSpeed)]   = StatBounds{Fixed::fromPermille(100),
                                                        Fixed::fromPermille(-900)};
    w.hero.stats.init(bases, bounds);
    w.refreshAllConditions();
}

}  // namespace dc::dev

#endif  // DC_DEV_DATA_H
