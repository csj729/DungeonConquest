// 틱 루프 — **시스템 실행 순서의 단일 정의점** (§10).
//
// 순서가 두 군데에 적히면 하나만 고치는 사고가 난다. World가 시스템을 직접 부르지
// 않는 이유도 이것이다 (world.h ↔ 시스템 헤더가 서로를 include하게 된다).
#ifndef DC_SIM_H
#define DC_SIM_H

#include "combat.h"
#include "sim_config.h"
#include "spawn.h"
#include "targeting.h"
#include "world.h"

namespace dc {

inline void stepWorld(World& w, const SimConfig& cfg) {
    w.beginTick();                    // 틱 전진 + 시간 조건 만료 처리

    spawnRun(w, cfg);                 // 상한 유지 스폰 · 게이지 임계 엘리트
    w.hero.target = selectTarget(w.entities, w.hero.posX, w.hero.posY, w.hero.manualTarget);
    w.notifyTargetChanged();          // 타겟 의존 조건부 모디파이어 재평가
    combatRun(w, cfg);                // 영웅 공격 · 몹 추격/공격 · 잠식 충전

    w.endTick();                      // 죽음 일괄 적용 (틱 종료 압축)
}

}  // namespace dc

#endif  // DC_SIM_H
