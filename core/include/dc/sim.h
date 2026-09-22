// 틱 루프 — **시스템 실행 순서의 단일 정의점** (§10).
//
// 순서가 두 군데에 적히면 하나만 고치는 사고가 난다. World가 시스템을 직접 부르지
// 않는 이유도 이것이다 (world.h ↔ 시스템 헤더가 서로를 include하게 된다).
#ifndef DC_SIM_H
#define DC_SIM_H

#include "combat.h"
#include "grid.h"
#include "separation.h"
#include "sim_config.h"
#include "spawn.h"
#include "targeting.h"
#include "world.h"

namespace dc {

// 입력 진입점. 카드 선택만 `SimConfig`가 필요해서 여기서 갈라진다.
// **모든 입력이 같은 `(틱 번호, 값)` 형식**이라는 성질은 그대로다.
inline bool applyInput(World& w, const SimConfig& cfg, const RecipeTable& table,
                       const InputEvent& e) {
    if (e.kind == InputKind::CardChoice) return chooseCard(w, cfg, table, e.value);
    return w.applyInput(e);
}

inline void stepWorld(World& w, const SimConfig& cfg, SimScratch& scratch) {
    w.beginTick();                    // 틱 전진 + 시간 조건 만료 처리

    progressRun(w, cfg);              // 게이지 → 구간. 스폰보다 먼저여야 이 틱에 반영된다
    spawnRun(w, cfg);                 // 상한 유지 스폰 · 게이지 임계 엘리트

    // **타겟이 이동을 정한다** (§3). 영웅은 추격 후 공격만 하므로 어디로 갈지는
    // 누구를 때릴지에서 따라 나온다 — 손잡이가 둘이 아니라 하나다.
    w.hero.target = selectTarget(w.entities, w.hero.posX, w.hero.posY, w.hero.manualTarget,
                                 w.hero.stats.value(Stat::Range),
                                 cfg.targetPriorityFalloffPerTile);
    w.notifyTargetChanged();          // 타겟 의존 조건부 모디파이어 재평가

    // **분리가 이동보다 먼저다. 순서를 바꾸면 전투가 멎는다.**
    //
    // 분리를 이동 뒤에 두면 마지막 발언권이 제약에 넘어간다 — 이동이 사거리
    // 안으로 들여놓은 타겟을 분리가 도로 밀어내고, 전투는 밀려난 좌표를 본다.
    // 실측에서 영웅이 타겟을 2.877타일까지 따라붙었는데(사거리 3.0) 분리가
    // 3.027로 되돌려 **매 틱 빗나갔다.** 잡몹 무리에 둘러싸인 채 엘리트를
    // 쫓는 동안 1200초 내내 처치 0 — 런이 영구 교착에 빠진다.
    //
    // 분리를 앞에 두면 이동이 마지막 발언권을 갖고 전투는 이동의 결과를 본다.
    // 첫 링 반지름은 변하지 않는다 — 분리가 몹을 영웅 이격(1.0)까지 밀어낸 뒤
    // 이동이 사거리(1.4)로 좁히려 해도 이미 안쪽이라 움직이지 않는다.
    separationRun(w, cfg, scratch);   // 적 간 충돌 — 균등 그리드를 쓰는 유일한 곳
    movementRun(w, cfg);              // 영웅 → 타겟, 몹 → 영웅. 양쪽 다 추격뿐
    decayRun(w, cfg);                 // E_DECAY 도트 — 전투보다 먼저(도트로 죽을 적은 못 때린다)
    boltRun(w, cfg);                  // R_BOLT 주기 방전 — 도트와 같은 이유로 전투보다 먼저
    stormRun(w, cfg);                 // RL_STORM 상시 회전 칼날 — 같은 이유로 전투보다 먼저
    orbRun(w, cfg);                   // 구슬 습득·소멸 — 전투보다 먼저(회복이 피해에 앞선다)
    qteRun(w, cfg);                   // 창이 닫힐 틱이면 판정 적용 — 전투보다 먼저
    combatRun(w, cfg);                // 사거리 안이면 공격 · 잠식 충전

    w.endTick();                      // 죽음 일괄 적용 (틱 종료 압축)

    // 게임오버는 잠식 게이지 하나로 통합돼 있다 (§2) — HP와 몬스터 수 상한을
    // 한 게이지가 흡수하므로 조건이 하나뿐이다.
    if (!w.run.over() && w.hero.dead()) {
        w.run.outcome = RunOutcome::Dead;
        w.run.endTick = w.tickCount();
    }
}

}  // namespace dc

#endif  // DC_SIM_H
