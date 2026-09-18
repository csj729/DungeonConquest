// **임시 틱 드라이버.** Spawn / Targeting / Combat이 붙으면 이 파일은 삭제된다.
//
// 체크섬과 재현성 하네스는 "상태가 실제로 움직이는 틱"이 있어야 검증할 수 있는데
// 아직 시스템이 없다. 그래서 스폰·난수 소비·삭제·게이지 충전을 섞은 각본으로
// 대신한다. 게임 규칙과는 아무 관계가 없다 — 상태를 골고루 휘젓는 것이 목적이다.
//
// **core/ 안이 아니라 tools/ 안에 두는 이유**: 시뮬 코어에 들어가면 삭제 시점을
// 놓친다. 여기 있으면 디렉터리 하나만 지우면 된다.
#ifndef DC_DEV_SCRIPT_H
#define DC_DEV_SCRIPT_H

#include "../include/dc/world.h"

namespace dc::dev {

// 한 틱. 시스템이 들어올 자리마다 상태를 건드린다.
inline void scriptTick(World& w) {
    // Spawn 자리
    const uint32_t n = w.rngSpawn.range(3);
    for (uint32_t i = 0; i < n; ++i) {
        SpawnDesc d;
        d.posX      = Fixed::fromRaw(static_cast<int32_t>(w.rngSpawn.nextU32() & 0xFFFF));
        d.posY      = Fixed::fromRaw(static_cast<int32_t>(w.rngSpawn.nextU32() & 0xFFFF));
        d.maxHp     = Fixed(20);
        d.archetype = w.rngSpawn.chancePermille(80) ? Archetype::Elite : Archetype::Trash;
        if (d.archetype == Archetype::Elite) d.ccGaugeMax = Fixed(100);
        if (!w.entities.spawn(d, w.tickCount(), w.masterSeed()).valid()) break;
        ++w.spawn.spawnedTotal;
    }

    // Combat 자리 — 피해 누적과 처치
    if (w.entities.count() > 0) {
        const uint32_t victim = w.rngCombat.range(w.entities.count());
        w.entities.damageTaken[victim] = w.entities.damageTaken[victim] + Fixed(7);
        ++w.hero.prdTrials;
        if (w.entities.damageTaken[victim].raw >= w.entities.maxHp[victim].raw) {
            if (w.entities.markDead(w.entities.idAt(victim))) {
                if (w.entities.archetype[victim] == Archetype::Elite) {
                    ++w.run.killedElite;
                    w.run.clearPoints += 10;
                } else {
                    ++w.run.killedTrash;
                    w.run.clearPoints += 1;
                }
                w.hero.prdTrials = 0;
            }
        }
        w.hero.target = w.entities.idAt(victim);
    }

    // 잠식 게이지 자리 — 물량 충전
    w.hero.corruption = w.hero.corruption
                      + Fixed::fromRaw(static_cast<int32_t>(w.entities.count()));

    // 진행 자리
    if (w.run.clearPoints >= 60) {
        w.run.clearPoints = 0;
        if (++w.run.segmentIndex >= 8) { w.run.segmentIndex = 0; ++w.run.mapIndex; }
    }

    w.tick();
}

inline void runScript(World& w, int32_t ticks) {
    for (int32_t t = 0; t < ticks; ++t) scriptTick(w);
}

}  // namespace dc::dev

#endif  // DC_DEV_SCRIPT_H
