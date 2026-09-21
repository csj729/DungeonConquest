// 스폰 시스템 (§2) — **동시 생존 상한을 유지한다.**
//
// 스폰율을 고정하지 않고 "죽은 만큼 채운다". 스폰율 고정은 처치율이 빌드마다
// 달라서 튜닝 폭이 지나치게 좁고, 빠르면 무한히 쌓이고 느리면 물량 게임이
// 아니게 된다. 상한 유지 방식은 **발산이 원천 차단**된다.
#ifndef DC_SPAWN_H
#define DC_SPAWN_H

#include <cstdint>

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

inline void spawnRun(World& w, const SimConfig& cfg) {
    if (cfg.spawnIntervalTicks <= 0) return;   // 설정 전에는 아무것도 하지 않는다

    const int32_t globalSeg = w.run.globalSegment(cfg.segmentsPerMap) + 1;   // 1 기반

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
    const Fixed hp = cfg.trashHpFor(globalSeg);

    for (int32_t i = 0; i < batch; ++i) {
        const uint32_t dir  = w.spawn.directionCursor & 3u;
        const int32_t  slot = i / static_cast<int32_t>(cfg.directions == 0 ? 4 : cfg.directions);
        Fixed x, y;
        spawnPosition(cfg, w.hero.posX, w.hero.posY, dir, slot, &x, &y);

        SpawnDesc d = makeDesc(cfg.trash, hp);
        d.posX = x;
        d.posY = y;
        if (!w.entities.spawn(d, w.tickCount(), w.masterSeed()).valid()) break;
        ++w.spawn.spawnedTotal;
        w.spawn.directionCursor = (w.spawn.directionCursor + 1) & 3u;
    }
}

}  // namespace dc

#endif  // DC_SPAWN_H
