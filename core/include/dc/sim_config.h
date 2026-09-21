// 시스템이 읽는 정적 설정 — `data/*.json`에서 온다.
//
// **기본값을 두지 않는다.** 여기 값이 코드에 박히면 "밸런스 수치는 data/*.json이
// 단일 진실 원천"(CLAUDE.md)이 깨진다. 전부 0으로 시작하고 로더가 채운다.
// 로더가 붙기 전까지는 `core/tools/dev_script.h`가 그 자리를 대신하며,
// `tools/verify_core_constants.py`가 dev 값과 데이터의 정합을 검사한다.
//
// `RecipeTable`과 같은 이유로 **World 밖에 산다** — 런마다 변하지 않으므로
// 스냅샷·체크섬이 같은 값을 반복해서 물 이유가 없다. 시스템에 인자로 넘긴다.
#ifndef DC_SIM_CONFIG_H
#define DC_SIM_CONFIG_H

#include <cstdint>

#include "config.h"
#include "entity_store.h"
#include "fixed.h"

namespace dc {

constexpr uint32_t MAX_ELITE_TYPES  = 8;
constexpr uint32_t MAX_ELITE_SPAWNS = 32;   // 맵당 19마리 + 여유

// 몬스터 한 종류의 전투 데이터 (§9 "몬스터 전투 데이터").
struct MonsterConfig {
    Fixed     hp{};
    Fixed     armor{};
    Fixed     damage{};
    Fixed     approachSpeed{};
    Fixed     attackRange{};
    Fixed     ccGaugeMax{};
    int32_t   cooldownTicks   = 0;
    int32_t   windupTicks     = 0;   // 엘리트·보스의 텔레그래프 패턴 전용. 잡몹은 0
    int32_t   targetPriority  = 0;
    uint16_t  typeId          = 0;
    Archetype archetype       = Archetype::Trash;
    uint8_t   flags           = EntityFlag::None;
};

// 클리어 게이지가 이 점수에 닿으면 해당 엘리트가 등장한다 (§2).
// **웨이브 시퀀스가 아니라 게이지 임계**이므로 시간이 아니라 처치량에 걸린다.
struct EliteSpawnPoint {
    int32_t  atClearPoints = 0;
    uint32_t eliteIndex    = 0;
};

struct SimConfig {
    // ── 진행 (progression.json) ──
    int32_t tickHz         = 0;
    int32_t totalSegments  = 0;
    int32_t segmentsPerMap = 0;
    int32_t armorK         = 0;   // 방어력 비율 감쇠 상수 K (§9)

    // ── 스폰 (spawn.json) ──
    // 램프는 **파이썬이 지수를 미리 풀어 담은 정수 표**다. 코어가 실수 거듭제곱을
    // 다시 풀지 않는다 (PRD 상수 C와 같은 방식).
    int32_t  capBySegment[32]   = {0};   // 전역 구간 1..totalSegments
    int32_t  batchBySegment[16] = {0};   // 맵 내 구간 1..segmentsPerMap
    int32_t  spawnIntervalTicks = 0;
    int32_t  spawnRadiusMilli   = 0;
    int32_t  minSeparationMilli = 0;   // 스폰 시 같은 방향 안에서 벌리는 간격
    // 적 간 충돌·회피 (§11). **이 두 값이 "몇 마리가 붙을 수 있는가"를 정한다.**
    int32_t  separationMilli     = 0;   // 몹 ↔ 몹
    int32_t  heroSeparationMilli = 0;   // 몹 ↔ 영웅 — 첫 링의 반지름
    uint32_t directions         = 0;

    // 잡몹 체력 성장 — 구간당 배율(permille). **한 대 피해 성장을 따라간다**(§2).
    int32_t trashHpScalePerSegmentPermille = 0;

    // ── 잠식 (progression.json) ──
    int32_t corruptionThreshold            = 0;
    int32_t corruptionPerMobPermille       = 0;
    int32_t corruptionOverflowMultPermille = 0;

    // ── 클리어 게이지 (segments.json) ──
    int32_t trashPoints          = 0;
    int32_t elitePoints          = 0;
    int32_t clearPointsPerSegment = 0;

    // ── 영웅 (hero.json) ──
    uint32_t procPrdCQ16 = 0;


    // ── 로스터 ──
    MonsterConfig   trash{};        // 잡몹은 근접 한 종류뿐이다 (원거리는 폐지)
    MonsterConfig   boss{};
    MonsterConfig   elites[MAX_ELITE_TYPES]{};
    uint32_t        eliteCount = 0;
    EliteSpawnPoint eliteSpawns[MAX_ELITE_SPAWNS]{};
    uint32_t        eliteSpawnCount = 0;

    // 전역 구간(1 기반)의 동시 생존 상한. 범위를 벗어나면 양 끝으로 물린다.
    int32_t capFor(int32_t globalSegment1Based) const {
        if (totalSegments <= 0) return 0;
        int32_t s = globalSegment1Based;
        if (s < 1) s = 1;
        if (s > totalSegments) s = totalSegments;
        if (s > 32) s = 32;
        return capBySegment[s - 1];
    }

    int32_t batchFor(int32_t mapSegment1Based) const {
        if (segmentsPerMap <= 0) return 0;
        int32_t s = mapSegment1Based;
        if (s < 1) s = 1;
        if (s > segmentsPerMap) s = segmentsPerMap;
        if (s > 16) s = 16;
        return batchBySegment[s - 1];
    }

    // 스폰 시점의 구간으로 체력이 고정된다 (§2). 배율은 permille 거듭제곱이지만
    // **정수 곱-시프트를 반복**하므로 실수가 끼지 않는다.
    Fixed trashHpFor(int32_t globalSegment1Based) const {
        int64_t raw = trash.hp.raw;
        const int32_t steps = globalSegment1Based > 1 ? globalSegment1Based - 1 : 0;
        for (int32_t i = 0; i < steps; ++i) {
            raw = (raw * trashHpScalePerSegmentPermille) / 1000;
        }
        if (raw > 0x7FFFFFFF) raw = 0x7FFFFFFF;
        return Fixed::fromRaw(static_cast<int32_t>(raw));
    }
};

}  // namespace dc

#endif  // DC_SIM_CONFIG_H
