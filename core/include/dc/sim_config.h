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

// 아이템 스탯 표의 상한. 수직 슬라이스가 51종이라 여유를 둔 값이다 —
// config::MAX_ITEM_TYPES(1024) 전체를 잡으면 SimConfig가 수십 KB가 된다.
#define DC_MAX_ITEM_TYPES_CFG 64

#include <cstdint>

#include "card.h"
#include "config.h"
#include "stat_id.h"
#include "entity_store.h"
#include "fixed.h"

namespace dc {

constexpr uint32_t MAX_SKILLS       = 8;
constexpr uint32_t MAX_ELITE_TYPES  = 8;
constexpr uint32_t MAX_ELITE_SPAWNS = 32;   // 맵당 19마리 + 여유

// 고유 스킬 (§3). 발동은 통합 proc 1회, 어떤 스킬인지는 가중 추첨이다.
struct SkillConfig {
    Fixed   mult{};          // 기본 공격 대비 배율
    int32_t weight = 0;      // 추첨 가중치(permille)
    bool    aoe    = false;
};

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
    // 추격 정지 여유 — 사거리 경계가 아니라 안쪽에서 멈춘다. 경계에 서면
    // 절삭·분리 밀림이 매 틱 사거리 밖으로 밀어내 전투가 멎는다.
    int32_t  approachMarginMilli = 0;
    uint32_t directions         = 0;

    // 잡몹 체력 성장 — 구간당 배율(permille). **한 대 피해 성장을 따라간다**(§2).
    int32_t trashHpScalePerSegmentPermille = 0;

    // ── 잠식 (progression.json) ──
    int32_t corruptionThreshold            = 0;
    int32_t corruptionPerMobPermille       = 0;
    int32_t corruptionOverflowMultPermille = 0;

    // ── 잠식 회복 (§2) ──
    // 유입이 구간 2 이후 초당 26~34라 **회복은 선택이 아니라 전제다.**
    // 없으면 잠식 1250이 45초에 가득 차는데 맵 통과는 최소 210초다.
    int32_t segmentClearPurge  = 0;   // 구간 진입 시 일괄 정화

    // 회복 구슬 — 잡몹은 확률, 엘리트는 무조건 드랍한다.
    int32_t orbTrashDropPermille = 0;
    int32_t orbTrashAmount       = 0;
    int32_t orbEliteAmount       = 0;
    int32_t orbPickupRadiusMilli = 0;
    int32_t orbLifetimeTicks     = 0;

    // ── 클리어 게이지 (segments.json) ──
    int32_t trashPoints          = 0;
    int32_t elitePoints          = 0;
    // 구간이 시작되는 누적 클리어 포인트. **구간은 균등하지 않다** — 잡몹이
    // 21 → 66마리, 엘리트가 0 → 5마리로 늘어 마지막 구간이 첫 구간의 5.5배다.
    // 파이썬이 segments.json의 구성에서 누적해 담은 표를 그대로 읽는다.
    int32_t segmentStartPoints[16] = {0};   // batchBySegment와 같은 상한

    // ── 영웅 (hero.json) ──
    uint32_t procPrdCQ16 = 0;
    Fixed    aoeRadius{};
    // E_SWARM(군집) — 주변 적을 세는 반경과 중첩 상한.
    Fixed    swarmRadius{};
    int32_t  swarmMaxStacks = 0;
    // E_DECAY(부식) 지속 · E_PIERCE(관통) 판정 폭
    int32_t  decayTicks = 0;
    Fixed    pierceWidth{};
    Fixed    pierceLength{};
    // 유물 — R_RAGE 지속·상한, R_BOLT 주기, R_FROST 반경
    int32_t  rageDurationTicks = 0;
    int32_t  rageMaxStacks     = 0;
    int32_t  boltIntervalTicks = 0;
    Fixed    frostRadius{};
    // RL_STORM(폭풍의 핵) — 반경과 초당 피해(공격력 대비 permille)
    Fixed    stormRadius{};
    int32_t  stormDpsPermille = 0;
    // 타겟 우선순위 거리 감쇠 (타일당). 닿을 수 없는 표적에 묶이지 않게 한다.
    int32_t  targetPriorityFalloffPerTile = 0;

