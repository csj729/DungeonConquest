// 스폰 시스템 (§2) — **동시 생존 상한을 유지한다.**
//
// 스폰율을 고정하지 않고 "죽은 만큼 채운다". 스폰율 고정은 처치율이 빌드마다
// 달라서 튜닝 폭이 지나치게 좁고, 빠르면 무한히 쌓이고 느리면 물량 게임이
// 아니게 된다. 상한 유지 방식은 **발산이 원천 차단**된다.
#ifndef DC_SPAWN_H
#define DC_SPAWN_H

#include <cstdint>

#include "items_apply.h"
#include "sim_config.h"
#include "world.h"

namespace dc {

// 4방향 단위 벡터. **순서가 고정이다** (북 → 동 → 남 → 서) —
// 나머지 배분 순서를 고정하는 것이 결정론 요구사항이다 (§2).
constexpr int32_t SPAWN_DIR_X[4] = { 0,  1,  0, -1};
constexpr int32_t SPAWN_DIR_Y[4] = { 1,  0, -1,  0};

inline uint32_t aliveCount(const EntityStore& e) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < e.count(); ++i) if (!e.deadAt(i)) ++n;
    return n;
}

// 방향 안의 좌표. 같은 방향에 여러 마리가 들어와도 겹치지 않도록 수직 방향으로
// 최소 이격만큼 벌린다. 0, +1, -1, +2, -2... 순서라 **좌우 대칭이고 결정적이다.**
inline void spawnPosition(const SimConfig& cfg, Fixed heroX, Fixed heroY,
                          uint32_t direction, int32_t slot, Fixed* outX, Fixed* outY) {
    const uint32_t d = direction & 3u;
    // millitile → Fixed. 타일 1 = Fixed(1)이므로 milli를 1000으로 나눈다.
    const int64_t r = (static_cast<int64_t>(cfg.spawnRadiusMilli) * Fixed::ONE_RAW) / 1000;
    const int64_t s = (static_cast<int64_t>(cfg.minSeparationMilli) * Fixed::ONE_RAW) / 1000;

    // 0 → 0, 1 → +1, 2 → -1, 3 → +2, 4 → -2 ...
    const int32_t k    = (slot + 1) / 2;
    const int32_t sign = (slot % 2 == 1) ? 1 : -1;
    const int64_t off  = (slot == 0) ? 0 : static_cast<int64_t>(sign) * k * s;

    const int64_t px = static_cast<int64_t>(heroX.raw) + SPAWN_DIR_X[d] * r + (-SPAWN_DIR_Y[d]) * off;
    const int64_t py = static_cast<int64_t>(heroY.raw) + SPAWN_DIR_Y[d] * r + ( SPAWN_DIR_X[d]) * off;
    *outX = Fixed::fromRaw(static_cast<int32_t>(px));
    *outY = Fixed::fromRaw(static_cast<int32_t>(py));
}

inline SpawnDesc makeDesc(const MonsterConfig& m, Fixed hp) {
    SpawnDesc d;
    d.maxHp               = hp;
    d.armor               = m.armor;
    d.attackDamage        = m.damage;
    d.approachSpeed       = m.approachSpeed;
    d.attackRange         = m.attackRange;
    d.ccGaugeMax          = m.ccGaugeMax;
    d.attackCooldownTicks = m.cooldownTicks;
    d.windupTicks         = m.windupTicks;
    d.targetPriority      = m.targetPriority;
    d.typeId              = m.typeId;
    d.archetype           = m.archetype;
    d.flags               = m.flags;
    return d;
}