    // ── QTE (§3) ──
    int32_t  qteCooldownTicks     = 0;   // 빈도 상한. proc 확률로 조절하지 않는다
    int32_t  qtePerfectWindowTicks = 0;  // 완벽 구간 반폭. 20Hz라 최소 1틱 = 50ms
    Fixed    qteSuccessMult{};           // 스킬 증폭 — 성공
    Fixed    qtePerfectMult{};           // 스킬 증폭 — 완벽
    int32_t  qtePerfectCcGainPermille = 0;  // 위기 회피 완벽 — ccGauge 충전 비율
    int32_t  groggyTicks          = 0;   // 그로기 지속

    SkillConfig skills[MAX_SKILLS]{};
    uint32_t    skillCount = 0;

    // ── 레벨업 카드 (§4) ──
    // 필요 경험치는 **파이썬이 지수 곡선을 미리 풀어 담은 정수 표**다.
    // 코어가 실수 거듭제곱을 다시 풀지 않는다 (PRD 상수·스폰 램프와 같은 방식).
    int64_t  levelNeed[MAX_LEVEL_NEED] = {0};
    int32_t  cardGradeRate[MAX_CARD_GRADES]   = {0};   // permille
    int32_t  cardGradeBudget[MAX_CARD_GRADES] = {0};   // 위력 증가분 permille
    int32_t  cardGradeStep[MAX_CARD_GRADES]   = {0};   // 각인·유물 증가량 비 permille
    int32_t  engraveBase[MAX_ENGRAVINGS] = {0};        // 고급 기준 수치 permille
    int32_t  relicBase[MAX_RELICS]       = {0};
    uint32_t engraveCount   = 0;
    uint32_t relicCount     = 0;
    uint32_t legendPoolSize = 0;
    uint32_t cardsPerLevel  = 0;
    int32_t  expPerEhpPermille = 0;
    // 클리어 게이지 목표. 도달하면 보스가 등장하고, 보스를 잡으면 클리어다 (§2).
    int32_t  clearTargetPoints = 0;
    // 일반 등급 스탯 카드가 고를 수 있는 스탯. **여기서 빌드 축이 갈린다** —
    // 화력(공격력·공속)이냐 생존(방어력·잠식 최대치)이냐.
    uint8_t  statCardPool[8] = {0};
    uint32_t statCardPoolSize = 0;

    int64_t needFor(int32_t level) const {
        if (level < 1) return 0;
        const uint32_t i = static_cast<uint32_t>(level - 1);
        return i < MAX_LEVEL_NEED ? levelNeed[i] : levelNeed[MAX_LEVEL_NEED - 1];
    }


    // ── 아이템 (§5) ──
    //
    // **위력의 주력이다** — 한 판 성장의 70%를 아이템 조합이 담당한다 (design.md §4).
    // 아이템 하나가 주는 스탯 배율(permille)을 스탯별로 담는다. 보유 개수만큼
    // 곱이 아니라 합으로 쌓인다 — 같은 아이템 2개가 제곱으로 뛰면 조합 사다리가 무너진다.
    int32_t  itemStats[DC_MAX_ITEM_TYPES_CFG][STAT_COUNT] = {{0}};
    int32_t  itemSlowAura[DC_MAX_ITEM_TYPES_CFG] = {0};   // CC 축 — 스탯이 아니라 둔화
    uint32_t itemTypeCount = 0;

    // 레벨업 화면 맨 왼쪽 고정 칸이 뽑는 풀. **항상 흔함 등급**이다 (§4).
    uint16_t commonPool[16] = {0};
    uint32_t commonPoolSize = 0;

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

    // 누적 클리어 포인트 → 맵 내 구간(0 기반). 표가 오름차순이므로 뒤에서부터 찾는다
    // (구간 8개라 이진 탐색이 오히려 느리다).
    int32_t segmentForPoints(int32_t points) const {
        int32_t seg = 0;
        for (int32_t i = segmentsPerMap - 1; i >= 0; --i) {
            if (i < 16 && points >= segmentStartPoints[i]) { seg = i; break; }
        }
        return seg;
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