// 구간 진행 — **게이지가 구간을 넘긴다. 시간이 아니다** (§2).
//
// 이 한 줄이 없으면 런 전체가 구간 1에 갇힌다. 배치 2마리·상한 30·잡몹 체력
// 기본값이 끝까지 유지되어 **난이도 램프가 통째로 죽는다** — 데이터에 담아둔
// 배치 표 8칸과 상한 표 24칸 중 첫 칸만 쓰이고 나머지는 도달 불가가 된다.
//
// segmentIndex는 지금은 clearPoints의 함수지만 [상태]로 남긴다 — 맵 진행
// (mapIndex)이 붙으면 게이지가 리셋되면서 순수 함수가 아니게 된다.
// RL_FORGE가 테이블을 요구하므로 `table`을 받는다 — 아이템을 넣는 쪽은 어디든
// `Inventory::matches`를 통과해야 한다 (§5).
inline void progressRun(World& w, const SimConfig& cfg, const RecipeTable& table) {
    if (cfg.segmentsPerMap <= 0) return;
    const int32_t seg = cfg.segmentForPoints(w.run.clearPoints);
    // **진행도는 되돌아가지 않는다** (§2). 게이지가 줄지 않으므로 지금은
    // 성립하지만, 불변식을 코드에 남겨 나중에 깎는 효과가 생겨도 안전하게 한다.
    if (seg <= w.run.segmentIndex) return;

    // 구간을 **몇 칸 건너뛰어도 한 칸당 한 번씩** 정화한다. 엘리트를 연달아 잡아
    // 게이지가 한 틱에 두 구간을 넘길 수 있으므로, 넘긴 칸 수로 곱해야 한다 —
    // 한 번만 주면 빨리 미는 빌드가 오히려 손해를 본다.
    w.metrics.purgeSegment += w.purgeCorruption(
        Fixed((seg - w.run.segmentIndex) * cfg.segmentClearPurge)).raw;

    // RL_FORGE(대장장이의 화로) — **넘긴 칸마다 한 개**다. 정화와 같은 이유로
    // 곱해야 한다. 한 번만 주면 엘리트를 연달아 잡아 두 구간을 한 틱에 넘기는
    // 빌드가 오히려 손해를 본다.
    //
    // **런 전체 기대 획득량이 획득 시점으로 4배 갈린다** — 전설 유물 중 유일하게
    // 시점에 민감한 효과다 (cards_vertical_slice.md §3).
    if (w.cards.legendHas(legendIndexOf(LegendId::Forge))) {
        for (int32_t k = w.run.segmentIndex; k < seg; ++k) forgeGrant(w, cfg, table);
    }
    w.run.segmentIndex = seg;
    // R_TIDE(밀물의 인장)가 여기서 리셋된다 — 구간 경과에 비례해 오르는 유물이다.
    w.run.segmentStartTick = w.tickCount();
}

// 잡몹 `count`마리를 사방에 나눠 넣는다. 상한 유지 스폰과 보스 졸개 소환이
// 공유한다 — **상한을 볼지 말지는 부르는 쪽이 정한다.** 소환은 일부러 상한을
// 넘긴다(아래 참조).
inline int32_t spawnTrashBatch(World& w, const SimConfig& cfg, int32_t count, Fixed hp) {
    int32_t made = 0;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t dir  = w.spawn.directionCursor & 3u;
        const int32_t  slot = i / static_cast<int32_t>(cfg.directions == 0 ? 4 : cfg.directions);
        Fixed x, y;
        spawnPosition(cfg, w.hero.posX, w.hero.posY, dir, slot, &x, &y);

        SpawnDesc d = makeDesc(cfg.trash, hp);
        d.posX = x;
        d.posY = y;
        if (!w.entities.spawn(d, w.tickCount(), w.masterSeed()).valid()) break;
        ++w.spawn.spawnedTotal;
        ++made;
        w.spawn.directionCursor = (w.spawn.directionCursor + 1) & 3u;
    }
    return made;
}

inline void spawnRun(World& w, const SimConfig& cfg) {
    if (cfg.spawnIntervalTicks <= 0) return;   // 설정 전에는 아무것도 하지 않는다

    const int32_t globalSeg = w.run.globalSegment(cfg.segmentsPerMap) + 1;   // 1 기반

    // ── 보스: 게이지가 다 차면 등장한다 (§2) ──
    if (!w.run.bossSpawned && cfg.clearTargetPoints > 0
        && w.run.clearPoints >= cfg.clearTargetPoints) {
        Fixed bx, by;
        spawnPosition(cfg, w.hero.posX, w.hero.posY, w.spawn.directionCursor & 3u, 0, &bx, &by);
        SpawnDesc d = makeDesc(cfg.boss, cfg.boss.hp);
        d.posX = bx;
        d.posY = by;
        if (w.entities.spawn(d, w.tickCount(), w.masterSeed()).valid()) {
            w.run.bossSpawned = true;
            w.run.bossAlive   = true;
        }
    }

    // ── 엘리트: 클리어 게이지 임계 (§2) ──
    // 시간이 아니라 **처치량**에 걸린다. 빌드가 빠르면 빨리 나오고 느리면 늦게 나온다.
    while (w.spawn.nextEliteIndex < cfg.eliteSpawnCount) {
        const EliteSpawnPoint& sp = cfg.eliteSpawns[w.spawn.nextEliteIndex];
        if (w.run.clearPoints < sp.atClearPoints) break;
        if (sp.eliteIndex < cfg.eliteCount) {
            const MonsterConfig& m = cfg.elites[sp.eliteIndex];
            Fixed x, y;
            const uint32_t dir = w.spawn.directionCursor & 3u;
            spawnPosition(cfg, w.hero.posX, w.hero.posY, dir, 0, &x, &y);
            SpawnDesc d = makeDesc(m, m.hp);
            d.posX = x;
            d.posY = y;
            if (w.entities.spawn(d, w.tickCount(), w.masterSeed()).valid()) {
                ++w.spawn.spawnedTotal;
                w.spawn.directionCursor = (w.spawn.directionCursor + 1) & 3u;
            }
        }
        ++w.spawn.nextEliteIndex;
    }

    // ── 보스 페이즈 2: 졸개 소환 ──
    //
    // **일부러 동시 생존 상한을 넘긴다.** 상한 안에서 부르면 아래 상한 유지 스폰이
    // 그만큼 덜 넣어 아무 일도 일어나지 않는다 — 소환이 통째로 무의미해진다.
    // 넘기면 초과분에 잠식 가속(×3)이 걸리므로(§2), 소환의 압박이 "몹이 몇 마리
    // 늘었다"가 아니라 **게이지 기울기가 꺾인다**로 나타난다. 영웅이 처치해
    // 상한 아래로 되돌리면 저절로 풀리므로, 처치 속도가 곧 해제 조건이다.
    //
    // 전에 읽은 페이즈 2 플래그를 쓴다 — bossPhaseRun이 이 틱 뒤에 돌지만,
    // 소환은 스폰 시스템에 두는 편이 시스템 경계가 흐려지지 않는다. 한 틱
    // 늦는 것은 첫 소환이 어차피 한 주기 뒤이므로 결과에 나타나지 않는다.
    if (w.run.bossPhase2 && w.run.bossAlive && cfg.bossPhase2SummonCount > 0
        && cfg.bossPhase2SummonPeriodTicks > 0
        && w.tickCount() >= w.run.bossSummonNextTick) {
        w.run.bossSummonNextTick = w.tickCount() + cfg.bossPhase2SummonPeriodTicks;
        spawnTrashBatch(w, cfg, cfg.bossPhase2SummonCount, cfg.trashHpFor(globalSeg));
    }

    // ── 잡몹: 상한 유지 ──
    if (w.tickCount() < w.spawn.nextSpawnTick) return;
    w.spawn.nextSpawnTick = w.tickCount() + cfg.spawnIntervalTicks;

    const int32_t cap     = cfg.capFor(globalSeg);
    const int32_t alive   = static_cast<int32_t>(aliveCount(w.entities));
    const int32_t deficit = cap - alive;
    if (deficit <= 0) return;

    int32_t batch = cfg.batchFor(w.run.segmentIndex + 1);
    if (batch > deficit) batch = deficit;

    // 스폰 시점의 구간으로 체력이 고정된다 (§2). 나중에 조회하지 않는다.
    spawnTrashBatch(w, cfg, batch, cfg.trashHpFor(globalSeg));
}

}  // namespace dc

#endif  // DC_SPAWN_H
